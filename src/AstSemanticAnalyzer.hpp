#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Ast.hpp"
#include "TypeChecker.hpp"

struct AstSemanticModel {
    std::unordered_map<AstNodeId, TypeRef> nodeTypes;
    std::unordered_map<AstNodeId, bool> nodeConstness;
    TypeCheckerMetadata metadata;
};

bool analyzeAstSemantics(
    const AstModule& module,
    const std::unordered_set<std::string>& classNames,
    const std::unordered_map<std::string, TypeRef>& typeAliases,
    const std::unordered_map<std::string, TypeRef>& functionSignatures,
    std::vector<TypeError>& out,
    AstSemanticModel* outModel = nullptr);
