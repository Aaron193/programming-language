#include "Token.hpp"

#include "Scanner.hpp"

Token::Token(Scanner* scanner, TokenType type)
    : m_scanner(scanner),
      m_type(type),
      m_lexeme(scanner->m_start, (int)(scanner->m_current - scanner->m_start)),
      m_line(scanner->m_tokenStartLine),
      m_column(scanner->m_tokenStartColumn),
      m_span(SourceSpan{
          makeSourcePosition(scanner->m_tokenStartOffset,
                             scanner->m_tokenStartLine,
                             scanner->m_tokenStartColumn),
          makeSourcePosition(scanner->m_offset, scanner->m_line,
                             scanner->m_column)}),
      m_length((int)(scanner->m_current - scanner->m_start)) {}

Token::Token(Scanner* scanner, std::string message)
    : m_scanner(scanner),
      m_type(TokenType::ERROR),
      m_lexeme(std::move(message)),
      m_length(m_lexeme.length()),
      m_line(scanner->m_tokenStartLine),
      m_column(scanner->m_tokenStartColumn),
      m_span(SourceSpan{
          makeSourcePosition(scanner->m_tokenStartOffset,
                             scanner->m_tokenStartLine,
                             scanner->m_tokenStartColumn),
          makeSourcePosition(scanner->m_offset, scanner->m_line,
                             scanner->m_column)}) {}

Token Token::synthetic(TokenType type, std::string lexeme, const SourceSpan& span) {
    Token token;
    token.m_type = type;
    token.m_lexeme = std::move(lexeme);
    token.m_length = token.m_lexeme.length();
    token.m_line = span.line();
    token.m_column = span.column();
    token.m_span = span;
    return token;
}
