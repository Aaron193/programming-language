#include "Scanner.hpp"

#include <cstring>

Scanner::Scanner(std::string_view source)
    : m_source(source.data()),
      m_start(source.data()),
      m_current(source.data()) {}

char Scanner::advance() {
    const char current = *m_current++;
    ++m_offset;
    if (current == '\n') {
        ++m_line;
        m_column = 1;
    } else {
        ++m_column;
    }
    return current;
}
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
    advance();
    return true;
}

bool Scanner::isDigit(char c) { return c >= '0' && c <= '9'; }
bool Scanner::isAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

Token Scanner::nextToken() {
    skipWhitespace();
    m_start = m_current;
    m_tokenStartLine = m_line;
    m_tokenStartColumn = m_column;
    m_tokenStartOffset = m_offset;

    if (isEOF()) {
        return createToken(TokenType::END_OF_FILE);
    }

    const char c = advance();

    if (isDigit(c)) {
        while (isDigit(peek())) {
            advance();
        }

        bool hasDecimal = false;
        if (peek() == '.' && isDigit(peekNext())) {
            hasDecimal = true;
            advance();

            while (isDigit(peek())) {
                advance();
            }
        }

        auto isSuffixBoundary = [this](char ch) {
            return !isAlpha(ch) && !isDigit(ch) && ch != '_';
        };

        auto tryConsumeSuffix = [this, &isSuffixBoundary](const char* suffix) {
            size_t length = std::strlen(suffix);
            if (std::strncmp(m_current, suffix, length) != 0) {
                return false;
            }

            if (!isSuffixBoundary(*(m_current + length))) {
                return false;
            }

            for (size_t index = 0; index < length; ++index) {
                advance();
            }
            return true;
        };

        if (isAlpha(peek())) {
            bool matchedSuffix = false;
            if (hasDecimal) {
                matchedSuffix =
                    tryConsumeSuffix("f32") || tryConsumeSuffix("f64");
            } else {
                matchedSuffix =
                    tryConsumeSuffix("usize") || tryConsumeSuffix("i16") ||
                    tryConsumeSuffix("i32") || tryConsumeSuffix("i64") ||
                    tryConsumeSuffix("u16") || tryConsumeSuffix("u32") ||
                    tryConsumeSuffix("u64") || tryConsumeSuffix("i8") ||
                    tryConsumeSuffix("u8") || tryConsumeSuffix("f32") ||
                    tryConsumeSuffix("f64") || tryConsumeSuffix("u");
            }

            if (!matchedSuffix) {
                return createErrorToken("Invalid numeric literal suffix.");
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
        case '@':
            return createToken(TokenType::AT);
        case '=':
            if (match('>')) return createToken(TokenType::FAT_ARROW);
            return match('=') ? createToken(TokenType::EQUAL_EQUAL)
                              : createToken(TokenType::EQUAL);
        case '!':
            return match('=') ? createToken(TokenType::BANG_EQUAL)
                              : createToken(TokenType::BANG);
        case '~':
            return createToken(TokenType::TILDE);
        case '&':
            if (match('&')) return createToken(TokenType::LOGICAL_AND);
            return match('=') ? createToken(TokenType::AMPERSAND_EQUAL)
                              : createToken(TokenType::AMPERSAND);
        case '|':
            if (match('|')) return createToken(TokenType::LOGICAL_OR);
            return match('=') ? createToken(TokenType::PIPE_EQUAL)
                              : createToken(TokenType::PIPE);
        case '^':
            return match('=') ? createToken(TokenType::CARET_EQUAL)
                              : createToken(TokenType::CARET);
        case '+':
            if (match('+')) return createToken(TokenType::PLUS_PLUS);
            return match('=') ? createToken(TokenType::PLUS_EQUAL)
                              : createToken(TokenType::PLUS);

        case '-':
            if (match('-')) return createToken(TokenType::MINUS_MINUS);
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

        case '?':
            return createToken(TokenType::QUESTION);

        case '"': {
            while (peek() != '"' && !isEOF()) {
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
            if (matchKeyword("as", 2)) return TokenType::AS_KW;
            break;

        case 'b':
            if (matchKeyword("bool", 4)) return TokenType::TYPE_BOOL;
            if (matchKeyword("break", 5)) return TokenType::BREAK;
            break;

        case 'c':
            if (matchKeyword("const", 5)) return TokenType::CONST;
            if (matchKeyword("continue", 8)) return TokenType::CONTINUE;
            break;

        case 'e':
            if (matchKeyword("else", 4)) return TokenType::ELSE;
            break;

        case 'f':
            if (matchKeyword("f32", 3)) return TokenType::TYPE_F32;
            if (matchKeyword("f64", 3)) return TokenType::TYPE_F64;
            if (matchKeyword("fn", 2)) return TokenType::TYPE_FN;
            if (matchKeyword("false", 5)) return TokenType::FALSE;
            if (matchKeyword("for", 3)) return TokenType::FOR;
            break;

        case 'i':
            if (matchKeyword("i8", 2)) return TokenType::TYPE_I8;
            if (matchKeyword("i16", 3)) return TokenType::TYPE_I16;
            if (matchKeyword("i32", 3)) return TokenType::TYPE_I32;
            if (matchKeyword("i64", 3)) return TokenType::TYPE_I64;
            if (matchKeyword("if", 2)) return TokenType::IF;
            break;

        case 'n':
            if (matchKeyword("null", 4)) return TokenType::TYPE_NULL_KW;
            break;

        case 'p':
            if (matchKeyword("print", 5)) return TokenType::PRINT;
            break;

        case 'r':
            if (matchKeyword("return", 6)) return TokenType::_RETURN;
            break;

        case 's':
            if (matchKeyword("str", 3)) return TokenType::TYPE_STR;
            if (matchKeyword("struct", 6)) return TokenType::STRUCT;
            if (matchKeyword("super", 5)) return TokenType::SUPER;
            break;

        case 't':
            if (matchKeyword("type", 4)) return TokenType::TYPE;
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
            if (matchKeyword("void", 4)) return TokenType::TYPE_VOID;
            break;

        case 'w':
            if (matchKeyword("while", 5)) return TokenType::WHILE;
            break;
    }

    return TokenType::IDENTIFIER;
}
