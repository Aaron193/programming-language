#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Ast.hpp"
#include "AstBinder.hpp"
#include "AstSemanticAnalyzer.hpp"
#include "TypeChecker.hpp"

bool checkAstTypes(
    const AstModule& module,
    const std::unordered_set<std::string>& classNames,
    const std::unordered_map<std::string, TypeRef>& typeAliases,
    const std::unordered_map<std::string, TypeRef>& functionSignatures,
    const AstBindResult& bindings,
    std::vector<TypeError>& out,
    AstSemanticModel* outModel = nullptr);
