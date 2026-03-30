#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

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
    const std::string& sourcePath,
    const std::vector<std::string>& packageSearchPaths,
    std::vector<TypeError>& out,
    AstSemanticModel* outModel = nullptr);
