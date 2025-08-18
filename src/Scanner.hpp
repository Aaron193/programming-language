#pragma once
#include <cstddef>
#include <string_view>

#include "./Token.hpp"

class Scanner {
   private:
    bool isWhitespace(char c);
    char advance();
    char peek();
    char peekNext();
    bool match(char c);
    bool isDigit(char c);
    bool isAlpha(char c);
    bool isEOF();
    void skipWhitespace();
    TokenType getIdentifier();
    bool matchKeyword(const char* keyword, size_t length);
    Token createToken(TokenType type);
    Token createErrorToken(std::string message);

   public:
    Scanner(std::string_view source);

    // pointer to the start of the source code
    const char* source;
    // pointer to current lexeme start
    const char* start;
    // pointer to current character
    const char* current;
    // line number
    size_t line = 1;

    Token nextToken();
};
