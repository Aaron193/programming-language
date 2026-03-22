#include "AstFrontend.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <type_traits>

#include "AstParser.hpp"
#include "AstToHirLowering.hpp"
#include "AstBinder.hpp"
#include "AstSymbolCollector.hpp"
#include "AstTypeChecker.hpp"
#include "HirOptimizer.hpp"
#include "NativePackage.hpp"
#include "SyntaxRules.hpp"

namespace {

template <typename Func>
uint64_t measureMicros(Func&& func) {
    const auto start = std::chrono::steady_clock::now();
    func();
    const auto end = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start)
            .count());
}

bool hasStrictDirective(std::string_view source) {
    return source.rfind("#!strict", 0) == 0;
}

void appendParserErrors(const AstParser& parser,
                        std::vector<TypeError>& outErrors) {
    outErrors = parser.errors();
}

TypeRef nodeTypeOrAny(const AstSemanticModel& model, AstNodeId nodeId) {
    auto it = model.nodeTypes.find(nodeId);
    if (it == model.nodeTypes.end() || !it->second) {
        return TypeInfo::makeAny();
    }

    return it->second;
}

bool validateNativePackageImport(const ImportTarget& importTarget,
                                 const SourceSpan& importSpan,
                                 const NativePackageDescriptor& descriptor,
                                 TypeError& outError) {
    outError = TypeError{};

    if (importTarget.kind != ImportTargetKind::NATIVE_PACKAGE) {
        return true;
    }

    if (descriptor.packageNamespace != importTarget.packageNamespace ||
        descriptor.packageName != importTarget.packageName) {
        outError = TypeError{importSpan,
                             "Native package '" + importTarget.rawSpecifier +
                                 "' declared '" + descriptor.packageId +
                                 "' in registration metadata.",
                             "import.native_package_metadata_mismatch"};
        return false;
    }

    return true;
}

const std::string& internLexeme(FrontendIdentifierInterner* interner,
                                const Token& token,
                                std::string& fallbackStorage) {
    fallbackStorage = tokenLexeme(token);
    if (interner == nullptr) {
        return fallbackStorage;
    }

    return interner->value(interner->intern(fallbackStorage));
}

FrontendFileFingerprint fingerprintForPath(const std::string& path) {
    FrontendFileFingerprint fingerprint;
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(path, ec);
    if (ec) {
        return fingerprint;
    }

    const auto writeTime = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return fingerprint;
    }

    fingerprint.valid = true;
    fingerprint.size = static_cast<uint64_t>(fileSize);
    fingerprint.modifiedNanos = writeTime.time_since_epoch().count();
    return fingerprint;
}

bool fingerprintMatches(const FrontendFileFingerprint& fingerprint,
                        const std::string& path) {
    if (!fingerprint.valid) {
        return false;
    }

    const FrontendFileFingerprint current = fingerprintForPath(path);
    return current.valid && current.size == fingerprint.size &&
           current.modifiedNanos == fingerprint.modifiedNanos;
}

bool moduleGraphNodeUpToDate(const AstFrontendModuleGraphCache& cache,
                             const std::string& canonicalId,
                             std::unordered_set<std::string>& visiting) {
    auto nodeIt = cache.nodes.find(canonicalId);
    if (nodeIt == cache.nodes.end()) {
        return false;
    }

    if (!visiting.emplace(canonicalId).second) {
        return true;
    }

    const auto& node = nodeIt->second;
    if (!fingerprintMatches(node.fingerprint,
                            node.importedInterface.importTarget.resolvedPath)) {
        visiting.erase(canonicalId);
        return false;
    }

    for (const auto& dependency : node.dependencies) {
        if (!moduleGraphNodeUpToDate(cache, dependency, visiting)) {
            visiting.erase(canonicalId);
            return false;
        }
    }

    visiting.erase(canonicalId);
    return true;
}

std::vector<std::string> collectDependencyIds(const AstFrontendResult& frontend) {
    std::vector<std::string> dependencies;
    dependencies.reserve(frontend.importedModules.size());
    for (const auto& [nodeId, importedModule] : frontend.importedModules) {
        (void)nodeId;
        dependencies.push_back(importedModule.importTarget.canonicalId);
    }

    std::sort(dependencies.begin(), dependencies.end());
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()),
                       dependencies.end());
    return dependencies;
}

void appendImportTrace(std::vector<TypeError>& diagnostics,
                       const FrontendImportTraceFrame& frame) {
    for (auto& diagnostic : diagnostics) {
        diagnostic.addImportTrace(frame);
    }
}

void collectExportedSymbolTypes(AstFrontendResult& frontend,
                                FrontendIdentifierInterner* interner) {
    frontend.semanticModel.exportedSymbolTypes.clear();

    for (const auto& item : frontend.module.items) {
        if (!item) {
            continue;
        }

        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, AstFunctionDecl>) {
                    std::string fallbackName;
                    const std::string& name =
                        internLexeme(interner, value.name, fallbackName);
                    if (!isPublicSymbolName(name)) {
                        return;
                    }

                    TypeRef exportedType = nodeTypeOrAny(frontend.semanticModel,
                                                         value.node.id);
                    if (exportedType->isAny()) {
                        auto signatureIt =
                            frontend.functionSignatures.find(name);
                        if (signatureIt != frontend.functionSignatures.end() &&
                            signatureIt->second) {
                            exportedType = signatureIt->second;
                        }
                    }
                    frontend.semanticModel.exportedSymbolTypes[name] =
                        exportedType;
                } else if constexpr (std::is_same_v<T, AstClassDecl>) {
                    std::string fallbackName;
                    const std::string& name =
                        internLexeme(interner, value.name, fallbackName);
                    if (!isPublicSymbolName(name)) {
                        return;
                    }

                    TypeRef exportedType = nodeTypeOrAny(frontend.semanticModel,
                                                         value.node.id);
                    if (exportedType->isAny()) {
                        exportedType = TypeInfo::makeClass(name);
                    }
                    frontend.semanticModel.exportedSymbolTypes[name] =
                        exportedType;
                } else if constexpr (std::is_same_v<T, AstStmtPtr>) {
                    if (!value) {
                        return;
                    }

                    const auto* varDecl = std::get_if<AstVarDeclStmt>(&value->value);
                    if (!varDecl) {
                        return;
                    }

                    std::string fallbackName;
                    const std::string& name =
                        internLexeme(interner, varDecl->name, fallbackName);
                    if (!isPublicSymbolName(name)) {
                        return;
                    }

                    frontend.semanticModel.exportedSymbolTypes[name] =
                        nodeTypeOrAny(frontend.semanticModel, value->node.id);
                }
            },
            item->value);
    }
}

bool buildImportedModuleInterface(const ImportTarget& importTarget,
                                  const SourceSpan& importSpan,
                                  const AstFrontendOptions& options,
                                  AstFrontendMode mode,
                                  AstFrontendModuleGraphCache& cache,
                                  AstImportedModuleInterface& outInterface,
                                  std::vector<TypeError>& outDiagnostics) {
    outDiagnostics.clear();

    auto cachedIt = cache.nodes.find(importTarget.canonicalId);
    if (cachedIt != cache.nodes.end()) {
        std::unordered_set<std::string> visiting;
        if (cachedIt->second.mode == mode &&
            moduleGraphNodeUpToDate(cache, importTarget.canonicalId, visiting)) {
            cache.stats.hits++;
            if (cachedIt->second.buildSucceeded) {
                outInterface = cachedIt->second.importedInterface;
                return true;
            }

            outDiagnostics = cachedIt->second.diagnostics;
            return false;
        }

        cache.stats.rebuilds++;
        cache.nodes.erase(cachedIt);
    }
    cache.stats.misses++;

    if (!cache.modulesInProgress.insert(importTarget.canonicalId).second) {
        outDiagnostics.push_back(
            TypeError{importSpan,
                      "Circular import detected: '" + importTarget.displayName +
                          "'.",
                      "import.cycle"});
        return false;
    }

    auto clearInProgress = [&]() {
        cache.modulesInProgress.erase(importTarget.canonicalId);
    };

    AstImportedModuleInterface importedInterface;
    importedInterface.importTarget = importTarget;
    AstFrontendModuleGraphNode cachedNode;
    cachedNode.mode = mode;
    cachedNode.fingerprint = fingerprintForPath(importTarget.resolvedPath);
    cachedNode.importedInterface.importTarget = importTarget;

    if (importTarget.kind == ImportTargetKind::NATIVE_PACKAGE) {
        NativePackageDescriptor packageDescriptor;
        std::string packageError;
        TypeError importError;
        if (!loadNativePackageDescriptor(importTarget.resolvedPath,
                                         packageDescriptor, packageError, false,
                                         nullptr)) {
            outDiagnostics.push_back(TypeError{
                importSpan, packageError, "import.native_package_load"});
            cachedNode.diagnostics = outDiagnostics;
            cache.nodes[importTarget.canonicalId] = cachedNode;
            clearInProgress();
            return false;
        }
        if (!validateNativePackageImport(importTarget, importSpan,
                                         packageDescriptor,
                                         importError)) {
            outDiagnostics.push_back(importError);
            cachedNode.diagnostics = outDiagnostics;
            cache.nodes[importTarget.canonicalId] = cachedNode;
            clearInProgress();
            return false;
        }

        importedInterface.exportTypes = std::move(packageDescriptor.exportTypes);
    } else {
        std::ifstream file(importTarget.resolvedPath);
        if (!file) {
            outDiagnostics.push_back(
                TypeError{importSpan,
                          "Failed to open module '" + importTarget.resolvedPath +
                              "'.",
                          "import.module_open_failed"});
            cachedNode.diagnostics = outDiagnostics;
            cache.nodes[importTarget.canonicalId] = cachedNode;
            clearInProgress();
            return false;
        }

        std::string source((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        const AstFrontendMode importedMode =
            (mode == AstFrontendMode::StrictChecked || hasStrictDirective(source))
                ? AstFrontendMode::StrictChecked
                : AstFrontendMode::LoweringOnly;

        AstFrontendResult importedFrontend;
        std::vector<TypeError> importedErrors;
        AstFrontendOptions importedOptions;
        importedOptions.sourcePath = importTarget.resolvedPath;
        importedOptions.packageSearchPaths = options.packageSearchPaths;
        importedOptions.moduleGraphCache = &cache;
        const AstFrontendBuildStatus status =
            buildAstFrontend(source, importedOptions, importedMode, importedErrors,
                             importedFrontend);
        if (status != AstFrontendBuildStatus::Success) {
            outDiagnostics = importedErrors;
            if (outDiagnostics.empty()) {
                outDiagnostics.push_back(TypeError{
                    importSpan,
                    "Failed to type-check imported module '" +
                        importTarget.resolvedPath + "'.",
                    "import.module_build_failed"});
            }
            cachedNode.dependencies = collectDependencyIds(importedFrontend);
            cachedNode.diagnostics = outDiagnostics;
            cache.nodes[importTarget.canonicalId] = cachedNode;
            clearInProgress();
            return false;
        }

        importedInterface.exportTypes =
            importedFrontend.semanticModel.exportedSymbolTypes;
        cachedNode.dependencies = collectDependencyIds(importedFrontend);
    }

    cachedNode.importedInterface = importedInterface;
    cachedNode.buildSucceeded = true;
    cachedNode.diagnostics.clear();
    cache.nodes[importTarget.canonicalId] = cachedNode;
    outInterface = std::move(importedInterface);
    clearInProgress();
    return true;
}

class FrontendImportResolver {
   public:
    FrontendImportResolver(AstFrontendResult& frontend,
                           const AstFrontendOptions& options,
                           AstFrontendModuleGraphCache& cache,
                           std::vector<TypeError>& errors)
        : m_frontend(frontend),
          m_options(options),
          m_cache(cache),
          m_errors(errors) {}

    bool run() {
        for (const auto& item : m_frontend.module.items) {
            if (item) {
                resolveItem(*item);
            }
        }

        return m_errors.empty();
    }

   private:
    AstFrontendResult& m_frontend;
    const AstFrontendOptions& m_options;
    AstFrontendModuleGraphCache& m_cache;
    std::vector<TypeError>& m_errors;

    void addError(const SourceSpan& span, const std::string& message,
                  std::string code = "import.error") {
        m_errors.push_back(TypeError{span, message, std::move(code)});
    }

    void addImportDiagnostics(const AstImportExpr& importExpr,
                              const ImportTarget& importTarget,
                              std::vector<TypeError> diagnostics) {
        for (auto& diagnostic : diagnostics) {
            if (diagnostic.code.rfind("import.", 0) == 0) {
                diagnostic.span = importExpr.path.span();
                diagnostic.line = diagnostic.span.line();
                diagnostic.column = diagnostic.span.column();
            }
        }
        appendImportTrace(
            diagnostics,
            FrontendImportTraceFrame{importExpr.path.span(), m_options.sourcePath,
                                     importTarget.rawSpecifier,
                                     importTarget.canonicalId});
        m_errors.insert(m_errors.end(),
                        std::make_move_iterator(diagnostics.begin()),
                        std::make_move_iterator(diagnostics.end()));
    }

    void resolveItem(const AstItem& item) {
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, AstFunctionDecl>) {
                    if (value.body) {
                        resolveStmt(*value.body);
                    }
                } else if constexpr (std::is_same_v<T, AstClassDecl>) {
                    for (const auto& method : value.methods) {
                        if (method.body) {
                            resolveStmt(*method.body);
                        }
                    }
                } else if constexpr (std::is_same_v<T, AstStmtPtr>) {
                    if (value) {
                        resolveStmt(*value);
                    }
                }
            },
            item.value);
    }

    void resolveStmt(const AstStmt& stmt) {
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, AstBlockStmt>) {
                    for (const auto& item : value.items) {
                        if (item) {
                            resolveItem(*item);
                        }
                    }
                } else if constexpr (std::is_same_v<T, AstExprStmt>) {
                    if (value.expression) {
                        resolveExpr(*value.expression);
                    }
                } else if constexpr (std::is_same_v<T, AstPrintStmt>) {
                    if (value.expression) {
                        resolveExpr(*value.expression);
                    }
                } else if constexpr (std::is_same_v<T, AstReturnStmt>) {
                    if (value.value) {
                        resolveExpr(*value.value);
                    }
                } else if constexpr (std::is_same_v<T, AstIfStmt>) {
                    if (value.condition) {
                        resolveExpr(*value.condition);
                    }
                    if (value.thenBranch) {
                        resolveStmt(*value.thenBranch);
                    }
                    if (value.elseBranch) {
                        resolveStmt(*value.elseBranch);
                    }
                } else if constexpr (std::is_same_v<T, AstWhileStmt>) {
                    if (value.condition) {
                        resolveExpr(*value.condition);
                    }
                    if (value.body) {
                        resolveStmt(*value.body);
                    }
                } else if constexpr (std::is_same_v<T, AstVarDeclStmt>) {
                    if (value.initializer) {
                        resolveExpr(*value.initializer);
                    }
                } else if constexpr (std::is_same_v<T, AstDestructuredImportStmt>) {
                    if (value.initializer) {
                        resolveExpr(*value.initializer);
                    }
                } else if constexpr (std::is_same_v<T, AstForStmt>) {
                    if (const auto* initDecl =
                            std::get_if<std::unique_ptr<AstVarDeclStmt>>(
                                &value.initializer)) {
                        if (*initDecl && (*initDecl)->initializer) {
                            resolveExpr(*(*initDecl)->initializer);
                        }
                    } else if (const auto* initExpr =
                                   std::get_if<AstExprPtr>(&value.initializer)) {
                        if (*initExpr) {
                            resolveExpr(**initExpr);
                        }
                    }
                    if (value.condition) {
                        resolveExpr(*value.condition);
                    }
                    if (value.increment) {
                        resolveExpr(*value.increment);
                    }
                    if (value.body) {
                        resolveStmt(*value.body);
                    }
                } else if constexpr (std::is_same_v<T, AstForEachStmt>) {
                    if (value.iterable) {
                        resolveExpr(*value.iterable);
                    }
                    if (value.body) {
                        resolveStmt(*value.body);
                    }
                }
            },
            stmt.value);
    }

    void resolveExpr(const AstExpr& expr) {
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, AstGroupingExpr>) {
                    if (value.expression) {
                        resolveExpr(*value.expression);
                    }
                } else if constexpr (std::is_same_v<T, AstUnaryExpr>) {
                    if (value.operand) {
                        resolveExpr(*value.operand);
                    }
                } else if constexpr (std::is_same_v<T, AstUpdateExpr>) {
                    if (value.operand) {
                        resolveExpr(*value.operand);
                    }
                } else if constexpr (std::is_same_v<T, AstBinaryExpr>) {
                    if (value.left) {
                        resolveExpr(*value.left);
                    }
                    if (value.right) {
                        resolveExpr(*value.right);
                    }
                } else if constexpr (std::is_same_v<T, AstAssignmentExpr>) {
                    if (value.target) {
                        resolveExpr(*value.target);
                    }
                    if (value.value) {
                        resolveExpr(*value.value);
                    }
                } else if constexpr (std::is_same_v<T, AstCallExpr>) {
                    if (value.callee) {
                        resolveExpr(*value.callee);
                    }
                    for (const auto& argument : value.arguments) {
                        if (argument) {
                            resolveExpr(*argument);
                        }
                    }
                } else if constexpr (std::is_same_v<T, AstMemberExpr>) {
                    if (value.object) {
                        resolveExpr(*value.object);
                    }
                } else if constexpr (std::is_same_v<T, AstIndexExpr>) {
                    if (value.object) {
                        resolveExpr(*value.object);
                    }
                    if (value.index) {
                        resolveExpr(*value.index);
                    }
                } else if constexpr (std::is_same_v<T, AstCastExpr>) {
                    if (value.expression) {
                        resolveExpr(*value.expression);
                    }
                } else if constexpr (std::is_same_v<T, AstFunctionExpr>) {
                    if (value.blockBody) {
                        resolveStmt(*value.blockBody);
                    }
                    if (value.expressionBody) {
                        resolveExpr(*value.expressionBody);
                    }
                } else if constexpr (std::is_same_v<T, AstImportExpr>) {
                    resolveImportExpr(expr.node.id, value);
                } else if constexpr (std::is_same_v<T, AstArrayLiteralExpr>) {
                    for (const auto& element : value.elements) {
                        if (element) {
                            resolveExpr(*element);
                        }
                    }
                } else if constexpr (std::is_same_v<T, AstDictLiteralExpr>) {
                    for (const auto& entry : value.entries) {
                        if (entry.key) {
                            resolveExpr(*entry.key);
                        }
                        if (entry.value) {
                            resolveExpr(*entry.value);
                        }
                    }
                }
            },
            expr.value);
    }

    void resolveImportExpr(AstNodeId nodeId, const AstImportExpr& importExpr) {
        if (m_options.sourcePath.empty()) {
            addError(importExpr.path.span(),
                     "@import(...) is not allowed in interactive mode.",
                     "import.interactive_mode");
            return;
        }

        std::string pathText(importExpr.path.start(), importExpr.path.length());
        if (pathText.length() < 2) {
            addError(importExpr.path.span(), "Invalid import path.",
                     "import.invalid_path");
            return;
        }

        const std::string rawPath = pathText.substr(1, pathText.length() - 2);
        ImportTarget importTarget;
        std::string resolveError;
        if (!resolveImportTarget(m_options.sourcePath, rawPath,
                                 m_options.packageSearchPaths, importTarget,
                                 resolveError)) {
            addError(importExpr.path.span(), resolveError, "import.resolve_failed");
            return;
        }

        AstImportedModuleInterface importedInterface;
        std::vector<TypeError> importDiagnostics;
        if (!buildImportedModuleInterface(importTarget, importExpr.path.span(),
                                          m_options,
                                          m_frontend.mode, m_cache,
                                          importedInterface, importDiagnostics)) {
            if (importDiagnostics.empty()) {
                addError(importExpr.path.span(),
                         "Failed to build imported module interface.",
                         "import.interface_build_failed");
            } else {
                addImportDiagnostics(importExpr, importTarget,
                                     std::move(importDiagnostics));
            }
            return;
        }

        m_frontend.importedModules[nodeId] = std::move(importedInterface);
    }
};

bool bindAndCheckFrontend(const AstFrontendResult& frontend,
                          std::vector<TypeError>& outErrors,
                          AstBindResult* outBindings,
                          AstSemanticModel* outModel,
                          uint64_t& outBindMicros,
                          uint64_t& outTypecheckMicros) {
    AstBindResult bindings;
    outBindMicros += measureMicros([&]() {
        bindAst(frontend.module, frontend.classNames, frontend.typeAliases,
                frontend.functionSignatures, frontend.importedModules, outErrors,
                &bindings);
    });
    if (!outErrors.empty()) {
        return false;
    }
    if (outBindings != nullptr) {
        *outBindings = bindings;
    }

    outTypecheckMicros += measureMicros([&]() {
        checkAstTypes(frontend.module, frontend.classNames, frontend.typeAliases,
                      frontend.functionSignatures, bindings, outErrors,
                      outModel);
    });
    return outErrors.empty();
}

AstFrontendBuildStatus runSemanticPhases(AstFrontendResult& frontend,
                                         std::vector<TypeError>& outErrors,
                                         FrontendIdentifierInterner* interner) {
    if (frontend.mode == AstFrontendMode::StrictChecked) {
        if (!bindAndCheckFrontend(frontend, outErrors, &frontend.bindings,
                                  &frontend.semanticModel,
                                  frontend.timings.initialBindMicros,
                                  frontend.timings.initialTypecheckMicros)) {
            return AstFrontendBuildStatus::SemanticError;
        }
    } else {
        std::vector<TypeError> ignoredErrors;
        bindAndCheckFrontend(frontend, ignoredErrors, &frontend.bindings,
                             &frontend.semanticModel,
                             frontend.timings.initialBindMicros,
                             frontend.timings.initialTypecheckMicros);
    }

    frontend.hirModule = std::make_unique<HirModule>();
    frontend.timings.hirLowerMicros += measureMicros([&]() {
        lowerAstToHir(frontend, *frontend.hirModule);
    });
    frontend.timings.hirOptimizeMicros +=
        measureMicros([&]() { optimizeHir(*frontend.hirModule); });

    collectExportedSymbolTypes(frontend, interner);
    outErrors.clear();
    return AstFrontendBuildStatus::Success;
}

}  // namespace

AstFrontendBuildStatus buildAstFrontend(std::string_view source,
                                        const AstFrontendOptions& options,
                                        AstFrontendMode mode,
                                        std::vector<TypeError>& outErrors,
                                        AstFrontendResult& outFrontend) {
    const auto totalStart = std::chrono::steady_clock::now();
    const AstFrontendModuleGraphStats initialGraphStats =
        options.moduleGraphCache ? options.moduleGraphCache->stats
                                 : AstFrontendModuleGraphStats{};
    auto finalizeTotal = [&]() {
        if (options.moduleGraphCache != nullptr) {
            outFrontend.timings.moduleCacheHits =
                options.moduleGraphCache->stats.hits - initialGraphStats.hits;
            outFrontend.timings.moduleCacheMisses =
                options.moduleGraphCache->stats.misses - initialGraphStats.misses;
            outFrontend.timings.moduleCacheRebuilds =
                options.moduleGraphCache->stats.rebuilds -
                initialGraphStats.rebuilds;
            outFrontend.timings.internedIdentifierCount =
                options.moduleGraphCache->identifierInterner.size();
        }
        outFrontend.timings.diagnosticCount = outErrors.size();
        outFrontend.timings.totalMicros = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - totalStart)
                .count());
    };
    AstModule module;
    AstParser parser(source);
    outFrontend = AstFrontendResult{};
    outFrontend.mode = mode;

    bool parseSuccess = false;
    outFrontend.timings.parseMicros = measureMicros([&]() {
        parseSuccess = parser.parseModule(module);
    });
    if (!parseSuccess) {
        appendParserErrors(parser, outErrors);
        finalizeTotal();
        return AstFrontendBuildStatus::ParseFailed;
    }
    outFrontend.terminalLine =
        1 + static_cast<size_t>(std::count(source.begin(), source.end(), '\n'));
    size_t terminalColumn = 1;
    if (!source.empty() && source.back() != '\n') {
        const size_t lastNewline = source.find_last_of('\n');
        terminalColumn =
            (lastNewline == std::string_view::npos)
                ? source.size() + 1
                : source.size() - lastNewline;
    }
    outFrontend.terminalPosition =
        makeSourcePosition(source.size(), outFrontend.terminalLine, terminalColumn);
    outFrontend.module = std::move(module);
    outFrontend.timings.symbolCollectionMicros = measureMicros([&]() {
        collectSymbolsFromAst(outFrontend.module, outFrontend.classNames,
                              outFrontend.functionSignatures,
                              &outFrontend.typeAliases);
    });

    AstFrontendModuleGraphCache localModuleGraphCache;
    AstFrontendModuleGraphCache& moduleGraphCache =
        options.moduleGraphCache ? *options.moduleGraphCache : localModuleGraphCache;
    FrontendImportResolver importResolver(outFrontend, options, moduleGraphCache,
                                          outErrors);
    const bool importSuccess = [&]() {
        bool success = false;
        outFrontend.timings.importResolutionMicros = measureMicros(
            [&]() { success = importResolver.run(); });
        return success;
    }();
    if (!importSuccess) {
        finalizeTotal();
        return AstFrontendBuildStatus::SemanticError;
    }

    const AstFrontendBuildStatus status =
        runSemanticPhases(outFrontend, outErrors,
                          options.moduleGraphCache
                              ? &options.moduleGraphCache->identifierInterner
                              : nullptr);
    finalizeTotal();
    return status;
}
