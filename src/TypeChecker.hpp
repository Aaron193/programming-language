#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "TypeInfo.hpp"

struct TypeError {
    size_t line;
    std::string message;
};

struct TypeCheckerDeclarationType {
    size_t line = 0;
    size_t functionDepth = 0;
    size_t scopeDepth = 0;
    std::string name;
    TypeRef type;
};

struct TypeCheckerMetadata {
    std::unordered_map<std::string, std::unordered_map<std::string, TypeRef>>
        classFieldTypes;
    std::unordered_map<std::string, std::unordered_map<std::string, TypeRef>>
        classMethodSignatures;
    std::unordered_map<std::string, std::string> superclassOf;
    std::unordered_map<std::string, TypeRef> topLevelSymbolTypes;
    std::vector<TypeCheckerDeclarationType> declarationTypes;
};

class TypeChecker {
   public:
    bool collectSymbols(
        std::string_view source, std::unordered_set<std::string>& outClassNames,
        std::unordered_map<std::string, TypeRef>& outFunctionSignatures);

    bool check(
        std::string_view source,
        const std::unordered_set<std::string>& classNames,
        const std::unordered_map<std::string, TypeRef>& functionSignatures,
        std::vector<TypeError>& out,
        TypeCheckerMetadata* outMetadata = nullptr);
};
