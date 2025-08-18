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

    _EOF,
    ERROR,
};

class Scanner;

class Token {
   public:
    Token(Scanner* scanner, TokenType type);
    Token(Scanner* scanner, std::string message);

    Scanner* scanner;
    TokenType type;
    const char* start;
    size_t length;
    size_t line;
};