#include "Compiler.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "AstBytecodeEmitter.hpp"
#include "AstFrontend.hpp"
#include "FrontendTypeUtils.hpp"
#include "HirBytecodeEmitter.hpp"
#include "StdLib.hpp"

namespace {

void printDiagnosticPrefix(const SourceSpan& span) {
    std::cerr << "[error][compile][line " << span.line() << ":" << span.column()
              << "] ";
}

bool reportAstFrontendFailure(AstFrontendBuildStatus status,
                              const std::vector<TypeError>& errors,
                              CompilerEmitterMode emitterMode) {
    if (status == AstFrontendBuildStatus::SemanticError) {
        for (const auto& error : errors) {
            printDiagnosticPrefix(error.span);
            std::cerr << error.message << std::endl;
        }
        return true;
    }

    if (status == AstFrontendBuildStatus::ParseFailed) {
        for (const auto& error : errors) {
            printDiagnosticPrefix(error.span);
            std::cerr << error.message << std::endl;
        }
        printDiagnosticPrefix(makePointSpan(1, 1));
        std::cerr << "AST frontend failed to parse source";
        if (emitterMode == CompilerEmitterMode::ForceAst) {
            std::cerr << " for forced AST emission";
        } else if (emitterMode == CompilerEmitterMode::ForceHir) {
            std::cerr << " for forced HIR emission";
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
    m_currentClass = nullptr;
    m_contexts.clear();
    m_globalSlots.clear();
    m_classNames.clear();
    m_typeAliases.clear();
    m_functionSignatures.clear();
    m_classFieldTypes.clear();
    m_classMethodSignatures.clear();
    m_classOperatorMethods.clear();
    m_superclassOf.clear();
    m_globalTypes.clear();
    m_globalConstness.clear();
    m_exprTypeStack.clear();
    m_globalNames.clear();
    m_exportedNames.clear();
    m_lastFrontendTimings = AstFrontendResult::Timings{};
    m_hadError = false;
    m_panicMode = false;
    registerStandardLibraryTypeSignatures(m_functionSignatures);

    AstFrontendResult astFrontend;
    std::vector<TypeError> astErrors;
    AstFrontendMode frontendMode = m_strictMode
                                       ? AstFrontendMode::StrictChecked
                                       : AstFrontendMode::LoweringOnly;
    AstFrontendImportCache importCache;
    AstFrontendOptions frontendOptions;
    frontendOptions.sourcePath = sourcePath;
    frontendOptions.packageSearchPaths = m_packageSearchPaths;
    frontendOptions.importCache = &importCache;
    AstFrontendBuildStatus astStatus =
        buildAstFrontend(source, frontendOptions, frontendMode, astErrors,
                         astFrontend);

    if (astStatus != AstFrontendBuildStatus::Success) {
        m_lastFrontendTimings = astFrontend.timings;
        reportAstFrontendFailure(astStatus, astErrors, m_emitterMode);
        return false;
    }

    m_lastFrontendTimings = astFrontend.timings;
    m_classNames = astFrontend.classNames;
    m_typeAliases = astFrontend.typeAliases;
    m_functionSignatures = astFrontend.functionSignatures;
    m_classFieldTypes = astFrontend.semanticModel.metadata.classFieldTypes;
    m_classMethodSignatures =
        astFrontend.semanticModel.metadata.classMethodSignatures;
    m_classOperatorMethods = astFrontend.semanticModel.classOperatorMethods;
    m_superclassOf = astFrontend.semanticModel.metadata.superclassOf;
    m_contexts.push_back(
        FunctionContext{{}, {}, 0, false, false, TypeInfo::makeAny()});

    if (m_emitterMode != CompilerEmitterMode::ForceAst) {
        if (astFrontend.hirModule == nullptr) {
            errorAtLine(1, "Internal compiler error: missing HIR frontend result.");
            return false;
        }

        HirBytecodeEmitter emitter(*this, *astFrontend.hirModule,
                                   astFrontend.terminalLine);
        return emitter.emitModule();
    }

    AstBytecodeEmitter emitter(*this, astFrontend);
    return emitter.emitModule();
}

TypeRef Compiler::tokenToType(const Token& token) const {
    if (token.type() == TokenType::TYPE_FN) {
        return nullptr;
    }

    return frontendTokenToType(token,
                               FrontendTypeContext{m_classNames, m_typeAliases});
}

void Compiler::emitByte(uint8_t byte, size_t line) {
    currentChunk()->write(byte, line);
}

void Compiler::emitBytes(uint8_t byte1, uint8_t byte2, size_t line) {
    emitByte(byte1, line);
    emitByte(byte2, line);
}

Value Compiler::makeStringValue(const std::string& text) {
    if (m_gc == nullptr) {
        errorAtLine(1, "Internal compiler error: GC allocator unavailable.");
        return Value();
    }

    return Value(m_gc->internString(text));
}

uint8_t Compiler::makeConstant(Value value) {
    int constant = currentChunk()->addConstant(value);
    if (constant > UINT8_MAX) {
        errorAtLine(1, "Too many constants in one chunk.");
        return 0;
    }

    return static_cast<uint8_t>(constant);
}

void Compiler::pushCallResultType(const TypeRef& calleeType) {
    if (calleeType && calleeType->kind == TypeKind::FUNCTION &&
        calleeType->returnType) {
        pushExprType(calleeType->returnType);
    } else if (calleeType && calleeType->kind == TypeKind::CLASS) {
        pushExprType(calleeType);
    } else {
        pushExprType(TypeInfo::makeAny());
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

    if (m_globalNames.size() >= static_cast<size_t>(UINT8_MAX) + 1) {
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
        errorAtLine(1, "Too many closure variables in function.");
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

void Compiler::errorAt(const Token& token, const std::string& message) {
    if (m_panicMode) {
        return;
    }

    m_panicMode = true;
    m_hadError = true;

    printDiagnosticPrefix(token.span());
    if (token.type() == TokenType::END_OF_FILE) {
        std::cerr << " at end";
    } else if (token.type() != TokenType::ERROR) {
        std::cerr << " at '" << std::string(token.start(), token.length())
                  << "'";
    }

    std::cerr << " " << message << std::endl;
}

void Compiler::errorAtSpan(const SourceSpan& span, const std::string& message) {
    if (m_panicMode) {
        return;
    }

    m_panicMode = true;
    m_hadError = true;
    printDiagnosticPrefix(span);
    std::cerr << message << std::endl;
}

void Compiler::errorAtLine(size_t line, const std::string& message) {
    if (m_panicMode) {
        return;
    }

    m_panicMode = true;
    m_hadError = true;
    printDiagnosticPrefix(makePointSpan(line, 1));
    std::cerr << message << std::endl;
}
