#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Ast.hpp"
#include "AstBinder.hpp"
#include "TypeChecker.hpp"

struct AstSemanticModel {
    std::unordered_map<AstNodeId, TypeRef> nodeTypes;
    std::unordered_map<AstNodeId, bool> nodeConstness;
    std::unordered_map<AstNodeId, AstImportedModuleInterface> importedModules;
    std::unordered_map<std::string, TypeRef> exportedValueTypes;
    std::unordered_map<std::string, TypeRef> exportedTypeBindings;
    std::unordered_map<std::string, std::unordered_map<int, std::string>>
        classOperatorMethods;
    AstBindingMetadata metadata;
};

bool analyzeAstSemantics(
    const AstModule& module,
    const std::unordered_set<std::string>& classNames,
    const std::unordered_map<std::string, TypeRef>& typeAliases,
    const std::unordered_map<std::string, TypeRef>& functionSignatures,
    const std::unordered_map<AstNodeId, AstImportedModuleInterface>&
        importedModules,
    const std::string& sourcePath,
    const std::vector<std::string>& packageSearchPaths,
    std::vector<TypeError>& out,
    AstSemanticModel* outModel = nullptr);
