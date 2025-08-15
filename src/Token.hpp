#pragma once
#include <cstddef>
#include <string>

enum TokenType {
    BANG,
    PLUS,
    MINUS,
    STAR,
    GREATER,
    LESS,
    SLASH,
    OPEN_PAREN,
    CLOSE_PAREN,
    OPEN_CURLY,
    CLOSE_CURLY,
    SEMI_COLON,
    COMMA,
    DOT,
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