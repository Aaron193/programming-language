#include "Compiler.hpp"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "Chunk.hpp"
#include "ModuleResolver.hpp"
#include "StdLib.hpp"
#include "TypeChecker.hpp"

bool Compiler::compile(std::string_view source, Chunk& chunk,
                       const std::string& sourcePath) {
    m_chunk = &chunk;
    m_sourcePath = sourcePath;
    m_classNames.clear();
    m_functionSignatures.clear();
    m_classFieldTypes.clear();
    m_classMethodSignatures.clear();
    registerStandardLibraryTypeSignatures(m_functionSignatures);
    collectClassNames(source);
    collectFunctionSignatures(source);

    std::vector<TypeError> typeErrors;
    TypeChecker typeChecker;
    if (!typeChecker.check(source, m_classNames, m_functionSignatures,
                           typeErrors)) {
        for (const auto& error : typeErrors) {
            std::cerr << "[error][compile][line " << error.line << "] "
                      << error.message << std::endl;
        }
        return false;
    }

    m_scanner = std::make_unique<Scanner>(source);
    m_parser = std::make_unique<Parser>();
    m_currentClass = nullptr;
    m_contexts.clear();
    m_globalSlots.clear();
    m_globalTypes.clear();
    m_globalNames.clear();
    m_exportedNames.clear();
    m_hasBufferedToken = false;
    m_contexts.push_back(
        FunctionContext{{}, {}, 0, false, false, TypeInfo::makeAny()});

    advance();

    while (m_parser->current.type() != TokenType::END_OF_FILE) {
        declaration();
    }

    consume(TokenType::END_OF_FILE, "Expected end of source.");
    emitReturn();

    return !m_parser->hadError;
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
        case TokenType::TYPE_NULL_KW:
            return TypeInfo::makeNull();
        case TokenType::IDENTIFIER: {
            std::string className(token.start(), token.length());
            if (m_classNames.find(className) != m_classNames.end()) {
                return TypeInfo::makeClass(className);
            }
            return nullptr;
        }
        default:
            return nullptr;
    }
}

void Compiler::collectClassNames(std::string_view source) {
    Scanner scanner(source);
    while (true) {
        Token token = scanner.nextToken();
        if (token.type() == TokenType::END_OF_FILE) {
            return;
        }

        if (token.type() != TokenType::CLASS) {
            continue;
        }

        Token name = scanner.nextToken();
        while (name.type() == TokenType::ERROR) {
            name = scanner.nextToken();
        }

        if (name.type() == TokenType::IDENTIFIER) {
            m_classNames.emplace(name.start(), name.length());
        }
    }
}

void Compiler::collectFunctionSignatures(std::string_view source) {
    Scanner scanner(source);
    int classDepth = 0;

    while (true) {
        Token token = scanner.nextToken();
        if (token.type() == TokenType::END_OF_FILE) {
            return;
        }

        if (token.type() == TokenType::OPEN_CURLY) {
            classDepth++;
            continue;
        }
        if (token.type() == TokenType::CLOSE_CURLY) {
            if (classDepth > 0) classDepth--;
            continue;
        }

        if (classDepth != 0 || token.type() != TokenType::FUNCTION) {
            continue;
        }

        Token functionName = scanner.nextToken();
        if (functionName.type() != TokenType::IDENTIFIER) {
            continue;
        }

        Token openParen = scanner.nextToken();
        if (openParen.type() != TokenType::OPEN_PAREN) {
            continue;
        }

        std::vector<TypeRef> params;
        Token current = scanner.nextToken();
        if (current.type() != TokenType::CLOSE_PAREN) {
            while (true) {
                Token maybeType = current;
                Token maybeName;
                TypeRef parameterType = TypeInfo::makeAny();

                if (isTypeToken(maybeType.type())) {
                    parameterType = tokenToType(maybeType);
                    maybeName = scanner.nextToken();
                } else if (maybeType.type() == TokenType::IDENTIFIER) {
                    Token lookahead = scanner.nextToken();
                    if (lookahead.type() == TokenType::IDENTIFIER) {
                        TypeRef identifierType = tokenToType(maybeType);
                        if (identifierType) {
                            parameterType = identifierType;
                            maybeName = lookahead;
                        } else {
                            maybeName = maybeType;
                            current = lookahead;
                            params.push_back(parameterType);

                            if (current.type() == TokenType::COMMA) {
                                current = scanner.nextToken();
                                continue;
                            }
                            if (current.type() == TokenType::CLOSE_PAREN) {
                                break;
                            }
                            break;
                        }
                    } else {
                        maybeName = maybeType;
                        current = lookahead;
                        params.push_back(parameterType);

                        if (current.type() == TokenType::COMMA) {
                            current = scanner.nextToken();
                            continue;
                        }
                        if (current.type() == TokenType::CLOSE_PAREN) {
                            break;
                        }
                        break;
                    }
                } else {
                    break;
                }

                if (maybeName.type() != TokenType::IDENTIFIER) {
                    break;
                }

                params.push_back(parameterType ? parameterType
                                               : TypeInfo::makeAny());
                current = scanner.nextToken();

                if (current.type() == TokenType::COMMA) {
                    current = scanner.nextToken();
                    continue;
                }
                if (current.type() == TokenType::CLOSE_PAREN) {
                    break;
                }
                break;
            }
        }

        TypeRef returnType = TypeInfo::makeAny();
        Token maybeArrow = scanner.nextToken();
        if (maybeArrow.type() == TokenType::ARROW) {
            Token returnToken = scanner.nextToken();
            TypeRef typedReturn = tokenToType(returnToken);
            if (typedReturn) {
                returnType = typedReturn;
            }
        }

        std::string functionNameText(functionName.start(),
                                     functionName.length());
        m_functionSignatures[functionNameText] =
            TypeInfo::makeFunction(params, returnType);
    }
}

void Compiler::emitByte(uint8_t byte) {
    currentChunk()->write(byte, m_parser->previous.line());
}

void Compiler::emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

void Compiler::emitReturn() { emitByte(OpCode::RETURN); }

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
        case TokenType::SHIFT_LEFT_EQUAL:
        case TokenType::SHIFT_RIGHT_EQUAL:
        case TokenType::PLUS_PLUS:
        case TokenType::MINUS_MINUS:
            return true;
        default:
            return false;
    }
}

uint8_t Compiler::arithmeticOpcode(TokenType operatorType,
                                   const TypeRef& leftType) const {
    if (leftType && leftType->isInteger()) {
        const bool isSigned = leftType->isSigned();
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

void Compiler::emitCoerceToType(const TypeRef& targetType) {
    if (!targetType || targetType->isAny()) {
        return;
    }

    if (targetType->isInteger()) {
        emitByte(OpCode::NARROW_INT);
        emitByte(static_cast<uint8_t>(targetType->kind));
        return;
    }

    if (targetType->isFloat()) {
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
              makeConstant(Value(targetType->className)));
}

bool Compiler::emitCompoundBinary(TokenType assignmentType,
                                  const TypeRef& leftType) {
    switch (assignmentType) {
        case TokenType::PLUS_EQUAL:
        case TokenType::MINUS_EQUAL:
        case TokenType::STAR_EQUAL:
        case TokenType::SLASH_EQUAL:
            emitByte(arithmeticOpcode(assignmentType, leftType));
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
    return makeConstant(Value(std::string(name.start(), name.length())));
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
    m_globalTypes.push_back(nullptr);
    return slot;
}

void Compiler::namedVariable(const Token& name, bool canAssign) {
    uint8_t arg = 0;
    uint8_t getOp = OpCode::GET_GLOBAL_SLOT;
    uint8_t setOp = OpCode::SET_GLOBAL_SLOT;
    TypeRef declaredType = TypeInfo::makeAny();

    int local = resolveLocal(name);
    if (local != -1) {
        getOp = OpCode::GET_LOCAL;
        setOp = OpCode::SET_LOCAL;
        arg = static_cast<uint8_t>(local);
        declaredType = currentContext().locals[local].type;
    } else {
        int upvalue =
            resolveUpvalue(name, static_cast<int>(m_contexts.size()) - 1);
        if (upvalue != -1) {
            getOp = OpCode::GET_UPVALUE;
            setOp = OpCode::SET_UPVALUE;
            arg = static_cast<uint8_t>(upvalue);
        } else {
            arg = globalSlot(name);
            if (arg < m_globalTypes.size()) {
                declaredType = m_globalTypes[arg] ? m_globalTypes[arg]
                                                  : TypeInfo::makeAny();
            }
        }
    }

    if (canAssign) {
        TokenType assignmentType = m_parser->current.type();

        if (assignmentType == TokenType::EQUAL) {
            advance();
            expression();
            emitCoerceToType(declaredType);
            emitCheckInstanceType(declaredType);
            emitBytes(setOp, arg);
            return;
        }

        if (assignmentType == TokenType::PLUS_PLUS ||
            assignmentType == TokenType::MINUS_MINUS) {
            advance();
            emitBytes(getOp, arg);
            emitConstant(Value(1.0));
            emitByte(arithmeticOpcode(assignmentType == TokenType::PLUS_PLUS
                                          ? TokenType::PLUS
                                          : TokenType::MINUS,
                                      declaredType));
            emitCoerceToType(declaredType);
            emitBytes(setOp, arg);
            return;
        }

        if (assignmentType == TokenType::PLUS_EQUAL ||
            assignmentType == TokenType::MINUS_EQUAL ||
            assignmentType == TokenType::STAR_EQUAL ||
            assignmentType == TokenType::SLASH_EQUAL ||
            assignmentType == TokenType::SHIFT_LEFT_EQUAL ||
            assignmentType == TokenType::SHIFT_RIGHT_EQUAL) {
            advance();
            emitBytes(getOp, arg);
            expression();
            emitCompoundBinary(assignmentType, declaredType);
            emitCoerceToType(declaredType);
            emitBytes(setOp, arg);
            return;
        }
    }

    emitBytes(getOp, arg);
}

uint8_t Compiler::parseVariable(const std::string& message,
                                const TypeRef& declaredType) {
    consume(TokenType::IDENTIFIER, message);

    if (currentContext().scopeDepth > 0) {
        addLocal(m_parser->previous,
                 declaredType ? declaredType : TypeInfo::makeAny());
        return 0;
    }

    uint8_t slot = globalSlot(m_parser->previous);
    if (slot < m_globalTypes.size()) {
        m_globalTypes[slot] = declaredType ? declaredType : TypeInfo::makeAny();
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

void Compiler::addLocal(const Token& name, const TypeRef& declaredType) {
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

    currentContext().locals.push_back(Local{
        name, -1, false, declaredType ? declaredType : TypeInfo::makeAny()});
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

int Compiler::addUpvalue(int contextIndex, uint8_t index, bool isLocal) {
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

    upvalues.push_back(Upvalue{index, isLocal});
    return static_cast<int>(upvalues.size()) - 1;
}

int Compiler::resolveUpvalue(const Token& name, int contextIndex) {
    if (contextIndex <= 0) {
        return -1;
    }

    int local = resolveLocalInContext(name, contextIndex - 1);
    if (local != -1) {
        m_contexts[contextIndex - 1].locals[local].isCaptured = true;
        return addUpvalue(contextIndex, static_cast<uint8_t>(local), true);
    }

    int upvalue = resolveUpvalue(name, contextIndex - 1);
    if (upvalue != -1) {
        return addUpvalue(contextIndex, static_cast<uint8_t>(upvalue), false);
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
        case TokenType::TYPE_NULL_KW:
            return true;
        default:
            return false;
    }
}

bool Compiler::parseTypeExpr() { return parseTypeExprType() != nullptr; }

TypeRef Compiler::parseTypeExprType() {
    if (isTypeToken(m_parser->current.type())) {
        TypeRef type = tokenToType(m_parser->current);
        advance();
        return type;
    }

    if (m_parser->current.type() == TokenType::IDENTIFIER) {
        TypeRef type = tokenToType(m_parser->current);
        if (!type) return nullptr;
        advance();
        return type;
    }

    return nullptr;
}

bool Compiler::isTypedVarDeclarationStart() {
    if (!isTypeToken(m_parser->current.type()) &&
        m_parser->current.type() != TokenType::IDENTIFIER) {
        return false;
    }

    const Token& next = peekNextToken();
    return next.type() == TokenType::IDENTIFIER;
}

void Compiler::advance() {
    m_parser->previous = m_parser->current;

    while (true) {
        if (m_hasBufferedToken) {
            m_parser->current = m_bufferedToken;
            m_hasBufferedToken = false;
        } else {
            m_parser->current = m_scanner->nextToken();
        }

        if (m_parser->current.type() != TokenType::ERROR) break;

        errorAtCurrent(m_parser->current.start());
    }
}

const Token& Compiler::peekNextToken() {
    if (!m_hasBufferedToken) {
        while (true) {
            m_bufferedToken = m_scanner->nextToken();
            if (m_bufferedToken.type() != TokenType::ERROR) {
                m_hasBufferedToken = true;
                break;
            }

            errorAt(m_bufferedToken, m_bufferedToken.start());
        }
    }

    return m_bufferedToken;
}

void Compiler::synchronize() {
    m_parser->panicMode = false;

    while (m_parser->current.type() != TokenType::END_OF_FILE) {
        if (m_parser->previous.type() == TokenType::SEMI_COLON) {
            return;
        }

        switch (m_parser->current.type()) {
            case TokenType::CLASS:
            case TokenType::FUNCTION:
            case TokenType::VAR:
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
            case TokenType::TYPE_NULL_KW:
            case TokenType::IMPORT:
            case TokenType::EXPORT:
            case TokenType::FOR:
            case TokenType::IF:
            case TokenType::WHILE:
            case TokenType::PRINT:
            case TokenType::_RETURN:
                return;
            default:
                break;
        }

        advance();
    }
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

void Compiler::consume(TokenType type, const std::string& message) {
    if (m_parser->current.type() == type) {
        advance();
        return;
    }

    errorAtCurrent(message);
}

void Compiler::expression() { parsePrecedence(PREC_ASSIGNMENT); }

void Compiler::declaration() {
    if (m_parser->current.type() == TokenType::CLASS) {
        advance();
        classDeclaration();
    } else if (m_parser->current.type() == TokenType::IMPORT) {
        advance();
        importDeclaration();
    } else if (m_parser->current.type() == TokenType::EXPORT) {
        advance();
        exportDeclaration();
    } else if (m_parser->current.type() == TokenType::FUNCTION) {
        advance();
        functionDeclaration();
    } else if (m_parser->current.type() == TokenType::VAR) {
        advance();
        varDeclaration();
    } else if (isTypedVarDeclarationStart()) {
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

void Compiler::exportDeclaration() {
    if (currentContext().scopeDepth != 0) {
        errorAtCurrent("'export' is only allowed at the top level.");
    }

    if (m_parser->current.type() == TokenType::FUNCTION) {
        advance();
        if (m_parser->current.type() != TokenType::IDENTIFIER) {
            errorAtCurrent("Expected function name.");
            return;
        }
        Token exportName = m_parser->current;
        functionDeclaration();
        emitExportName(exportName);
        return;
    }

    if (m_parser->current.type() == TokenType::VAR) {
        advance();
        if (m_parser->current.type() != TokenType::IDENTIFIER) {
            errorAtCurrent("Expected variable name.");
            return;
        }
        Token exportName = m_parser->current;
        varDeclaration();
        emitExportName(exportName);
        return;
    }

    if (m_parser->current.type() == TokenType::CLASS) {
        advance();
        if (m_parser->current.type() != TokenType::IDENTIFIER) {
            errorAtCurrent("Expected class name.");
            return;
        }
        Token exportName = m_parser->current;
        classDeclaration();
        emitExportName(exportName);
        return;
    }

    errorAtCurrent("Expected 'function', 'var', or 'class' after 'export'.");
}

void Compiler::importDeclaration() {
    if (m_sourcePath.empty()) {
        errorAtCurrent(
            "Import statements are not allowed in interactive mode.");
        return;
    }

    auto emitImportPath = [&](const std::string& resolvedPath) {
        emitBytes(OpCode::IMPORT_MODULE, makeConstant(Value(resolvedPath)));
    };

    auto parseAndResolvePath = [&]() -> std::string {
        consume(TokenType::FROM, "Expected 'from' in import declaration.");
        consume(TokenType::STRING,
                "Expected string literal module path in import declaration.");

        std::string pathText(m_parser->previous.start(),
                             m_parser->previous.length());
        if (pathText.length() < 2) {
            errorAt(m_parser->previous, "Invalid import path.");
            return "";
        }

        std::string rawPath = pathText.substr(1, pathText.length() - 2);
        std::string resolvedPath = resolveImportPath(m_sourcePath, rawPath);
        if (resolvedPath.empty()) {
            errorAt(m_parser->previous,
                    "Cannot find module '" + rawPath + "'.");
            return "";
        }

        return resolvedPath;
    };

    if (m_parser->current.type() == TokenType::OPEN_CURLY) {
        struct NamedBinding {
            Token exportName;
            Token localName;
        };

        advance();
        std::vector<NamedBinding> bindings;

        if (m_parser->current.type() != TokenType::CLOSE_CURLY) {
            do {
                consume(TokenType::IDENTIFIER,
                        "Expected export name in named import list.");
                Token exportName = m_parser->previous;
                Token localName = exportName;

                if (m_parser->current.type() == TokenType::AS ||
                    m_parser->current.type() == TokenType::AS_KW) {
                    advance();
                    consume(TokenType::IDENTIFIER,
                            "Expected local alias after 'as'.");
                    localName = m_parser->previous;
                }

                bindings.push_back(NamedBinding{exportName, localName});

                if (m_parser->current.type() != TokenType::COMMA) {
                    break;
                }
                advance();
            } while (true);
        }

        consume(TokenType::CLOSE_CURLY,
                "Expected '}' after named import list.");
        std::string resolvedPath = parseAndResolvePath();
        consume(TokenType::SEMI_COLON,
                "Expected ';' after import declaration.");

        if (resolvedPath.empty()) {
            return;
        }

        emitImportPath(resolvedPath);
        for (const auto& binding : bindings) {
            emitByte(OpCode::DUP);
            emitBytes(OpCode::GET_PROPERTY,
                      identifierConstant(binding.exportName));

            uint8_t variable = 0;
            if (currentContext().scopeDepth > 0) {
                addLocal(binding.localName);
            } else {
                variable = globalSlot(binding.localName);
            }
            defineVariable(variable);
        }
        emitByte(OpCode::POP);
        return;
    }

    consume(TokenType::IDENTIFIER,
            "Expected module alias or named import list after 'import'.");
    Token aliasName = m_parser->previous;

    std::string resolvedPath = parseAndResolvePath();
    consume(TokenType::SEMI_COLON, "Expected ';' after import declaration.");

    if (resolvedPath.empty()) {
        return;
    }

    emitImportPath(resolvedPath);

    uint8_t variable = 0;
    if (currentContext().scopeDepth > 0) {
        addLocal(aliasName);
    } else {
        variable = globalSlot(aliasName);
    }
    defineVariable(variable);
}

void Compiler::classDeclaration() {
    ClassContext classContext;
    classContext.hasSuperclass = false;
    classContext.enclosing = m_currentClass;
    classContext.className.clear();
    m_currentClass = &classContext;

    consume(TokenType::IDENTIFIER, "Expected class name.");
    Token nameToken = m_parser->previous;
    classContext.className = std::string(nameToken.start(), nameToken.length());
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
            errorAt(superclassName, "A class cannot inherit from itself.");
        }

        namedVariable(superclassName, false);
        emitByte(OpCode::INHERIT);
        classContext.hasSuperclass = true;
    }

    consume(TokenType::OPEN_CURLY, "Expected '{' before class body.");
    while (m_parser->current.type() != TokenType::CLOSE_CURLY &&
           m_parser->current.type() != TokenType::END_OF_FILE) {
        classMemberDeclaration();
    }
    consume(TokenType::CLOSE_CURLY, "Expected '}' after class body.");
    emitByte(OpCode::POP);

    m_currentClass = m_currentClass->enclosing;
}

void Compiler::classMemberDeclaration() {
    if (isTypeToken(m_parser->current.type()) ||
        (m_parser->current.type() == TokenType::IDENTIFIER &&
         peekNextToken().type() == TokenType::IDENTIFIER)) {
        typedClassMemberDeclaration();
        return;
    }

    methodDeclaration();
}

void Compiler::typedClassMemberDeclaration() {
    TypeRef declaredType = parseTypeExprType();
    if (!declaredType) {
        errorAtCurrent("Expected class member type.");
        return;
    }

    consume(TokenType::IDENTIFIER, "Expected class member name.");
    Token memberName = m_parser->previous;
    std::string memberNameText(memberName.start(), memberName.length());
    std::string className = m_currentClass ? m_currentClass->className : "";

    if (m_parser->current.type() == TokenType::SEMI_COLON) {
        if (!className.empty()) {
            m_classFieldTypes[className][memberNameText] = declaredType;
        }
        advance();
        return;
    }

    if (m_parser->current.type() != TokenType::OPEN_PAREN) {
        errorAtCurrent("Expected ';' after typed field or '(' for method.");
        return;
    }

    uint8_t nameConstant = identifierConstant(memberName);
    CompiledFunction compiled =
        compileFunction(std::string(memberName.start(), memberName.length()),
                        true, declaredType);

    if (!className.empty()) {
        m_classMethodSignatures[className][memberNameText] =
            TypeInfo::makeFunction(compiled.parameterTypes, declaredType);
    }

    emitBytes(OpCode::CLOSURE, makeConstant(Value(compiled.function)));
    for (const auto& upvalue : compiled.upvalues) {
        emitByte(static_cast<uint8_t>(upvalue.isLocal ? 1 : 0));
        emitByte(upvalue.index);
    }
    emitBytes(OpCode::METHOD, nameConstant);
}

void Compiler::methodDeclaration() {
    consume(TokenType::IDENTIFIER, "Expected method name.");
    Token nameToken = m_parser->previous;
    std::string methodName(nameToken.start(), nameToken.length());
    uint8_t nameConstant = identifierConstant(nameToken);

    CompiledFunction compiled = compileFunction(
        std::string(nameToken.start(), nameToken.length()), true);

    if (m_currentClass && !m_currentClass->className.empty()) {
        m_classMethodSignatures[m_currentClass->className][methodName] =
            TypeInfo::makeFunction(compiled.parameterTypes,
                                   TypeInfo::makeAny());
    }

    emitBytes(OpCode::CLOSURE, makeConstant(Value(compiled.function)));
    for (const auto& upvalue : compiled.upvalues) {
        emitByte(static_cast<uint8_t>(upvalue.isLocal ? 1 : 0));
        emitByte(upvalue.index);
    }
    emitBytes(OpCode::METHOD, nameConstant);
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
    consume(TokenType::CLOSE_PAREN, "Expected ')' after condition.");

    int thenJump = emitJump(OpCode::JUMP_IF_FALSE);
    emitByte(OpCode::POP);
    statement();

    if (m_parser->current.type() == TokenType::ELSE) {
        int elseJump = emitJump(OpCode::JUMP);
        patchJump(thenJump);
        emitByte(OpCode::POP);
        advance();
        statement();
        patchJump(elseJump);
    } else {
        patchJump(thenJump);
        emitByte(OpCode::POP);
    }
}

void Compiler::whileStatement() {
    int loopStart = currentChunk()->count();

    consume(TokenType::OPEN_PAREN, "Expected '(' after 'while'.");
    expression();
    consume(TokenType::CLOSE_PAREN, "Expected ')' after condition.");

    int exitJump = emitJump(OpCode::JUMP_IF_FALSE);
    emitByte(OpCode::POP);
    statement();
    emitLoop(loopStart);
    patchJump(exitJump);
    emitByte(OpCode::POP);
}

void Compiler::forStatement() {
    beginScope();

    consume(TokenType::OPEN_PAREN, "Expected '(' after 'for'.");

    if (m_parser->current.type() == TokenType::SEMI_COLON) {
        advance();
    } else if (m_parser->current.type() == TokenType::VAR) {
        advance();

        consume(TokenType::IDENTIFIER, "Expected variable name.");
        Token loopVariable = m_parser->previous;

        if (m_parser->current.type() == TokenType::COLON) {
            advance();

            addLocal(loopVariable);
            emitByte(OpCode::NIL);
            markInitialized();
            uint8_t loopVariableSlot =
                static_cast<uint8_t>(currentContext().locals.size() - 1);

            expression();
            consume(TokenType::CLOSE_PAREN,
                    "Expected ')' after foreach iterable.");

            emitByte(OpCode::ITER_INIT);

            int loopStart = currentChunk()->count();
            emitByte(OpCode::DUP);
            emitByte(OpCode::ITER_HAS_NEXT);
            int exitJump = emitJump(OpCode::JUMP_IF_FALSE);
            emitByte(OpCode::POP);

            emitByte(OpCode::DUP);
            emitByte(OpCode::ITER_NEXT);
            emitBytes(OpCode::SET_LOCAL, loopVariableSlot);
            emitByte(OpCode::POP);

            statement();
            emitLoop(loopStart);

            patchJump(exitJump);
            emitByte(OpCode::POP);
            emitByte(OpCode::POP);

            endScope();
            return;
        }

        uint8_t global = 0;
        if (currentContext().scopeDepth > 0) {
            addLocal(loopVariable);
        } else {
            global = globalSlot(loopVariable);
        }

        if (m_parser->current.type() == TokenType::EQUAL) {
            advance();
            expression();
        } else {
            emitByte(OpCode::NIL);
        }

        consume(TokenType::SEMI_COLON,
                "Expected ';' after variable declaration.");
        defineVariable(global);
    } else {
        expression();
        consume(TokenType::SEMI_COLON, "Expected ';' after loop initializer.");
        emitByte(OpCode::POP);
    }

    int loopStart = currentChunk()->count();
    int exitJump = -1;

    if (m_parser->current.type() != TokenType::SEMI_COLON) {
        expression();
        consume(TokenType::SEMI_COLON, "Expected ';' after loop condition.");
        exitJump = emitJump(OpCode::JUMP_IF_FALSE);
        emitByte(OpCode::POP);
    } else {
        advance();
    }

    if (m_parser->current.type() != TokenType::CLOSE_PAREN) {
        int bodyJump = emitJump(OpCode::JUMP);
        int incrementStart = currentChunk()->count();

        expression();
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
        emitByte(OpCode::POP);
    }

    endScope();
}

void Compiler::printStatement() {
    expression();
    if (m_parser->current.type() == TokenType::SEMI_COLON) {
        advance();
    }
    emitByte(OpCode::PRINT_OP);
}

void Compiler::returnStatement() {
    if (!currentContext().inFunction) {
        errorAtCurrent("Cannot return from top-level code.");
    }

    if (m_parser->current.type() == TokenType::SEMI_COLON) {
        advance();
        emitByte(OpCode::NIL);
        emitByte(OpCode::RETURN);
        return;
    }

    expression();
    emitCoerceToType(currentContext().returnType);
    emitCheckInstanceType(currentContext().returnType);
    if (m_parser->current.type() == TokenType::SEMI_COLON) {
        advance();
    }
    emitByte(OpCode::RETURN);
}

void Compiler::expressionStatement() {
    expression();
    if (m_parser->current.type() == TokenType::SEMI_COLON) {
        advance();
    }
    emitByte(OpCode::POP);
}

void Compiler::varDeclaration() {
    uint8_t global =
        parseVariable("Expected variable name.", TypeInfo::makeAny());

    if (m_parser->current.type() == TokenType::EQUAL) {
        advance();
        expression();
    } else {
        emitByte(OpCode::NIL);
    }

    consume(TokenType::SEMI_COLON, "Expected ';' after variable declaration.");
    defineVariable(global);
}

void Compiler::typedVarDeclaration() {
    TypeRef declaredType = parseTypeExprType();
    if (!declaredType) {
        errorAtCurrent("Expected type in typed variable declaration.");
        return;
    }

    uint8_t global =
        parseVariable("Expected variable name after type.", declaredType);
    consume(TokenType::EQUAL,
            "Expected '=' in typed variable declaration (initializer is "
            "required).");
    expression();
    emitCoerceToType(declaredType);
    emitCheckInstanceType(declaredType);

    consume(TokenType::SEMI_COLON,
            "Expected ';' after typed variable declaration.");
    defineVariable(global);
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
        advance();
        ParseFn infixRule = getRule(m_parser->previous.type()).infix;
        infixRule(canAssign);
    }

    if (canAssign && isAssignmentOperator(m_parser->current.type())) {
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

        case TokenType::BANG:
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
        case TokenType::EQUAL_EQUAL:
        case TokenType::BANG_EQUAL:
            return ParseRule{nullptr,
                             [this](bool canAssign) { binary(canAssign); },
                             PREC_EQUALITY};
        case TokenType::AND:
            return ParseRule{nullptr,
                             [this](bool canAssign) { andOperator(canAssign); },
                             PREC_AND};
        case TokenType::OR:
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
    emitConstant(std::stod(literal));
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
    emitBytes(OpCode::GET_SUPER, name);
}

void Compiler::literal(bool canAssign) {
    (void)canAssign;
    switch (m_parser->previous.type()) {
        case TokenType::TRUE:
            emitByte(OpCode::TRUE_LITERAL);
            break;
        case TokenType::FALSE:
            emitByte(OpCode::FALSE_LITERAL);
            break;
        case TokenType::_NULL:
        case TokenType::TYPE_NULL_KW:
            emitByte(OpCode::NIL);
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
    emitConstant(Value(value));
}

void Compiler::grouping(bool canAssign) {
    (void)canAssign;
    expression();
    consume(TokenType::CLOSE_PAREN, "Expected ')' after expression.");
}

void Compiler::arrayLiteral(bool canAssign) {
    (void)canAssign;

    uint8_t count = 0;
    if (m_parser->current.type() != TokenType::CLOSE_BRACKET) {
        do {
            expression();
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
}

void Compiler::dictLiteral(bool canAssign) {
    (void)canAssign;

    uint8_t pairCount = 0;
    if (m_parser->current.type() != TokenType::CLOSE_CURLY) {
        do {
            expression();
            consume(TokenType::COLON,
                    "Expected ':' between dictionary key and value.");
            expression();

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
}

void Compiler::unary(bool canAssign) {
    (void)canAssign;
    TokenType operatorType = m_parser->previous.type();

    parsePrecedence(PREC_UNARY);

    switch (operatorType) {
        case TokenType::BANG:
            emitByte(OpCode::NOT);
            break;
        case TokenType::MINUS:
            emitByte(OpCode::NEGATE);
            break;
        default:
            return;
    }
}

void Compiler::prefixUpdate(bool canAssign) {
    (void)canAssign;

    TokenType updateToken = m_parser->previous.type();
    bool isIncrement = updateToken == TokenType::PLUS_PLUS;

    auto emitNamedUpdate = [&](const Token& nameToken) {
        uint8_t arg = 0;
        uint8_t getOp = OpCode::GET_GLOBAL_SLOT;
        uint8_t setOp = OpCode::SET_GLOBAL_SLOT;
        TypeRef declaredType = TypeInfo::makeAny();

        int local = resolveLocal(nameToken);
        if (local != -1) {
            getOp = OpCode::GET_LOCAL;
            setOp = OpCode::SET_LOCAL;
            arg = static_cast<uint8_t>(local);
            declaredType = currentContext().locals[local].type;
        } else {
            int upvalue = resolveUpvalue(
                nameToken, static_cast<int>(m_contexts.size()) - 1);
            if (upvalue != -1) {
                getOp = OpCode::GET_UPVALUE;
                setOp = OpCode::SET_UPVALUE;
                arg = static_cast<uint8_t>(upvalue);
            } else {
                arg = globalSlot(nameToken);
                if (arg < m_globalTypes.size()) {
                    declaredType = m_globalTypes[arg] ? m_globalTypes[arg]
                                                      : TypeInfo::makeAny();
                }
            }
        }

        emitBytes(getOp, arg);
        emitConstant(Value(1.0));
        emitByte(arithmeticOpcode(
            isIncrement ? TokenType::PLUS : TokenType::MINUS, declaredType));
        emitCoerceToType(declaredType);
        emitBytes(setOp, arg);
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
    emitConstant(Value(1.0));
    emitByte(isIncrement ? OpCode::ADD : OpCode::SUB);
    emitBytes(OpCode::SET_PROPERTY, propertyName);
}

void Compiler::binary(bool canAssign) {
    (void)canAssign;
    TokenType operatorType = m_parser->previous.type();
    ParseRule rule = getRule(operatorType);
    parsePrecedence(static_cast<Precedence>(rule.precedence + 1));

    switch (operatorType) {
        case TokenType::PLUS:
            emitByte(OpCode::ADD);
            break;
        case TokenType::MINUS:
            emitByte(OpCode::SUB);
            break;
        case TokenType::STAR:
            emitByte(OpCode::MULT);
            break;
        case TokenType::SLASH:
            emitByte(OpCode::DIV);
            break;
        case TokenType::GREATER:
            emitByte(OpCode::GREATER_THAN);
            break;
        case TokenType::GREATER_EQUAL:
            emitByte(OpCode::GREATER_EQUAL_THAN);
            break;
        case TokenType::LESS:
            emitByte(OpCode::LESS_THAN);
            break;
        case TokenType::LESS_EQUAL:
            emitByte(OpCode::LESS_EQUAL_THAN);
            break;
        case TokenType::SHIFT_LEFT_TOKEN:
            emitByte(OpCode::SHIFT_LEFT);
            break;
        case TokenType::SHIFT_RIGHT_TOKEN:
            emitByte(OpCode::SHIFT_RIGHT);
            break;
        case TokenType::EQUAL_EQUAL:
            emitByte(OpCode::EQUAL_OP);
            break;
        case TokenType::BANG_EQUAL:
            emitByte(OpCode::NOT_EQUAL_OP);
            break;
        default:
            return;
    }
}

void Compiler::castOperator(bool canAssign) {
    (void)canAssign;

    TypeRef targetType = parseTypeExprType();
    if (!targetType) {
        errorAtCurrent("Expected type after 'as'.");
        return;
    }

    if (targetType->isInteger()) {
        emitBytes(OpCode::NARROW_INT, static_cast<uint8_t>(targetType->kind));
        return;
    }

    if (targetType->isFloat()) {
        emitByte(OpCode::INT_TO_FLOAT);
        return;
    }

    if (targetType->kind == TypeKind::STR) {
        emitByte(OpCode::INT_TO_STR);
    }
}

void Compiler::call(bool canAssign) {
    (void)canAssign;

    uint8_t argCount = 0;
    if (m_parser->current.type() != TokenType::CLOSE_PAREN) {
        do {
            expression();
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
    emitBytes(OpCode::CALL, argCount);
}

void Compiler::dot(bool canAssign) {
    consume(TokenType::IDENTIFIER, "Expected property name after '.'.");
    uint8_t name = identifierConstant(m_parser->previous);

    if (canAssign) {
        TokenType assignmentType = m_parser->current.type();

        if (assignmentType == TokenType::EQUAL) {
            advance();
            expression();
            emitBytes(OpCode::SET_PROPERTY, name);
            return;
        }

        if (assignmentType == TokenType::PLUS_PLUS ||
            assignmentType == TokenType::MINUS_MINUS) {
            advance();
            emitByte(OpCode::DUP);
            emitBytes(OpCode::GET_PROPERTY, name);
            emitConstant(Value(1.0));
            emitByte(assignmentType == TokenType::PLUS_PLUS ? OpCode::ADD
                                                            : OpCode::SUB);
            emitBytes(OpCode::SET_PROPERTY, name);
            return;
        }

        if (assignmentType == TokenType::PLUS_EQUAL ||
            assignmentType == TokenType::MINUS_EQUAL ||
            assignmentType == TokenType::STAR_EQUAL ||
            assignmentType == TokenType::SLASH_EQUAL ||
            assignmentType == TokenType::SHIFT_LEFT_EQUAL ||
            assignmentType == TokenType::SHIFT_RIGHT_EQUAL) {
            advance();
            emitByte(OpCode::DUP);
            emitBytes(OpCode::GET_PROPERTY, name);
            expression();
            emitCompoundBinary(assignmentType);
            emitBytes(OpCode::SET_PROPERTY, name);
            return;
        }
    }

    emitBytes(OpCode::GET_PROPERTY, name);
}

void Compiler::subscript(bool canAssign) {
    expression();
    consume(TokenType::CLOSE_BRACKET, "Expected ']' after index.");

    if (!canAssign) {
        emitByte(OpCode::GET_INDEX);
        return;
    }

    TokenType assignmentType = m_parser->current.type();
    if (assignmentType == TokenType::EQUAL) {
        advance();
        expression();
        emitByte(OpCode::SET_INDEX);
        return;
    }

    if (assignmentType == TokenType::PLUS_PLUS ||
        assignmentType == TokenType::MINUS_MINUS) {
        advance();
        emitByte(OpCode::DUP2);
        emitByte(OpCode::GET_INDEX);
        emitConstant(Value(1.0));
        emitByte(assignmentType == TokenType::PLUS_PLUS ? OpCode::ADD
                                                        : OpCode::SUB);
        emitByte(OpCode::SET_INDEX);
        return;
    }

    if (assignmentType == TokenType::PLUS_EQUAL ||
        assignmentType == TokenType::MINUS_EQUAL ||
        assignmentType == TokenType::STAR_EQUAL ||
        assignmentType == TokenType::SLASH_EQUAL ||
        assignmentType == TokenType::SHIFT_LEFT_EQUAL ||
        assignmentType == TokenType::SHIFT_RIGHT_EQUAL) {
        advance();
        emitByte(OpCode::DUP2);
        emitByte(OpCode::GET_INDEX);
        expression();
        emitCompoundBinary(assignmentType);
        emitByte(OpCode::SET_INDEX);
        return;
    }

    emitByte(OpCode::GET_INDEX);
}

void Compiler::andOperator(bool canAssign) {
    (void)canAssign;
    int endJump = emitJump(OpCode::JUMP_IF_FALSE);

    emitByte(OpCode::POP);
    parsePrecedence(PREC_AND);
    patchJump(endJump);
}

void Compiler::orOperator(bool canAssign) {
    (void)canAssign;
    int elseJump = emitJump(OpCode::JUMP_IF_FALSE);
    int endJump = emitJump(OpCode::JUMP);

    patchJump(elseJump);
    emitByte(OpCode::POP);

    parsePrecedence(PREC_OR);
    patchJump(endJump);
}

Compiler::CompiledFunction Compiler::compileFunction(
    const std::string& name, bool isMethod, const TypeRef& declaredReturnType) {
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
    if (m_parser->current.type() != TokenType::CLOSE_PAREN) {
        do {
            TypeRef parameterType = TypeInfo::makeAny();
            if (isTypeToken(m_parser->current.type()) ||
                (m_parser->current.type() == TokenType::IDENTIFIER &&
                 peekNextToken().type() == TokenType::IDENTIFIER)) {
                parameterType = parseTypeExprType();
                if (!parameterType) {
                    errorAtCurrent("Expected parameter type annotation.");
                }
            }

            consume(TokenType::IDENTIFIER, "Expected parameter name.");
            parameters.emplace_back(m_parser->previous.start(),
                                    m_parser->previous.length());
            parameterTypes.push_back(parameterType);
            addLocal(m_parser->previous, parameterType);
            markInitialized();

            if (m_parser->current.type() != TokenType::COMMA) {
                break;
            }
            advance();
        } while (true);
    }

    consume(TokenType::CLOSE_PAREN, "Expected ')' after parameters.");

    if (m_parser->current.type() == TokenType::ARROW) {
        advance();
        TypeRef parsedReturnType = parseTypeExprType();
        if (!parsedReturnType) {
            errorAtCurrent("Expected return type after '->'.");
        } else {
            currentContext().returnType = parsedReturnType;
        }
    }

    consume(TokenType::OPEN_CURLY, "Expected '{' before function body.");

    for (size_t index = 0; index < parameterTypes.size(); ++index) {
        const TypeRef& parameterType = parameterTypes[index];
        if (!parameterType || parameterType->kind != TypeKind::CLASS) {
            continue;
        }

        emitBytes(OpCode::GET_LOCAL, static_cast<uint8_t>(index));
        emitCheckInstanceType(parameterType);
        emitByte(OpCode::POP);
    }

    while (m_parser->current.type() != TokenType::CLOSE_CURLY &&
           m_parser->current.type() != TokenType::END_OF_FILE) {
        declaration();
    }
    consume(TokenType::CLOSE_CURLY, "Expected '}' after function body.");

    emitByte(OpCode::NIL);
    emitByte(OpCode::RETURN);

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