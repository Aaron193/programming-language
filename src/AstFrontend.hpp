#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Ast.hpp"
#include "AstSemanticAnalyzer.hpp"
#include "TypeChecker.hpp"

struct AstFrontendResult {
    AstModule module;
    std::unordered_set<std::string> classNames;
    std::unordered_map<std::string, TypeRef> typeAliases;
    std::unordered_map<std::string, TypeRef> functionSignatures;
    AstSemanticModel semanticModel;
};

bool buildAstFrontend(std::string_view source, std::vector<TypeError>& outErrors,
                      AstFrontendResult& outFrontend);
