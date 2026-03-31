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

}  // namespace

bool collectSymbolsFromAst(
    const AstModule& module, std::unordered_set<std::string>& outClassNames,
    std::unordered_map<std::string, TypeRef>& outFunctionSignatures,
    std::unordered_map<std::string, TypeRef>* outTypeAliases,
    const std::unordered_map<std::string, const AstImportedModuleInterface*>*
        importedModulesByName) {
    outClassNames.clear();
    outFunctionSignatures.clear();
    if (outTypeAliases != nullptr) {
        outTypeAliases->clear();
    }

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

    if (outTypeAliases != nullptr) {
        for (const auto& item : module.items) {
            if (item == nullptr) {
                continue;
            }

            const auto* aliasDecl = std::get_if<AstTypeAliasDecl>(&item->value);
            if (aliasDecl == nullptr || !aliasDecl->aliasedType) {
                continue;
            }

            FrontendTypeContext typeContext{outClassNames, *outTypeAliases, "",
                                            {}, importedModulesByName};
            TypeRef resolved =
                frontendResolveTypeExpr(*aliasDecl->aliasedType, typeContext);
            if (resolved) {
                (*outTypeAliases)[std::string(aliasDecl->name.start(),
                                              aliasDecl->name.length())] =
                    resolved;
            }
        }
    }

    const std::unordered_map<std::string, TypeRef> emptyAliases;
    const auto& aliases =
        outTypeAliases != nullptr ? *outTypeAliases : emptyAliases;
    FrontendTypeContext typeContext{outClassNames, aliases, "", {},
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

        TypeRef returnType =
            functionDecl->returnType
                ? frontendResolveTypeExpr(*functionDecl->returnType, typeContext)
                : TypeInfo::makeAny();
        if (!returnType) {
            returnType = TypeInfo::makeAny();
        }

        outFunctionSignatures[std::string(functionDecl->name.start(),
                                          functionDecl->name.length())] =
            TypeInfo::makeFunction(std::move(params), returnType);
    }

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
