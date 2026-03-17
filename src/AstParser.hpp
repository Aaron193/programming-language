#pragma once

#include <deque>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Ast.hpp"
#include "Scanner.hpp"

class AstParser {
   public:
    struct ParseError {
        size_t line = 0;
        size_t column = 1;
        SourceSpan span = makePointSpan(1, 1);
        std::string message;

        ParseError() = default;
        ParseError(size_t lineValue, std::string messageValue)
            : line(lineValue == 0 ? 1 : lineValue),
              column(1),
              span(makePointSpan(lineValue == 0 ? 1 : lineValue, 1)),
              message(std::move(messageValue)) {}
        ParseError(SourceSpan spanValue, std::string messageValue)
            : line(spanValue.line()),
              column(spanValue.column()),
              span(std::move(spanValue)),
              message(std::move(messageValue)) {}
    };

    explicit AstParser(std::string_view source);

    bool parseModule(AstModule& outModule);
    const std::vector<ParseError>& errors() const { return m_errors; }

   private:
    Scanner m_scanner;
    Token m_current;
    Token m_previous;
    std::deque<Token> m_bufferedTokens;
    bool m_hadError = false;
    AstNodeId m_nextNodeId = 1;
    std::vector<ParseError> m_errors;

    AstNodeInfo makeNodeInfo(const Token& token);
    AstNodeInfo makeNodeInfo(const SourceSpan& span);

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
    void errorAtLine(size_t line, const std::string& message);
    void errorAtSpan(const SourceSpan& span, const std::string& message);
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
