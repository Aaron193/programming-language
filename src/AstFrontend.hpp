#pragma once

#include <string>
#include <string_view>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Ast.hpp"
#include "AstSemanticAnalyzer.hpp"
#include "SourceLocation.hpp"
#include "TypeChecker.hpp"

struct AstFrontendImportCache {
    std::unordered_map<std::string, AstImportedModuleInterface> resolvedModules;
    std::unordered_set<std::string> modulesInProgress;
};

struct AstFrontendOptions {
    std::string sourcePath;
    std::vector<std::string> packageSearchPaths;
    AstFrontendImportCache* importCache = nullptr;
};

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
    struct Timings {
        uint64_t parseMicros = 0;
        uint64_t symbolCollectionMicros = 0;
        uint64_t importResolutionMicros = 0;
        uint64_t initialSemanticMicros = 0;
        uint64_t optimizationMicros = 0;
        uint64_t semanticRefreshMicros = 0;
        uint64_t totalMicros = 0;
    };

    AstModule module;
    std::unordered_set<std::string> classNames;
    std::unordered_map<std::string, TypeRef> typeAliases;
    std::unordered_map<std::string, TypeRef> functionSignatures;
    std::unordered_map<AstNodeId, AstImportedModuleInterface> importedModules;
    AstSemanticModel semanticModel;
    AstFrontendMode mode = AstFrontendMode::StrictChecked;
    size_t terminalLine = 1;
    SourcePosition terminalPosition = makeSourcePosition(0, 1, 1);
    Timings timings;
};

AstFrontendBuildStatus buildAstFrontend(std::string_view source,
                                        const AstFrontendOptions& options,
                                        AstFrontendMode mode,
                                        std::vector<TypeError>& outErrors,
                                        AstFrontendResult& outFrontend);
