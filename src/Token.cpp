#include "Token.hpp"

#include "Scanner.hpp"

Token::Token(Scanner* scanner, TokenType type)
    : m_scanner(scanner),
      m_type(type),
      m_lexeme(scanner->m_start, (int)(scanner->m_current - scanner->m_start)),
      m_line(scanner->m_line),
      m_length((int)(scanner->m_current - scanner->m_start)) {}

Token::Token(Scanner* scanner, std::string message)
    : m_scanner(scanner),
      m_type(TokenType::ERROR),
      m_lexeme(std::move(message)),
      m_length(m_lexeme.length()),
      m_line(scanner->m_line) {}
