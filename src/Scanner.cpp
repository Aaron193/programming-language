#include "Scanner.hpp"

#include <cstring>

Scanner::Scanner(std::string_view source)
    : source(source.data()), start(source.data()), current(source.data()) {}

char Scanner::advance() { return *this->current++; }
char Scanner::peek() { return *this->current; }
char Scanner::peekNext() {
    if (this->isEOF()) return '\0';
    return *(this->current + 1);
}

bool Scanner::isEOF() { return *this->current == '\0'; }

void Scanner::skipWhitespace() {
    while (true) {
        switch (this->peek()) {
            case ' ':
            case '\t':
            case '\v':
            case '\f':
            case '\r':
                this->advance();
                break;
            case '\n':
                this->line++;
                this->advance();
                break;
            case '/':
                if (this->peekNext() == '/') {
                    while (this->peek() != '\n' && !this->isEOF()) {
                        this->advance();
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

bool Scanner::match(char c) {
    if (this->isEOF()) return false;
    if (this->peek() != c) return false;
    this->current++;
    return true;
}

bool Scanner::isDigit(char c) { return c >= '0' && c <= '9'; }
bool Scanner::isAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

Token Scanner::nextToken() {
    this->skipWhitespace();
    this->start = this->current;

    if (this->isEOF()) {
        return this->createToken(TokenType::_EOF);
    }

    const char c = this->advance();

    if (this->isDigit(c)) {
        while (isdigit(this->peek())) this->advance();

        if (this->peek() == '.' && isdigit(this->peekNext())) {
            this->advance();

            while (isdigit(this->peek())) this->advance();
        }

        return this->createToken(TokenType::NUMBER);
    }

    if (this->isAlpha(c)) {
        while (this->isAlpha(this->peek()) || this->isDigit(this->peek())) {
            this->advance();
        }

        return this->createToken(this->getIdentifier());
    }

    switch (c) {
        case '=':
            return this->match('=') ? this->createToken(TokenType::EQUAL_EQUAL)
                                    : this->createToken(TokenType::EQUAL);
        case '!':
            return this->match('=') ? this->createToken(TokenType::BANG_EQUAL)
                                    : this->createToken(TokenType::BANG);
        case '+':
            return this->createToken(TokenType::PLUS);

        case '-':
            return this->createToken(TokenType::MINUS);

        case '*':
            return this->createToken(TokenType::STAR);

        case '/':
            return this->createToken(TokenType::SLASH);

        case '>':
            return this->match('=')
                       ? this->createToken(TokenType::GREATER_EQUAL)
                       : this->createToken(TokenType::GREATER);

        case '<':
            return this->match('=') ? this->createToken(TokenType::LESS_EQUAL)
                                    : this->createToken(TokenType::LESS);

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

        case '"': {
            while (this->peek() != '"' && !this->isEOF()) {
                if (this->peek() == '\n') {
                    this->line++;
                }

                this->advance();
            }
            if (this->isEOF()) {
                return this->createErrorToken("Unterminated string.");
            }
            this->advance();
            return this->createToken(TokenType::STRING);
        }
    }

    return this->createErrorToken("Unexpected Token.");
}

Token Scanner::createToken(TokenType type) { return Token(this, type); }

Token Scanner::createErrorToken(std::string message) {
    return Token(this, message);
}

bool Scanner::matchKeyword(const char* keyword, size_t length) {
    if ((this->current - this->start) != length) return false;
    return std::strncmp(this->start, keyword, length) == 0;
}

TokenType Scanner::getIdentifier() {
    const size_t length = this->current - this->start;

    switch (this->start[0]) {
        case 'a':
            if (this->matchKeyword("and", 3)) return TokenType::AND;
            break;

        case 'c':
            if (this->matchKeyword("class", 5)) return TokenType::CLASS;
            break;

        case 'e':
            if (this->matchKeyword("else", 4)) return TokenType::ELSE;
            break;

        case 'f':
            if (this->matchKeyword("false", 5)) return TokenType::FALSE;
            if (this->matchKeyword("for", 3)) return TokenType::FOR;
            if (this->matchKeyword("function", 8)) return TokenType::FUNCTION;
            break;

        case 'i':
            if (this->matchKeyword("if", 2)) return TokenType::IF;
            break;

        case 'n':
            if (this->matchKeyword("null", 4)) return TokenType::_NULL;
            break;

        case 'o':
            if (this->matchKeyword("or", 2)) return TokenType::OR;
            break;

        case 'r':
            if (this->matchKeyword("return", 6)) return TokenType::_RETURN;
            break;

        case 's':
            if (this->matchKeyword("super", 5)) return TokenType::SUPER;
            break;

        case 't':
            if (this->matchKeyword("true", 4)) return TokenType::TRUE;
            if (this->matchKeyword("this", 4)) return TokenType::THIS;
            break;

        case 'v':
            if (this->matchKeyword("var", 3)) return TokenType::VAR;
            break;

        case 'w':
            if (this->matchKeyword("while", 5)) return TokenType::WHILE;
            break;
    }

    return TokenType::IDENTIFIER;
}