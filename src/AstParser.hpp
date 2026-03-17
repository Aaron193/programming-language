#pragma once

#include <deque>
#include <memory>
#include <string_view>

#include "Ast.hpp"
#include "Scanner.hpp"

class AstParser {
   public:
    explicit AstParser(std::string_view source);

    bool parseModule(AstModule& outModule);

   private:
    Scanner m_scanner;
    Token m_current;
    Token m_previous;
    std::deque<Token> m_bufferedTokens;
    bool m_hadError = false;

    void advance();
    const Token& peekToken(size_t offset = 1);
    const Token& tokenAt(size_t offset);
    bool check(TokenType type) const;
    bool match(TokenType type);
    bool consume(TokenType type);
    bool hasLineBreakBeforeCurrent() const;
    std::string tokenText(const Token& token) const;
    void error();

    std::unique_ptr<AstTypeExpr> parseTypeExpr();
    std::vector<AstParameter> parseParameters();
    bool parseTopLevelTypeDeclaration(AstModule& outModule);
    bool parseTopLevelFunctionDeclaration(AstModule& outModule);
    bool parseClassMember(AstClassDecl& outClassDecl);

    void skipBlock();
    void skipParenthesized();
    void skipStatement();
    void skipExpression(TokenType primaryTerminator = TokenType::END_OF_FILE);
    void skipVariableDeclaration();
};
