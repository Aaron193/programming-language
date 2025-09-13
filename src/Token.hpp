#pragma once
#include <cstddef>
#include <string>

// Some tokens start with "_" because they are namespace conflicts
enum TokenType {
    BANG,
    BANG_EQUAL,
    PLUS,
    MINUS,
    STAR,
    GREATER,
    GREATER_EQUAL,
    LESS,
    LESS_EQUAL,
    SLASH,
    OPEN_PAREN,
    CLOSE_PAREN,
    OPEN_CURLY,
    CLOSE_CURLY,
    SEMI_COLON,
    COMMA,
    DOT,
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
    VAR,
    _RETURN,

    END_OF_FILE,
    ERROR,
};

class Scanner;

class Token {
   private:
    Token(Scanner* scanner, TokenType type);
    Token(Scanner* scanner, std::string message);

    Scanner* m_scanner;
    TokenType m_type;
    const char* m_start;
    size_t m_length;
    size_t m_line;

   public:
    TokenType type() const { return m_type; }
    const char* start() const { return m_start; }
    size_t length() const { return m_length; }
    size_t line() const { return m_line; }
};