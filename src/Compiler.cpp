#include "Compiler.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Chunk.hpp"
#include "AstBytecodeEmitter.hpp"
#include "AstFrontend.hpp"
#include "ModuleResolver.hpp"
#include "StdLib.hpp"
#include "SyntaxRules.hpp"

namespace {
bool isBitwiseAssignmentOperator(TokenType type) {
    switch (type) {
        case TokenType::AMPERSAND_EQUAL:
        case TokenType::CARET_EQUAL:
        case TokenType::PIPE_EQUAL:
            return true;
        default:
            return false;
    }
}

TypeRef bitwiseResultType(const TypeRef& lhs, const TypeRef& rhs) {
    if (!lhs || !rhs || !lhs->isInteger() || !rhs->isInteger()) {
        return nullptr;
    }

    int width = std::max(lhs->bitWidth(), rhs->bitWidth());
    if (lhs->isSigned() && rhs->isSigned()) {
        switch (width) {
            case 8:
                return TypeInfo::makeI8();
            case 16:
                return TypeInfo::makeI16();
            case 32:
                return TypeInfo::makeI32();
            default:
                return TypeInfo::makeI64();
        }
    }

    switch (width) {
        case 8:
            return TypeInfo::makeU8();
        case 16:
            return TypeInfo::makeU16();
        case 32:
            return TypeInfo::makeU32();
        default:
            return TypeInfo::makeU64();
    }
}

bool hasStrictDirective(std::string_view source) {
    return source.rfind("#!strict", 0) == 0;
}

std::string lineLeadingContinuationMessage(TokenType type) {
    return "Continuation token '" +
           std::string(continuationTokenText(type)) +
           "' must stay on the previous line.";
}

std::string_view stripStrictDirectiveLine(std::string_view source) {
    if (!hasStrictDirective(source)) {
        return source;
    }

    size_t newlinePos = source.find('\n');
    if (newlinePos == std::string_view::npos) {
        return std::string_view();
    }

    return source.substr(newlinePos + 1);
}

struct NumericLiteralInfo {
    std::string core;
    TypeRef type;
    bool isUnsigned = false;
    bool isFloat = false;
    bool valid = false;
};

NumericLiteralInfo parseNumericLiteralInfo(const std::string& literal) {
    NumericLiteralInfo info;
    info.core = literal;
    info.valid = true;

    auto assignSuffix = [&](size_t suffixLength, const TypeRef& suffixType,
                            bool isUnsigned, bool isFloat) {
        info.type = suffixType;
        info.isUnsigned = isUnsigned;
        info.isFloat = isFloat;
        info.core = literal.substr(0, literal.length() - suffixLength);
    };

    if (literal.size() >= 5 &&
        literal.compare(literal.size() - 5, 5, "usize") == 0) {
        assignSuffix(5, TypeInfo::makeUSize(), true, false);
    } else if (literal.size() >= 3 &&
               literal.compare(literal.size() - 3, 3, "i16") == 0) {
        assignSuffix(3, TypeInfo::makeI16(), false, false);
    } else if (literal.size() >= 3 &&
               literal.compare(literal.size() - 3, 3, "i32") == 0) {
        assignSuffix(3, TypeInfo::makeI32(), false, false);
    } else if (literal.size() >= 3 &&
               literal.compare(literal.size() - 3, 3, "i64") == 0) {
        assignSuffix(3, TypeInfo::makeI64(), false, false);
    } else if (literal.size() >= 3 &&
               literal.compare(literal.size() - 3, 3, "u16") == 0) {
        assignSuffix(3, TypeInfo::makeU16(), true, false);
    } else if (literal.size() >= 3 &&
               literal.compare(literal.size() - 3, 3, "u32") == 0) {
        assignSuffix(3, TypeInfo::makeU32(), true, false);
    } else if (literal.size() >= 3 &&
               literal.compare(literal.size() - 3, 3, "u64") == 0) {
        assignSuffix(3, TypeInfo::makeU64(), true, false);
    } else if (literal.size() >= 3 &&
               literal.compare(literal.size() - 3, 3, "f32") == 0) {
        assignSuffix(3, TypeInfo::makeF32(), false, true);
    } else if (literal.size() >= 3 &&
               literal.compare(literal.size() - 3, 3, "f64") == 0) {
        assignSuffix(3, TypeInfo::makeF64(), false, true);
    } else if (literal.size() >= 2 &&
               literal.compare(literal.size() - 2, 2, "i8") == 0) {
        assignSuffix(2, TypeInfo::makeI8(), false, false);
    } else if (literal.size() >= 2 &&
               literal.compare(literal.size() - 2, 2, "u8") == 0) {
        assignSuffix(2, TypeInfo::makeU8(), true, false);
    } else if (!literal.empty() && literal.back() == 'u') {
        assignSuffix(1, TypeInfo::makeU32(), true, false);
    }

    if (info.core.empty()) {
        info.valid = false;
        return info;
    }

    const bool hasDecimalPoint = info.core.find('.') != std::string::npos;

    if (!info.type) {
        if (hasDecimalPoint) {
            info.type = TypeInfo::makeF64();
            info.isFloat = true;
        } else {
            info.type = TypeInfo::makeI32();
        }
    }

    if (info.type->isFloat()) {
        info.isFloat = true;
    }

    if (info.type->isInteger() && hasDecimalPoint) {
        info.valid = false;
    }

    return info;
}

bool validateNativePackageImport(const ImportTarget& importTarget,
                                 const NativePackageDescriptor& descriptor,
                                 std::string& outError) {
    outError.clear();

    if (importTarget.kind != ImportTargetKind::NATIVE_PACKAGE) {
        return true;
    }

    if (descriptor.packageNamespace != importTarget.packageNamespace ||
        descriptor.packageName != importTarget.packageName) {
        outError = "Native package '" + importTarget.rawSpecifier +
                   "' declared '" + descriptor.packageId +
                   "' in registration metadata.";
        return false;
    }

    return true;
}

bool reportAstFrontendFailure(AstFrontendBuildStatus status,
                              const std::vector<TypeError>& errors,
                              CompilerEmitterMode emitterMode) {
    if (status == AstFrontendBuildStatus::SemanticError) {
        for (const auto& error : errors) {
            std::cerr << "[error][compile][line " << error.line << "] "
                      << error.message << std::endl;
        }
        return true;
    }

    if (status == AstFrontendBuildStatus::ParseFailed) {
        for (const auto& error : errors) {
            std::cerr << "[error][compile][line " << error.line << "] "
                      << error.message << std::endl;
        }
        std::cerr << "[error][compile][line 1] AST frontend failed to parse "
                     "source";
        if (emitterMode == CompilerEmitterMode::ForceAst) {
            std::cerr << " for forced AST emission";
        }
        std::cerr << "." << std::endl;
        return true;
    }

    return false;
}
}  // namespace

bool Compiler::compile(std::string_view source, Chunk& chunk,
                       const std::string& sourcePath) {
    m_chunk = &chunk;
    m_sourcePath = sourcePath;
    m_classNames.clear();
    m_typeAliases.clear();
    m_functionSignatures.clear();
    m_classFieldTypes.clear();
    m_classMethodSignatures.clear();
    m_classOperatorMethods.clear();
    m_superclassOf.clear();
    registerStandardLibraryTypeSignatures(m_functionSignatures);

    AstFrontendResult astFrontend;
    std::vector<TypeError> astErrors;
    AstFrontendMode frontendMode = m_strictMode
                                       ? AstFrontendMode::StrictChecked
                                       : AstFrontendMode::LoweringOnly;
    AstFrontendBuildStatus astStatus =
        buildAstFrontend(source, frontendMode, astErrors, astFrontend);

    if (astStatus != AstFrontendBuildStatus::Success) {
        reportAstFrontendFailure(astStatus, astErrors, m_emitterMode);
        return false;
    }

    m_classNames = astFrontend.classNames;
    m_typeAliases = astFrontend.typeAliases;
    m_functionSignatures = astFrontend.functionSignatures;
    m_classFieldTypes = astFrontend.semanticModel.metadata.classFieldTypes;
    m_classMethodSignatures =
        astFrontend.semanticModel.metadata.classMethodSignatures;
    m_classOperatorMethods = astFrontend.semanticModel.classOperatorMethods;
    m_superclassOf = astFrontend.semanticModel.metadata.superclassOf;

    m_scanner.reset();
    m_parser = std::make_unique<Parser>();
    m_currentClass = nullptr;
    m_contexts.clear();
    m_globalSlots.clear();
    m_globalTypes.clear();
    m_globalConstness.clear();
    m_exprTypeStack.clear();
    m_globalNames.clear();
    m_exportedNames.clear();
    m_bufferedTokens.clear();
    m_contexts.push_back(
        FunctionContext{{}, {}, 0, false, false, TypeInfo::makeAny()});

    AstBytecodeEmitter emitter(*this, astFrontend);
    return emitter.emitModule();
}

TypeRef Compiler::tokenToType(const Token& token) const {
    switch (token.type()) {
        case TokenType::TYPE_I8:
            return TypeInfo::makeI8();
        case TokenType::TYPE_I16:
            return TypeInfo::makeI16();
        case TokenType::TYPE_I32:
            return TypeInfo::makeI32();
        case TokenType::TYPE_I64:
            return TypeInfo::makeI64();
        case TokenType::TYPE_U8:
            return TypeInfo::makeU8();
        case TokenType::TYPE_U16:
            return TypeInfo::makeU16();
        case TokenType::TYPE_U32:
            return TypeInfo::makeU32();
        case TokenType::TYPE_U64:
            return TypeInfo::makeU64();
        case TokenType::TYPE_USIZE:
            return TypeInfo::makeUSize();
        case TokenType::TYPE_F32:
            return TypeInfo::makeF32();
        case TokenType::TYPE_F64:
            return TypeInfo::makeF64();
        case TokenType::TYPE_BOOL:
            return TypeInfo::makeBool();
        case TokenType::TYPE_STR:
            return TypeInfo::makeStr();
        case TokenType::TYPE_FN:
            return nullptr;
        case TokenType::TYPE_VOID:
            return TypeInfo::makeVoid();
        case TokenType::TYPE_NULL_KW:
            return TypeInfo::makeNull();
        case TokenType::IDENTIFIER: {
            std::string className(token.start(), token.length());
            auto aliasIt = m_typeAliases.find(className);
            if (aliasIt != m_typeAliases.end()) {
                return aliasIt->second;
            }
            if (m_classNames.find(className) != m_classNames.end()) {
                return TypeInfo::makeClass(className);
            }
            return nullptr;
        }
        default:
            return nullptr;
    }
}

bool Compiler::resolveImportExportTypes(
    const ImportTarget& importTarget,
    std::unordered_map<std::string, TypeRef>& outExportTypes,
    std::string& outError) {
    outExportTypes.clear();

    if (importTarget.kind == ImportTargetKind::NATIVE_PACKAGE) {
        NativePackageDescriptor packageDescriptor;
        if (!loadNativePackageDescriptor(importTarget.resolvedPath,
                                         packageDescriptor, outError, false,
                                         nullptr)) {
            return false;
        }

        if (!validateNativePackageImport(importTarget, packageDescriptor,
                                         outError)) {
            return false;
        }

        outExportTypes = std::move(packageDescriptor.exportTypes);
        return true;
    }

    std::ifstream file(importTarget.resolvedPath);
    if (!file) {
        outError = "Failed to open module '" + importTarget.resolvedPath + "'.";
        return false;
    }

    std::string source((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());

    Chunk chunk;
    Compiler moduleCompiler;
    moduleCompiler.setGC(m_gc);
    moduleCompiler.setPackageSearchPaths(m_packageSearchPaths);
    moduleCompiler.setEmitterMode(m_emitterMode);
    bool moduleStrict = m_strictMode || hasStrictDirective(source);
    std::string_view compileSource = stripStrictDirectiveLine(source);
    moduleCompiler.setStrictMode(moduleStrict);
    if (!moduleCompiler.compile(compileSource, chunk,
                                importTarget.resolvedPath)) {
        outError = "Failed to type-check imported module '" +
                   importTarget.resolvedPath + "'.";
        return false;
    }

    std::unordered_map<std::string, TypeRef> moduleGlobals;
    const auto& names = moduleCompiler.globalNames();
    const auto& types = moduleCompiler.globalTypes();
    for (size_t i = 0; i < names.size(); ++i) {
        moduleGlobals[names[i]] =
            i < types.size() && types[i] ? types[i] : TypeInfo::makeAny();
    }

    for (const auto& name : moduleCompiler.exportedNames()) {
        auto it = moduleGlobals.find(name);
        outExportTypes[name] =
            it != moduleGlobals.end() ? it->second : TypeInfo::makeAny();
    }

    return true;
}

void Compiler::emitByte(uint8_t byte) {
    currentChunk()->write(byte, m_parser->previous.line());
}

void Compiler::emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

void Compiler::emitByte(uint8_t byte, size_t line) {
    currentChunk()->write(byte, line);
}

void Compiler::emitBytes(uint8_t byte1, uint8_t byte2, size_t line) {
    emitByte(byte1, line);
    emitByte(byte2, line);
}

uint8_t Compiler::parseCallArguments(std::vector<TypeRef>& argumentTypes) {
    uint8_t argCount = 0;
    if (m_parser->current.type() != TokenType::CLOSE_PAREN) {
        do {
            expression();
            argumentTypes.push_back(popExprType());
            if (argCount == UINT8_MAX) {
                errorAtCurrent("Cannot have more than 255 arguments.");
                break;
            }
            argCount++;

            if (m_parser->current.type() != TokenType::COMMA) {
                break;
            }
            advance();
        } while (true);
    }

    consume(TokenType::CLOSE_PAREN, "Expected ')' after arguments.");
    return argCount;
}

void Compiler::pushCallResultType(const TypeRef& calleeType) {
    if (calleeType && calleeType->kind == TypeKind::FUNCTION &&
        calleeType->returnType) {
        pushExprType(calleeType->returnType);
    } else {
        pushExprType(TypeInfo::makeAny());
    }
}

void Compiler::emitInvokeCall(uint8_t invokeOpcode, uint8_t name,
                              const TypeRef& calleeType) {
    consume(TokenType::OPEN_PAREN, "Expected '(' after method name.");

    std::vector<TypeRef> argumentTypes;
    uint8_t argCount = parseCallArguments(argumentTypes);
    emitByte(invokeOpcode);
    emitByte(name);
    emitByte(argCount);
    pushCallResultType(calleeType);
}

void Compiler::emitReturn() {
    emitByte(OpCode::NIL);
    emitByte(OpCode::RETURN);
}

Value Compiler::makeStringValue(const std::string& text) {
    if (m_gc == nullptr) {
        errorAtCurrent("Internal compiler error: GC allocator unavailable.");
        return Value();
    }

    return Value(m_gc->internString(text));
}

uint8_t Compiler::makeConstant(Value value) {
    int constant = currentChunk()->addConstant(value);
    if (constant > UINT8_MAX) {
        errorAtCurrent("Too many constants in one chunk.");
        return 0;
    }

    return static_cast<uint8_t>(constant);
}

void Compiler::emitConstant(Value value) {
    emitBytes(OpCode::CONSTANT, makeConstant(value));
}

int Compiler::emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count() - 2;
}

void Compiler::patchJump(int offset) {
    int jump = currentChunk()->count() - offset - 2;
    if (jump > UINT16_MAX) {
        errorAtCurrent("Too much code to jump over.");
        return;
    }

    currentChunk()->setByteAt(offset, static_cast<uint8_t>((jump >> 8) & 0xff));
    currentChunk()->setByteAt(offset + 1, static_cast<uint8_t>(jump & 0xff));
}

void Compiler::emitLoop(int loopStart) {
    emitByte(OpCode::LOOP);

    int offset = currentChunk()->count() - loopStart + 2;
    if (offset > UINT16_MAX) {
        errorAtCurrent("Loop body too large.");
        return;
    }

    emitByte(static_cast<uint8_t>((offset >> 8) & 0xff));
    emitByte(static_cast<uint8_t>(offset & 0xff));
}

bool Compiler::isAssignmentOperator(TokenType type) const {
    switch (type) {
        case TokenType::EQUAL:
        case TokenType::PLUS_EQUAL:
        case TokenType::MINUS_EQUAL:
        case TokenType::STAR_EQUAL:
        case TokenType::SLASH_EQUAL:
        case TokenType::AMPERSAND_EQUAL:
        case TokenType::CARET_EQUAL:
        case TokenType::PIPE_EQUAL:
        case TokenType::SHIFT_LEFT_EQUAL:
        case TokenType::SHIFT_RIGHT_EQUAL:
        case TokenType::PLUS_PLUS:
        case TokenType::MINUS_MINUS:
            return true;
        default:
            return false;
    }
}

void Compiler::pushExprType(const TypeRef& type) {
    m_exprTypeStack.push_back(type ? type : TypeInfo::makeAny());
}

TypeRef Compiler::popExprType() {
    if (m_exprTypeStack.empty()) {
        return TypeInfo::makeAny();
    }

    TypeRef type = m_exprTypeStack.back();
    m_exprTypeStack.pop_back();
    return type ? type : TypeInfo::makeAny();
}

TypeRef Compiler::peekExprType() const {
    if (m_exprTypeStack.empty()) {
        return TypeInfo::makeAny();
    }
    return m_exprTypeStack.back() ? m_exprTypeStack.back()
                                  : TypeInfo::makeAny();
}

uint8_t Compiler::arithmeticOpcode(TokenType operatorType,
                                   const TypeRef& numericType) const {
    if (m_strictMode && numericType && numericType->isInteger()) {
        const bool isSigned = numericType->isSigned();
        switch (operatorType) {
            case TokenType::PLUS:
            case TokenType::PLUS_EQUAL:
                return isSigned ? OpCode::IADD : OpCode::UADD;
            case TokenType::MINUS:
            case TokenType::MINUS_EQUAL:
                return isSigned ? OpCode::ISUB : OpCode::USUB;
            case TokenType::STAR:
            case TokenType::STAR_EQUAL:
                return isSigned ? OpCode::IMULT : OpCode::UMULT;
            case TokenType::SLASH:
            case TokenType::SLASH_EQUAL:
                return isSigned ? OpCode::IDIV : OpCode::UDIV;
            default:
                break;
        }
    }

    switch (operatorType) {
        case TokenType::PLUS:
        case TokenType::PLUS_EQUAL:
            return OpCode::ADD;
        case TokenType::MINUS:
        case TokenType::MINUS_EQUAL:
            return OpCode::SUB;
        case TokenType::STAR:
        case TokenType::STAR_EQUAL:
            return OpCode::MULT;
        case TokenType::SLASH:
        case TokenType::SLASH_EQUAL:
            return OpCode::DIV;
        default:
            return OpCode::ADD;
    }
}

TypeRef Compiler::inferVariableType(const Token& name) const {
    const auto& locals = currentContext().locals;
    for (int index = static_cast<int>(locals.size()) - 1; index >= 0; --index) {
        if (identifiersEqual(name, locals[index].name)) {
            return locals[index].type ? locals[index].type
                                      : TypeInfo::makeAny();
        }
    }

    std::string variableName(name.start(), name.length());
    auto slotIt = m_globalSlots.find(variableName);
    if (slotIt != m_globalSlots.end()) {
        size_t slot = slotIt->second;
        if (slot < m_globalTypes.size() && m_globalTypes[slot]) {
            return m_globalTypes[slot];
        }
    }

    return TypeInfo::makeAny();
}

TypeRef Compiler::lookupClassFieldType(const std::string& className,
                                       const std::string& fieldName) const {
    std::string current = className;
    std::unordered_set<std::string> visited;

    while (!current.empty() && visited.emplace(current).second) {
        auto classFields = m_classFieldTypes.find(current);
        if (classFields != m_classFieldTypes.end()) {
            auto fieldIt = classFields->second.find(fieldName);
            if (fieldIt != classFields->second.end() && fieldIt->second) {
                return fieldIt->second;
            }
        }

        auto superIt = m_superclassOf.find(current);
        if (superIt == m_superclassOf.end()) {
            break;
        }

        current = superIt->second;
    }

    return nullptr;
}

TypeRef Compiler::lookupClassMethodType(const std::string& className,
                                        const std::string& methodName) const {
    std::string current = className;
    std::unordered_set<std::string> visited;

    while (!current.empty() && visited.emplace(current).second) {
        auto classMethods = m_classMethodSignatures.find(current);
        if (classMethods != m_classMethodSignatures.end()) {
            auto methodIt = classMethods->second.find(methodName);
            if (methodIt != classMethods->second.end() && methodIt->second) {
                return methodIt->second;
            }
        }

        auto superIt = m_superclassOf.find(current);
        if (superIt == m_superclassOf.end()) {
            break;
        }

        current = superIt->second;
    }

    return nullptr;
}

int Compiler::lookupClassFieldSlot(const std::string& className,
                                   const std::string& fieldName) const {
    std::vector<std::string> orderedFieldNames;
    std::unordered_set<std::string> seenFields;
    std::unordered_set<std::string> visited;
    std::vector<std::string> lineage;
    std::string current = className;

    while (!current.empty() && visited.emplace(current).second) {
        lineage.push_back(current);
        auto superIt = m_superclassOf.find(current);
        if (superIt == m_superclassOf.end()) {
            break;
        }
        current = superIt->second;
    }

    for (auto it = lineage.rbegin(); it != lineage.rend(); ++it) {
        auto classFields = m_classFieldTypes.find(*it);
        if (classFields == m_classFieldTypes.end()) {
            continue;
        }

        std::vector<std::string> ownFieldNames;
        ownFieldNames.reserve(classFields->second.size());
        for (const auto& [name, type] : classFields->second) {
            (void)type;
            if (seenFields.emplace(name).second) {
                ownFieldNames.push_back(name);
            }
        }

        std::sort(ownFieldNames.begin(), ownFieldNames.end());
        orderedFieldNames.insert(orderedFieldNames.end(), ownFieldNames.begin(),
                                 ownFieldNames.end());
    }

    for (size_t index = 0; index < orderedFieldNames.size(); ++index) {
        if (orderedFieldNames[index] == fieldName) {
            return static_cast<int>(index);
        }
    }

    return -1;
}

void Compiler::emitCoerceToType(const TypeRef& sourceType,
                                const TypeRef& targetType) {
    if (!sourceType || !targetType || sourceType->isAny() ||
        targetType->isAny()) {
        return;
    }

    if (sourceType->kind == targetType->kind) {
        return;
    }

    if (sourceType->isInteger() && targetType->isInteger()) {
        const bool sameSignedness =
            sourceType->isSigned() == targetType->isSigned();
        if (sameSignedness && sourceType->bitWidth() < targetType->bitWidth()) {
            emitByte(OpCode::WIDEN_INT);
            emitByte(static_cast<uint8_t>(targetType->kind));
        }
        return;
    }

    if (sourceType->isInteger() && targetType->isFloat()) {
        emitByte(OpCode::INT_TO_FLOAT);
        return;
    }
}

void Compiler::emitCheckInstanceType(const TypeRef& targetType) {
    if (!targetType || targetType->kind != TypeKind::CLASS ||
        targetType->className.empty()) {
        return;
    }

    emitBytes(OpCode::CHECK_INSTANCE_TYPE,
              makeConstant(makeStringValue(targetType->className)));
}

bool Compiler::emitCompoundBinary(TokenType assignmentType,
                                  const TypeRef& leftType,
                                  const TypeRef& rightType) {
    TypeRef promoted = numericPromotion(leftType, rightType);
    TypeRef arithmeticType = promoted ? promoted : leftType;

    switch (assignmentType) {
        case TokenType::PLUS_EQUAL:
        case TokenType::MINUS_EQUAL:
        case TokenType::STAR_EQUAL:
        case TokenType::SLASH_EQUAL:
            emitByte(arithmeticOpcode(assignmentType, arithmeticType));
            return true;
        case TokenType::AMPERSAND_EQUAL:
            emitByte(OpCode::BITWISE_AND);
            return true;
        case TokenType::CARET_EQUAL:
            emitByte(OpCode::BITWISE_XOR);
            return true;
        case TokenType::PIPE_EQUAL:
            emitByte(OpCode::BITWISE_OR);
            return true;
        case TokenType::SHIFT_LEFT_EQUAL:
            emitByte(OpCode::SHIFT_LEFT);
            return true;
        case TokenType::SHIFT_RIGHT_EQUAL:
            emitByte(OpCode::SHIFT_RIGHT);
            return true;
        default:
            return false;
    }
}

uint8_t Compiler::identifierConstant(const Token& name) {
    return makeConstant(
        makeStringValue(std::string(name.start(), name.length())));
}

uint8_t Compiler::globalSlot(const Token& name) {
    std::string globalName(name.start(), name.length());
    auto slotIt = m_globalSlots.find(globalName);
    if (slotIt != m_globalSlots.end()) {
        return slotIt->second;
    }

    if (m_globalNames.size() >= (static_cast<size_t>(UINT8_MAX) + 1)) {
        errorAt(name, "Too many global variables.");
        return 0;
    }

    uint8_t slot = static_cast<uint8_t>(m_globalNames.size());
    m_globalSlots.emplace(globalName, slot);
    m_globalNames.push_back(std::move(globalName));
    m_globalTypes.push_back(TypeInfo::makeAny());
    m_globalConstness.push_back(false);
    return slot;
}

Compiler::ResolvedVariable Compiler::resolveNamedVariable(const Token& name) {
    uint8_t arg = 0;
    uint8_t getOp = OpCode::GET_GLOBAL_SLOT;
    uint8_t setOp = OpCode::SET_GLOBAL_SLOT;
    TypeRef declaredType = TypeInfo::makeAny();
    bool isConst = false;

    int local = resolveLocal(name);
    if (local != -1) {
        getOp = OpCode::GET_LOCAL;
        setOp = OpCode::SET_LOCAL;
        arg = static_cast<uint8_t>(local);
        declaredType = currentContext().locals[local].type;
        isConst = currentContext().locals[local].isConst;
    } else {
        int upvalue =
            resolveUpvalue(name, static_cast<int>(m_contexts.size()) - 1);
        if (upvalue != -1) {
            getOp = OpCode::GET_UPVALUE;
            setOp = OpCode::SET_UPVALUE;
            arg = static_cast<uint8_t>(upvalue);
            declaredType = currentContext().upvalues[upvalue].type
                               ? currentContext().upvalues[upvalue].type
                               : TypeInfo::makeAny();
            isConst = currentContext().upvalues[upvalue].isConst;
        } else {
            arg = globalSlot(name);
            if (arg < m_globalTypes.size()) {
                declaredType = m_globalTypes[arg] ? m_globalTypes[arg]
                                                  : TypeInfo::makeAny();
            }
            if (arg < m_globalConstness.size()) {
                isConst = m_globalConstness[arg];
            }
        }
    }

    return ResolvedVariable{arg, getOp, setOp, declaredType, isConst};
}

void Compiler::namedVariable(const Token& name, bool canAssign) {
    ResolvedVariable resolved = resolveNamedVariable(name);
    uint8_t arg = resolved.arg;
    uint8_t getOp = resolved.getOp;
    uint8_t setOp = resolved.setOp;
    TypeRef declaredType = resolved.type;

    if (canAssign) {
        TokenType assignmentType = m_parser->current.type();

        if (assignmentType == TokenType::EQUAL) {
            if (resolved.isConst) {
                errorAt(name, "Cannot assign to const variable '" +
                                  std::string(name.start(), name.length()) +
                                  "'.");
                advance();
                expression();
                popExprType();
                pushExprType(declaredType);
                return;
            }
            advance();
            expression();
            TypeRef rhsType = popExprType();
            emitCoerceToType(rhsType, declaredType);
            emitCheckInstanceType(declaredType);
            emitBytes(setOp, arg);
            pushExprType(declaredType && !declaredType->isAny() ? declaredType
                                                                : rhsType);
            return;
        }

        if (assignmentType == TokenType::PLUS_PLUS ||
            assignmentType == TokenType::MINUS_MINUS) {
            if (resolved.isConst) {
                errorAt(name, "Cannot assign to const variable '" +
                                  std::string(name.start(), name.length()) +
                                  "'.");
                advance();
                pushExprType(declaredType);
                return;
            }
            advance();
            emitBytes(getOp, arg);
            emitConstant(Value(static_cast<int64_t>(1)));
            emitByte(arithmeticOpcode(assignmentType == TokenType::PLUS_PLUS
                                          ? TokenType::PLUS
                                          : TokenType::MINUS,
                                      declaredType));
            emitCoerceToType(declaredType, declaredType);
            emitBytes(setOp, arg);
            pushExprType(declaredType);
            return;
        }

        if (assignmentType == TokenType::PLUS_EQUAL ||
            assignmentType == TokenType::MINUS_EQUAL ||
            assignmentType == TokenType::STAR_EQUAL ||
            assignmentType == TokenType::SLASH_EQUAL ||
            assignmentType == TokenType::AMPERSAND_EQUAL ||
            assignmentType == TokenType::CARET_EQUAL ||
            assignmentType == TokenType::PIPE_EQUAL ||
            assignmentType == TokenType::SHIFT_LEFT_EQUAL ||
            assignmentType == TokenType::SHIFT_RIGHT_EQUAL) {
            if (resolved.isConst) {
                errorAt(name, "Cannot assign to const variable '" +
                                  std::string(name.start(), name.length()) +
                                  "'.");
                advance();
                expression();
                popExprType();
                pushExprType(declaredType);
                return;
            }
            advance();
            emitBytes(getOp, arg);
            expression();
            TypeRef rhsType = popExprType();
            emitCompoundBinary(assignmentType, declaredType, rhsType);
            TypeRef resultType = declaredType;
            if (assignmentType == TokenType::PLUS_EQUAL ||
                assignmentType == TokenType::MINUS_EQUAL ||
                assignmentType == TokenType::STAR_EQUAL ||
                assignmentType == TokenType::SLASH_EQUAL) {
                resultType = numericPromotion(declaredType, rhsType);
            } else if (isBitwiseAssignmentOperator(assignmentType)) {
                resultType = bitwiseResultType(declaredType, rhsType);
            }
            emitCoerceToType(resultType ? resultType : declaredType,
                             declaredType);
            emitBytes(setOp, arg);
            pushExprType(declaredType);
            return;
        }
    }

    emitBytes(getOp, arg);
    pushExprType(declaredType);
}

uint8_t Compiler::parseVariable(const std::string& message,
                                const TypeRef& declaredType, bool isConst) {
    consume(TokenType::IDENTIFIER, message);

    TypeRef normalized = declaredType ? declaredType : TypeInfo::makeAny();
    if (currentContext().scopeDepth > 0) {
        addLocal(m_parser->previous, normalized, isConst);
        return 0;
    }

    uint8_t slot = globalSlot(m_parser->previous);
    if (slot < m_globalTypes.size()) {
        m_globalTypes[slot] = normalized;
    }
    if (slot < m_globalConstness.size()) {
        m_globalConstness[slot] = isConst;
    }

    return slot;
}

void Compiler::defineVariable(uint8_t global) {
    if (currentContext().scopeDepth > 0) {
        markInitialized();
        return;
    }

    emitBytes(OpCode::DEFINE_GLOBAL_SLOT, global);
}

void Compiler::beginScope() { currentContext().scopeDepth++; }

void Compiler::endScope() {
    currentContext().scopeDepth--;

    while (!currentContext().locals.empty() &&
           currentContext().locals.back().depth > currentContext().scopeDepth) {
        if (currentContext().locals.back().isCaptured) {
            emitByte(OpCode::CLOSE_UPVALUE);
        } else {
            emitByte(OpCode::POP);
        }
        currentContext().locals.pop_back();
    }
}

void Compiler::addLocal(const Token& name, const TypeRef& declaredType,
                        bool isConst) {
    if (currentContext().locals.size() > UINT8_MAX) {
        errorAt(name, "Too many local variables in function.");
        return;
    }

    for (auto it = currentContext().locals.rbegin();
         it != currentContext().locals.rend(); ++it) {
        if (it->depth != -1 && it->depth < currentContext().scopeDepth) {
            break;
        }

        if (identifiersEqual(name, it->name)) {
            errorAt(name,
                    "Variable with this name already declared in this scope.");
            return;
        }
    }

    currentContext().locals.push_back(
        Local{name, -1, false, declaredType ? declaredType : TypeInfo::makeAny(),
              isConst});
}

int Compiler::resolveLocal(const Token& name) {
    return resolveLocalInContext(name, static_cast<int>(m_contexts.size()) - 1);
}

int Compiler::resolveLocalInContext(const Token& name, int contextIndex) {
    auto& locals = m_contexts[contextIndex].locals;
    for (int index = static_cast<int>(locals.size()) - 1; index >= 0; --index) {
        if (!identifiersEqual(name, locals[index].name)) {
            continue;
        }

        if (locals[index].depth == -1) {
            errorAt(name, "Cannot read local variable in its own initializer.");
        }
        return index;
    }

    return -1;
}

int Compiler::addUpvalue(int contextIndex, uint8_t index, bool isLocal,
                         const TypeRef& type, bool isConst) {
    auto& upvalues = m_contexts[contextIndex].upvalues;
    for (size_t i = 0; i < upvalues.size(); ++i) {
        if (upvalues[i].index == index && upvalues[i].isLocal == isLocal) {
            return static_cast<int>(i);
        }
    }

    if (upvalues.size() >= UINT8_MAX) {
        errorAtCurrent("Too many closure variables in function.");
        return -1;
    }

    upvalues.push_back(
        Upvalue{index, isLocal, type ? type : TypeInfo::makeAny(), isConst});
    return static_cast<int>(upvalues.size()) - 1;
}

int Compiler::resolveUpvalue(const Token& name, int contextIndex) {
    if (contextIndex <= 0) {
        return -1;
    }

    int local = resolveLocalInContext(name, contextIndex - 1);
    if (local != -1) {
        m_contexts[contextIndex - 1].locals[local].isCaptured = true;
        const auto& localInfo = m_contexts[contextIndex - 1].locals[local];
        return addUpvalue(contextIndex, static_cast<uint8_t>(local), true,
                          localInfo.type, localInfo.isConst);
    }

    int upvalue = resolveUpvalue(name, contextIndex - 1);
    if (upvalue != -1) {
        const auto& upvalueInfo = m_contexts[contextIndex - 1].upvalues[upvalue];
        return addUpvalue(
            contextIndex, static_cast<uint8_t>(upvalue), false,
            upvalueInfo.type, upvalueInfo.isConst);
    }

    return -1;
}

void Compiler::markInitialized() {
    if (currentContext().scopeDepth == 0 || currentContext().locals.empty()) {
        return;
    }

    currentContext().locals.back().depth = currentContext().scopeDepth;
}

bool Compiler::identifiersEqual(const Token& lhs, const Token& rhs) const {
    if (lhs.length() != rhs.length()) {
        return false;
    }

    std::string_view lhsView(lhs.start(), lhs.length());
    std::string_view rhsView(rhs.start(), rhs.length());
    return lhsView == rhsView;
}

bool Compiler::isTypeToken(TokenType type) const {
    switch (type) {
        case TokenType::TYPE_I8:
        case TokenType::TYPE_I16:
        case TokenType::TYPE_I32:
        case TokenType::TYPE_I64:
        case TokenType::TYPE_U8:
        case TokenType::TYPE_U16:
        case TokenType::TYPE_U32:
        case TokenType::TYPE_U64:
        case TokenType::TYPE_USIZE:
        case TokenType::TYPE_F32:
        case TokenType::TYPE_F64:
        case TokenType::TYPE_BOOL:
        case TokenType::TYPE_STR:
        case TokenType::TYPE_FN:
        case TokenType::TYPE_VOID:
        case TokenType::TYPE_NULL_KW:
            return true;
        default:
            return false;
    }
}

bool Compiler::isCollectionTypeName(const Token& token) const {
    if (token.type() != TokenType::IDENTIFIER) {
        return false;
    }

    std::string_view name(token.start(), token.length());
    return isCollectionTypeNameText(name);
}

bool Compiler::isTypedTypeAnnotationStart() {
    if (m_parser->current.type() == TokenType::TYPE_FN) {
        return peekNextToken().type() == TokenType::OPEN_PAREN;
    }

    if (isTypeToken(m_parser->current.type())) {
        return true;
    }

    if (m_parser->current.type() != TokenType::IDENTIFIER) {
        return false;
    }

    const Token& next = peekNextToken();
    if (isHandleTypeNameText(
            std::string_view(m_parser->current.start(),
                             m_parser->current.length()))) {
        return next.type() == TokenType::LESS;
    }

    if (isCollectionTypeName(m_parser->current)) {
        return next.type() == TokenType::LESS;
    }

    return tokenToType(m_parser->current) != nullptr;
}

bool Compiler::parseTypeExpr() { return parseTypeExprType() != nullptr; }

TypeRef Compiler::parseTypeExprType() {
    auto applyOptionalSuffix = [this](TypeRef baseType) -> TypeRef {
        if (!baseType) {
            return nullptr;
        }

        while (m_parser->current.type() == TokenType::QUESTION) {
            advance();
            baseType = TypeInfo::makeOptional(baseType);
        }

        return baseType;
    };

    if (m_parser->current.type() == TokenType::TYPE_FN) {
        advance();
        consume(TokenType::OPEN_PAREN, "Expected '(' after 'fn'.");

        std::vector<TypeRef> paramTypes;
        if (m_parser->current.type() != TokenType::CLOSE_PAREN) {
            while (true) {
                TypeRef paramType = parseTypeExprType();
                if (!paramType) {
                    errorAtCurrent("Expected parameter type in function type.");
                    return nullptr;
                }
                if (paramType->isVoid()) {
                    errorAtCurrent("Function type parameter cannot be 'void'.");
                    return nullptr;
                }
                paramTypes.push_back(paramType);

                if (m_parser->current.type() == TokenType::COMMA) {
                    advance();
                    continue;
                }
                break;
            }
        }

        consume(TokenType::CLOSE_PAREN,
                "Expected ')' after function type parameters.");
        TypeRef returnType = parseTypeExprType();
        if (!returnType) {
            errorAtCurrent("Expected function return type.");
            return nullptr;
        }

        return applyOptionalSuffix(
            TypeInfo::makeFunction(paramTypes, returnType));
    }

    if (isTypeToken(m_parser->current.type())) {
        TypeRef type = tokenToType(m_parser->current);
        advance();
        return applyOptionalSuffix(type);
    }

    if (m_parser->current.type() == TokenType::IDENTIFIER) {
        Token identifierToken = m_parser->current;
        std::string name(identifierToken.start(), identifierToken.length());

        if (isHandleTypeNameText(name)) {
            advance();
            consume(TokenType::LESS, "Expected '<' after 'handle'.");
            consume(TokenType::IDENTIFIER,
                    "Expected package namespace in handle type.");
            std::string packageNamespace(m_parser->previous.start(),
                                         m_parser->previous.length());
            consume(TokenType::COLON,
                    "Expected ':' after handle package namespace.");
            consume(TokenType::IDENTIFIER,
                    "Expected package name in handle type.");
            std::string packageName(m_parser->previous.start(),
                                    m_parser->previous.length());
            consume(TokenType::COLON,
                    "Expected ':' before handle type name.");
            consume(TokenType::IDENTIFIER, "Expected native handle type name.");
            std::string typeName(m_parser->previous.start(),
                                 m_parser->previous.length());
            consume(TokenType::GREATER, "Expected '>' after handle type.");

            if (!isValidPackageIdPart(packageNamespace) ||
                !isValidPackageIdPart(packageName) ||
                !isValidHandleTypeName(typeName)) {
                errorAt(identifierToken,
                        "Handle type must use handle<namespace:name:Type> "
                        "with lowercase package IDs and an alphanumeric type "
                        "name.");
                return nullptr;
            }

            return applyOptionalSuffix(TypeInfo::makeNativeHandle(
                makePackageId(packageNamespace, packageName), typeName));
        }

        if (isCollectionTypeName(identifierToken)) {
            advance();
            consume(TokenType::LESS,
                    "Expected '<' after collection type name.");

            if (name == "Array") {
                TypeRef elementType = parseTypeExprType();
                if (!elementType) {
                    errorAtCurrent("Expected element type in Array<T>.");
                    return nullptr;
                }
                if (elementType->isVoid()) {
                    errorAtCurrent(
                        "'void' is not valid as an Array element type.");
                    return nullptr;
                }
                consume(TokenType::GREATER, "Expected '>' after Array type.");
                return applyOptionalSuffix(TypeInfo::makeArray(elementType));
            }

            if (name == "Set") {
                TypeRef elementType = parseTypeExprType();
                if (!elementType) {
                    errorAtCurrent("Expected element type in Set<T>.");
                    return nullptr;
                }
                if (elementType->isVoid()) {
                    errorAtCurrent(
                        "'void' is not valid as a Set element type.");
                    return nullptr;
                }
                consume(TokenType::GREATER, "Expected '>' after Set type.");
                return applyOptionalSuffix(TypeInfo::makeSet(elementType));
            }

            TypeRef keyType = parseTypeExprType();
            if (!keyType) {
                errorAtCurrent("Expected key type in Dict<K, V>.");
                return nullptr;
            }
            if (keyType->isVoid()) {
                errorAtCurrent("'void' is not valid as a Dict key type.");
                return nullptr;
            }
            consume(TokenType::COMMA, "Expected ',' in Dict<K, V> type.");
            TypeRef valueType = parseTypeExprType();
            if (!valueType) {
                errorAtCurrent("Expected value type in Dict<K, V>.");
                return nullptr;
            }
            if (valueType->isVoid()) {
                errorAtCurrent("'void' is not valid as a Dict value type.");
                return nullptr;
            }
            consume(TokenType::GREATER, "Expected '>' after Dict type.");
            return applyOptionalSuffix(TypeInfo::makeDict(keyType, valueType));
        }

        TypeRef type = tokenToType(identifierToken);
        if (!type) return nullptr;
        advance();
        return applyOptionalSuffix(type);
    }

    return nullptr;
}

bool Compiler::isTypedVarDeclarationStart() {
    if (m_parser->current.type() == TokenType::TYPE_FN) {
        return looksLikeFunctionTypeDeclarationStart();
    }

    if (isTypeToken(m_parser->current.type())) {
        const TokenType nextType = peekNextToken().type();
        return nextType == TokenType::IDENTIFIER ||
               nextType == TokenType::QUESTION;
    }

    if (m_parser->current.type() != TokenType::IDENTIFIER) {
        return false;
    }

    const Token& next = peekNextToken();
    if (isHandleTypeNameText(
            std::string_view(m_parser->current.start(),
                             m_parser->current.length()))) {
        return next.type() == TokenType::LESS;
    }

    if (isCollectionTypeName(m_parser->current)) {
        return next.type() == TokenType::LESS;
    }

    return tokenToType(m_parser->current) != nullptr &&
           (next.type() == TokenType::IDENTIFIER ||
            next.type() == TokenType::QUESTION);
}

void Compiler::advance() {
    m_parser->previous = m_parser->current;

    while (true) {
        if (!m_bufferedTokens.empty()) {
            m_parser->current = m_bufferedTokens.front();
            m_bufferedTokens.pop_front();
        } else {
            m_parser->current = m_scanner->nextToken();
        }

        if (m_parser->current.type() != TokenType::ERROR) break;

        errorAtCurrent(m_parser->current.start());
    }
}

const Token& Compiler::peekToken(size_t offset) {
    while (m_bufferedTokens.size() < offset) {
        while (true) {
            Token token = m_scanner->nextToken();
            if (token.type() != TokenType::ERROR) {
                m_bufferedTokens.push_back(token);
                break;
            }

            errorAt(token, token.start());
        }
    }

    return m_bufferedTokens[offset - 1];
}

const Token& Compiler::peekNextToken() {
    return peekToken();
}

const Token& Compiler::tokenAt(size_t offset) {
    if (offset == 0) {
        return m_parser->current;
    }

    return peekToken(offset);
}

bool Compiler::parseTypeLookahead(size_t& offset) {
    auto consumeOptionalSuffix = [&]() {
        while (tokenAt(offset).type() == TokenType::QUESTION) {
            ++offset;
        }
    };

    const Token& current = tokenAt(offset);
    if (current.type() == TokenType::TYPE_FN) {
        ++offset;
        if (tokenAt(offset).type() != TokenType::OPEN_PAREN) {
            return false;
        }

        ++offset;
        if (tokenAt(offset).type() != TokenType::CLOSE_PAREN) {
            while (true) {
                if (!parseTypeLookahead(offset)) {
                    return false;
                }
                if (tokenAt(offset).type() == TokenType::COMMA) {
                    ++offset;
                    continue;
                }
                if (tokenAt(offset).type() != TokenType::CLOSE_PAREN) {
                    return false;
                }
                break;
            }
        }

        ++offset;
        if (!parseTypeLookahead(offset)) {
            return false;
        }

        consumeOptionalSuffix();
        return true;
    }

    if (isTypeToken(current.type())) {
        ++offset;
        consumeOptionalSuffix();
        return true;
    }

    if (current.type() != TokenType::IDENTIFIER) {
        return false;
    }

    std::string_view name(current.start(), current.length());
    ++offset;

    if (isHandleTypeNameText(name)) {
        if (tokenAt(offset).type() != TokenType::LESS) {
            return false;
        }
        ++offset;

        if (tokenAt(offset).type() != TokenType::IDENTIFIER) {
            return false;
        }
        std::string_view packageNamespace(tokenAt(offset).start(),
                                          tokenAt(offset).length());
        ++offset;
        if (tokenAt(offset).type() != TokenType::COLON ||
            !isValidPackageIdPart(packageNamespace)) {
            return false;
        }
        ++offset;

        if (tokenAt(offset).type() != TokenType::IDENTIFIER) {
            return false;
        }
        std::string_view packageName(tokenAt(offset).start(),
                                     tokenAt(offset).length());
        ++offset;
        if (tokenAt(offset).type() != TokenType::COLON ||
            !isValidPackageIdPart(packageName)) {
            return false;
        }
        ++offset;

        if (tokenAt(offset).type() != TokenType::IDENTIFIER) {
            return false;
        }
        std::string_view typeName(tokenAt(offset).start(),
                                  tokenAt(offset).length());
        ++offset;
        if (tokenAt(offset).type() != TokenType::GREATER ||
            !isValidHandleTypeName(typeName)) {
            return false;
        }
        ++offset;
        consumeOptionalSuffix();
        return true;
    }

    if (isCollectionTypeNameText(name) && tokenAt(offset).type() == TokenType::LESS) {
        ++offset;

        if (name == "Array" || name == "Set") {
            if (!parseTypeLookahead(offset) ||
                tokenAt(offset).type() != TokenType::GREATER) {
                return false;
            }
            ++offset;
            consumeOptionalSuffix();
            return true;
        }

        if (!parseTypeLookahead(offset) ||
            tokenAt(offset).type() != TokenType::COMMA) {
            return false;
        }
        ++offset;

        if (!parseTypeLookahead(offset) ||
            tokenAt(offset).type() != TokenType::GREATER) {
            return false;
        }
        ++offset;
        consumeOptionalSuffix();
        return true;
    }

    consumeOptionalSuffix();
    return true;
}

bool Compiler::looksLikeFunctionTypeDeclarationStart() {
    if (m_parser->current.type() != TokenType::TYPE_FN) {
        return false;
    }

    size_t offset = 0;
    if (!parseTypeLookahead(offset)) {
        return false;
    }

    return tokenAt(offset).type() == TokenType::IDENTIFIER;
}

bool Compiler::hasLineBreakBeforeCurrent() const {
    return m_parser->previous.line() != 0 &&
           m_parser->current.line() > m_parser->previous.line();
}

void Compiler::synchronize() {
    m_parser->panicMode = false;

    while (m_parser->current.type() != TokenType::END_OF_FILE) {
        if (isRecoveryBoundaryToken(m_parser->current.type())) {
            return;
        }

        advance();
    }
}

bool Compiler::isRecoveryBoundaryToken(TokenType type) const {
    switch (type) {
        case TokenType::TYPE:
        case TokenType::VAR:
        case TokenType::CONST:
        case TokenType::TYPE_FN:
        case TokenType::IMPORT:
        case TokenType::FOR:
        case TokenType::IF:
        case TokenType::WHILE:
        case TokenType::PRINT:
        case TokenType::_RETURN:
        case TokenType::CLOSE_CURLY:
        case TokenType::END_OF_FILE:
            return true;
        default:
            return false;
    }
}

void Compiler::rejectStraySemicolon() {
    if (m_parser->current.type() != TokenType::SEMI_COLON) {
        return;
    }

    errorAtCurrent("Semicolons are only allowed inside 'for (...)' clauses.");
    advance();
}

bool Compiler::recoverLineLeadingContinuation(
    std::initializer_list<TokenType> terminators) {
    if (!hasLineBreakBeforeCurrent() ||
        !isLineContinuationToken(m_parser->current.type())) {
        return false;
    }

    errorAtCurrent(lineLeadingContinuationMessage(m_parser->current.type()));

    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;

    while (m_parser->current.type() != TokenType::END_OF_FILE) {
        if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
            for (TokenType terminator : terminators) {
                if (m_parser->current.type() == terminator) {
                    return true;
                }
            }

            if (m_parser->current.type() == TokenType::SEMI_COLON ||
                m_parser->current.type() == TokenType::CLOSE_CURLY ||
                isRecoveryBoundaryToken(m_parser->current.type())) {
                return true;
            }
        }

        switch (m_parser->current.type()) {
            case TokenType::OPEN_PAREN:
                ++parenDepth;
                break;
            case TokenType::CLOSE_PAREN:
                if (parenDepth > 0) {
                    --parenDepth;
                }
                break;
            case TokenType::OPEN_BRACKET:
                ++bracketDepth;
                break;
            case TokenType::CLOSE_BRACKET:
                if (bracketDepth > 0) {
                    --bracketDepth;
                }
                break;
            case TokenType::OPEN_CURLY:
                ++braceDepth;
                break;
            case TokenType::CLOSE_CURLY:
                if (braceDepth > 0) {
                    --braceDepth;
                }
                break;
            default:
                break;
        }

        advance();
    }

    return true;
}

bool Compiler::rejectUnexpectedTrailingToken(
    std::initializer_list<TokenType> allowedTerminators) {
    if (m_parser->current.type() == TokenType::END_OF_FILE ||
        m_parser->current.type() == TokenType::CLOSE_CURLY ||
        hasLineBreakBeforeCurrent() ||
        m_parser->current.type() == TokenType::SEMI_COLON) {
        return false;
    }

    for (TokenType terminator : allowedTerminators) {
        if (m_parser->current.type() == terminator) {
            return false;
        }
    }

    errorAtCurrent("Unexpected token.");

    while (m_parser->current.type() != TokenType::END_OF_FILE &&
           !hasLineBreakBeforeCurrent()) {
        bool reachedTerminator = false;
        for (TokenType terminator : allowedTerminators) {
            if (m_parser->current.type() == terminator) {
                reachedTerminator = true;
                break;
            }
        }

        if (reachedTerminator || m_parser->current.type() == TokenType::SEMI_COLON ||
            m_parser->current.type() == TokenType::CLOSE_CURLY ||
            isRecoveryBoundaryToken(m_parser->current.type())) {
            break;
        }

        advance();
    }

    return true;
}

void Compiler::errorAtCurrent(const std::string& message) {
    errorAt(m_parser->current, message);
}

void Compiler::errorAt(const Token& token, const std::string& message) {
    // Suppress snowballing errors
    if (m_parser->panicMode) return;
    m_parser->panicMode = true;

    std::cerr << "[error][compile][line " << token.line() << "]";

    if (token.type() == TokenType::END_OF_FILE) {
        std::cerr << " at end";
    } else if (token.type() == TokenType::ERROR) {
        // Nothing
    } else {
        std::cerr << " at '" << std::string(token.start(), token.length())
                  << "'";
    }

    std::cerr << " " << message << std::endl;
    m_parser->hadError = true;
}

void Compiler::errorAtLine(size_t line, const std::string& message) {
    if (m_parser && m_parser->panicMode) {
        return;
    }
    if (m_parser) {
        m_parser->panicMode = true;
        m_parser->hadError = true;
    }

    std::cerr << "[error][compile][line " << line << "] " << message
              << std::endl;
}

void Compiler::consume(TokenType type, const std::string& message) {
    if (m_parser->current.type() == type) {
        advance();
        return;
    }

    errorAtCurrent(message);
}

void Compiler::expression() { parsePrecedence(PREC_ASSIGNMENT); }

void Compiler::declaration() {
    if (m_parser->current.type() == TokenType::SEMI_COLON) {
        rejectStraySemicolon();
        return;
    }

    if (m_parser->current.type() == TokenType::TYPE) {
        advance();
        typeDeclaration();
    } else if (m_parser->current.type() == TokenType::TYPE_FN &&
               peekNextToken().type() == TokenType::IDENTIFIER) {
        advance();
        functionDeclaration();
    } else if (m_parser->current.type() == TokenType::CONST ||
               m_parser->current.type() == TokenType::VAR) {
        typedVarDeclaration();
    } else {
        statement();
    }

    if (m_parser->panicMode) {
        synchronize();
    }
}

void Compiler::emitExportName(const Token& nameToken) {
    std::string exportedName(nameToken.start(), nameToken.length());
    m_exportedNames.push_back(exportedName);

    uint8_t slot = globalSlot(nameToken);
    emitBytes(OpCode::GET_GLOBAL_SLOT, slot);
    emitBytes(OpCode::EXPORT_NAME, identifierConstant(nameToken));
    emitByte(OpCode::POP);
}

void Compiler::typeDeclaration(Token* declaredName) {
    if (currentContext().scopeDepth != 0) {
        errorAtCurrent("Type declarations are only allowed at the top level.");
    }

    consume(TokenType::IDENTIFIER, "Expected type name.");
    Token nameToken = m_parser->previous;
    if (declaredName != nullptr) {
        *declaredName = nameToken;
    }

    if (m_parser->current.type() != TokenType::STRUCT &&
        m_parser->current.type() != TokenType::LESS &&
        m_parser->current.type() != TokenType::OPEN_CURLY) {
        TypeRef aliasedType = parseTypeExprType();
        if (!aliasedType) {
            errorAtCurrent("Expected aliased type or 'struct' after type name.");
            return;
        }
        m_typeAliases[std::string(nameToken.start(), nameToken.length())] =
            aliasedType;
        rejectStraySemicolon();
        return;
    }

    if (m_parser->current.type() == TokenType::STRUCT) {
        advance();
    }

    ClassContext classContext;
    classContext.hasSuperclass = false;
    classContext.enclosing = m_currentClass;
    classContext.className = std::string(nameToken.start(), nameToken.length());
    m_currentClass = &classContext;
    m_superclassOf[classContext.className] = "";

    uint8_t nameConstant = identifierConstant(nameToken);
    uint8_t variable = 0;
    if (currentContext().scopeDepth > 0) {
        addLocal(nameToken);
    } else {
        variable = globalSlot(nameToken);
    }

    emitBytes(OpCode::CLASS_OP, nameConstant);
    defineVariable(variable);
    namedVariable(nameToken, false);

    if (m_parser->current.type() == TokenType::LESS) {
        advance();
        consume(TokenType::IDENTIFIER, "Expected superclass name.");
        Token superclassName = m_parser->previous;

        if (identifiersEqual(nameToken, superclassName)) {
            errorAt(superclassName, "A type cannot inherit from itself.");
        }

        m_superclassOf[classContext.className] =
            std::string(superclassName.start(), superclassName.length());

        namedVariable(superclassName, false);
        emitByte(OpCode::INHERIT);
        classContext.hasSuperclass = true;
    }

    consume(TokenType::OPEN_CURLY, "Expected '{' before struct body.");
    while (m_parser->current.type() != TokenType::CLOSE_CURLY &&
           m_parser->current.type() != TokenType::END_OF_FILE) {
        classMemberDeclaration();
    }
    consume(TokenType::CLOSE_CURLY, "Expected '}' after struct body.");
    emitByte(OpCode::POP);

    if (currentContext().scopeDepth == 0 &&
        isPublicSymbolName(std::string_view(nameToken.start(),
                                            nameToken.length()))) {
        emitExportName(nameToken);
    }

    m_currentClass = m_currentClass->enclosing;
}

void Compiler::classMemberDeclaration() {
    if (m_parser->current.type() == TokenType::SEMI_COLON) {
        rejectStraySemicolon();
        return;
    }

    if (isTypeToken(m_parser->current.type()) &&
        m_parser->current.type() != TokenType::TYPE_FN) {
        errorAtCurrent("Expected struct field name or method.");
        advance();
        return;
    }

    std::vector<int> annotatedOperators;

    while (m_parser->current.type() == TokenType::AT) {
        advance();
        consume(TokenType::IDENTIFIER, "Expected annotation name after '@'.");
        std::string annotationName(m_parser->previous.start(),
                                   m_parser->previous.length());
        if (annotationName != "operator") {
            errorAt(m_parser->previous, "Unknown annotation '@" +
                                             annotationName + "'.");
        }
        consume(TokenType::OPEN_PAREN, "Expected '(' after annotation name.");
        consume(TokenType::STRING, "Expected string literal annotation value.");
        std::string literal(m_parser->previous.start(), m_parser->previous.length());
        consume(TokenType::CLOSE_PAREN, "Expected ')' after annotation value.");
        if (literal.size() >= 2) {
            int op = parseOperatorAnnotationToken(
                std::string_view(literal.data() + 1, literal.size() - 2));
            if (op == -1) {
                errorAt(m_parser->previous,
                        "Unsupported operator annotation.");
            } else {
                annotatedOperators.push_back(op);
            }
        }
    }

    if (m_parser->current.type() == TokenType::TYPE_FN) {
        advance();
        consume(TokenType::IDENTIFIER, "Expected method name.");
        Token nameToken = m_parser->previous;
        std::string methodName(nameToken.start(), nameToken.length());
        uint8_t nameConstant = identifierConstant(nameToken);

        CompiledFunction compiled =
            compileFunction(methodName, true);
        if (m_currentClass && !m_currentClass->className.empty()) {
            m_classMethodSignatures[m_currentClass->className][methodName] =
                TypeInfo::makeFunction(compiled.parameterTypes,
                                       compiled.returnType
                                           ? compiled.returnType
                                           : TypeInfo::makeAny());
            for (int op : annotatedOperators) {
                m_classOperatorMethods[m_currentClass->className][op] =
                    methodName;
            }
        }

        emitBytes(OpCode::CLOSURE, makeConstant(Value(compiled.function)));
        for (const auto& upvalue : compiled.upvalues) {
            emitByte(static_cast<uint8_t>(upvalue.isLocal ? 1 : 0));
            emitByte(upvalue.index);
        }
        emitBytes(OpCode::METHOD, nameConstant);
        return;
    }

    if (!annotatedOperators.empty()) {
        errorAtCurrent("@operator can only annotate a method.");
    }

    consume(TokenType::IDENTIFIER, "Expected struct field name or method.");
    Token memberName = m_parser->previous;
    std::string memberNameText(memberName.start(), memberName.length());
    std::string className = m_currentClass ? m_currentClass->className : "";

    TypeRef declaredType = parseTypeExprType();
    if (!declaredType) {
        errorAtCurrent("Expected field type after member name.");
        return;
    }

    if (declaredType->isVoid()) {
        errorAt(memberName, "Struct field '" + memberNameText +
                                "' cannot have type 'void'.");
    }
    if (!className.empty()) {
        m_classFieldTypes[className][memberNameText] = declaredType;
    }
    rejectStraySemicolon();
}

void Compiler::statement() {
    if (m_parser->current.type() == TokenType::PRINT) {
        advance();
        printStatement();
        return;
    }

    if (m_parser->current.type() == TokenType::IF) {
        advance();
        ifStatement();
        return;
    }

    if (m_parser->current.type() == TokenType::WHILE) {
        advance();
        whileStatement();
        return;
    }

    if (m_parser->current.type() == TokenType::FOR) {
        advance();
        forStatement();
        return;
    }

    if (m_parser->current.type() == TokenType::_RETURN) {
        advance();
        returnStatement();
        return;
    }

    if (m_parser->current.type() == TokenType::OPEN_CURLY) {
        advance();
        block();
        return;
    }

    expressionStatement();
}

void Compiler::functionDeclaration() {
    consume(TokenType::IDENTIFIER, "Expected function name.");
    Token nameToken = m_parser->previous;
    uint8_t variable = 0;
    std::string functionName(nameToken.start(), nameToken.length());
    TypeRef functionType = TypeInfo::makeAny();
    auto functionTypeIt = m_functionSignatures.find(functionName);
    if (functionTypeIt != m_functionSignatures.end()) {
        functionType = functionTypeIt->second;
    }

    if (currentContext().scopeDepth > 0) {
        addLocal(nameToken, functionType);
    } else {
        variable = globalSlot(nameToken);
        if (variable < m_globalTypes.size()) {
            m_globalTypes[variable] = functionType;
        }
    }

    CompiledFunction compiled = compileFunction(
        std::string(nameToken.start(), nameToken.length()), false,
        functionType && functionType->kind == TypeKind::FUNCTION
            ? functionType->returnType
            : TypeInfo::makeAny());
    emitBytes(OpCode::CLOSURE, makeConstant(Value(compiled.function)));
    for (const auto& upvalue : compiled.upvalues) {
        emitByte(static_cast<uint8_t>(upvalue.isLocal ? 1 : 0));
        emitByte(upvalue.index);
    }
    defineVariable(variable);

    if (currentContext().scopeDepth == 0 &&
        isPublicSymbolName(std::string_view(nameToken.start(),
                                            nameToken.length()))) {
        emitExportName(nameToken);
    }
}

void Compiler::block() {
    beginScope();

    while (m_parser->current.type() != TokenType::CLOSE_CURLY &&
           m_parser->current.type() != TokenType::END_OF_FILE) {
        declaration();
    }

    consume(TokenType::CLOSE_CURLY, "Expected '}' after block.");
    endScope();
}

void Compiler::ifStatement() {
    consume(TokenType::OPEN_PAREN, "Expected '(' after 'if'.");
    expression();
    recoverLineLeadingContinuation({TokenType::CLOSE_PAREN});
    popExprType();
    consume(TokenType::CLOSE_PAREN, "Expected ')' after condition.");

    int thenJump = emitJump(OpCode::JUMP_IF_FALSE_POP);
    statement();
    int endJump = emitJump(OpCode::JUMP);

    patchJump(thenJump);
    if (m_parser->current.type() == TokenType::ELSE) {
        advance();
        statement();
    }
    patchJump(endJump);
}

void Compiler::whileStatement() {
    int loopStart = currentChunk()->count();

    consume(TokenType::OPEN_PAREN, "Expected '(' after 'while'.");
    expression();
    recoverLineLeadingContinuation({TokenType::CLOSE_PAREN});
    popExprType();
    consume(TokenType::CLOSE_PAREN, "Expected ')' after condition.");

    int exitJump = emitJump(OpCode::JUMP_IF_FALSE_POP);
    statement();
    emitLoop(loopStart);
    patchJump(exitJump);
}

void Compiler::forStatement() {
    beginScope();

    consume(TokenType::OPEN_PAREN, "Expected '(' after 'for'.");

    if (m_parser->current.type() == TokenType::SEMI_COLON) {
        advance();
    } else if (m_parser->current.type() == TokenType::VAR ||
               m_parser->current.type() == TokenType::CONST) {
        bool isConst = m_parser->current.type() == TokenType::CONST;
        advance();
        consume(TokenType::IDENTIFIER, "Expected loop variable name.");
        Token loopVariable = m_parser->previous;
        TypeRef declaredType = parseTypeExprType();
        if (!declaredType) {
            errorAtCurrent("Expected type after loop variable name.");
            declaredType = TypeInfo::makeAny();
        }
        if (declaredType->isVoid()) {
            errorAtCurrent("Variables cannot be declared with type 'void'.");
        }

        if (m_parser->current.type() == TokenType::COLON) {
            advance();

            addLocal(loopVariable, declaredType, isConst);
            emitByte(OpCode::NIL);
            markInitialized();
            uint8_t loopVariableSlot =
                static_cast<uint8_t>(currentContext().locals.size() - 1);

            expression();
            recoverLineLeadingContinuation({TokenType::CLOSE_PAREN});
            popExprType();
            consume(TokenType::CLOSE_PAREN,
                    "Expected ')' after foreach iterable.");

            emitByte(OpCode::ITER_INIT);

            int loopStart = currentChunk()->count();
            int exitJump = emitJump(OpCode::ITER_HAS_NEXT_JUMP);
            emitBytes(OpCode::ITER_NEXT_SET_LOCAL, loopVariableSlot);

            statement();
            emitLoop(loopStart);

            patchJump(exitJump);
            emitByte(OpCode::POP);

            endScope();
            return;
        }

        addLocal(loopVariable, declaredType, isConst);
        consume(TokenType::EQUAL,
                "Expected '=' in loop variable declaration "
                "(initializer is required).");
        expression();
        recoverLineLeadingContinuation({TokenType::SEMI_COLON});
        TypeRef initializerType = popExprType();
        emitCoerceToType(initializerType, declaredType);
        emitCheckInstanceType(declaredType);

        consume(TokenType::SEMI_COLON, "Expected ';' after loop initializer.");
        defineVariable(0);
    } else {
        expression();
        recoverLineLeadingContinuation({TokenType::SEMI_COLON});
        popExprType();
        consume(TokenType::SEMI_COLON, "Expected ';' after loop initializer.");
        emitByte(OpCode::POP);
    }

    int loopStart = currentChunk()->count();
    int exitJump = -1;

    if (m_parser->current.type() != TokenType::SEMI_COLON) {
        expression();
        recoverLineLeadingContinuation({TokenType::SEMI_COLON});
        popExprType();
        consume(TokenType::SEMI_COLON, "Expected ';' after loop condition.");
        exitJump = emitJump(OpCode::JUMP_IF_FALSE_POP);
    } else {
        advance();
    }

    if (m_parser->current.type() != TokenType::CLOSE_PAREN) {
        int bodyJump = emitJump(OpCode::JUMP);
        int incrementStart = currentChunk()->count();

        expression();
        recoverLineLeadingContinuation({TokenType::CLOSE_PAREN});
        popExprType();
        emitByte(OpCode::POP);
        consume(TokenType::CLOSE_PAREN, "Expected ')' after for clauses.");

        emitLoop(loopStart);
        loopStart = incrementStart;
        patchJump(bodyJump);
    } else {
        advance();
    }

    statement();
    emitLoop(loopStart);

    if (exitJump != -1) {
        patchJump(exitJump);
    }

    endScope();
}

void Compiler::printStatement() {
    consume(TokenType::OPEN_PAREN, "Expected '(' after 'print'.");
    expression();
    recoverLineLeadingContinuation({TokenType::CLOSE_PAREN});
    consume(TokenType::CLOSE_PAREN, "Expected ')' after print argument.");
    popExprType();
    rejectStraySemicolon();
    emitByte(OpCode::PRINT_OP);
}

void Compiler::returnStatement() {
    if (!currentContext().inFunction) {
        errorAtCurrent("Cannot return from top-level code.");
    }

    TypeRef expectedReturnType = currentContext().returnType;

    if (m_parser->current.type() == TokenType::SEMI_COLON ||
        m_parser->current.type() == TokenType::CLOSE_CURLY ||
        m_parser->current.type() == TokenType::END_OF_FILE) {
        if (m_parser->current.type() == TokenType::SEMI_COLON) {
            rejectStraySemicolon();
        }
        if (expectedReturnType && !expectedReturnType->isAny() &&
            !expectedReturnType->isVoid()) {
            errorAtCurrent("Return statement requires a value of type '" +
                           expectedReturnType->toString() + "'.");
        }
        emitByte(OpCode::NIL);
        emitByte(OpCode::RETURN);
        return;
    }

    expression();
    recoverLineLeadingContinuation();
    rejectUnexpectedTrailingToken();
    TypeRef expressionType = popExprType();
    if (expectedReturnType && expectedReturnType->isVoid()) {
        errorAtCurrent(
            "Cannot return a value from a function returning "
            "'void'.");
    }
    emitCoerceToType(expressionType, expectedReturnType);
    emitCheckInstanceType(expectedReturnType);
    rejectStraySemicolon();
    emitByte(OpCode::RETURN);
}

void Compiler::expressionStatement() {
    expression();
    recoverLineLeadingContinuation();
    rejectUnexpectedTrailingToken();
    popExprType();
    rejectStraySemicolon();
    emitByte(OpCode::POP);
}

void Compiler::typedVarDeclaration(Token* declaredName) {
    if (m_parser->current.type() != TokenType::CONST &&
        m_parser->current.type() != TokenType::VAR) {
        errorAtCurrent("Expected 'var' or 'const'.");
        return;
    }

    bool isConst = m_parser->current.type() == TokenType::CONST;
    advance();

    auto parseImportTarget = [this]() -> ImportTarget {
        if (m_sourcePath.empty()) {
            errorAtCurrent("@import(...) is not allowed in interactive mode.");
            return ImportTarget{};
        }

        consume(TokenType::AT, "Expected '@' before import.");
        if (m_parser->current.type() != TokenType::IDENTIFIER) {
            errorAtCurrent("Expected import directive after '@'.");
            return ImportTarget{};
        }
        advance();
        std::string directive(m_parser->previous.start(),
                              m_parser->previous.length());
        if (directive != "import") {
            errorAt(m_parser->previous, "Unknown '@" + directive + "' directive.");
            return ImportTarget{};
        }
        consume(TokenType::OPEN_PAREN, "Expected '(' after '@import'.");
        consume(TokenType::STRING, "Expected string literal import path.");

        std::string pathText(m_parser->previous.start(),
                             m_parser->previous.length());
        consume(TokenType::CLOSE_PAREN, "Expected ')' after import path.");

        if (pathText.length() < 2) {
            errorAt(m_parser->previous, "Invalid import path.");
            return ImportTarget{};
        }

        std::string rawPath = pathText.substr(1, pathText.length() - 2);
        ImportTarget importTarget;
        std::string resolveError;
        if (!resolveImportTarget(m_sourcePath, rawPath, m_packageSearchPaths,
                                 importTarget, resolveError)) {
            errorAt(m_parser->previous, resolveError);
            return ImportTarget{};
        }

        if (importTarget.kind == ImportTargetKind::NATIVE_PACKAGE) {
            NativePackageDescriptor packageDescriptor;
            std::string packageError;
            if (!loadNativePackageDescriptor(importTarget.resolvedPath,
                                             packageDescriptor, packageError,
                                             false, nullptr)) {
                errorAt(m_parser->previous, packageError);
                return ImportTarget{};
            }

            if (!validateNativePackageImport(importTarget, packageDescriptor,
                                             packageError)) {
                errorAt(m_parser->previous, packageError);
                return ImportTarget{};
            }
        }

        return importTarget;
    };

    if (m_parser->current.type() == TokenType::OPEN_CURLY) {
        struct NamedBinding {
            Token exportName;
            Token localName;
            TypeRef resolvedType;
        };

        advance();
        std::vector<NamedBinding> bindings;
        if (m_parser->current.type() != TokenType::CLOSE_CURLY) {
            do {
                consume(TokenType::IDENTIFIER,
                        "Expected export name in destructured import.");
                Token exportName = m_parser->previous;
                Token localName = exportName;
                if (m_parser->current.type() == TokenType::AS_KW) {
                    advance();
                    consume(TokenType::IDENTIFIER,
                            "Expected local alias after 'as'.");
                    localName = m_parser->previous;
                }
                bindings.push_back(NamedBinding{exportName, localName,
                                                TypeInfo::makeAny()});
                if (m_parser->current.type() != TokenType::COMMA) {
                    break;
                }
                advance();
            } while (true);
        }
        consume(TokenType::CLOSE_CURLY, "Expected '}' after binding list.");
        consume(TokenType::EQUAL, "Expected '=' after destructured binding.");
        ImportTarget importTarget = parseImportTarget();
        rejectStraySemicolon();
        if (importTarget.canonicalId.empty()) {
            return;
        }

        std::unordered_map<std::string, TypeRef> moduleExportTypes;
        std::string moduleTypeError;
        if (!resolveImportExportTypes(importTarget, moduleExportTypes,
                                      moduleTypeError)) {
            errorAtCurrent(moduleTypeError);
            return;
        }

        emitBytes(OpCode::IMPORT_MODULE,
                  makeConstant(makeStringValue(importTarget.canonicalId)));
        for (auto& binding : bindings) {
            std::string exportName(binding.exportName.start(),
                                   binding.exportName.length());
            auto exportedTypeIt = moduleExportTypes.find(exportName);
            if (exportedTypeIt != moduleExportTypes.end()) {
                binding.resolvedType = exportedTypeIt->second;
            } else {
                errorAt(binding.exportName,
                        "Module or native package does not export '" +
                            exportName + "'.");
            }

            emitByte(OpCode::DUP);
            emitBytes(OpCode::GET_PROPERTY,
                      identifierConstant(binding.exportName));

            uint8_t variable = 0;
            if (currentContext().scopeDepth > 0) {
                addLocal(binding.localName, binding.resolvedType, true);
            } else {
                variable = globalSlot(binding.localName);
                if (variable < m_globalTypes.size()) {
                    m_globalTypes[variable] = binding.resolvedType;
                }
                if (variable < m_globalConstness.size()) {
                    m_globalConstness[variable] = true;
                }
            }
            defineVariable(variable);
        }
        emitByte(OpCode::POP);
        return;
    }

    consume(TokenType::IDENTIFIER, "Expected variable name after mutability.");
    Token nameToken = m_parser->previous;
    if (declaredName != nullptr) {
        *declaredName = nameToken;
    }

    TypeRef declaredType = TypeInfo::makeAny();
    bool omittedType = false;
    if (m_parser->current.type() == TokenType::EQUAL) {
        omittedType = true;
        if (peekNextToken().type() != TokenType::AT ||
            peekToken(2).type() != TokenType::IDENTIFIER ||
            std::string_view(peekToken(2).start(), peekToken(2).length()) != "import") {
            errorAt(nameToken,
                    "Variables require an explicit type unless initialized from '@import(...)'.");
        }
    } else {
        declaredType = parseTypeExprType();
        if (!declaredType) {
            errorAtCurrent("Expected type after variable name.");
            return;
        }
        if (declaredType->isVoid()) {
            errorAtCurrent("Variables cannot be declared with type 'void'.");
        }
    }

    uint8_t global = 0;
    if (currentContext().scopeDepth > 0) {
        addLocal(nameToken, declaredType, isConst);
    } else {
        global = globalSlot(nameToken);
        if (global < m_globalTypes.size()) {
            m_globalTypes[global] = declaredType;
        }
        if (global < m_globalConstness.size()) {
            m_globalConstness[global] = isConst;
        }
    }
    consume(TokenType::EQUAL,
            "Expected '=' in variable declaration (initializer is required).");

    TypeRef initializerType = TypeInfo::makeAny();
    if (!omittedType && declaredType && declaredType->kind == TypeKind::FUNCTION &&
        m_parser->current.type() == TokenType::TYPE_FN) {
        advance();
        initializerType = emitFunctionLiteral(declaredType);
    } else {
        expression();
        recoverLineLeadingContinuation();
        rejectUnexpectedTrailingToken();
        initializerType = popExprType();
    }
    if (!omittedType) {
        emitCoerceToType(initializerType, declaredType);
        emitCheckInstanceType(declaredType);
    } else {
        declaredType = initializerType;
        if (currentContext().scopeDepth == 0 && global < m_globalTypes.size()) {
            m_globalTypes[global] = declaredType;
        }
    }

    rejectStraySemicolon();
    defineVariable(global);

    if (currentContext().scopeDepth == 0 &&
        isPublicSymbolName(std::string_view(nameToken.start(),
                                            nameToken.length()))) {
        emitExportName(nameToken);
    }
}

void Compiler::parsePrecedence(Precedence precedence) {
    advance();
    bool canAssign = precedence <= PREC_ASSIGNMENT;

    ParseFn prefixRule = getRule(m_parser->previous.type()).prefix;
    if (!prefixRule) {
        errorAt(m_parser->previous, "Expected expression.");
        return;
    }

    prefixRule(canAssign);

    while (precedence <= getRule(m_parser->current.type()).precedence) {
        if (hasLineBreakBeforeCurrent()) {
            break;
        }
        advance();
        ParseFn infixRule = getRule(m_parser->previous.type()).infix;
        infixRule(canAssign);
    }

    if (canAssign && !hasLineBreakBeforeCurrent() &&
        isAssignmentOperator(m_parser->current.type())) {
        TokenType assignmentType = m_parser->current.type();
        errorAtCurrent("Invalid assignment target.");
        advance();

        if (assignmentType != TokenType::PLUS_PLUS &&
            assignmentType != TokenType::MINUS_MINUS) {
            expression();
        }
    }
}

Compiler::ParseRule Compiler::getRule(TokenType type) {
    switch (type) {
        case TokenType::AT:
            return ParseRule{
                [this](bool canAssign) { atExpression(canAssign); }, nullptr,
                PREC_NONE};
        case TokenType::OPEN_PAREN:
            return ParseRule{[this](bool canAssign) { grouping(canAssign); },
                             [this](bool canAssign) { call(canAssign); },
                             PREC_CALL};
        case TokenType::OPEN_BRACKET:
            return ParseRule{
                [this](bool canAssign) { arrayLiteral(canAssign); },
                [this](bool canAssign) { subscript(canAssign); }, PREC_CALL};
        case TokenType::OPEN_CURLY:
            return ParseRule{[this](bool canAssign) { dictLiteral(canAssign); },
                             nullptr, PREC_NONE};
        case TokenType::DOT:
            return ParseRule{
                nullptr, [this](bool canAssign) { dot(canAssign); }, PREC_CALL};
        case TokenType::NUMBER:
            return ParseRule{[this](bool canAssign) { number(canAssign); },
                             nullptr, PREC_NONE};
        case TokenType::IDENTIFIER:
        case TokenType::TYPE:
        case TokenType::TYPE_I8:
        case TokenType::TYPE_I16:
        case TokenType::TYPE_I32:
        case TokenType::TYPE_I64:
        case TokenType::TYPE_U8:
        case TokenType::TYPE_U16:
        case TokenType::TYPE_U32:
        case TokenType::TYPE_U64:
        case TokenType::TYPE_USIZE:
        case TokenType::TYPE_F32:
        case TokenType::TYPE_F64:
        case TokenType::TYPE_BOOL:
        case TokenType::TYPE_STR:
            return ParseRule{[this](bool canAssign) { variable(canAssign); },
                             nullptr, PREC_NONE};
        case TokenType::THIS:
            return ParseRule{
                [this](bool canAssign) { thisExpression(canAssign); }, nullptr,
                PREC_NONE};
        case TokenType::SUPER:
            return ParseRule{
                [this](bool canAssign) { superExpression(canAssign); }, nullptr,
                PREC_NONE};
        case TokenType::STRING:
            return ParseRule{
                [this](bool canAssign) { stringLiteral(canAssign); }, nullptr,
                PREC_NONE};
        case TokenType::TRUE:
        case TokenType::FALSE:
        case TokenType::_NULL:
        case TokenType::TYPE_NULL_KW:
            return ParseRule{[this](bool canAssign) { literal(canAssign); },
                             nullptr, PREC_NONE};
        case TokenType::TYPE_FN:
            return ParseRule{
                [this](bool canAssign) { functionLiteral(canAssign); }, nullptr,
                PREC_NONE};
        case TokenType::BANG:
        case TokenType::TILDE:
            return ParseRule{[this](bool canAssign) { unary(canAssign); },
                             nullptr, PREC_NONE};
        case TokenType::PLUS_PLUS:
        case TokenType::MINUS_MINUS:
            return ParseRule{
                [this](bool canAssign) { prefixUpdate(canAssign); }, nullptr,
                PREC_NONE};
        case TokenType::MINUS:
            return ParseRule{[this](bool canAssign) { unary(canAssign); },
                             [this](bool canAssign) { binary(canAssign); },
                             PREC_TERM};
        case TokenType::PLUS:
            return ParseRule{nullptr,
                             [this](bool canAssign) { binary(canAssign); },
                             PREC_TERM};

        case TokenType::SLASH:
            return ParseRule{nullptr,
                             [this](bool canAssign) { binary(canAssign); },
                             PREC_FACTOR};
        case TokenType::STAR:
            return ParseRule{nullptr,
                             [this](bool canAssign) { binary(canAssign); },
                             PREC_FACTOR};

        case TokenType::GREATER:
        case TokenType::GREATER_EQUAL:
        case TokenType::LESS:
        case TokenType::LESS_EQUAL:
            return ParseRule{nullptr,
                             [this](bool canAssign) { binary(canAssign); },
                             PREC_COMPARISON};
        case TokenType::SHIFT_LEFT_TOKEN:
        case TokenType::SHIFT_RIGHT_TOKEN:
            return ParseRule{nullptr,
                             [this](bool canAssign) { binary(canAssign); },
                             PREC_SHIFT};
        case TokenType::AMPERSAND:
            return ParseRule{nullptr,
                             [this](bool canAssign) { binary(canAssign); },
                             PREC_BITWISE_AND};
        case TokenType::CARET:
            return ParseRule{nullptr,
                             [this](bool canAssign) { binary(canAssign); },
                             PREC_BITWISE_XOR};
        case TokenType::PIPE:
            return ParseRule{nullptr,
                             [this](bool canAssign) { binary(canAssign); },
                             PREC_BITWISE_OR};
        case TokenType::EQUAL_EQUAL:
        case TokenType::BANG_EQUAL:
            return ParseRule{nullptr,
                             [this](bool canAssign) { binary(canAssign); },
                             PREC_EQUALITY};
        case TokenType::LOGICAL_AND:
            return ParseRule{nullptr,
                             [this](bool canAssign) { andOperator(canAssign); },
                             PREC_AND};
        case TokenType::LOGICAL_OR:
            return ParseRule{nullptr,
                             [this](bool canAssign) { orOperator(canAssign); },
                             PREC_OR};
        case TokenType::AS_KW:
            return ParseRule{
                nullptr, [this](bool canAssign) { castOperator(canAssign); },
                PREC_CALL};

        default:
            return ParseRule{nullptr, nullptr, PREC_NONE};
    }
}

void Compiler::number(bool canAssign) {
    (void)canAssign;
    std::string literal(m_parser->previous.start(),
                        m_parser->previous.length());
    NumericLiteralInfo literalInfo = parseNumericLiteralInfo(literal);
    if (!literalInfo.valid) {
        errorAt(m_parser->previous,
                "Invalid numeric literal '" + literal + "'.");
        pushExprType(TypeInfo::makeAny());
        return;
    }

    try {
        if (literalInfo.isFloat) {
            emitConstant(std::stod(literalInfo.core));
            pushExprType(literalInfo.type ? literalInfo.type
                                          : TypeInfo::makeF64());
            return;
        }

        if (literalInfo.isUnsigned) {
            emitConstant(static_cast<uint64_t>(std::stoull(literalInfo.core)));
            pushExprType(literalInfo.type ? literalInfo.type
                                          : TypeInfo::makeU32());
            return;
        }

        emitConstant(static_cast<int64_t>(std::stoll(literalInfo.core)));
        pushExprType(literalInfo.type ? literalInfo.type : TypeInfo::makeI32());
    } catch (...) {
        errorAt(m_parser->previous,
                "Invalid numeric literal '" + literal + "'.");
        pushExprType(TypeInfo::makeAny());
    }
}

void Compiler::variable(bool canAssign) {
    namedVariable(m_parser->previous, canAssign);
}

void Compiler::thisExpression(bool canAssign) {
    (void)canAssign;
    if (!currentContext().inMethod) {
        errorAt(m_parser->previous, "Cannot use 'this' outside of a method.");
        return;
    }

    emitByte(OpCode::GET_THIS);

    if (m_currentClass && !m_currentClass->className.empty()) {
        pushExprType(TypeInfo::makeClass(m_currentClass->className));
    } else {
        pushExprType(TypeInfo::makeAny());
    }
}

void Compiler::superExpression(bool canAssign) {
    (void)canAssign;
    if (m_currentClass == nullptr) {
        errorAt(m_parser->previous, "Cannot use 'super' outside of a class.");
        return;
    }

    if (!m_currentClass->hasSuperclass) {
        errorAt(m_parser->previous,
                "Cannot use 'super' in a class with no superclass.");
        return;
    }

    consume(TokenType::DOT, "Expected '.' after 'super'.");
    consume(TokenType::IDENTIFIER, "Expected superclass method name.");
    uint8_t name = identifierConstant(m_parser->previous);
    std::string methodName(m_parser->previous.start(),
                           m_parser->previous.length());

    TypeRef methodType = TypeInfo::makeAny();
    if (m_currentClass && !m_currentClass->className.empty()) {
        auto classIt = m_classMethodSignatures.find(m_currentClass->className);
        if (classIt != m_classMethodSignatures.end()) {
            auto methodIt = classIt->second.find(methodName);
            if (methodIt != classIt->second.end() && methodIt->second) {
                methodType = methodIt->second;
            }
        }
    }

    if (m_parser->current.type() == TokenType::OPEN_PAREN) {
        emitByte(OpCode::GET_THIS);
        emitInvokeCall(OpCode::INVOKE_SUPER, name, methodType);
        return;
    }

    emitBytes(OpCode::GET_SUPER, name);
    pushExprType(methodType);
}

void Compiler::literal(bool canAssign) {
    (void)canAssign;
    switch (m_parser->previous.type()) {
        case TokenType::TRUE:
            emitByte(OpCode::TRUE_LITERAL);
            pushExprType(TypeInfo::makeBool());
            break;
        case TokenType::FALSE:
            emitByte(OpCode::FALSE_LITERAL);
            pushExprType(TypeInfo::makeBool());
            break;
        case TokenType::_NULL:
        case TokenType::TYPE_NULL_KW:
            emitByte(OpCode::NIL);
            pushExprType(TypeInfo::makeNull());
            break;
        default:
            return;
    }
}

void Compiler::stringLiteral(bool canAssign) {
    (void)canAssign;
    std::string tokenText(m_parser->previous.start(),
                          m_parser->previous.length());
    if (tokenText.length() < 2) {
        errorAt(m_parser->previous, "Invalid string literal.");
        return;
    }

    std::string value = tokenText.substr(1, tokenText.length() - 2);
    emitConstant(makeStringValue(value));
    pushExprType(TypeInfo::makeStr());
}

void Compiler::atExpression(bool canAssign) {
    (void)canAssign;

    if (m_sourcePath.empty()) {
        errorAt(m_parser->previous,
                "@import(...) is not allowed in interactive mode.");
        pushExprType(TypeInfo::makeAny());
        return;
    }

    if (m_parser->current.type() != TokenType::IDENTIFIER) {
        errorAtCurrent("Expected directive name after '@'.");
        pushExprType(TypeInfo::makeAny());
        return;
    }
    advance();
    std::string directive(m_parser->previous.start(), m_parser->previous.length());
    if (directive != "import") {
        errorAt(m_parser->previous, "Unknown '@" + directive + "' directive.");
        pushExprType(TypeInfo::makeAny());
        return;
    }

    consume(TokenType::OPEN_PAREN, "Expected '(' after '@import'.");
    consume(TokenType::STRING, "Expected string literal import path.");
    std::string pathText(m_parser->previous.start(), m_parser->previous.length());
    consume(TokenType::CLOSE_PAREN, "Expected ')' after import path.");

    if (pathText.length() < 2) {
        errorAt(m_parser->previous, "Invalid import path.");
        pushExprType(TypeInfo::makeAny());
        return;
    }

    std::string rawPath = pathText.substr(1, pathText.length() - 2);
    ImportTarget importTarget;
    std::string resolveError;
    if (!resolveImportTarget(m_sourcePath, rawPath, m_packageSearchPaths,
                             importTarget, resolveError)) {
        errorAt(m_parser->previous, resolveError);
        pushExprType(TypeInfo::makeAny());
        return;
    }

    if (importTarget.kind == ImportTargetKind::NATIVE_PACKAGE) {
        NativePackageDescriptor packageDescriptor;
        std::string packageError;
        if (!loadNativePackageDescriptor(importTarget.resolvedPath,
                                         packageDescriptor, packageError, false,
                                         nullptr) ||
            !validateNativePackageImport(importTarget, packageDescriptor,
                                         packageError)) {
            errorAt(m_parser->previous, packageError);
            pushExprType(TypeInfo::makeAny());
            return;
        }
    }

    emitBytes(OpCode::IMPORT_MODULE,
              makeConstant(makeStringValue(importTarget.canonicalId)));
    pushExprType(TypeInfo::makeAny());
}

void Compiler::grouping(bool canAssign) {
    (void)canAssign;
    expression();
    consume(TokenType::CLOSE_PAREN, "Expected ')' after expression.");
}

void Compiler::arrayLiteral(bool canAssign) {
    (void)canAssign;

    uint8_t count = 0;
    TypeRef inferredElementType = nullptr;
    if (m_parser->current.type() != TokenType::CLOSE_BRACKET) {
        do {
            expression();
            TypeRef elementType = popExprType();
            if (!inferredElementType) {
                inferredElementType = elementType;
            } else if (elementType && inferredElementType &&
                       elementType->isNumeric() &&
                       inferredElementType->isNumeric()) {
                TypeRef promoted =
                    numericPromotion(inferredElementType, elementType);
                inferredElementType = promoted ? promoted : TypeInfo::makeAny();
            } else if (elementType && inferredElementType &&
                       isAssignable(elementType, inferredElementType)) {
                // keep inferredElementType
            } else if (elementType && inferredElementType &&
                       isAssignable(inferredElementType, elementType)) {
                inferredElementType = elementType;
            } else {
                inferredElementType = TypeInfo::makeAny();
            }

            if (count == UINT8_MAX) {
                errorAtCurrent(
                    "Array literal cannot have more than 255 elements.");
                break;
            }
            count++;

            if (m_parser->current.type() != TokenType::COMMA) {
                break;
            }
            advance();
        } while (true);
    }

    consume(TokenType::CLOSE_BRACKET, "Expected ']' after array literal.");
    emitBytes(OpCode::BUILD_ARRAY, count);
    pushExprType(TypeInfo::makeArray(
        inferredElementType ? inferredElementType : TypeInfo::makeAny()));
}

void Compiler::dictLiteral(bool canAssign) {
    (void)canAssign;

    uint8_t pairCount = 0;
    TypeRef inferredKeyType = nullptr;
    TypeRef inferredValueType = nullptr;
    if (m_parser->current.type() != TokenType::CLOSE_CURLY) {
        do {
            expression();
            TypeRef keyType = popExprType();
            consume(TokenType::COLON,
                    "Expected ':' between dictionary key and value.");
            expression();
            TypeRef valueType = popExprType();

            if (!inferredKeyType) {
                inferredKeyType = keyType;
            } else if (keyType && inferredKeyType &&
                       isAssignable(keyType, inferredKeyType)) {
                // keep inferredKeyType
            } else if (keyType && inferredKeyType &&
                       isAssignable(inferredKeyType, keyType)) {
                inferredKeyType = keyType;
            } else {
                inferredKeyType = TypeInfo::makeAny();
            }

            if (!inferredValueType) {
                inferredValueType = valueType;
            } else if (valueType && inferredValueType &&
                       valueType->isNumeric() &&
                       inferredValueType->isNumeric()) {
                TypeRef promoted =
                    numericPromotion(inferredValueType, valueType);
                inferredValueType = promoted ? promoted : TypeInfo::makeAny();
            } else if (valueType && inferredValueType &&
                       isAssignable(valueType, inferredValueType)) {
                // keep inferredValueType
            } else if (valueType && inferredValueType &&
                       isAssignable(inferredValueType, valueType)) {
                inferredValueType = valueType;
            } else {
                inferredValueType = TypeInfo::makeAny();
            }

            if (pairCount == UINT8_MAX) {
                errorAtCurrent(
                    "Dictionary literal cannot have more than 255 pairs.");
                break;
            }
            pairCount++;

            if (m_parser->current.type() != TokenType::COMMA) {
                break;
            }
            advance();
        } while (true);
    }

    consume(TokenType::CLOSE_CURLY, "Expected '}' after dictionary literal.");
    emitBytes(OpCode::BUILD_DICT, pairCount);
    pushExprType(TypeInfo::makeDict(
        inferredKeyType ? inferredKeyType : TypeInfo::makeAny(),
        inferredValueType ? inferredValueType : TypeInfo::makeAny()));
}

void Compiler::unary(bool canAssign) {
    (void)canAssign;
    TokenType operatorType = m_parser->previous.type();

    parsePrecedence(PREC_UNARY);
    TypeRef operandType = popExprType();
    TypeRef resultType = TypeInfo::makeAny();

    switch (operatorType) {
        case TokenType::BANG:
            emitByte(OpCode::NOT);
            resultType = TypeInfo::makeBool();
            break;
        case TokenType::TILDE:
            emitByte(OpCode::BITWISE_NOT);
            resultType = operandType;
            break;
        case TokenType::MINUS:
            if (m_strictMode && operandType && operandType->isInteger()) {
                emitByte(OpCode::INT_NEGATE);
                resultType = operandType;
            } else {
                emitByte(OpCode::NEGATE);
                resultType = operandType;
            }
            break;
        default:
            return;
    }

    pushExprType(resultType);
}

void Compiler::prefixUpdate(bool canAssign) {
    (void)canAssign;

    TokenType updateToken = m_parser->previous.type();
    bool isIncrement = updateToken == TokenType::PLUS_PLUS;

    auto emitNamedUpdate = [&](const Token& nameToken) {
        ResolvedVariable resolved = resolveNamedVariable(nameToken);
        if (resolved.isConst) {
            errorAt(nameToken, "Cannot assign to const variable '" +
                                   std::string(nameToken.start(),
                                               nameToken.length()) +
                                   "'.");
            pushExprType(resolved.type);
            return;
        }

        emitBytes(resolved.getOp, resolved.arg);
        emitConstant(Value(static_cast<int64_t>(1)));
        emitByte(arithmeticOpcode(
            isIncrement ? TokenType::PLUS : TokenType::MINUS, resolved.type));
        emitCoerceToType(resolved.type, resolved.type);
        emitBytes(resolved.setOp, resolved.arg);
        pushExprType(resolved.type);
    };

    if (m_parser->current.type() == TokenType::IDENTIFIER) {
        advance();
        Token nameToken = m_parser->previous;

        if (m_parser->current.type() != TokenType::DOT) {
            emitNamedUpdate(nameToken);
            return;
        }

        namedVariable(nameToken, false);
    } else if (m_parser->current.type() == TokenType::THIS) {
        advance();
        thisExpression(false);

        if (m_parser->current.type() != TokenType::DOT) {
            errorAtCurrent("Expected property target after update operator.");
            return;
        }
    } else {
        errorAtCurrent("Expected variable or property after update operator.");
        return;
    }

    uint8_t propertyName = 0;
    bool hasProperty = false;

    while (m_parser->current.type() == TokenType::DOT) {
        advance();
        consume(TokenType::IDENTIFIER, "Expected property name after '.'.");
        propertyName = identifierConstant(m_parser->previous);
        hasProperty = true;

        if (m_parser->current.type() == TokenType::DOT) {
            emitBytes(OpCode::GET_PROPERTY, propertyName);
        }
    }

    if (!hasProperty) {
        errorAtCurrent("Expected property target after update operator.");
        return;
    }

    emitByte(OpCode::DUP);
    emitBytes(OpCode::GET_PROPERTY, propertyName);
    emitConstant(Value(static_cast<int64_t>(1)));
    emitByte(isIncrement ? OpCode::ADD : OpCode::SUB);
    emitBytes(OpCode::SET_PROPERTY, propertyName);
    pushExprType(TypeInfo::makeAny());
}

void Compiler::binary(bool canAssign) {
    (void)canAssign;
    TokenType operatorType = m_parser->previous.type();
    TypeRef leftType = popExprType();
    ParseRule rule = getRule(operatorType);
    parsePrecedence(static_cast<Precedence>(rule.precedence + 1));
    TypeRef rightType = popExprType();

    if (leftType && leftType->kind == TypeKind::CLASS) {
        auto classIt = m_classOperatorMethods.find(leftType->className);
        if (classIt != m_classOperatorMethods.end()) {
            auto opIt = classIt->second.find(operatorType);
            if (opIt != classIt->second.end()) {
                TypeRef methodType =
                    lookupClassMethodType(leftType->className, opIt->second);
                emitByte(OpCode::INVOKE);
                emitByte(
                    makeConstant(makeStringValue(opIt->second)));
                emitByte(1);
                pushExprType(methodType && methodType->returnType
                                 ? methodType->returnType
                                 : TypeInfo::makeAny());
                return;
            }
        }
    }

    TypeRef resultType = TypeInfo::makeAny();
    TypeRef promotedNumeric = numericPromotion(leftType, rightType);

    switch (operatorType) {
        case TokenType::PLUS:
            if (leftType && rightType && leftType->kind == TypeKind::STR &&
                rightType->kind == TypeKind::STR) {
                emitByte(OpCode::ADD);
                resultType = TypeInfo::makeStr();
            } else if (promotedNumeric) {
                emitByte(arithmeticOpcode(operatorType, promotedNumeric));
                resultType = promotedNumeric;
            } else {
                emitByte(OpCode::ADD);
            }
            break;
        case TokenType::MINUS:
            if (promotedNumeric) {
                emitByte(arithmeticOpcode(operatorType, promotedNumeric));
                resultType = promotedNumeric;
            } else {
                emitByte(OpCode::SUB);
            }
            break;
        case TokenType::STAR:
            if (promotedNumeric) {
                emitByte(arithmeticOpcode(operatorType, promotedNumeric));
                resultType = promotedNumeric;
            } else {
                emitByte(OpCode::MULT);
            }
            break;
        case TokenType::SLASH:
            if (promotedNumeric) {
                emitByte(arithmeticOpcode(operatorType, promotedNumeric));
                resultType = promotedNumeric;
            } else {
                emitByte(OpCode::DIV);
            }
            break;
        case TokenType::GREATER:
            if (m_strictMode && promotedNumeric &&
                promotedNumeric->isInteger()) {
                emitByte(promotedNumeric->isSigned() ? OpCode::IGREATER
                                                     : OpCode::UGREATER);
            } else {
                emitByte(OpCode::GREATER_THAN);
            }
            resultType = TypeInfo::makeBool();
            break;
        case TokenType::GREATER_EQUAL:
            if (m_strictMode && promotedNumeric &&
                promotedNumeric->isInteger()) {
                emitByte(promotedNumeric->isSigned() ? OpCode::IGREATER_EQ
                                                     : OpCode::UGREATER_EQ);
            } else {
                emitByte(OpCode::GREATER_EQUAL_THAN);
            }
            resultType = TypeInfo::makeBool();
            break;
        case TokenType::LESS:
            if (m_strictMode && promotedNumeric &&
                promotedNumeric->isInteger()) {
                emitByte(promotedNumeric->isSigned() ? OpCode::ILESS
                                                     : OpCode::ULESS);
            } else {
                emitByte(OpCode::LESS_THAN);
            }
            resultType = TypeInfo::makeBool();
            break;
        case TokenType::LESS_EQUAL:
            if (m_strictMode && promotedNumeric &&
                promotedNumeric->isInteger()) {
                emitByte(promotedNumeric->isSigned() ? OpCode::ILESS_EQ
                                                     : OpCode::ULESS_EQ);
            } else {
                emitByte(OpCode::LESS_EQUAL_THAN);
            }
            resultType = TypeInfo::makeBool();
            break;
        case TokenType::SHIFT_LEFT_TOKEN:
            emitByte(OpCode::SHIFT_LEFT);
            resultType = leftType;
            break;
        case TokenType::SHIFT_RIGHT_TOKEN:
            emitByte(OpCode::SHIFT_RIGHT);
            resultType = leftType;
            break;
        case TokenType::AMPERSAND:
            emitByte(OpCode::BITWISE_AND);
            resultType = bitwiseResultType(leftType, rightType);
            break;
        case TokenType::CARET:
            emitByte(OpCode::BITWISE_XOR);
            resultType = bitwiseResultType(leftType, rightType);
            break;
        case TokenType::PIPE:
            emitByte(OpCode::BITWISE_OR);
            resultType = bitwiseResultType(leftType, rightType);
            break;
        case TokenType::EQUAL_EQUAL:
            emitByte(OpCode::EQUAL_OP);
            resultType = TypeInfo::makeBool();
            break;
        case TokenType::BANG_EQUAL:
            emitByte(OpCode::NOT_EQUAL_OP);
            resultType = TypeInfo::makeBool();
            break;
        default:
            return;
    }

    pushExprType(resultType);
}

void Compiler::castOperator(bool canAssign) {
    (void)canAssign;

    TypeRef targetType = parseTypeExprType();
    if (!targetType) {
        errorAtCurrent("Expected type after 'as'.");
        return;
    }

    popExprType();

    if (targetType->isInteger()) {
        emitBytes(OpCode::NARROW_INT, static_cast<uint8_t>(targetType->kind));
        pushExprType(targetType);
        return;
    }

    if (targetType->isFloat()) {
        emitByte(OpCode::INT_TO_FLOAT);
        pushExprType(targetType);
        return;
    }

    if (targetType->kind == TypeKind::STR) {
        emitByte(OpCode::INT_TO_STR);
    }

    pushExprType(targetType);
}

void Compiler::call(bool canAssign) {
    (void)canAssign;

    TypeRef calleeType = popExprType();
    std::vector<TypeRef> argumentTypes;
    uint8_t argCount = parseCallArguments(argumentTypes);
    emitBytes(OpCode::CALL, argCount);
    pushCallResultType(calleeType);
}

void Compiler::dot(bool canAssign) {
    TypeRef objectType = popExprType();
    consume(TokenType::IDENTIFIER, "Expected property name after '.'.");
    Token propertyToken = m_parser->previous;
    uint8_t name = identifierConstant(m_parser->previous);
    std::string propertyName(propertyToken.start(), propertyToken.length());

    TypeRef memberType = TypeInfo::makeAny();
    int fieldSlot = -1;
    bool knownField = false;
    if (objectType && objectType->kind == TypeKind::CLASS) {
        fieldSlot = lookupClassFieldSlot(objectType->className, propertyName);
        knownField = fieldSlot >= 0;
        memberType = lookupClassFieldType(objectType->className, propertyName);

        if (!memberType || memberType->isAny()) {
            memberType =
                lookupClassMethodType(objectType->className, propertyName);
        }

        if ((!memberType || memberType->isAny()) && m_strictMode) {
            errorAt(propertyToken, "Class '" + objectType->className +
                                       "' has no field or method named '" +
                                       propertyName + "'.");
        }
    }

    if (canAssign) {
        TokenType assignmentType = m_parser->current.type();

        if (assignmentType == TokenType::EQUAL) {
            advance();
            expression();
            TypeRef rhsType = popExprType();
            if (knownField) {
                emitBytes(OpCode::SET_FIELD_SLOT,
                          static_cast<uint8_t>(fieldSlot));
            } else {
                emitBytes(OpCode::SET_PROPERTY, name);
            }
            pushExprType((memberType && !memberType->isAny()) ? memberType
                                                              : rhsType);
            return;
        }

        if (assignmentType == TokenType::PLUS_PLUS ||
            assignmentType == TokenType::MINUS_MINUS) {
            advance();
            emitByte(OpCode::DUP);
            if (knownField) {
                emitBytes(OpCode::GET_FIELD_SLOT,
                          static_cast<uint8_t>(fieldSlot));
            } else {
                emitBytes(OpCode::GET_PROPERTY, name);
            }
            emitConstant(Value(static_cast<int64_t>(1)));
            emitByte(assignmentType == TokenType::PLUS_PLUS ? OpCode::ADD
                                                            : OpCode::SUB);
            if (knownField) {
                emitBytes(OpCode::SET_FIELD_SLOT,
                          static_cast<uint8_t>(fieldSlot));
            } else {
                emitBytes(OpCode::SET_PROPERTY, name);
            }
            pushExprType(memberType);
            return;
        }

        if (assignmentType == TokenType::PLUS_EQUAL ||
            assignmentType == TokenType::MINUS_EQUAL ||
            assignmentType == TokenType::STAR_EQUAL ||
            assignmentType == TokenType::SLASH_EQUAL ||
            assignmentType == TokenType::AMPERSAND_EQUAL ||
            assignmentType == TokenType::CARET_EQUAL ||
            assignmentType == TokenType::PIPE_EQUAL ||
            assignmentType == TokenType::SHIFT_LEFT_EQUAL ||
            assignmentType == TokenType::SHIFT_RIGHT_EQUAL) {
            advance();
            emitByte(OpCode::DUP);
            if (knownField) {
                emitBytes(OpCode::GET_FIELD_SLOT,
                          static_cast<uint8_t>(fieldSlot));
            } else {
                emitBytes(OpCode::GET_PROPERTY, name);
            }
            expression();
            TypeRef rhsType = popExprType();
            emitCompoundBinary(assignmentType, memberType, rhsType);
            if (knownField) {
                emitBytes(OpCode::SET_FIELD_SLOT,
                          static_cast<uint8_t>(fieldSlot));
            } else {
                emitBytes(OpCode::SET_PROPERTY, name);
            }
            pushExprType(memberType);
            return;
        }
    }

    if (m_parser->current.type() == TokenType::OPEN_PAREN) {
        emitInvokeCall(OpCode::INVOKE, name, memberType);
        return;
    }

    if (knownField) {
        emitBytes(OpCode::GET_FIELD_SLOT, static_cast<uint8_t>(fieldSlot));
    } else {
        emitBytes(OpCode::GET_PROPERTY, name);
    }
    pushExprType(memberType);
}

void Compiler::subscript(bool canAssign) {
    TypeRef containerType = popExprType();
    expression();
    popExprType();
    consume(TokenType::CLOSE_BRACKET, "Expected ']' after index.");

    TypeRef elementType = TypeInfo::makeAny();
    if (containerType) {
        if (containerType->kind == TypeKind::ARRAY &&
            containerType->elementType) {
            elementType = containerType->elementType;
        } else if (containerType->kind == TypeKind::DICT &&
                   containerType->valueType) {
            elementType = containerType->valueType;
        }
    }

    if (!canAssign) {
        emitByte(OpCode::GET_INDEX);
        pushExprType(elementType);
        return;
    }

    TokenType assignmentType = m_parser->current.type();
    if (assignmentType == TokenType::EQUAL) {
        advance();
        expression();
        TypeRef rhsType = popExprType();
        emitByte(OpCode::SET_INDEX);
        pushExprType((elementType && !elementType->isAny()) ? elementType
                                                            : rhsType);
        return;
    }

    if (assignmentType == TokenType::PLUS_PLUS ||
        assignmentType == TokenType::MINUS_MINUS) {
        advance();
        emitByte(OpCode::DUP2);
        emitByte(OpCode::GET_INDEX);
        emitConstant(Value(static_cast<int64_t>(1)));
        emitByte(assignmentType == TokenType::PLUS_PLUS ? OpCode::ADD
                                                        : OpCode::SUB);
        emitByte(OpCode::SET_INDEX);
        pushExprType(elementType);
        return;
    }

    if (assignmentType == TokenType::PLUS_EQUAL ||
        assignmentType == TokenType::MINUS_EQUAL ||
        assignmentType == TokenType::STAR_EQUAL ||
        assignmentType == TokenType::SLASH_EQUAL ||
        assignmentType == TokenType::AMPERSAND_EQUAL ||
        assignmentType == TokenType::CARET_EQUAL ||
        assignmentType == TokenType::PIPE_EQUAL ||
        assignmentType == TokenType::SHIFT_LEFT_EQUAL ||
        assignmentType == TokenType::SHIFT_RIGHT_EQUAL) {
        advance();
        emitByte(OpCode::DUP2);
        emitByte(OpCode::GET_INDEX);
        expression();
        TypeRef rhsType = popExprType();
        emitCompoundBinary(assignmentType, elementType, rhsType);
        emitByte(OpCode::SET_INDEX);
        pushExprType(elementType);
        return;
    }

    emitByte(OpCode::GET_INDEX);
    pushExprType(elementType);
}

void Compiler::andOperator(bool canAssign) {
    (void)canAssign;
    TypeRef leftType = popExprType();
    int endJump = emitJump(OpCode::JUMP_IF_FALSE);

    emitByte(OpCode::POP);
    parsePrecedence(PREC_AND);
    TypeRef rightType = popExprType();
    patchJump(endJump);
    pushExprType((leftType && !leftType->isAny()) ? leftType : rightType);
}

void Compiler::orOperator(bool canAssign) {
    (void)canAssign;
    TypeRef leftType = popExprType();
    int elseJump = emitJump(OpCode::JUMP_IF_FALSE);
    int endJump = emitJump(OpCode::JUMP);

    patchJump(elseJump);
    emitByte(OpCode::POP);

    parsePrecedence(PREC_OR);
    TypeRef rightType = popExprType();
    patchJump(endJump);
    pushExprType((leftType && !leftType->isAny()) ? leftType : rightType);
}

void Compiler::functionLiteral(bool canAssign) {
    (void)canAssign;
    TypeRef functionType = emitFunctionLiteral();
    pushExprType(functionType ? functionType : TypeInfo::makeAny());
}

TypeRef Compiler::emitFunctionLiteral(const TypeRef& expectedType) {
    TypeRef declaredReturnType = TypeInfo::makeAny();
    if (expectedType && expectedType->kind == TypeKind::FUNCTION &&
        expectedType->returnType) {
        declaredReturnType = expectedType->returnType;
    }

    CompiledFunction compiled = compileFunction("<closure>", false,
                                               declaredReturnType, expectedType,
                                               true);

    emitBytes(OpCode::CLOSURE, makeConstant(Value(compiled.function)));
    for (const auto& upvalue : compiled.upvalues) {
        emitByte(static_cast<uint8_t>(upvalue.isLocal ? 1 : 0));
        emitByte(upvalue.index);
    }

    return TypeInfo::makeFunction(
        compiled.parameterTypes,
        compiled.returnType ? compiled.returnType : TypeInfo::makeAny());
}

Compiler::CompiledFunction Compiler::compileFunction(
    const std::string& name, bool isMethod, const TypeRef& declaredReturnType,
    const TypeRef& expectedFunctionType, bool allowExpressionBody) {
    consume(TokenType::OPEN_PAREN, "Expected '(' after function name.");

    Chunk* enclosingChunk = m_chunk;

    auto functionChunk = std::make_unique<Chunk>();
    m_chunk = functionChunk.get();
    m_contexts.push_back(FunctionContext{
        {},
        {},
        1,
        true,
        isMethod,
        declaredReturnType ? declaredReturnType : TypeInfo::makeAny()});

    std::vector<std::string> parameters;
    std::vector<TypeRef> parameterTypes;
    bool omittedParameterTypeAnnotation = false;
    Token firstOmittedParameterToken;
    const bool hasExpectedFunctionType =
        expectedFunctionType &&
        expectedFunctionType->kind == TypeKind::FUNCTION;
    const auto& expectedParams = hasExpectedFunctionType
                                     ? expectedFunctionType->paramTypes
                                     : std::vector<TypeRef>{};
    if (m_parser->current.type() != TokenType::CLOSE_PAREN) {
        do {
            TypeRef parameterType = nullptr;
            Token parameterNameToken;
            size_t parameterIndex = parameterTypes.size();

            consume(TokenType::IDENTIFIER, "Expected parameter name.");
            parameterNameToken = m_parser->previous;

            if (!isTypedTypeAnnotationStart()) {
                if (hasExpectedFunctionType &&
                    parameterIndex < expectedParams.size() &&
                    expectedParams[parameterIndex] &&
                    !expectedParams[parameterIndex]->isAny()) {
                    parameterType = expectedParams[parameterIndex];
                } else {
                    std::string parameterName(parameterNameToken.start(),
                                              parameterNameToken.length());
                    errorAt(parameterNameToken,
                            "Parameter '" + parameterName +
                                "' must have a type annotation.");
                    parameterType = TypeInfo::makeAny();
                }
                if (!omittedParameterTypeAnnotation) {
                    omittedParameterTypeAnnotation = true;
                    firstOmittedParameterToken = parameterNameToken;
                }
            } else {
                parameterType = parseTypeExprType();
                if (!parameterType) {
                    errorAtCurrent("Expected parameter type annotation.");
                    parameterType = TypeInfo::makeAny();
                }
            }

            if (parameterType && parameterType->isVoid()) {
                std::string parameterName(parameterNameToken.start(),
                                          parameterNameToken.length());
                errorAt(parameterNameToken, "Parameter '" + parameterName +
                                                "' cannot have type 'void'.");
            }

            parameters.emplace_back(parameterNameToken.start(),
                                    parameterNameToken.length());
            parameterTypes.push_back(parameterType ? parameterType
                                                   : TypeInfo::makeAny());
            addLocal(parameterNameToken,
                     parameterType ? parameterType : TypeInfo::makeAny());
            markInitialized();

            if (m_parser->current.type() != TokenType::COMMA) {
                break;
            }
            advance();
        } while (true);
    }

    if (hasExpectedFunctionType &&
        parameterTypes.size() != expectedParams.size()) {
        errorAtCurrent("Closure parameter count mismatch: expected " +
                       std::to_string(expectedParams.size()) + ", got " +
                       std::to_string(parameterTypes.size()) + ".");
    }

    consume(TokenType::CLOSE_PAREN, "Expected ')' after parameters.");

    const bool isInitializer = isMethod && name == "init";
    const bool hasDeclaredReturnType =
        declaredReturnType && !declaredReturnType->isAny();

    for (size_t index = 0; index < parameterTypes.size(); ++index) {
        const TypeRef& parameterType = parameterTypes[index];
        if (!parameterType || parameterType->kind != TypeKind::CLASS) {
            continue;
        }

        emitBytes(OpCode::GET_LOCAL, static_cast<uint8_t>(index));
        emitCheckInstanceType(parameterType);
        emitByte(OpCode::POP);
    }

    if (allowExpressionBody && m_parser->current.type() == TokenType::FAT_ARROW) {
        if (hasLineBreakBeforeCurrent()) {
            errorAtCurrent(lineLeadingContinuationMessage(m_parser->current.type()));
        }
        if (omittedParameterTypeAnnotation) {
            errorAt(firstOmittedParameterToken,
                    "Expression-bodied lambdas require explicit parameter types.");
        }

        advance();
        if (m_parser->current.type() == TokenType::OPEN_CURLY) {
            errorAtCurrent(
                "Expression-bodied lambdas do not support block bodies; use "
                "'fn(...) { ... }'.");

            advance();
            while (m_parser->current.type() != TokenType::CLOSE_CURLY &&
                   m_parser->current.type() != TokenType::END_OF_FILE) {
                declaration();
            }
            consume(TokenType::CLOSE_CURLY,
                    "Expected '}' after lambda block body.");

            currentContext().returnType = TypeInfo::makeAny();
            emitByte(OpCode::NIL);
            emitByte(OpCode::RETURN);
        } else {
            expression();
            recoverLineLeadingContinuation({TokenType::COMMA,
                                            TokenType::CLOSE_PAREN,
                                            TokenType::CLOSE_BRACKET,
                                            TokenType::CLOSE_CURLY});
            rejectUnexpectedTrailingToken({TokenType::COMMA,
                                           TokenType::CLOSE_PAREN,
                                           TokenType::CLOSE_BRACKET,
                                           TokenType::CLOSE_CURLY});

            TypeRef bodyType = popExprType();
            currentContext().returnType =
                bodyType ? bodyType : TypeInfo::makeAny();
            emitByte(OpCode::RETURN);
        }
    } else {
        if (isTypedTypeAnnotationStart()) {
            TypeRef parsedReturnType = parseTypeExprType();
            if (!parsedReturnType) {
                errorAtCurrent("Expected return type after parameter list.");
            } else {
                currentContext().returnType = parsedReturnType;
            }
        } else if (hasExpectedFunctionType && expectedFunctionType->returnType &&
                   !expectedFunctionType->returnType->isAny()) {
            currentContext().returnType = expectedFunctionType->returnType;
        } else if (!isInitializer && !hasDeclaredReturnType) {
            errorAtCurrent("Function '" + name +
                           "' must declare a return type.");
        }

        consume(TokenType::OPEN_CURLY, "Expected '{' before function body.");

        while (m_parser->current.type() != TokenType::CLOSE_CURLY &&
               m_parser->current.type() != TokenType::END_OF_FILE) {
            declaration();
        }
        consume(TokenType::CLOSE_CURLY, "Expected '}' after function body.");

        emitByte(OpCode::NIL);
        emitByte(OpCode::RETURN);
    }

    FunctionContext functionContext = std::move(currentContext());
    m_contexts.pop_back();
    m_chunk = enclosingChunk;

    if (m_gc == nullptr) {
        errorAtCurrent("Internal compiler error: GC allocator unavailable.");
        return CompiledFunction{
            nullptr, {}, std::move(parameterTypes), functionContext.returnType};
    }

    auto function = m_gc->allocate<FunctionObject>();
    function->name = name;
    function->parameters = parameters;
    function->chunk = std::move(functionChunk);
    function->upvalueCount =
        static_cast<uint8_t>(functionContext.upvalues.size());
    return CompiledFunction{function, std::move(functionContext.upvalues),
                            std::move(parameterTypes),
                            functionContext.returnType};
}
