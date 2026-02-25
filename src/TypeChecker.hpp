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

class TypeChecker {
   public:
    bool check(
        std::string_view source,
        const std::unordered_set<std::string>& classNames,
        const std::unordered_map<std::string, TypeRef>& functionSignatures,
        std::vector<TypeError>& out);
};
