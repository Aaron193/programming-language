#pragma once

#include <string>
#include <string_view>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Ast.hpp"
#include "Hir.hpp"
#include "AstSemanticAnalyzer.hpp"
#include "FrontendInterner.hpp"
#include "SourceLocation.hpp"
#include "TypeChecker.hpp"

struct FrontendFileFingerprint {
    bool valid = false;
    uint64_t size = 0;
    int64_t modifiedNanos = 0;
};

struct AstFrontendModuleGraphNode {
    FrontendFileFingerprint fingerprint;
    AstImportedModuleInterface importedInterface;
    std::vector<std::string> dependencies;
    std::vector<TypeError> diagnostics;
    bool buildSucceeded = false;
};

struct AstFrontendModuleGraphStats {
    uint64_t hits = 0;
    uint64_t misses = 0;
    uint64_t rebuilds = 0;
};

struct AstFrontendModuleGraphCache {
    std::unordered_map<std::string, AstFrontendModuleGraphNode> nodes;
    std::unordered_set<std::string> modulesInProgress;
    FrontendIdentifierInterner identifierInterner;
    AstFrontendModuleGraphStats stats;
};

struct AstFrontendOptions {
    std::string sourcePath;
    std::vector<std::string> packageSearchPaths;
    AstFrontendModuleGraphCache* moduleGraphCache = nullptr;
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
        uint64_t initialBindMicros = 0;
        uint64_t initialTypecheckMicros = 0;
        uint64_t hirLowerMicros = 0;
        uint64_t hirOptimizeMicros = 0;
        uint64_t moduleCacheHits = 0;
        uint64_t moduleCacheMisses = 0;
        uint64_t moduleCacheRebuilds = 0;
        uint64_t diagnosticCount = 0;
        uint64_t internedIdentifierCount = 0;
        uint64_t totalMicros = 0;
    };

    AstModule module;
    std::unordered_set<std::string> classNames;
    std::unordered_map<std::string, TypeRef> typeAliases;
    std::unordered_map<std::string, TypeRef> functionSignatures;
    std::unordered_map<AstNodeId, AstImportedModuleInterface> importedModules;
    AstBindResult bindings;
    AstSemanticModel semanticModel;
    std::unique_ptr<HirModule> hirModule;
    size_t terminalLine = 1;
    SourcePosition terminalPosition = makeSourcePosition(0, 1, 1);
    Timings timings;
};

AstFrontendBuildStatus buildAstFrontend(std::string_view source,
                                        const AstFrontendOptions& options,
                                        std::vector<TypeError>& outErrors,
                                        AstFrontendResult& outFrontend);
