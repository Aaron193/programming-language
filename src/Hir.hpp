#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "Ast.hpp"
#include "AstBinder.hpp"
#include "TypeInfo.hpp"

using HirNodeId = size_t;

struct HirNodeInfo {
    HirNodeId id = 0;
    AstNodeId astNodeId = 0;
    size_t line = 0;
    SourceSpan span;
    TypeRef type = TypeInfo::makeAny();
    bool isConst = false;
};

struct HirParameter {
    HirNodeInfo node;
    Token name;
    TypeRef type = TypeInfo::makeAny();
};

struct HirExpr;
struct HirStmt;
struct HirItem;

using HirExprPtr = std::unique_ptr<HirExpr>;
using HirStmtPtr = std::unique_ptr<HirStmt>;
using HirItemPtr = std::unique_ptr<HirItem>;

struct HirLiteralExpr {
    Token token;
};

struct HirBindingExpr {
    Token name;
    AstBindingRef binding;
};

struct HirUnaryExpr {
    Token op;
    HirExprPtr operand;
};

struct HirUpdateExpr {
    Token op;
    HirExprPtr operand;
    bool isPrefix = false;
};

struct HirBinaryExpr {
    HirExprPtr left;
    Token op;
    HirExprPtr right;
};

struct HirAssignmentExpr {
    HirExprPtr target;
    Token op;
    HirExprPtr value;
};

struct HirCallExpr {
    HirExprPtr callee;
    std::vector<HirExprPtr> arguments;
};

struct HirMemberExpr {
    HirExprPtr object;
    Token member;
};

struct HirIndexExpr {
    HirExprPtr object;
    HirExprPtr index;
};

struct HirCastExpr {
    HirExprPtr expression;
    TypeRef targetType = TypeInfo::makeAny();
};

struct HirFunctionExpr {
    std::vector<HirParameter> params;
    TypeRef returnType = TypeInfo::makeAny();
    HirStmtPtr blockBody;
    HirExprPtr expressionBody;
    bool usesFatArrow = false;
};

struct HirImportExpr {
    Token path;
    AstImportedModuleInterface importedModule;
};

struct HirThisExpr {
    Token token;
    AstBindingRef binding;
};

struct HirSuperExpr {
    Token token;
    AstBindingRef binding;
};

struct HirArrayLiteralExpr {
    std::vector<HirExprPtr> elements;
};

struct HirDictEntry {
    HirExprPtr key;
    HirExprPtr value;
};

struct HirDictLiteralExpr {
    std::vector<HirDictEntry> entries;
};

struct HirExpr {
    HirNodeInfo node;
    using Variant =
        std::variant<HirLiteralExpr, HirBindingExpr, HirUnaryExpr,
                     HirUpdateExpr, HirBinaryExpr, HirAssignmentExpr,
                     HirCallExpr, HirMemberExpr, HirIndexExpr, HirCastExpr,
                     HirFunctionExpr, HirImportExpr, HirThisExpr, HirSuperExpr,
                     HirArrayLiteralExpr, HirDictLiteralExpr>;

    Variant value;
};

struct HirBlockStmt {
    std::vector<HirItemPtr> items;
};

struct HirExprStmt {
    HirExprPtr expression;
};

struct HirPrintStmt {
    HirExprPtr expression;
};

struct HirReturnStmt {
    HirExprPtr value;
};

struct HirIfStmt {
    HirExprPtr condition;
    HirStmtPtr thenBranch;
    HirStmtPtr elseBranch;
};

struct HirWhileStmt {
    HirExprPtr condition;
    HirStmtPtr body;
};

struct HirVarDeclStmt {
    HirNodeInfo node;
    bool isConst = false;
    Token name;
    TypeRef declaredType = TypeInfo::makeAny();
    HirExprPtr initializer;
    bool omittedType = false;
};

struct HirImportBinding {
    HirNodeInfo node;
    Token exportedName;
    std::optional<Token> localName;
    TypeRef expectedType = TypeInfo::makeAny();
    TypeRef bindingType = TypeInfo::makeAny();
};

struct HirDestructuredImportStmt {
    bool isConst = false;
    std::vector<HirImportBinding> bindings;
    HirExprPtr initializer;
};

struct HirForStmt {
    std::variant<std::monostate, std::unique_ptr<HirVarDeclStmt>, HirExprPtr>
        initializer;
    HirExprPtr condition;
    HirExprPtr increment;
    HirStmtPtr body;
};

struct HirForEachStmt {
    bool isConst = false;
    Token name;
    TypeRef declaredType = TypeInfo::makeAny();
    HirExprPtr iterable;
    HirStmtPtr body;
};

struct HirStmt {
    HirNodeInfo node;
    using Variant =
        std::variant<HirBlockStmt, HirExprStmt, HirPrintStmt, HirReturnStmt,
                     HirIfStmt, HirWhileStmt, HirVarDeclStmt,
                     HirDestructuredImportStmt, HirForStmt, HirForEachStmt>;

    Variant value;
};

struct HirFunctionDecl {
    HirNodeInfo node;
    Token name;
    std::vector<HirParameter> params;
    TypeRef returnType = TypeInfo::makeAny();
    HirStmtPtr body;
};

struct HirFieldDecl {
    HirNodeInfo node;
    Token name;
    TypeRef type = TypeInfo::makeAny();
};

struct HirMethodDecl {
    HirNodeInfo node;
    Token name;
    std::vector<HirParameter> params;
    TypeRef returnType = TypeInfo::makeAny();
    HirStmtPtr body;
    std::vector<int> annotatedOperators;
};

struct HirTypeAliasDecl {
    HirNodeInfo node;
    Token name;
    TypeRef aliasedType = TypeInfo::makeAny();
};

struct HirClassDecl {
    HirNodeInfo node;
    Token name;
    std::optional<Token> superclass;
    std::vector<HirFieldDecl> fields;
    std::vector<HirMethodDecl> methods;
};

struct HirItem {
    HirNodeInfo node;
    std::variant<HirTypeAliasDecl, HirClassDecl, HirFunctionDecl, HirStmtPtr>
        value;
};

struct HirModule {
    std::vector<HirItemPtr> items;
};
