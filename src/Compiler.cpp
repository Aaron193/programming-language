#include "Compiler.hpp"

#include <iostream>
#include <string>

#include "Chunk.hpp"

bool Compiler::compile(std::string_view source, Chunk& chunk) {
    m_chunk = &chunk;
    m_scanner = std::make_unique<Scanner>(source);
    m_parser = std::make_unique<Parser>();

    advance();

    while (m_parser->current.type() != TokenType::END_OF_FILE) {
        declaration();
    }

    consume(TokenType::END_OF_FILE, "Expected end of source.");
    emitReturn();

    return !m_parser->hadError;
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

uint8_t Compiler::identifierConstant(const Token& name) {
    return makeConstant(Value(std::string(name.start(), name.length())));
}

uint8_t Compiler::parseVariable(const std::string& message) {
    consume(TokenType::IDENTIFIER, message);
    return identifierConstant(m_parser->previous);
}

void Compiler::defineVariable(uint8_t global) {
    emitBytes(OpCode::DEFINE_GLOBAL, global);
}

void Compiler::advance() {
    m_parser->previous = m_parser->current;

    while (true) {
        m_parser->current = m_scanner->nextToken();

        if (m_parser->current.type() != TokenType::ERROR) break;

        errorAtCurrent(m_parser->current.start());
    }
}

void Compiler::errorAtCurrent(const std::string& message) {
    errorAt(m_parser->current, message);
}

void Compiler::errorAt(const Token& token, const std::string& message) {
    // Suppress snowballing errors
    if (m_parser->panicMode) return;
    m_parser->panicMode = true;

    std::cerr << "[line " << token.line() << "] Error";

    if (token.type() == TokenType::END_OF_FILE) {
        std::cerr << " at end";
    } else if (token.type() == TokenType::ERROR) {
        // Nothing
    } else {
        std::cerr << " at '" << std::string(token.start(), token.length())
                  << "'";
    }

    std::cerr << ": " << message << std::endl;
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
    if (m_parser->current.type() == TokenType::FUNCTION) {
        advance();
        functionDeclaration();
        return;
    }

    if (m_parser->current.type() == TokenType::VAR) {
        advance();
        varDeclaration();
        return;
    }

    statement();
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
    uint8_t global = identifierConstant(nameToken);

    std::shared_ptr<FunctionObject> function =
        compileFunction(std::string(nameToken.start(), nameToken.length()));
    emitConstant(Value(function));
    defineVariable(global);
}

void Compiler::block() {
    while (m_parser->current.type() != TokenType::CLOSE_CURLY &&
           m_parser->current.type() != TokenType::END_OF_FILE) {
        declaration();
    }

    consume(TokenType::CLOSE_CURLY, "Expected '}' after block.");
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
    consume(TokenType::OPEN_PAREN, "Expected '(' after 'for'.");

    if (m_parser->current.type() == TokenType::SEMI_COLON) {
        advance();
    } else if (m_parser->current.type() == TokenType::VAR) {
        advance();
        varDeclaration();
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
}

void Compiler::printStatement() {
    expression();
    if (m_parser->current.type() == TokenType::SEMI_COLON) {
        advance();
    }
    emitByte(OpCode::PRINT_OP);
}

void Compiler::returnStatement() {
    if (!m_inFunction) {
        errorAtCurrent("Cannot return from top-level code.");
    }

    if (m_parser->current.type() == TokenType::SEMI_COLON) {
        advance();
        emitByte(OpCode::NIL);
        emitByte(OpCode::RETURN);
        return;
    }

    expression();
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
    uint8_t global = parseVariable("Expected variable name.");

    if (m_parser->current.type() == TokenType::EQUAL) {
        advance();
        expression();
    } else {
        emitByte(OpCode::NIL);
    }

    consume(TokenType::SEMI_COLON, "Expected ';' after variable declaration.");
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

    if (canAssign && m_parser->current.type() == TokenType::EQUAL) {
        errorAtCurrent("Invalid assignment target.");
        advance();
        expression();
    }
}

Compiler::ParseRule Compiler::getRule(TokenType type) {
    switch (type) {
        case TokenType::OPEN_PAREN:
            return ParseRule{[this](bool canAssign) { grouping(canAssign); },
                             [this](bool canAssign) { call(canAssign); },
                             PREC_CALL};
        case TokenType::NUMBER:
            return ParseRule{[this](bool canAssign) { number(canAssign); },
                             nullptr, PREC_NONE};
        case TokenType::IDENTIFIER:
            return ParseRule{[this](bool canAssign) { variable(canAssign); },
                             nullptr, PREC_NONE};
        case TokenType::STRING:
            return ParseRule{
                [this](bool canAssign) { stringLiteral(canAssign); }, nullptr,
                PREC_NONE};
        case TokenType::TRUE:
        case TokenType::FALSE:
        case TokenType::_NULL:
            return ParseRule{[this](bool canAssign) { literal(canAssign); },
                             nullptr, PREC_NONE};

        case TokenType::BANG:
            return ParseRule{[this](bool canAssign) { unary(canAssign); },
                             nullptr, PREC_NONE};
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
    uint8_t arg = identifierConstant(m_parser->previous);

    if (canAssign && m_parser->current.type() == TokenType::EQUAL) {
        advance();
        expression();
        emitBytes(OpCode::SET_GLOBAL, arg);
        return;
    }

    emitBytes(OpCode::GET_GLOBAL, arg);
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

std::shared_ptr<FunctionObject> Compiler::compileFunction(
    const std::string& name) {
    consume(TokenType::OPEN_PAREN, "Expected '(' after function name.");

    std::vector<std::string> parameters;
    if (m_parser->current.type() != TokenType::CLOSE_PAREN) {
        do {
            consume(TokenType::IDENTIFIER, "Expected parameter name.");
            parameters.emplace_back(m_parser->previous.start(),
                                    m_parser->previous.length());

            if (m_parser->current.type() != TokenType::COMMA) {
                break;
            }
            advance();
        } while (true);
    }

    consume(TokenType::CLOSE_PAREN, "Expected ')' after parameters.");
    consume(TokenType::OPEN_CURLY, "Expected '{' before function body.");

    Chunk* enclosingChunk = m_chunk;
    bool enclosingInFunction = m_inFunction;
    auto functionChunk = std::make_shared<Chunk>();
    m_chunk = functionChunk.get();
    m_inFunction = true;

    while (m_parser->current.type() != TokenType::CLOSE_CURLY &&
           m_parser->current.type() != TokenType::END_OF_FILE) {
        declaration();
    }
    consume(TokenType::CLOSE_CURLY, "Expected '}' after function body.");

    emitByte(OpCode::NIL);
    emitByte(OpCode::RETURN);

    m_chunk = enclosingChunk;
    m_inFunction = enclosingInFunction;

    auto function = std::make_shared<FunctionObject>();
    function->name = name;
    function->parameters = parameters;
    function->chunk = functionChunk;
    return function;
}