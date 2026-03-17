#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "Token.hpp"

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
    Token name;
    std::unique_ptr<AstTypeExpr> type;
};

struct AstFunctionDecl {
    Token name;
    std::vector<AstParameter> params;
    std::unique_ptr<AstTypeExpr> returnType;
};

struct AstFieldDecl {
    Token name;
    std::unique_ptr<AstTypeExpr> type;
};

struct AstMethodDecl {
    Token name;
    std::vector<AstParameter> params;
    std::unique_ptr<AstTypeExpr> returnType;
    std::vector<int> annotatedOperators;
};

struct AstTypeAliasDecl {
    Token name;
    std::unique_ptr<AstTypeExpr> aliasedType;
};

struct AstClassDecl {
    Token name;
    std::optional<Token> superclass;
    std::vector<AstFieldDecl> fields;
    std::vector<AstMethodDecl> methods;
};

using AstDeclaration =
    std::variant<AstTypeAliasDecl, AstClassDecl, AstFunctionDecl>;

struct AstModule {
    std::vector<AstDeclaration> declarations;
};
