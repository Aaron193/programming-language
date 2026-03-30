#include "AstSemanticAnalyzer.hpp"

#include "AstBinder.hpp"
#include "AstTypeChecker.hpp"

bool analyzeAstSemantics(
    const AstModule& module,
    const std::unordered_set<std::string>& classNames,
    const std::unordered_map<std::string, TypeRef>& typeAliases,
    const std::unordered_map<std::string, TypeRef>& functionSignatures,
    const std::unordered_map<AstNodeId, AstImportedModuleInterface>&
        importedModules,
    const std::string& sourcePath,
    const std::vector<std::string>& packageSearchPaths,
    std::vector<TypeError>& out, AstSemanticModel* outModel) {
    AstBindResult bindings;
    if (!bindAst(module, classNames, typeAliases, functionSignatures,
                 importedModules, out, &bindings)) {
        return false;
    }

    return checkAstTypes(module, classNames, typeAliases, functionSignatures,
                         bindings, sourcePath, packageSearchPaths, out,
                         outModel);
}
