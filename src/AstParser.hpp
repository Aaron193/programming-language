#pragma once

#include <deque>
#include <initializer_list>
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
    AstNodeId m_nextNodeId = 1;

    AstNodeInfo makeNodeInfo(const Token& token);

    void advance();
    const Token& peekToken(size_t offset = 1);
    const Token& tokenAt(size_t offset);
    bool check(TokenType type) const;
    bool match(TokenType type);
    bool matchSameLine(TokenType type);
    bool consume(TokenType type);
    bool hasLineBreakBeforeCurrent() const;
    std::string tokenText(const Token& token) const;
    void error();
    void rejectStraySemicolon();
    bool isRecoveryBoundaryToken(TokenType type) const;
    bool recoverLineLeadingContinuation(
        std::initializer_list<TokenType> terminators = {});
    bool rejectUnexpectedTrailingToken(
        std::initializer_list<TokenType> allowedTerminators = {});

    bool parseTypeLookahead(size_t& offset);
    bool isTypedTypeAnnotationStart();

    std::unique_ptr<AstTypeExpr> parseTypeExpr();
    std::vector<AstParameter> parseParameters();

    AstItemPtr parseItem();
    bool parseTypeDeclaration(AstItem& outItem);
    bool parseFunctionDeclaration(AstItem& outItem);
    bool parseClassMember(AstClassDecl& outClassDecl);

    AstStmtPtr parseStatement();
    AstStmtPtr parseBlockStatement();
    AstStmtPtr parsePrintStatement(const Token& printToken);
    AstStmtPtr parseIfStatement(const Token& ifToken);
    AstStmtPtr parseWhileStatement(const Token& whileToken);
    AstStmtPtr parseForStatement(const Token& forToken);
    AstStmtPtr parseReturnStatement(const Token& returnToken);
    AstStmtPtr parseExpressionStatement();
    AstStmtPtr parseVariableDeclarationStatement(bool allowForClause = false);

    AstExprPtr parseExpression();
    AstExprPtr parseAssignment();
    AstExprPtr parseOr();
    AstExprPtr parseAnd();
    AstExprPtr parseEquality();
    AstExprPtr parseComparison();
    AstExprPtr parseBitwiseOr();
    AstExprPtr parseBitwiseXor();
    AstExprPtr parseBitwiseAnd();
    AstExprPtr parseShift();
    AstExprPtr parseTerm();
    AstExprPtr parseFactor();
    AstExprPtr parseUnary();
    AstExprPtr parseCast();
    AstExprPtr parseCall();
    AstExprPtr parseFunctionLiteralExpr();
    AstExprPtr parseImportExpression(const Token& atToken);
    AstExprPtr parsePrimary();
};
