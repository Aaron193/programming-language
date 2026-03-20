#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "SourceLocation.hpp"
#include "TypeInfo.hpp"

struct AstBindingMetadata;
using TypeCheckerMetadata = AstBindingMetadata;

struct TypeError {
    size_t line = 0;
    size_t column = 1;
    SourceSpan span = makePointSpan(1, 1);
    std::string message;

    TypeError() = default;
    TypeError(size_t lineValue, std::string messageValue)
        : line(lineValue == 0 ? 1 : lineValue),
          column(1),
          span(makePointSpan(lineValue == 0 ? 1 : lineValue, 1)),
          message(std::move(messageValue)) {}
    TypeError(SourceSpan spanValue, std::string messageValue)
        : line(spanValue.line()),
          column(spanValue.column()),
          span(std::move(spanValue)),
          message(std::move(messageValue)) {}
};

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
