#pragma once

#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "Ast.hpp"
#include "TypeInfo.hpp"

struct AstImportedModuleInterface;

bool collectSymbolsFromAst(
    const AstModule& module, std::unordered_set<std::string>& outClassNames,
    std::unordered_map<std::string, TypeRef>& outFunctionSignatures,
    std::unordered_map<std::string, TypeRef>* outTypeAliases = nullptr,
    const std::unordered_map<std::string, const AstImportedModuleInterface*>*
        importedModulesByName = nullptr);

bool collectSymbolsFromAst(
    std::string_view source, std::unordered_set<std::string>& outClassNames,
    std::unordered_map<std::string, TypeRef>& outFunctionSignatures,
    std::unordered_map<std::string, TypeRef>* outTypeAliases = nullptr,
    const std::unordered_map<std::string, const AstImportedModuleInterface*>*
        importedModulesByName = nullptr);

void collectFunctionSignaturesFromAst(
    const AstModule& module, const std::unordered_set<std::string>& classNames,
    const std::unordered_map<std::string, TypeRef>& typeAliases,
    std::unordered_map<std::string, TypeRef>& outFunctionSignatures,
    const std::unordered_map<std::string, const AstImportedModuleInterface*>*
        importedModulesByName = nullptr);
