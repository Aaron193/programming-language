#pragma once

#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "Ast.hpp"
#include "TypeInfo.hpp"

bool collectSymbolsFromAst(
    const AstModule& module, std::unordered_set<std::string>& outClassNames,
    std::unordered_map<std::string, TypeRef>& outFunctionSignatures,
    std::unordered_map<std::string, TypeRef>* outTypeAliases = nullptr);

bool collectSymbolsFromAst(
    std::string_view source, std::unordered_set<std::string>& outClassNames,
    std::unordered_map<std::string, TypeRef>& outFunctionSignatures,
    std::unordered_map<std::string, TypeRef>* outTypeAliases = nullptr);
