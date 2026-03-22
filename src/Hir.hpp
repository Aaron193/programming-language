#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "Ast.hpp"
#include "AstBinder.hpp"
#include "TypeInfo.hpp"

using HirNodeId = size_t;
using HirExprId = size_t;
using HirStmtId = size_t;
using HirItemId = size_t;

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

struct HirLiteralExpr {
    Token token;
};

struct HirBindingExpr {
    Token name;
    AstBindingRef binding;
};

struct HirUnaryExpr {
    Token op;
    std::optional<HirExprId> operand;
};

struct HirUpdateExpr {
    Token op;
    std::optional<HirExprId> operand;
    bool isPrefix = false;
};

struct HirBinaryExpr {
    std::optional<HirExprId> left;
    Token op;
    std::optional<HirExprId> right;
};

struct HirAssignmentExpr {
    std::optional<HirExprId> target;
    Token op;
    std::optional<HirExprId> value;
};

struct HirCallExpr {
    std::optional<HirExprId> callee;
    std::vector<HirExprId> arguments;
};

struct HirMemberExpr {
    std::optional<HirExprId> object;
    Token member;
};

struct HirIndexExpr {
    std::optional<HirExprId> object;
    std::optional<HirExprId> index;
};

struct HirCastExpr {
    std::optional<HirExprId> expression;
    TypeRef targetType = TypeInfo::makeAny();
};

struct HirFunctionExpr {
    std::vector<HirParameter> params;
    TypeRef returnType = TypeInfo::makeAny();
    std::optional<HirStmtId> blockBody;
    std::optional<HirExprId> expressionBody;
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
    std::vector<HirExprId> elements;
};

struct HirDictEntry {
    std::optional<HirExprId> key;
    std::optional<HirExprId> value;
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
    std::vector<HirItemId> items;
};

struct HirExprStmt {
    std::optional<HirExprId> expression;
};

struct HirPrintStmt {
    std::optional<HirExprId> expression;
};

struct HirReturnStmt {
    std::optional<HirExprId> value;
};

struct HirIfStmt {
    std::optional<HirExprId> condition;
    std::optional<HirStmtId> thenBranch;
    std::optional<HirStmtId> elseBranch;
};

struct HirWhileStmt {
    std::optional<HirExprId> condition;
    std::optional<HirStmtId> body;
};

struct HirVarDeclStmt {
    HirNodeInfo node;
    bool isConst = false;
    Token name;
    TypeRef declaredType = TypeInfo::makeAny();
    std::optional<HirExprId> initializer;
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
    std::optional<HirExprId> initializer;
};

struct HirForStmt {
    std::optional<HirStmtId> initializer;
    std::optional<HirExprId> condition;
    std::optional<HirExprId> increment;
    std::optional<HirStmtId> body;
};

struct HirForEachStmt {
    bool isConst = false;
    Token name;
    TypeRef declaredType = TypeInfo::makeAny();
    std::optional<HirExprId> iterable;
    std::optional<HirStmtId> body;
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
    std::optional<HirStmtId> body;
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
    std::optional<HirStmtId> body;
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
    std::variant<HirTypeAliasDecl, HirClassDecl, HirFunctionDecl, HirStmtId>
        value;
};

struct HirModule {
    std::vector<HirItemId> items;
    std::vector<HirExpr> exprArena;
    std::vector<HirStmt> stmtArena;
    std::vector<HirItem> itemArena;

    void clear() {
        items.clear();
        exprArena.clear();
        stmtArena.clear();
        itemArena.clear();
    }

    HirExprId addExpr(HirExpr expr) {
        exprArena.push_back(std::move(expr));
        return exprArena.size();
    }

    HirStmtId addStmt(HirStmt stmt) {
        stmtArena.push_back(std::move(stmt));
        return stmtArena.size();
    }

    HirItemId addItem(HirItem item) {
        itemArena.push_back(std::move(item));
        return itemArena.size();
    }

    HirExpr& expr(HirExprId id) { return exprArena.at(id - 1); }
    const HirExpr& expr(HirExprId id) const { return exprArena.at(id - 1); }

    HirStmt& stmt(HirStmtId id) { return stmtArena.at(id - 1); }
    const HirStmt& stmt(HirStmtId id) const { return stmtArena.at(id - 1); }

    HirItem& item(HirItemId id) { return itemArena.at(id - 1); }
    const HirItem& item(HirItemId id) const { return itemArena.at(id - 1); }
};
