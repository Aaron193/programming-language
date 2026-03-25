#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "Token.hpp"

using AstNodeId = size_t;

struct AstNodeInfo {
    AstNodeId id = 0;
    size_t line = 0;
    SourceSpan span;

    size_t column() const { return span.column(); }
};

enum class AstTypeKind {
    NAMED,
    FUNCTION,
    ARRAY,
    DICT,
    SET,
    OPTIONAL,
    NATIVE_HANDLE,
};

struct AstTypeExpr {
    AstNodeInfo node;
    AstTypeKind kind = AstTypeKind::NAMED;
    Token token;
    std::vector<std::unique_ptr<AstTypeExpr>> paramTypes;
    std::unique_ptr<AstTypeExpr> returnType;
    std::unique_ptr<AstTypeExpr> elementType;
    std::unique_ptr<AstTypeExpr> keyType;
    std::unique_ptr<AstTypeExpr> valueType;
    std::unique_ptr<AstTypeExpr> innerType;
    std::string packageNamespace;
    std::string packageName;
    std::string nativeHandleTypeName;
};

struct AstParameter {
    AstNodeInfo node;
    Token name;
    std::unique_ptr<AstTypeExpr> type;
};

struct AstExpr;
struct AstStmt;
struct AstItem;

using AstExprPtr = std::unique_ptr<AstExpr>;
using AstStmtPtr = std::unique_ptr<AstStmt>;
using AstItemPtr = std::unique_ptr<AstItem>;

struct AstLiteralExpr {
    Token token;
};

struct AstIdentifierExpr {
    Token name;
};

struct AstGroupingExpr {
    AstExprPtr expression;
};

struct AstUnaryExpr {
    Token op;
    AstExprPtr operand;
};

struct AstUpdateExpr {
    Token op;
    AstExprPtr operand;
    bool isPrefix = false;
};

struct AstBinaryExpr {
    AstExprPtr left;
    Token op;
    AstExprPtr right;
};

struct AstAssignmentExpr {
    AstExprPtr target;
    Token op;
    AstExprPtr value;
};

struct AstCallExpr {
    AstExprPtr callee;
    std::vector<AstExprPtr> arguments;
};

struct AstMemberExpr {
    AstExprPtr object;
    Token member;
};

struct AstIndexExpr {
    AstExprPtr object;
    AstExprPtr index;
};

struct AstCastExpr {
    AstExprPtr expression;
    std::unique_ptr<AstTypeExpr> targetType;
};

struct AstFunctionExpr {
    std::vector<AstParameter> params;
    std::unique_ptr<AstTypeExpr> returnType;
    AstStmtPtr blockBody;
    AstExprPtr expressionBody;
    bool usesFatArrow = false;
};

struct AstImportExpr {
    Token path;
};

struct AstThisExpr {
    Token token;
};

struct AstSuperExpr {
    Token token;
};

struct AstArrayLiteralExpr {
    std::vector<AstExprPtr> elements;
};

struct AstDictEntry {
    AstExprPtr key;
    AstExprPtr value;
};

struct AstDictLiteralExpr {
    std::vector<AstDictEntry> entries;
};

struct AstExpr {
    AstNodeInfo node;
    using Variant =
        std::variant<AstLiteralExpr, AstIdentifierExpr, AstGroupingExpr,
                     AstUnaryExpr, AstUpdateExpr, AstBinaryExpr,
                     AstAssignmentExpr, AstCallExpr, AstMemberExpr,
                     AstIndexExpr, AstCastExpr, AstFunctionExpr, AstImportExpr,
                     AstThisExpr, AstSuperExpr, AstArrayLiteralExpr,
                     AstDictLiteralExpr>;

    Variant value;
};

struct AstBlockStmt {
    std::vector<AstItemPtr> items;
};

struct AstExprStmt {
    AstExprPtr expression;
};

struct AstPrintStmt {
    Token keyword;
    AstExprPtr expression;
};

struct AstReturnStmt {
    AstExprPtr value;
};

struct AstBreakStmt {
    std::optional<Token> label;
};

struct AstContinueStmt {
    std::optional<Token> label;
};

struct AstIfStmt {
    AstExprPtr condition;
    AstStmtPtr thenBranch;
    AstStmtPtr elseBranch;
};

struct AstWhileStmt {
    std::optional<Token> label;
    AstExprPtr condition;
    AstStmtPtr body;
};

struct AstVarDeclStmt {
    AstNodeInfo node;
    bool isConst = false;
    Token name;
    std::unique_ptr<AstTypeExpr> declaredType;
    AstExprPtr initializer;
    bool omittedType = false;
};

struct AstImportBinding {
    AstNodeInfo node;
    Token exportedName;
    std::optional<Token> localName;
    std::unique_ptr<AstTypeExpr> expectedType;
};

struct AstDestructuredImportStmt {
    bool isConst = false;
    std::vector<AstImportBinding> bindings;
    AstExprPtr initializer;
};

struct AstForStmt {
    std::optional<Token> label;
    std::variant<std::monostate, std::unique_ptr<AstVarDeclStmt>, AstExprPtr>
        initializer;
    AstExprPtr condition;
    AstExprPtr increment;
    AstStmtPtr body;
};

struct AstForEachStmt {
    std::optional<Token> label;
    bool isConst = false;
    Token name;
    std::unique_ptr<AstTypeExpr> declaredType;
    AstExprPtr iterable;
    AstStmtPtr body;
};

struct AstStmt {
    AstNodeInfo node;
    using Variant =
        std::variant<AstBlockStmt, AstExprStmt, AstPrintStmt, AstReturnStmt,
                     AstBreakStmt, AstContinueStmt, AstIfStmt, AstWhileStmt, AstVarDeclStmt,
                     AstDestructuredImportStmt, AstForStmt, AstForEachStmt>;

    Variant value;
};

struct AstFunctionDecl {
    AstNodeInfo node;
    Token name;
    std::vector<AstParameter> params;
    std::unique_ptr<AstTypeExpr> returnType;
    AstStmtPtr body;
};

struct AstFieldDecl {
    AstNodeInfo node;
    Token name;
    std::unique_ptr<AstTypeExpr> type;
};

struct AstMethodDecl {
    AstNodeInfo node;
    Token name;
    std::vector<AstParameter> params;
    std::unique_ptr<AstTypeExpr> returnType;
    AstStmtPtr body;
    std::vector<int> annotatedOperators;
};

struct AstTypeAliasDecl {
    AstNodeInfo node;
    Token name;
    std::unique_ptr<AstTypeExpr> aliasedType;
};

struct AstClassDecl {
    AstNodeInfo node;
    Token name;
    std::optional<Token> superclass;
    std::vector<AstFieldDecl> fields;
    std::vector<AstMethodDecl> methods;
};

using AstDeclaration =
    std::variant<AstTypeAliasDecl, AstClassDecl, AstFunctionDecl>;

struct AstItem {
    AstNodeInfo node;
    std::variant<AstTypeAliasDecl, AstClassDecl, AstFunctionDecl, AstStmtPtr>
        value;
};

struct AstModule {
    std::vector<AstItemPtr> items;
};
