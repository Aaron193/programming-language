#pragma once
#include <cstddef>
#include <string>

// Some tokens start with "_" because they are namespace conflicts
enum TokenType {
    BANG,
    BANG_EQUAL,
    PLUS,
    PLUS_PLUS,
    PLUS_EQUAL,
    MINUS,
    MINUS_MINUS,
    MINUS_EQUAL,
    STAR,
    STAR_EQUAL,
    GREATER,
    GREATER_EQUAL,
    LESS,
    LESS_EQUAL,
    SHIFT_LEFT_TOKEN,
    SHIFT_LEFT_EQUAL,
    SHIFT_RIGHT_TOKEN,
    SHIFT_RIGHT_EQUAL,
    SLASH,
    SLASH_EQUAL,
    OPEN_PAREN,
    CLOSE_PAREN,
    OPEN_BRACKET,
    CLOSE_BRACKET,
    OPEN_CURLY,
    CLOSE_CURLY,
    SEMI_COLON,
    COMMA,
    COLON,
    DOT,
    QUESTION,
    EQUAL,
    EQUAL_EQUAL,

    // identifier
    IDENTIFIER,
    STRING,
    NUMBER,

    // reserved
    PRINT,
    FUNCTION,
    AND,
    OR,
    CLASS,
    SUPER,
    FOR,
    WHILE,
    IF,
    ELSE,
    TRUE,
    FALSE,
    _NULL,
    THIS,
    AUTO,
    _RETURN,
    IMPORT,
    EXPORT,
    FROM,
    AS,

    // type-system keywords/operators (phase 1)
    TYPE_I8,
    TYPE_I16,
    TYPE_I32,
    TYPE_I64,
    TYPE_U8,
    TYPE_U16,
    TYPE_U32,
    TYPE_U64,
    TYPE_USIZE,
    TYPE_F32,
    TYPE_F64,
    TYPE_BOOL,
    TYPE_STR,
    TYPE_FN,
    TYPE_VOID,
    TYPE_NULL_KW,
    AS_KW,
    ARROW,

    END_OF_FILE,
    ERROR,
};

class Scanner;

class Token {
    friend class Scanner;

   private:
    Token(Scanner* scanner, TokenType type);
    Token(Scanner* scanner, std::string message);

    Scanner* m_scanner;
    TokenType m_type;
    std::string m_lexeme;
    size_t m_length;
    size_t m_line;

   public:
    Token()
        : m_scanner(nullptr),
          m_type(TokenType::ERROR),
          m_lexeme(""),
          m_length(0),
          m_line(0) {}
    TokenType type() const { return m_type; }
    const char* start() const { return m_lexeme.c_str(); }
    size_t length() const { return m_length; }
    size_t line() const { return m_line; }
};