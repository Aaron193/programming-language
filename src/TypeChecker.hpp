#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "FrontendDiagnostic.hpp"
#include "SourceLocation.hpp"
#include "TypeInfo.hpp"

struct AstBindingMetadata;
using TypeCheckerMetadata = AstBindingMetadata;

using TypeError = FrontendDiagnostic;

class TypeChecker {
   public:
    bool collectSymbols(
        std::string_view source, std::unordered_set<std::string>& outClassNames,
        std::unordered_map<std::string, TypeRef>& outFunctionSignatures,
        std::unordered_map<std::string, TypeRef>* outTypeAliases = nullptr);

    bool check(
        std::string_view source,
        const std::unordered_set<std::string>& classNames,
        const std::unordered_map<std::string, TypeRef>& typeAliases,
        const std::unordered_map<std::string, TypeRef>& functionSignatures,
        std::vector<TypeError>& out,
        TypeCheckerMetadata* outMetadata = nullptr);
};
