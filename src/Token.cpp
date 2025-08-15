#include "Token.hpp"

#include "Scanner.hpp"

Token::Token(Scanner* scanner, TokenType type)
    : scanner(scanner),
      type(type),
      start(scanner->start),
      line(scanner->line),
      length((int)(scanner->current - scanner->start)) {}

Token::Token(Scanner* scanner, std::string message)
    : scanner(scanner),
      type(TokenType::ERROR),
      start(message.c_str()),
      length(message.length()),
      line(scanner->line) {}
