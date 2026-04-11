#include "AstSymbolCollector.hpp"

#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "Ast.hpp"
#include "FrontendTypeUtils.hpp"
#include "AstParser.hpp"
#include "NativePackage.hpp"
#include "SyntaxRules.hpp"

namespace {

void collectClassNamesFromAst(const AstModule& module,
                              std::unordered_set<std::string>& outClassNames) {
    outClassNames.clear();
    for (const auto& item : module.items) {
        if (item == nullptr) {
            continue;
        }

        const auto* classDecl = std::get_if<AstClassDecl>(&item->value);
        if (classDecl != nullptr) {
            outClassNames.emplace(std::string(classDecl->name.start(),
                                              classDecl->name.length()));
        }
    }
}

void collectTypeAliasesFromAst(
    const AstModule& module, const std::unordered_set<std::string>& classNames,
    std::unordered_map<std::string, TypeRef>& outTypeAliases,
    const std::unordered_map<std::string, const AstImportedModuleInterface*>*
        importedModulesByName) {
    outTypeAliases.clear();
    for (const auto& item : module.items) {
        if (item == nullptr) {
            continue;
        }

        const auto* aliasDecl = std::get_if<AstTypeAliasDecl>(&item->value);
        if (aliasDecl == nullptr || !aliasDecl->aliasedType) {
            continue;
        }

        FrontendTypeContext typeContext{classNames, outTypeAliases, "", {},
                                        importedModulesByName};
        TypeRef resolved =
            frontendResolveTypeExpr(*aliasDecl->aliasedType, typeContext);
        if (resolved) {
            outTypeAliases[std::string(aliasDecl->name.start(),
                                       aliasDecl->name.length())] = resolved;
        }
    }
}

}  // namespace

void collectFunctionSignaturesFromAst(
    const AstModule& module, const std::unordered_set<std::string>& classNames,
    const std::unordered_map<std::string, TypeRef>& typeAliases,
    std::unordered_map<std::string, TypeRef>& outFunctionSignatures,
    const std::unordered_map<std::string, const AstImportedModuleInterface*>*
        importedModulesByName) {
    outFunctionSignatures.clear();
    FrontendTypeContext typeContext{classNames, typeAliases, "", {},
                                    importedModulesByName};

    for (const auto& item : module.items) {
        if (item == nullptr) {
            continue;
        }

        const auto* functionDecl = std::get_if<AstFunctionDecl>(&item->value);
        if (functionDecl == nullptr) {
            continue;
        }

        std::vector<TypeRef> params;
        params.reserve(functionDecl->params.size());
        for (const auto& param : functionDecl->params) {
            TypeRef resolved =
                param.type ? frontendResolveTypeExpr(*param.type, typeContext)
                           : nullptr;
            params.push_back(resolved ? resolved : TypeInfo::makeAny());
        }

        TypeRef returnType = nullptr;
        if (functionDecl->returnType) {
            returnType =
                frontendResolveTypeExpr(*functionDecl->returnType, typeContext);
        } else {
            returnType = TypeInfo::makeVoid();
        }
        if (!returnType) {
            returnType = TypeInfo::makeAny();
        }

        outFunctionSignatures[std::string(functionDecl->name.start(),
                                          functionDecl->name.length())] =
            TypeInfo::makeFunction(std::move(params), returnType);
    }
}

bool collectSymbolsFromAst(
    const AstModule& module, std::unordered_set<std::string>& outClassNames,
    std::unordered_map<std::string, TypeRef>& outFunctionSignatures,
    std::unordered_map<std::string, TypeRef>* outTypeAliases,
    const std::unordered_map<std::string, const AstImportedModuleInterface*>*
        importedModulesByName) {
    collectClassNamesFromAst(module, outClassNames);

    std::unordered_map<std::string, TypeRef> localAliases;
    if (outTypeAliases != nullptr) {
        collectTypeAliasesFromAst(module, outClassNames, *outTypeAliases,
                                  importedModulesByName);
    } else {
        collectTypeAliasesFromAst(module, outClassNames, localAliases,
                                  importedModulesByName);
    }

    const auto& aliases = outTypeAliases != nullptr ? *outTypeAliases : localAliases;
    collectFunctionSignaturesFromAst(module, outClassNames, aliases,
                                     outFunctionSignatures, importedModulesByName);

    return true;
}

bool collectSymbolsFromAst(
    std::string_view source, std::unordered_set<std::string>& outClassNames,
    std::unordered_map<std::string, TypeRef>& outFunctionSignatures,
    std::unordered_map<std::string, TypeRef>* outTypeAliases,
    const std::unordered_map<std::string, const AstImportedModuleInterface*>*
        importedModulesByName) {
    AstModule module;
    AstParser parser(source);
    if (!parser.parseModule(module)) {
        return false;
    }

    return collectSymbolsFromAst(module, outClassNames, outFunctionSignatures,
                                 outTypeAliases, importedModulesByName);
}
