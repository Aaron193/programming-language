#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Ast.hpp"
#include "AstSemanticAnalyzer.hpp"
#include "TypeChecker.hpp"

enum class AstFrontendMode {
    StrictChecked,
    LoweringOnly,
};

enum class AstFrontendBuildStatus {
    Success,
    ParseFailed,
    SemanticError,
};

struct AstFrontendResult {
    AstModule module;
    std::unordered_set<std::string> classNames;
    std::unordered_map<std::string, TypeRef> typeAliases;
    std::unordered_map<std::string, TypeRef> functionSignatures;
    AstSemanticModel semanticModel;
    AstFrontendMode mode = AstFrontendMode::StrictChecked;
    size_t terminalLine = 1;
};

AstFrontendBuildStatus buildAstFrontend(std::string_view source,
                                        AstFrontendMode mode,
                                        std::vector<TypeError>& outErrors,
                                        AstFrontendResult& outFrontend);
