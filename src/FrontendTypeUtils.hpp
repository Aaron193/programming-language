#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "Ast.hpp"
#include "TypeInfo.hpp"

struct FrontendTypeContext {
    const std::unordered_set<std::string>& classNames;
    const std::unordered_map<std::string, TypeRef>& typeAliases;
};

TypeRef frontendTokenToType(const Token& token,
                            const FrontendTypeContext& context);

TypeRef frontendResolveTypeExpr(const AstTypeExpr& typeExpr,
                                const FrontendTypeContext& context);
