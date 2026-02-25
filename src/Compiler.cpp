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

    expressionStatement();
}

void Compiler::printStatement() {
    expression();
    if (m_parser->current.type() == TokenType::SEMI_COLON) {
        advance();
    }
    emitByte(OpCode::PRINT_OP);
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

    ParseFn prefixRule = getRule(m_parser->previous.type()).prefix;
    if (!prefixRule) {
        errorAt(m_parser->previous, "Expected expression.");
        return;
    }

    prefixRule();

    while (precedence <= getRule(m_parser->current.type()).precedence) {
        advance();
        ParseFn infixRule = getRule(m_parser->previous.type()).infix;
        infixRule();
    }
}

Compiler::ParseRule Compiler::getRule(TokenType type) {
    switch (type) {
        case TokenType::OPEN_PAREN:
            return ParseRule{[this]() { grouping(); }, nullptr, PREC_NONE};
        case TokenType::NUMBER:
            return ParseRule{[this]() { number(); }, nullptr, PREC_NONE};
        case TokenType::IDENTIFIER:
            return ParseRule{[this]() { variable(); }, nullptr, PREC_NONE};
        case TokenType::STRING:
            return ParseRule{[this]() { stringLiteral(); }, nullptr, PREC_NONE};
        case TokenType::TRUE:
        case TokenType::FALSE:
        case TokenType::_NULL:
            return ParseRule{[this]() { literal(); }, nullptr, PREC_NONE};

        case TokenType::BANG:
            return ParseRule{[this]() { unary(); }, nullptr, PREC_NONE};
        case TokenType::MINUS:
            return ParseRule{[this]() { unary(); }, [this]() { binary(); },
                             PREC_TERM};
        case TokenType::PLUS:
            return ParseRule{nullptr, [this]() { binary(); }, PREC_TERM};

        case TokenType::SLASH:
            return ParseRule{nullptr, [this]() { binary(); }, PREC_FACTOR};
        case TokenType::STAR:
            return ParseRule{nullptr, [this]() { binary(); }, PREC_FACTOR};

        case TokenType::GREATER:
        case TokenType::GREATER_EQUAL:
        case TokenType::LESS:
        case TokenType::LESS_EQUAL:
            return ParseRule{nullptr, [this]() { binary(); }, PREC_COMPARISON};
        case TokenType::EQUAL_EQUAL:
        case TokenType::BANG_EQUAL:
            return ParseRule{nullptr, [this]() { binary(); }, PREC_EQUALITY};

        default:
            return ParseRule{nullptr, nullptr, PREC_NONE};
    }
}

void Compiler::number() {
    std::string literal(m_parser->previous.start(),
                        m_parser->previous.length());
    emitConstant(std::stod(literal));
}

void Compiler::variable() {
    uint8_t arg = identifierConstant(m_parser->previous);
    emitBytes(OpCode::GET_GLOBAL, arg);
}

void Compiler::literal() {
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

void Compiler::stringLiteral() {
    std::string tokenText(m_parser->previous.start(),
                          m_parser->previous.length());
    if (tokenText.length() < 2) {
        errorAt(m_parser->previous, "Invalid string literal.");
        return;
    }

    std::string value = tokenText.substr(1, tokenText.length() - 2);
    emitConstant(Value(value));
}

void Compiler::grouping() {
    expression();
    consume(TokenType::CLOSE_PAREN, "Expected ')' after expression.");
}

void Compiler::unary() {
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

void Compiler::binary() {
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