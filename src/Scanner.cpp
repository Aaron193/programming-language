#include "Scanner.hpp"

Scanner::Scanner(std::string_view source)
    : source(source.data()), start(source.data()), current(source.data()) {}

bool Scanner::isWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
           c == '\r';
}

char Scanner::advance() { return *this->current++; }

bool Scanner::isEOF() { return *current == '\0'; }

void Scanner::skipWhitespace() {
    while (this->isWhitespace(*current)) {
        if (*current == '\n') {
            line++;
        }
        this->advance();
    }
}

Token Scanner::nextToken() {
    this->skipWhitespace();
    this->start = this->current;

    if (this->isEOF()) {
        return this->createToken(TokenType::_EOF);
    }

    const char c = this->advance();

    switch (c) {
        case '!':
            return this->createToken(TokenType::BANG);
        case '+':
            return this->createToken(TokenType::PLUS);
        case '-':
            return this->createToken(TokenType::MINUS);
        case '*':
            return this->createToken(TokenType::STAR);
        case '/':
            return this->createToken(TokenType::SLASH);
        case '>':
            return this->createToken(TokenType::GREATER);
        case '<':
            return this->createToken(TokenType::LESS);
        case '(':
            return this->createToken(TokenType::OPEN_PAREN);
        case ')':
            return this->createToken(TokenType::CLOSE_PAREN);
        case '{':
            return this->createToken(TokenType::OPEN_CURLY);
        case '}':
            return this->createToken(TokenType::CLOSE_CURLY);
        case ';':
            return this->createToken(TokenType::SEMI_COLON);
        case ',':
            return this->createToken(TokenType::COMMA);
        case '.':
            return this->createToken(TokenType::DOT);
    }

    return this->createToken(TokenType::ERROR);
}

Token Scanner::createToken(TokenType type) { return Token(this, type); }

Token Scanner::createErrorToken(std::string message) {
    return Token(this, message);
}
