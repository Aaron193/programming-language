#include "Compiler.hpp"

#include <iostream>

#include "Chunk.hpp"

bool Compiler::compile(std::string_view source, Chunk& chunk) {
    m_scanner = std::make_unique<Scanner>(source);

    advance();
    // m_scanner->expression();
    // m_scanner->consume(TokenType::END_OF_FILE, "Expected end of
    // expression.");

    return !m_parser->hadError;
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