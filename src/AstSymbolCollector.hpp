#pragma once

#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "TypeInfo.hpp"

bool collectSymbolsFromAst(
    std::string_view source, std::unordered_set<std::string>& outClassNames,
    std::unordered_map<std::string, TypeRef>& outFunctionSignatures,
    std::unordered_map<std::string, TypeRef>* outTypeAliases = nullptr);
