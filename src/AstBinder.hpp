#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Ast.hpp"
#include "NativePackage.hpp"
#include "TypeChecker.hpp"

struct AstImportedModuleInterface {
    ImportTarget importTarget;
    std::unordered_map<std::string, TypeRef> exportTypes;
};

struct AstBindingMetadata {
    std::unordered_map<std::string, std::unordered_map<std::string, TypeRef>>
        classFieldTypes;
    std::unordered_map<std::string, std::unordered_map<std::string, TypeRef>>
        classMethodSignatures;
    std::unordered_map<std::string, std::string> superclassOf;
};

enum class AstBindingKind {
    Variable,
    Function,
    Class,
    ThisValue,
    SuperValue,
};

struct AstBindingRef {
    AstBindingKind kind = AstBindingKind::Variable;
    AstNodeId declarationNodeId = 0;
    std::string name;
    bool isConst = false;
    std::string className;
};

struct AstBindResult {
    std::unordered_map<AstNodeId, AstBindingRef> references;
    std::unordered_map<AstNodeId, AstImportedModuleInterface> importedModules;
    std::unordered_map<std::string, std::unordered_map<int, std::string>>
        classOperatorMethods;
    AstBindingMetadata metadata;
};

bool bindAst(
    const AstModule& module,
    const std::unordered_set<std::string>& classNames,
    const std::unordered_map<std::string, TypeRef>& typeAliases,
    const std::unordered_map<std::string, TypeRef>& functionSignatures,
    const std::unordered_map<AstNodeId, AstImportedModuleInterface>&
        importedModules,
    std::vector<TypeError>& out,
    AstBindResult* outBindings = nullptr);
