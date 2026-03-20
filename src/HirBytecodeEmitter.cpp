#include "HirBytecodeEmitter.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

#include "FrontendTypeUtils.hpp"
#include "NumericLiteral.hpp"

namespace hir_bytecode_emitter_detail {

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

bool parseNumericLiteralValue(const NumericLiteralInfo& info, Value& outValue) {
    try {
        if (info.isFloat) {
            const double parsed = std::stod(info.core);
            if (!std::isfinite(parsed)) {
                return false;
            }
            outValue = Value(parsed);
            return true;
        }

        if (info.isUnsigned) {
            outValue = Value(static_cast<uint64_t>(std::stoull(info.core)));
            return true;
        }

        outValue = Value(static_cast<int64_t>(std::stoll(info.core)));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace hir_bytecode_emitter_detail

HirBytecodeEmitter::HirBytecodeEmitter(Compiler& compiler,
                                       const HirModule& module,
                                       size_t terminalLine)
    : m_compiler(compiler),
      m_module(module),
      m_terminalLine(terminalLine == 0 ? 1 : terminalLine) {}

bool HirBytecodeEmitter::emitModule() {
    for (const auto& item : m_module.items) {
        if (item) {
            emitItem(*item);
        }
    }

    emitReturn(lastModuleLine());
    return !m_compiler.m_hadError;
}

std::string HirBytecodeEmitter::tokenText(const Token& token) const {
    return std::string(token.start(), token.length());
}

size_t HirBytecodeEmitter::safeLine(size_t line) const {
    return line == 0 ? 1 : line;
}

size_t HirBytecodeEmitter::lastModuleLine() const {
    return safeLine(m_terminalLine);
}

void HirBytecodeEmitter::errorAtSpan(const SourceSpan& span,
                                     const std::string& message) {
    m_compiler.errorAtSpan(span, message);
}

void HirBytecodeEmitter::errorAtNode(const HirNodeInfo& node,
                                     const std::string& message) {
    errorAtSpan(node.span, message);
}

void HirBytecodeEmitter::errorAtToken(const Token& token,
                                      const std::string& message) {
    errorAtSpan(token.span(), message);
}

void HirBytecodeEmitter::errorAtLine(size_t line, const std::string& message) {
    m_compiler.errorAtLine(safeLine(line), message);
}

TypeRef HirBytecodeEmitter::nodeType(const HirNodeInfo& node) const {
    return node.type ? node.type : TypeInfo::makeAny();
}

void HirBytecodeEmitter::emitByte(uint8_t byte, size_t line) {
    m_compiler.emitByte(byte, safeLine(line));
}

void HirBytecodeEmitter::emitBytes(uint8_t byte1, uint8_t byte2, size_t line) {
    m_compiler.emitBytes(byte1, byte2, safeLine(line));
}

void HirBytecodeEmitter::emitConstant(Value value, size_t line) {
    emitBytes(OpCode::CONSTANT, m_compiler.makeConstant(value), line);
}

int HirBytecodeEmitter::emitJump(uint8_t instruction, size_t line) {
    emitByte(instruction, line);
    emitByte(0xff, line);
    emitByte(0xff, line);
    return m_compiler.currentChunk()->count() - 2;
}

void HirBytecodeEmitter::patchJump(int offset) {
    int jump = m_compiler.currentChunk()->count() - offset - 2;
    if (jump > UINT16_MAX) {
        errorAtLine(lastModuleLine(), "Too much code to jump over.");
        return;
    }

    m_compiler.currentChunk()->setByteAt(
        offset, static_cast<uint8_t>((jump >> 8) & 0xff));
    m_compiler.currentChunk()->setByteAt(offset + 1,
                                         static_cast<uint8_t>(jump & 0xff));
}

void HirBytecodeEmitter::emitLoop(int loopStart, size_t line) {
    emitByte(OpCode::LOOP, line);
    int offset = m_compiler.currentChunk()->count() - loopStart + 2;
    if (offset > UINT16_MAX) {
        errorAtLine(line, "Loop body too large.");
        return;
    }
    emitByte(static_cast<uint8_t>((offset >> 8) & 0xff), line);
    emitByte(static_cast<uint8_t>(offset & 0xff), line);
}

void HirBytecodeEmitter::emitReturn(size_t line) {
    emitByte(OpCode::NIL, line);
    emitByte(OpCode::RETURN, line);
}

void HirBytecodeEmitter::emitCoerceToType(const TypeRef& sourceType,
                                          const TypeRef& targetType,
                                          size_t line) {
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
            emitByte(OpCode::WIDEN_INT, line);
            emitByte(static_cast<uint8_t>(targetType->kind), line);
        }
        return;
    }

    if (sourceType->isInteger() && targetType->isFloat()) {
        emitByte(OpCode::INT_TO_FLOAT, line);
    }
}

void HirBytecodeEmitter::emitCheckInstanceType(const TypeRef& targetType,
                                               size_t line) {
    if (!targetType || targetType->kind != TypeKind::CLASS ||
        targetType->className.empty()) {
        return;
    }

    emitBytes(OpCode::CHECK_INSTANCE_TYPE,
              m_compiler.makeConstant(
                  m_compiler.makeStringValue(targetType->className)),
              line);
}

bool HirBytecodeEmitter::emitCompoundBinary(TokenType assignmentType,
                                            const TypeRef& leftType,
                                            const TypeRef& rightType,
                                            size_t line) {
    TypeRef promoted = numericPromotion(leftType, rightType);
    TypeRef arithmeticType = promoted ? promoted : leftType;

    switch (assignmentType) {
        case TokenType::PLUS_EQUAL:
        case TokenType::MINUS_EQUAL:
        case TokenType::STAR_EQUAL:
        case TokenType::SLASH_EQUAL:
            emitByte(
                m_compiler.arithmeticOpcode(assignmentType, arithmeticType),
                line);
            return true;
        case TokenType::AMPERSAND_EQUAL:
            emitByte(OpCode::BITWISE_AND, line);
            return true;
        case TokenType::CARET_EQUAL:
            emitByte(OpCode::BITWISE_XOR, line);
            return true;
        case TokenType::PIPE_EQUAL:
            emitByte(OpCode::BITWISE_OR, line);
            return true;
        case TokenType::SHIFT_LEFT_EQUAL:
            emitByte(OpCode::SHIFT_LEFT, line);
            return true;
        case TokenType::SHIFT_RIGHT_EQUAL:
            emitByte(OpCode::SHIFT_RIGHT, line);
            return true;
        default:
            return false;
    }
}

void HirBytecodeEmitter::beginScope() {
    m_compiler.currentContext().scopeDepth++;
}

void HirBytecodeEmitter::endScope(size_t line) {
    m_compiler.currentContext().scopeDepth--;

    while (!m_compiler.currentContext().locals.empty() &&
           m_compiler.currentContext().locals.back().depth >
               m_compiler.currentContext().scopeDepth) {
        if (m_compiler.currentContext().locals.back().isCaptured) {
            emitByte(OpCode::CLOSE_UPVALUE, line);
        } else {
            emitByte(OpCode::POP, line);
        }
        m_compiler.currentContext().locals.pop_back();
    }
}

void HirBytecodeEmitter::defineVariable(uint8_t global, size_t line) {
    if (m_compiler.currentContext().scopeDepth > 0) {
        m_compiler.markInitialized();
        return;
    }

    emitBytes(OpCode::DEFINE_GLOBAL_SLOT, global, line);
}

void HirBytecodeEmitter::emitVariableRead(const Token& name, size_t line) {
    Compiler::ResolvedVariable resolved = m_compiler.resolveNamedVariable(name);
    emitBytes(resolved.getOp, resolved.arg, line);
    m_compiler.pushExprType(resolved.type);
}

TypeRef HirBytecodeEmitter::lookupClassFieldType(
    const TypeRef& objectType, const std::string& propertyName) const {
    if (!objectType || objectType->kind != TypeKind::CLASS) {
        return TypeInfo::makeAny();
    }
    TypeRef fieldType =
        m_compiler.lookupClassFieldType(objectType->className, propertyName);
    return fieldType ? fieldType : TypeInfo::makeAny();
}

TypeRef HirBytecodeEmitter::lookupClassMethodType(
    const TypeRef& objectType, const std::string& methodName) const {
    if (!objectType || objectType->kind != TypeKind::CLASS) {
        return TypeInfo::makeAny();
    }
    TypeRef methodType =
        m_compiler.lookupClassMethodType(objectType->className, methodName);
    return methodType ? methodType : TypeInfo::makeAny();
}

int HirBytecodeEmitter::lookupClassFieldSlot(
    const TypeRef& objectType, const std::string& fieldName) const {
    if (!objectType || objectType->kind != TypeKind::CLASS) {
        return -1;
    }
    return m_compiler.lookupClassFieldSlot(objectType->className, fieldName);
}

void HirBytecodeEmitter::emitExportName(const Token& nameToken, size_t line) {
    std::string exportedName(nameToken.start(), nameToken.length());
    m_compiler.m_exportedNames.push_back(exportedName);

    uint8_t slot = m_compiler.globalSlot(nameToken);
    emitBytes(OpCode::GET_GLOBAL_SLOT, slot, line);
    emitBytes(OpCode::EXPORT_NAME, m_compiler.identifierConstant(nameToken),
              line);
    emitByte(OpCode::POP, line);
}

void HirBytecodeEmitter::emitArguments(const std::vector<HirExprPtr>& arguments,
                                       size_t line, uint8_t& argCount) {
    argCount = 0;
    for (const auto& argument : arguments) {
        emitExpr(*argument);
        m_compiler.popExprType();
        if (argCount == UINT8_MAX) {
            errorAtLine(line, "Cannot have more than 255 arguments.");
            return;
        }
        ++argCount;
    }
}

void HirBytecodeEmitter::emitClosureObject(
    const Compiler::CompiledFunction& compiled, size_t line) {
    emitBytes(OpCode::CLOSURE,
              m_compiler.makeConstant(Value(compiled.function)), line);
    for (const auto& upvalue : compiled.upvalues) {
        emitByte(static_cast<uint8_t>(upvalue.isLocal ? 1 : 0), line);
        emitByte(upvalue.index, line);
    }
}

Compiler::CompiledFunction HirBytecodeEmitter::compileFunction(
    const std::string& name, const std::vector<HirParameter>& params,
    const HirNodeInfo& functionNode, const HirStmt* blockBody,
    const HirExpr* expressionBody, bool isMethod) {
    Chunk* enclosingChunk = m_compiler.m_chunk;
    auto functionChunk = std::make_unique<Chunk>();
    m_compiler.m_chunk = functionChunk.get();

    TypeRef functionType = nodeType(functionNode);
    TypeRef returnType = functionType &&
                                 functionType->kind == TypeKind::FUNCTION &&
                                 functionType->returnType
                             ? functionType->returnType
                             : TypeInfo::makeAny();

    m_compiler.m_contexts.push_back(
        Compiler::FunctionContext{{}, {}, 1, true, isMethod, returnType});

    std::vector<std::string> parameterNames;
    std::vector<TypeRef> parameterTypes;
    for (size_t index = 0; index < params.size(); ++index) {
        const HirParameter& param = params[index];
        TypeRef paramType = nodeType(param.node);
        parameterNames.push_back(tokenText(param.name));
        parameterTypes.push_back(paramType);
        m_compiler.addLocal(param.name, paramType);
        m_compiler.markInitialized();

        if (paramType && paramType->kind == TypeKind::CLASS) {
            emitBytes(OpCode::GET_LOCAL, static_cast<uint8_t>(index),
                      param.node.line);
            emitCheckInstanceType(paramType, param.node.line);
            emitByte(OpCode::POP, param.node.line);
        }
    }

    if (expressionBody) {
        emitExpr(*expressionBody);
        m_compiler.popExprType();
        emitByte(OpCode::RETURN, expressionBody->node.line);
    } else if (blockBody) {
        emitFunctionBody(*blockBody);
        emitReturn(blockBody->node.line);
    } else {
        emitReturn(functionNode.line);
    }

    Compiler::FunctionContext functionContext =
        std::move(m_compiler.currentContext());
    m_compiler.m_contexts.pop_back();
    m_compiler.m_chunk = enclosingChunk;

    if (m_compiler.m_gc == nullptr) {
        errorAtNode(functionNode,
                    "Internal compiler error: GC allocator unavailable.");
        return Compiler::CompiledFunction{
            nullptr, {}, std::move(parameterTypes), functionContext.returnType};
    }

    auto function = m_compiler.m_gc->allocate<FunctionObject>();
    function->name = name;
    function->parameters = std::move(parameterNames);
    function->chunk = std::move(functionChunk);
    function->upvalueCount =
        static_cast<uint8_t>(functionContext.upvalues.size());
    return Compiler::CompiledFunction{
        function, std::move(functionContext.upvalues),
        std::move(parameterTypes), functionContext.returnType};
}

void HirBytecodeEmitter::emitFunctionBody(const HirStmt& body) {
    if (const auto* block = std::get_if<HirBlockStmt>(&body.value)) {
        for (const auto& item : block->items) {
            if (item) {
                emitItem(*item);
            }
        }
        return;
    }

    emitStmt(body);
}

void HirBytecodeEmitter::emitFunctionDecl(const HirFunctionDecl& functionDecl) {
    size_t line = functionDecl.node.line;
    TypeRef functionType = nodeType(functionDecl.node);
    uint8_t variable = 0;

    if (m_compiler.currentContext().scopeDepth > 0) {
        m_compiler.addLocal(functionDecl.name, functionType);
    } else {
        variable = m_compiler.globalSlot(functionDecl.name);
        if (variable < m_compiler.m_globalTypes.size()) {
            m_compiler.m_globalTypes[variable] = functionType;
        }
    }

    Compiler::CompiledFunction compiled = compileFunction(
        tokenText(functionDecl.name), functionDecl.params, functionDecl.node,
        functionDecl.body.get(), nullptr, false);
    emitClosureObject(compiled, line);
    defineVariable(variable, line);

    if (m_compiler.currentContext().scopeDepth == 0 &&
        isPublicSymbolName(std::string_view(functionDecl.name.start(),
                                            functionDecl.name.length()))) {
        emitExportName(functionDecl.name, line);
    }
}

void HirBytecodeEmitter::emitClassDecl(const HirClassDecl& classDecl) {
    size_t line = classDecl.node.line;
    if (m_compiler.currentContext().scopeDepth != 0) {
        errorAtNode(classDecl.node,
                    "Type declarations are only allowed at the top level.");
        return;
    }

    Compiler::ClassContext classContext;
    classContext.hasSuperclass = false;
    classContext.enclosing = m_compiler.m_currentClass;
    classContext.className = tokenText(classDecl.name);
    m_compiler.m_currentClass = &classContext;

    uint8_t variable = m_compiler.globalSlot(classDecl.name);
    TypeRef classType = nodeType(classDecl.node);
    if (!classType || classType->isAny()) {
        classType = TypeInfo::makeClass(tokenText(classDecl.name));
    }
    if (variable < m_compiler.m_globalTypes.size()) {
        m_compiler.m_globalTypes[variable] = classType;
    }
    emitBytes(OpCode::CLASS_OP, m_compiler.identifierConstant(classDecl.name),
              line);
    defineVariable(variable, line);
    emitBytes(OpCode::GET_GLOBAL_SLOT, variable, line);

    if (classDecl.superclass.has_value()) {
        Token superclassName = *classDecl.superclass;
        emitVariableRead(superclassName, superclassName.line());
        m_compiler.popExprType();
        emitByte(OpCode::INHERIT, superclassName.line());
        classContext.hasSuperclass = true;
    }

    for (const auto& method : classDecl.methods) {
        Compiler::CompiledFunction compiled =
            compileFunction(tokenText(method.name), method.params, method.node,
                            method.body.get(), nullptr, true);
        emitClosureObject(compiled, method.node.line);
        emitBytes(OpCode::METHOD, m_compiler.identifierConstant(method.name),
                  method.node.line);
    }

    emitByte(OpCode::POP, line);

    if (isPublicSymbolName(std::string_view(classDecl.name.start(),
                                            classDecl.name.length()))) {
        emitExportName(classDecl.name, line);
    }

    m_compiler.m_currentClass = m_compiler.m_currentClass->enclosing;
}

void HirBytecodeEmitter::emitVarDecl(const HirVarDeclStmt& stmt,
                                     size_t stmtLine, bool allowExport) {
    size_t line = stmt.node.line == 0 ? stmtLine : stmt.node.line;
    TypeRef declaredType = stmt.omittedType
                               ? TypeInfo::makeAny()
                               : stmt.declaredType;
    uint8_t global = 0;

    if (m_compiler.currentContext().scopeDepth > 0) {
        m_compiler.addLocal(stmt.name, declaredType, stmt.isConst);
    } else {
        global = m_compiler.globalSlot(stmt.name);
        if (global < m_compiler.m_globalConstness.size()) {
            m_compiler.m_globalConstness[global] = stmt.isConst;
        }
    }

    TypeRef initializerType = TypeInfo::makeAny();
    if (stmt.initializer) {
        emitExpr(*stmt.initializer);
        initializerType = m_compiler.popExprType();
    } else {
        emitByte(OpCode::NIL, line);
        initializerType = TypeInfo::makeNull();
    }

    TypeRef finalType = declaredType;
    if (!stmt.omittedType) {
        emitCoerceToType(initializerType, declaredType, line);
        emitCheckInstanceType(declaredType, line);
    } else {
        finalType = initializerType;
    }

    if (m_compiler.currentContext().scopeDepth == 0 &&
        global < m_compiler.m_globalTypes.size()) {
        m_compiler.m_globalTypes[global] = finalType;
    }

    defineVariable(global, line);

    if (allowExport && m_compiler.currentContext().scopeDepth == 0 &&
        isPublicSymbolName(
            std::string_view(stmt.name.start(), stmt.name.length()))) {
        emitExportName(stmt.name, line);
    }
}

void HirBytecodeEmitter::emitDestructuredImport(
    const HirDestructuredImportStmt& stmt, size_t line) {
    if (!stmt.initializer ||
        !std::holds_alternative<HirImportExpr>(stmt.initializer->value)) {
        if (stmt.initializer) {
            errorAtNode(stmt.initializer->node,
                        "Expected '@import(...)' in destructured import.");
        } else {
            errorAtLine(line,
                        "Expected '@import(...)' in destructured import.");
        }
        return;
    }

    const auto& importExpr = std::get<HirImportExpr>(stmt.initializer->value);
    if (importExpr.importedModule.importTarget.canonicalId.empty()) {
        errorAtToken(importExpr.path,
                     "Internal frontend error: unresolved import target.");
        return;
    }

    emitBytes(OpCode::IMPORT_MODULE,
              m_compiler.makeConstant(m_compiler.makeStringValue(
                  importExpr.importedModule.importTarget.canonicalId)),
              line);

    for (const auto& binding : stmt.bindings) {
        Token localName = binding.localName.has_value() ? *binding.localName
                                                        : binding.exportedName;
        TypeRef bindingType = nodeType(binding.node);
        emitByte(OpCode::DUP, binding.node.line);
        emitBytes(OpCode::GET_PROPERTY,
                  m_compiler.identifierConstant(binding.exportedName),
                  binding.node.line);

        uint8_t variable = 0;
        if (m_compiler.currentContext().scopeDepth > 0) {
            m_compiler.addLocal(localName, bindingType, true);
        } else {
            variable = m_compiler.globalSlot(localName);
            if (variable < m_compiler.m_globalTypes.size()) {
                m_compiler.m_globalTypes[variable] = bindingType;
            }
            if (variable < m_compiler.m_globalConstness.size()) {
                m_compiler.m_globalConstness[variable] = true;
            }
        }
        defineVariable(variable, binding.node.line);
    }

    emitByte(OpCode::POP, line);
}

void HirBytecodeEmitter::emitItem(const HirItem& item) {
    std::visit(
        [this](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, HirTypeAliasDecl>) {
                return;
            } else if constexpr (std::is_same_v<T, HirClassDecl>) {
                emitClassDecl(value);
            } else if constexpr (std::is_same_v<T, HirFunctionDecl>) {
                emitFunctionDecl(value);
            } else if constexpr (std::is_same_v<T, HirStmtPtr>) {
                if (value) {
                    emitStmt(*value);
                }
            }
        },
        item.value);
}

void HirBytecodeEmitter::emitStmt(const HirStmt& stmt) {
    std::visit(
        [this, &stmt](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, HirBlockStmt>) {
                beginScope();
                for (const auto& item : value.items) {
                    if (item) {
                        emitItem(*item);
                    }
                }
                endScope(stmt.node.line);
            } else if constexpr (std::is_same_v<T, HirExprStmt>) {
                if (value.expression) {
                    emitExpr(*value.expression);
                    m_compiler.popExprType();
                    emitByte(OpCode::POP, stmt.node.line);
                }
            } else if constexpr (std::is_same_v<T, HirPrintStmt>) {
                if (value.expression) {
                    emitExpr(*value.expression);
                    m_compiler.popExprType();
                } else {
                    emitByte(OpCode::NIL, stmt.node.line);
                }
                emitByte(OpCode::PRINT_OP, stmt.node.line);
            } else if constexpr (std::is_same_v<T, HirReturnStmt>) {
                if (!m_compiler.currentContext().inFunction) {
                    errorAtNode(stmt.node,
                                "Cannot return from top-level code.");
                    return;
                }

                if (value.value) {
                    emitExpr(*value.value);
                    TypeRef expressionType = m_compiler.popExprType();
                    emitCoerceToType(expressionType,
                                     m_compiler.currentContext().returnType,
                                     stmt.node.line);
                    emitCheckInstanceType(
                        m_compiler.currentContext().returnType, stmt.node.line);
                } else {
                    emitByte(OpCode::NIL, stmt.node.line);
                }
                emitByte(OpCode::RETURN, stmt.node.line);
            } else if constexpr (std::is_same_v<T, HirIfStmt>) {
                emitExpr(*value.condition);
                m_compiler.popExprType();
                int thenJump =
                    emitJump(OpCode::JUMP_IF_FALSE_POP, stmt.node.line);
                emitStmt(*value.thenBranch);
                int endJump = emitJump(OpCode::JUMP, stmt.node.line);
                patchJump(thenJump);
                if (value.elseBranch) {
                    emitStmt(*value.elseBranch);
                }
                patchJump(endJump);
            } else if constexpr (std::is_same_v<T, HirWhileStmt>) {
                int loopStart = m_compiler.currentChunk()->count();
                emitExpr(*value.condition);
                m_compiler.popExprType();
                int exitJump =
                    emitJump(OpCode::JUMP_IF_FALSE_POP, stmt.node.line);
                emitStmt(*value.body);
                emitLoop(loopStart, stmt.node.line);
                patchJump(exitJump);
            } else if constexpr (std::is_same_v<T, HirVarDeclStmt>) {
                emitVarDecl(value, stmt.node.line);
            } else if constexpr (std::is_same_v<T, HirDestructuredImportStmt>) {
                emitDestructuredImport(value, stmt.node.line);
            } else if constexpr (std::is_same_v<T, HirForStmt>) {
                beginScope();

                if (const auto* initDecl =
                        std::get_if<std::unique_ptr<HirVarDeclStmt>>(
                            &value.initializer)) {
                    if (initDecl->get() != nullptr) {
                        emitVarDecl(**initDecl, stmt.node.line, false);
                    }
                } else if (const auto* initExpr =
                               std::get_if<HirExprPtr>(&value.initializer)) {
                    if (initExpr->get() != nullptr) {
                        emitExpr(**initExpr);
                        m_compiler.popExprType();
                        emitByte(OpCode::POP, stmt.node.line);
                    }
                }

                int loopStart = m_compiler.currentChunk()->count();
                int exitJump = -1;

                if (value.condition) {
                    emitExpr(*value.condition);
                    m_compiler.popExprType();
                    exitJump =
                        emitJump(OpCode::JUMP_IF_FALSE_POP, stmt.node.line);
                }

                if (value.increment) {
                    int bodyJump = emitJump(OpCode::JUMP, stmt.node.line);
                    int incrementStart = m_compiler.currentChunk()->count();
                    emitExpr(*value.increment);
                    m_compiler.popExprType();
                    emitByte(OpCode::POP, stmt.node.line);
                    emitLoop(loopStart, stmt.node.line);
                    loopStart = incrementStart;
                    patchJump(bodyJump);
                }

                emitStmt(*value.body);
                emitLoop(loopStart, stmt.node.line);
                if (exitJump != -1) {
                    patchJump(exitJump);
                }

                endScope(stmt.node.line);
            } else if constexpr (std::is_same_v<T, HirForEachStmt>) {
                beginScope();
                TypeRef declaredType = value.declaredType;
                m_compiler.addLocal(value.name, declaredType, value.isConst);
                emitByte(OpCode::NIL, stmt.node.line);
                m_compiler.markInitialized();
                uint8_t loopVariableSlot = static_cast<uint8_t>(
                    m_compiler.currentContext().locals.size() - 1);

                emitExpr(*value.iterable);
                m_compiler.popExprType();
                emitByte(OpCode::ITER_INIT, stmt.node.line);

                int loopStart = m_compiler.currentChunk()->count();
                int exitJump =
                    emitJump(OpCode::ITER_HAS_NEXT_JUMP, stmt.node.line);
                emitBytes(OpCode::ITER_NEXT_SET_LOCAL, loopVariableSlot,
                          stmt.node.line);
                emitStmt(*value.body);
                emitLoop(loopStart, stmt.node.line);
                patchJump(exitJump);
                emitByte(OpCode::POP, stmt.node.line);
                endScope(stmt.node.line);
            }
        },
        stmt.value);
}

void HirBytecodeEmitter::emitAssignmentToVariable(
    const HirBindingExpr& target, const Token& op, const HirExpr* valueExpr,
    size_t line) {
    Compiler::ResolvedVariable resolved =
        m_compiler.resolveNamedVariable(target.name);
    TypeRef declaredType = resolved.type;
    const std::string targetName(target.name.start(), target.name.length());

    if (resolved.isConst) {
        errorAtToken(target.name,
                     "Cannot assign to const variable '" + targetName + "'.");
        if (valueExpr) {
            emitExpr(*valueExpr);
            m_compiler.popExprType();
        }
        m_compiler.pushExprType(declaredType);
        return;
    }

    if (op.type() == TokenType::EQUAL) {
        emitExpr(*valueExpr);
        TypeRef rhsType = m_compiler.popExprType();
        emitCoerceToType(rhsType, declaredType, line);
        emitCheckInstanceType(declaredType, line);
        emitBytes(resolved.setOp, resolved.arg, line);
        m_compiler.pushExprType(nodeType(valueExpr->node));
        return;
    }

    if (op.type() == TokenType::PLUS_PLUS ||
        op.type() == TokenType::MINUS_MINUS) {
        emitBytes(resolved.getOp, resolved.arg, line);
        emitConstant(Value(static_cast<int64_t>(1)), line);
        emitByte(m_compiler.arithmeticOpcode(op.type() == TokenType::PLUS_PLUS
                                                 ? TokenType::PLUS
                                                 : TokenType::MINUS,
                                             declaredType),
                 line);
        emitCoerceToType(declaredType, declaredType, line);
        emitBytes(resolved.setOp, resolved.arg, line);
        m_compiler.pushExprType(declaredType);
        return;
    }

    emitBytes(resolved.getOp, resolved.arg, line);
    emitExpr(*valueExpr);
    TypeRef rhsType = m_compiler.popExprType();
    emitCompoundBinary(op.type(), declaredType, rhsType, line);
    TypeRef resultType = declaredType;
    if (op.type() == TokenType::PLUS_EQUAL ||
        op.type() == TokenType::MINUS_EQUAL ||
        op.type() == TokenType::STAR_EQUAL ||
        op.type() == TokenType::SLASH_EQUAL) {
        resultType = numericPromotion(declaredType, rhsType);
    } else if (hir_bytecode_emitter_detail::isBitwiseAssignmentOperator(
                   op.type())) {
        resultType = hir_bytecode_emitter_detail::bitwiseResultType(
            declaredType, rhsType);
    }
    emitCoerceToType(resultType ? resultType : declaredType, declaredType,
                     line);
    emitBytes(resolved.setOp, resolved.arg, line);
    m_compiler.pushExprType(declaredType);
}

void HirBytecodeEmitter::emitAssignmentToMember(const HirMemberExpr& target,
                                                const Token& op,
                                                const HirExpr* valueExpr,
                                                size_t line) {
    emitExpr(*target.object);
    TypeRef objectType = m_compiler.popExprType();
    std::string propertyName = tokenText(target.member);
    int fieldSlot = lookupClassFieldSlot(objectType, propertyName);
    bool knownField = fieldSlot >= 0;
    TypeRef memberType = lookupClassFieldType(objectType, propertyName);
    if (!memberType || memberType->isAny()) {
        memberType = lookupClassMethodType(objectType, propertyName);
    }

    if (op.type() == TokenType::EQUAL) {
        emitExpr(*valueExpr);
        TypeRef rhsType = m_compiler.popExprType();
        if (knownField) {
            emitBytes(OpCode::SET_FIELD_SLOT, static_cast<uint8_t>(fieldSlot),
                      line);
        } else {
            emitBytes(OpCode::SET_PROPERTY,
                      m_compiler.identifierConstant(target.member), line);
        }
        m_compiler.pushExprType(
            (memberType && !memberType->isAny()) ? memberType : rhsType);
        return;
    }

    if (op.type() == TokenType::PLUS_PLUS ||
        op.type() == TokenType::MINUS_MINUS) {
        emitByte(OpCode::DUP, line);
        if (knownField) {
            emitBytes(OpCode::GET_FIELD_SLOT, static_cast<uint8_t>(fieldSlot),
                      line);
        } else {
            emitBytes(OpCode::GET_PROPERTY,
                      m_compiler.identifierConstant(target.member), line);
        }
        emitConstant(Value(static_cast<int64_t>(1)), line);
        emitByte(op.type() == TokenType::PLUS_PLUS ? OpCode::ADD : OpCode::SUB,
                 line);
        if (knownField) {
            emitBytes(OpCode::SET_FIELD_SLOT, static_cast<uint8_t>(fieldSlot),
                      line);
        } else {
            emitBytes(OpCode::SET_PROPERTY,
                      m_compiler.identifierConstant(target.member), line);
        }
        m_compiler.pushExprType(memberType);
        return;
    }

    emitByte(OpCode::DUP, line);
    if (knownField) {
        emitBytes(OpCode::GET_FIELD_SLOT, static_cast<uint8_t>(fieldSlot),
                  line);
    } else {
        emitBytes(OpCode::GET_PROPERTY,
                  m_compiler.identifierConstant(target.member), line);
    }
    emitExpr(*valueExpr);
    TypeRef rhsType = m_compiler.popExprType();
    emitCompoundBinary(op.type(), memberType, rhsType, line);
    if (knownField) {
        emitBytes(OpCode::SET_FIELD_SLOT, static_cast<uint8_t>(fieldSlot),
                  line);
    } else {
        emitBytes(OpCode::SET_PROPERTY,
                  m_compiler.identifierConstant(target.member), line);
    }
    m_compiler.pushExprType(memberType);
}

void HirBytecodeEmitter::emitAssignmentToIndex(const HirIndexExpr& target,
                                               const Token& op,
                                               const HirExpr* valueExpr,
                                               size_t line) {
    emitExpr(*target.object);
    TypeRef containerType = m_compiler.popExprType();
    emitExpr(*target.index);
    m_compiler.popExprType();

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

    if (op.type() == TokenType::EQUAL) {
        emitExpr(*valueExpr);
        TypeRef rhsType = m_compiler.popExprType();
        emitByte(OpCode::SET_INDEX, line);
        m_compiler.pushExprType(
            (elementType && !elementType->isAny()) ? elementType : rhsType);
        return;
    }

    if (op.type() == TokenType::PLUS_PLUS ||
        op.type() == TokenType::MINUS_MINUS) {
        emitByte(OpCode::DUP2, line);
        emitByte(OpCode::GET_INDEX, line);
        emitConstant(Value(static_cast<int64_t>(1)), line);
        emitByte(op.type() == TokenType::PLUS_PLUS ? OpCode::ADD : OpCode::SUB,
                 line);
        emitByte(OpCode::SET_INDEX, line);
        m_compiler.pushExprType(elementType);
        return;
    }

    emitByte(OpCode::DUP2, line);
    emitByte(OpCode::GET_INDEX, line);
    emitExpr(*valueExpr);
    TypeRef rhsType = m_compiler.popExprType();
    emitCompoundBinary(op.type(), elementType, rhsType, line);
    emitByte(OpCode::SET_INDEX, line);
    m_compiler.pushExprType(elementType);
}

void HirBytecodeEmitter::emitExpr(const HirExpr& expr) {
    std::visit(
        [this, &expr](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, HirLiteralExpr>) {
                switch (value.token.type()) {
                    case TokenType::TRUE:
                        emitByte(OpCode::TRUE_LITERAL, expr.node.line);
                        break;
                    case TokenType::FALSE:
                        emitByte(OpCode::FALSE_LITERAL, expr.node.line);
                        break;
                    case TokenType::_NULL:
                    case TokenType::TYPE_NULL_KW:
                        emitByte(OpCode::NIL, expr.node.line);
                        break;
                    case TokenType::STRING: {
                        std::string token(value.token.start(),
                                          value.token.length());
                        std::string str = token.substr(1, token.length() - 2);
                        emitConstant(m_compiler.makeStringValue(str),
                                     expr.node.line);
                        break;
                    }
                    case TokenType::NUMBER: {
                        std::string literal(value.token.start(),
                                            value.token.length());
                        auto literalInfo = parseNumericLiteralInfo(literal);
                        if (!literalInfo.valid) {
                            errorAtToken(
                                value.token,
                                "Invalid numeric literal '" + literal + "'.");
                            emitByte(OpCode::NIL, expr.node.line);
                            break;
                        }
                        Value parsed;
                        if (!hir_bytecode_emitter_detail::
                                parseNumericLiteralValue(literalInfo, parsed)) {
                            errorAtToken(
                                value.token,
                                "Invalid numeric literal '" + literal + "'.");
                            emitByte(OpCode::NIL, expr.node.line);
                            break;
                        }
                        emitConstant(parsed, expr.node.line);
                        break;
                    }
                    default:
                        emitByte(OpCode::NIL, expr.node.line);
                        break;
                }
                m_compiler.pushExprType(nodeType(expr.node));
            } else if constexpr (std::is_same_v<T, HirBindingExpr>) {
                emitVariableRead(value.name, expr.node.line);
            } else if constexpr (std::is_same_v<T, HirUnaryExpr>) {
                emitExpr(*value.operand);
                TypeRef operandType = m_compiler.popExprType();
                switch (value.op.type()) {
                    case TokenType::BANG:
                        emitByte(OpCode::NOT, expr.node.line);
                        break;
                    case TokenType::TILDE:
                        emitByte(OpCode::BITWISE_NOT, expr.node.line);
                        break;
                    case TokenType::MINUS:
                        if (m_compiler.m_strictMode && operandType &&
                            operandType->isInteger()) {
                            emitByte(OpCode::INT_NEGATE, expr.node.line);
                        } else {
                            emitByte(OpCode::NEGATE, expr.node.line);
                        }
                        break;
                    default:
                        break;
                }
                m_compiler.pushExprType(nodeType(expr.node));
            } else if constexpr (std::is_same_v<T, HirUpdateExpr>) {
                if (const auto* identifier =
                        std::get_if<HirBindingExpr>(&value.operand->value)) {
                    emitAssignmentToVariable(*identifier, value.op,
                                             value.operand.get(),
                                             expr.node.line);
                } else if (const auto* member = std::get_if<HirMemberExpr>(
                               &value.operand->value)) {
                    emitAssignmentToMember(*member, value.op,
                                           value.operand.get(), expr.node.line);
                } else if (const auto* index = std::get_if<HirIndexExpr>(
                               &value.operand->value)) {
                    emitAssignmentToIndex(*index, value.op, value.operand.get(),
                                          expr.node.line);
                } else {
                    errorAtNode(expr.node, "Invalid assignment target.");
                    emitByte(OpCode::NIL, expr.node.line);
                    m_compiler.pushExprType(TypeInfo::makeAny());
                }
            } else if constexpr (std::is_same_v<T, HirBinaryExpr>) {
                if (value.op.type() == TokenType::LOGICAL_AND) {
                    emitExpr(*value.left);
                    TypeRef leftType = m_compiler.popExprType();
                    int endJump =
                        emitJump(OpCode::JUMP_IF_FALSE, expr.node.line);
                    emitByte(OpCode::POP, expr.node.line);
                    emitExpr(*value.right);
                    TypeRef rightType = m_compiler.popExprType();
                    patchJump(endJump);
                    m_compiler.pushExprType((leftType && !leftType->isAny())
                                                ? leftType
                                                : rightType);
                    return;
                }

                if (value.op.type() == TokenType::LOGICAL_OR) {
                    emitExpr(*value.left);
                    TypeRef leftType = m_compiler.popExprType();
                    int elseJump =
                        emitJump(OpCode::JUMP_IF_FALSE, expr.node.line);
                    int endJump = emitJump(OpCode::JUMP, expr.node.line);
                    patchJump(elseJump);
                    emitByte(OpCode::POP, expr.node.line);
                    emitExpr(*value.right);
                    TypeRef rightType = m_compiler.popExprType();
                    patchJump(endJump);
                    m_compiler.pushExprType((leftType && !leftType->isAny())
                                                ? leftType
                                                : rightType);
                    return;
                }

                emitExpr(*value.left);
                TypeRef leftType = m_compiler.popExprType();
                emitExpr(*value.right);
                TypeRef rightType = m_compiler.popExprType();

                if (leftType && leftType->kind == TypeKind::CLASS) {
                    auto classIt = m_compiler.m_classOperatorMethods.find(
                        leftType->className);
                    if (classIt != m_compiler.m_classOperatorMethods.end()) {
                        auto opIt = classIt->second.find(value.op.type());
                        if (opIt != classIt->second.end()) {
                            emitByte(OpCode::INVOKE, expr.node.line);
                            emitByte(
                                m_compiler.makeConstant(
                                    m_compiler.makeStringValue(opIt->second)),
                                expr.node.line);
                            emitByte(1, expr.node.line);
                            m_compiler.pushExprType(nodeType(expr.node));
                            return;
                        }
                    }
                }

                TypeRef promotedNumeric = numericPromotion(leftType, rightType);
                switch (value.op.type()) {
                    case TokenType::PLUS:
                        if (leftType && rightType &&
                            leftType->kind == TypeKind::STR &&
                            rightType->kind == TypeKind::STR) {
                            emitByte(OpCode::ADD, expr.node.line);
                        } else if (promotedNumeric) {
                            emitByte(m_compiler.arithmeticOpcode(
                                         value.op.type(), promotedNumeric),
                                     expr.node.line);
                        } else {
                            emitByte(OpCode::ADD, expr.node.line);
                        }
                        break;
                    case TokenType::MINUS:
                    case TokenType::STAR:
                    case TokenType::SLASH:
                        if (promotedNumeric) {
                            emitByte(m_compiler.arithmeticOpcode(
                                         value.op.type(), promotedNumeric),
                                     expr.node.line);
                        } else {
                            emitByte(value.op.type() == TokenType::MINUS
                                         ? OpCode::SUB
                                         : (value.op.type() == TokenType::STAR
                                                ? OpCode::MULT
                                                : OpCode::DIV),
                                     expr.node.line);
                        }
                        break;
                    case TokenType::GREATER:
                        emitByte((m_compiler.m_strictMode && promotedNumeric &&
                                  promotedNumeric->isInteger())
                                     ? (promotedNumeric->isSigned()
                                            ? OpCode::IGREATER
                                            : OpCode::UGREATER)
                                     : OpCode::GREATER_THAN,
                                 expr.node.line);
                        break;
                    case TokenType::GREATER_EQUAL:
                        emitByte((m_compiler.m_strictMode && promotedNumeric &&
                                  promotedNumeric->isInteger())
                                     ? (promotedNumeric->isSigned()
                                            ? OpCode::IGREATER_EQ
                                            : OpCode::UGREATER_EQ)
                                     : OpCode::GREATER_EQUAL_THAN,
                                 expr.node.line);
                        break;
                    case TokenType::LESS:
                        emitByte(
                            (m_compiler.m_strictMode && promotedNumeric &&
                             promotedNumeric->isInteger())
                                ? (promotedNumeric->isSigned() ? OpCode::ILESS
                                                               : OpCode::ULESS)
                                : OpCode::LESS_THAN,
                            expr.node.line);
                        break;
                    case TokenType::LESS_EQUAL:
                        emitByte((m_compiler.m_strictMode && promotedNumeric &&
                                  promotedNumeric->isInteger())
                                     ? (promotedNumeric->isSigned()
                                            ? OpCode::ILESS_EQ
                                            : OpCode::ULESS_EQ)
                                     : OpCode::LESS_EQUAL_THAN,
                                 expr.node.line);
                        break;
                    case TokenType::SHIFT_LEFT_TOKEN:
                        emitByte(OpCode::SHIFT_LEFT, expr.node.line);
                        break;
                    case TokenType::SHIFT_RIGHT_TOKEN:
                        emitByte(OpCode::SHIFT_RIGHT, expr.node.line);
                        break;
                    case TokenType::AMPERSAND:
                        emitByte(OpCode::BITWISE_AND, expr.node.line);
                        break;
                    case TokenType::CARET:
                        emitByte(OpCode::BITWISE_XOR, expr.node.line);
                        break;
                    case TokenType::PIPE:
                        emitByte(OpCode::BITWISE_OR, expr.node.line);
                        break;
                    case TokenType::EQUAL_EQUAL:
                        emitByte(OpCode::EQUAL_OP, expr.node.line);
                        break;
                    case TokenType::BANG_EQUAL:
                        emitByte(OpCode::NOT_EQUAL_OP, expr.node.line);
                        break;
                    default:
                        break;
                }
                m_compiler.pushExprType(nodeType(expr.node));
            } else if constexpr (std::is_same_v<T, HirAssignmentExpr>) {
                if (const auto* identifier =
                        std::get_if<HirBindingExpr>(&value.target->value)) {
                    emitAssignmentToVariable(*identifier, value.op,
                                             value.value.get(), expr.node.line);
                } else if (const auto* member = std::get_if<HirMemberExpr>(
                               &value.target->value)) {
                    emitAssignmentToMember(*member, value.op, value.value.get(),
                                           expr.node.line);
                } else if (const auto* index = std::get_if<HirIndexExpr>(
                               &value.target->value)) {
                    emitAssignmentToIndex(*index, value.op, value.value.get(),
                                          expr.node.line);
                } else {
                    errorAtNode(expr.node, "Invalid assignment target.");
                    emitByte(OpCode::NIL, expr.node.line);
                    m_compiler.pushExprType(TypeInfo::makeAny());
                }
            } else if constexpr (std::is_same_v<T, HirCallExpr>) {
                if (const auto* member =
                        std::get_if<HirMemberExpr>(&value.callee->value)) {
                    if (std::holds_alternative<HirSuperExpr>(
                            member->object->value)) {
                        emitByte(OpCode::GET_THIS, expr.node.line);
                        uint8_t argCount = 0;
                        emitArguments(value.arguments, expr.node.line,
                                      argCount);
                        emitByte(OpCode::INVOKE_SUPER, expr.node.line);
                        emitByte(m_compiler.identifierConstant(member->member),
                                 expr.node.line);
                        emitByte(argCount, expr.node.line);
                        m_compiler.pushExprType(nodeType(expr.node));
                        return;
                    }

                    emitExpr(*member->object);
                    m_compiler.popExprType();
                    uint8_t argCount = 0;
                    emitArguments(value.arguments, expr.node.line, argCount);
                    emitByte(OpCode::INVOKE, expr.node.line);
                    emitByte(m_compiler.identifierConstant(member->member),
                             expr.node.line);
                    emitByte(argCount, expr.node.line);
                    m_compiler.pushExprType(nodeType(expr.node));
                    return;
                }

                emitExpr(*value.callee);
                TypeRef calleeType = m_compiler.popExprType();
                uint8_t argCount = 0;
                emitArguments(value.arguments, expr.node.line, argCount);
                emitBytes(OpCode::CALL, argCount, expr.node.line);
                m_compiler.pushCallResultType(calleeType);
            } else if constexpr (std::is_same_v<T, HirMemberExpr>) {
                if (std::holds_alternative<HirSuperExpr>(value.object->value)) {
                    emitBytes(OpCode::GET_SUPER,
                              m_compiler.identifierConstant(value.member),
                              expr.node.line);
                    m_compiler.pushExprType(nodeType(expr.node));
                    return;
                }

                emitExpr(*value.object);
                TypeRef objectType = m_compiler.popExprType();
                std::string propertyName = tokenText(value.member);
                int fieldSlot = lookupClassFieldSlot(objectType, propertyName);
                if (fieldSlot >= 0) {
                    emitBytes(OpCode::GET_FIELD_SLOT,
                              static_cast<uint8_t>(fieldSlot), expr.node.line);
                } else {
                    emitBytes(OpCode::GET_PROPERTY,
                              m_compiler.identifierConstant(value.member),
                              expr.node.line);
                }
                m_compiler.pushExprType(nodeType(expr.node));
            } else if constexpr (std::is_same_v<T, HirIndexExpr>) {
                emitExpr(*value.object);
                m_compiler.popExprType();
                emitExpr(*value.index);
                m_compiler.popExprType();
                emitByte(OpCode::GET_INDEX, expr.node.line);
                m_compiler.pushExprType(nodeType(expr.node));
            } else if constexpr (std::is_same_v<T, HirCastExpr>) {
                emitExpr(*value.expression);
                m_compiler.popExprType();
                TypeRef targetType = nodeType(expr.node);
                if (targetType && targetType->kind == TypeKind::CLASS) {
                    emitCheckInstanceType(targetType, expr.node.line);
                } else if (targetType && targetType->isInteger()) {
                    emitBytes(OpCode::NARROW_INT,
                              static_cast<uint8_t>(targetType->kind),
                              expr.node.line);
                } else if (targetType && targetType->isFloat()) {
                    emitByte(OpCode::INT_TO_FLOAT, expr.node.line);
                } else if (targetType && targetType->kind == TypeKind::STR) {
                    emitByte(OpCode::INT_TO_STR, expr.node.line);
                }
                m_compiler.pushExprType(targetType);
            } else if constexpr (std::is_same_v<T, HirFunctionExpr>) {
                Compiler::CompiledFunction compiled = compileFunction(
                    "<closure>", value.params, expr.node, value.blockBody.get(),
                    value.expressionBody.get(), false);
                emitClosureObject(compiled, expr.node.line);
                m_compiler.pushExprType(nodeType(expr.node));
            } else if constexpr (std::is_same_v<T, HirImportExpr>) {
                if (!value.importedModule.importTarget.canonicalId.empty()) {
                    emitBytes(
                        OpCode::IMPORT_MODULE,
                        m_compiler.makeConstant(m_compiler.makeStringValue(
                            value.importedModule.importTarget.canonicalId)),
                        expr.node.line);
                } else {
                    errorAtToken(value.path,
                                 "Internal frontend error: unresolved import "
                                 "target.");
                    emitByte(OpCode::NIL, expr.node.line);
                }
                m_compiler.pushExprType(nodeType(expr.node));
            } else if constexpr (std::is_same_v<T, HirThisExpr>) {
                emitByte(OpCode::GET_THIS, expr.node.line);
                m_compiler.pushExprType(nodeType(expr.node));
            } else if constexpr (std::is_same_v<T, HirSuperExpr>) {
                errorAtNode(expr.node, "Expected member access after 'super'.");
                emitByte(OpCode::NIL, expr.node.line);
                m_compiler.pushExprType(TypeInfo::makeAny());
            } else if constexpr (std::is_same_v<T, HirArrayLiteralExpr>) {
                uint8_t count = 0;
                for (const auto& element : value.elements) {
                    emitExpr(*element);
                    m_compiler.popExprType();
                    if (count == UINT8_MAX) {
                        errorAtNode(expr.node,
                                    "Array literal cannot have more than 255 "
                                    "elements.");
                        break;
                    }
                    ++count;
                }
                emitBytes(OpCode::BUILD_ARRAY, count, expr.node.line);
                m_compiler.pushExprType(nodeType(expr.node));
            } else if constexpr (std::is_same_v<T, HirDictLiteralExpr>) {
                uint8_t pairCount = 0;
                for (const auto& entry : value.entries) {
                    emitExpr(*entry.key);
                    m_compiler.popExprType();
                    emitExpr(*entry.value);
                    m_compiler.popExprType();
                    if (pairCount == UINT8_MAX) {
                        errorAtNode(expr.node,
                                    "Dictionary literal cannot have more than "
                                    "255 pairs.");
                        break;
                    }
                    ++pairCount;
                }
                emitBytes(OpCode::BUILD_DICT, pairCount, expr.node.line);
                m_compiler.pushExprType(nodeType(expr.node));
            }
        },
        expr.value);
}
