#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Ast.hpp"
#include "NativePackage.hpp"
#include "TypeChecker.hpp"

struct AstBindingMetadata {
    std::unordered_map<std::string, std::unordered_map<std::string, TypeRef>>
        classFieldTypes;
    std::unordered_map<std::string, std::unordered_map<std::string, TypeRef>>
        classMethodSignatures;
    std::unordered_map<std::string, std::string> superclassOf;
};

struct ImportedModuleSymbol {
    TypeRef type;
    std::string doc;
    std::string kind;
    SourceSpan range = makePointSpan(1, 1);
    SourceSpan selectionRange = makePointSpan(1, 1);
    bool hasDeclarationSite = false;
};

struct AstImportedModuleInterface {
    ImportTarget importTarget;
    std::unordered_map<std::string, ImportedModuleSymbol> valueExports;
    std::unordered_map<std::string, ImportedModuleSymbol> typeExports;
    AstBindingMetadata metadata;
    std::unordered_map<std::string, std::unordered_map<int, std::string>>
        classOperatorMethods;
};

enum class AstBindingKind {
    Variable,
    Function,
    Class,
    ImportedModule,
    ThisValue,
    SuperValue,
};

struct AstBindingRef {
    AstBindingKind kind = AstBindingKind::Variable;
    AstNodeId declarationNodeId = 0;
    std::string name;
    bool isConst = false;
    std::string className;
    AstNodeId importExprNodeId = 0;
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
