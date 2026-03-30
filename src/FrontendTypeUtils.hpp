#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Ast.hpp"
#include "TypeInfo.hpp"

struct FrontendTypeContext {
    const std::unordered_set<std::string>& classNames;
    const std::unordered_map<std::string, TypeRef>& typeAliases;
    std::string sourcePath;
    std::vector<std::string> packageSearchPaths;
};

TypeRef frontendTokenToType(const Token& token,
                            const FrontendTypeContext& context);

TypeRef frontendResolveTypeExpr(const AstTypeExpr& typeExpr,
                                const FrontendTypeContext& context);
