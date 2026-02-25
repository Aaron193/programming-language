#include "Scanner.hpp"

#include <cstring>

Scanner::Scanner(std::string_view source)
    : m_source(source.data()),
      m_start(source.data()),
      m_current(source.data()) {}

char Scanner::advance() { return *m_current++; }
char Scanner::peek() { return *m_current; }
char Scanner::peekNext() {
    if (isEOF()) return '\0';
    return *(m_current + 1);
}

bool Scanner::isEOF() { return *m_current == '\0'; }

void Scanner::skipWhitespace() {
    while (true) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r') {
            advance();
        } else if (c == '\n') {
            m_line++;
            advance();
        } else if (c == '/' && peekNext() == '/') {
            while (peek() != '\n' && !isEOF()) {
                advance();
            }
        } else {
            break;
        }
    }
}

bool Scanner::match(char c) {
    if (isEOF()) return false;
    if (peek() != c) return false;
    m_current++;
    return true;
}

bool Scanner::isDigit(char c) { return c >= '0' && c <= '9'; }
bool Scanner::isAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

Token Scanner::nextToken() {
    skipWhitespace();
    m_start = m_current;

    if (isEOF()) {
        return createToken(TokenType::END_OF_FILE);
    }

    const char c = advance();

    if (isDigit(c)) {
        while (isdigit(peek())) {
            advance();
        }

        if (peek() == '.' && isdigit(peekNext())) {
            advance();

            while (isdigit(peek())) {
                advance();
            }
        }

        return createToken(TokenType::NUMBER);
    }

    if (isAlpha(c)) {
        while (isAlpha(peek()) || isDigit(peek())) {
            advance();
        }

        return createToken(getIdentifier());
    }

    switch (c) {
        case '=':
            return match('=') ? createToken(TokenType::EQUAL_EQUAL)
                              : createToken(TokenType::EQUAL);
        case '!':
            return match('=') ? createToken(TokenType::BANG_EQUAL)
                              : createToken(TokenType::BANG);
        case '+':
            if (match('+')) return createToken(TokenType::PLUS_PLUS);
            return match('=') ? createToken(TokenType::PLUS_EQUAL)
                              : createToken(TokenType::PLUS);

        case '-':
            if (match('-')) return createToken(TokenType::MINUS_MINUS);
            if (match('>')) return createToken(TokenType::ARROW);
            return match('=') ? createToken(TokenType::MINUS_EQUAL)
                              : createToken(TokenType::MINUS);

        case '*':
            return match('=') ? createToken(TokenType::STAR_EQUAL)
                              : createToken(TokenType::STAR);

        case '/':
            return match('=') ? createToken(TokenType::SLASH_EQUAL)
                              : createToken(TokenType::SLASH);

        case '>':
            if (match('>')) {
                return match('=') ? createToken(TokenType::SHIFT_RIGHT_EQUAL)
                                  : createToken(TokenType::SHIFT_RIGHT_TOKEN);
            }
            return match('=') ? createToken(TokenType::GREATER_EQUAL)
                              : createToken(TokenType::GREATER);

        case '<':
            if (match('<')) {
                return match('=') ? createToken(TokenType::SHIFT_LEFT_EQUAL)
                                  : createToken(TokenType::SHIFT_LEFT_TOKEN);
            }
            return match('=') ? createToken(TokenType::LESS_EQUAL)
                              : createToken(TokenType::LESS);

        case '(':
            return createToken(TokenType::OPEN_PAREN);

        case ')':
            return createToken(TokenType::CLOSE_PAREN);

        case '[':
            return createToken(TokenType::OPEN_BRACKET);

        case ']':
            return createToken(TokenType::CLOSE_BRACKET);

        case '{':
            return createToken(TokenType::OPEN_CURLY);

        case '}':
            return createToken(TokenType::CLOSE_CURLY);

        case ';':
            return createToken(TokenType::SEMI_COLON);

        case ',':
            return createToken(TokenType::COMMA);

        case ':':
            return createToken(TokenType::COLON);

        case '.':
            return createToken(TokenType::DOT);

        case '"': {
            while (peek() != '"' && !isEOF()) {
                if (peek() == '\n') {
                    m_line++;
                }

                advance();
            }

            if (isEOF()) {
                return createErrorToken("Unterminated string.");
            }

            advance();
            return createToken(TokenType::STRING);
        }
    }

    return createErrorToken("Unexpected Token.");
}

Token Scanner::createToken(TokenType type) { return Token(this, type); }

Token Scanner::createErrorToken(std::string message) {
    return Token(this, message);
}

bool Scanner::matchKeyword(const char* keyword, size_t length) {
    if ((m_current - m_start) != length) return false;
    return std::strncmp(m_start, keyword, length) == 0;
}

TokenType Scanner::getIdentifier() {
    switch (m_start[0]) {
        case 'a':
            if (matchKeyword("and", 3)) return TokenType::AND;
            if (matchKeyword("as", 2)) return TokenType::AS_KW;
            break;

        case 'b':
            if (matchKeyword("bool", 4)) return TokenType::TYPE_BOOL;
            break;

        case 'c':
            if (matchKeyword("class", 5)) return TokenType::CLASS;
            break;

        case 'e':
            if (matchKeyword("else", 4)) return TokenType::ELSE;
            if (matchKeyword("export", 6)) return TokenType::EXPORT;
            break;

        case 'f':
            if (matchKeyword("f32", 3)) return TokenType::TYPE_F32;
            if (matchKeyword("f64", 3)) return TokenType::TYPE_F64;
            if (matchKeyword("false", 5)) return TokenType::FALSE;
            if (matchKeyword("for", 3)) return TokenType::FOR;
            if (matchKeyword("from", 4)) return TokenType::FROM;
            if (matchKeyword("function", 8)) return TokenType::FUNCTION;
            break;

        case 'i':
            if (matchKeyword("i8", 2)) return TokenType::TYPE_I8;
            if (matchKeyword("i16", 3)) return TokenType::TYPE_I16;
            if (matchKeyword("i32", 3)) return TokenType::TYPE_I32;
            if (matchKeyword("i64", 3)) return TokenType::TYPE_I64;
            if (matchKeyword("if", 2)) return TokenType::IF;
            if (matchKeyword("import", 6)) return TokenType::IMPORT;
            break;

        case 'n':
            if (matchKeyword("null", 4)) return TokenType::TYPE_NULL_KW;
            break;

        case 'o':
            if (matchKeyword("or", 2)) return TokenType::OR;
            break;

        case 'p':
            if (matchKeyword("print", 5)) return TokenType::PRINT;
            break;

        case 'r':
            if (matchKeyword("return", 6)) return TokenType::_RETURN;
            break;

        case 's':
            if (matchKeyword("str", 3)) return TokenType::TYPE_STR;
            if (matchKeyword("super", 5)) return TokenType::SUPER;
            break;

        case 't':
            if (matchKeyword("true", 4)) return TokenType::TRUE;
            if (matchKeyword("this", 4)) return TokenType::THIS;
            break;

        case 'u':
            if (matchKeyword("u8", 2)) return TokenType::TYPE_U8;
            if (matchKeyword("u16", 3)) return TokenType::TYPE_U16;
            if (matchKeyword("u32", 3)) return TokenType::TYPE_U32;
            if (matchKeyword("u64", 3)) return TokenType::TYPE_U64;
            if (matchKeyword("usize", 5)) return TokenType::TYPE_USIZE;
            break;

        case 'v':
            if (matchKeyword("var", 3)) return TokenType::VAR;
            break;

        case 'w':
            if (matchKeyword("while", 5)) return TokenType::WHILE;
            break;
    }

    return TokenType::IDENTIFIER;
}