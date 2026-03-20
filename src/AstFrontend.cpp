#include "AstFrontend.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
#include <type_traits>

#include "AstOptimizer.hpp"
#include "AstParser.hpp"
#include "AstToHirLowering.hpp"
#include "AstBinder.hpp"
#include "AstSymbolCollector.hpp"
#include "AstTypeChecker.hpp"
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
    outErrors.clear();
    for (const auto& error : parser.errors()) {
        outErrors.push_back(TypeError{error.span, error.message});
    }
}

TypeRef nodeTypeOrAny(const AstSemanticModel& model, AstNodeId nodeId) {
    auto it = model.nodeTypes.find(nodeId);
    if (it == model.nodeTypes.end() || !it->second) {
        return TypeInfo::makeAny();
    }

    return it->second;
}

bool validateNativePackageImport(const ImportTarget& importTarget,
                                 const NativePackageDescriptor& descriptor,
                                 std::string& outError) {
    outError.clear();

    if (importTarget.kind != ImportTargetKind::NATIVE_PACKAGE) {
        return true;
    }

    if (descriptor.packageNamespace != importTarget.packageNamespace ||
        descriptor.packageName != importTarget.packageName) {
        outError = "Native package '" + importTarget.rawSpecifier +
                   "' declared '" + descriptor.packageId +
                   "' in registration metadata.";
        return false;
    }

    return true;
}

void collectExportedSymbolTypes(AstFrontendResult& frontend) {
    frontend.semanticModel.exportedSymbolTypes.clear();

    for (const auto& item : frontend.module.items) {
        if (!item) {
            continue;
        }

        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, AstFunctionDecl>) {
                    const std::string name =
                        std::string(value.name.start(), value.name.length());
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
                    const std::string name =
                        std::string(value.name.start(), value.name.length());
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

                    const std::string name =
                        std::string(varDecl->name.start(), varDecl->name.length());
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
                                  const AstFrontendOptions& options,
                                  AstFrontendMode mode,
                                  AstFrontendImportCache& cache,
                                  AstImportedModuleInterface& outInterface,
                                  std::string& outError) {
    outError.clear();

    auto cachedIt = cache.resolvedModules.find(importTarget.canonicalId);
    if (cachedIt != cache.resolvedModules.end()) {
        outInterface = cachedIt->second;
        return true;
    }

    if (!cache.modulesInProgress.insert(importTarget.canonicalId).second) {
        outError = "Circular import detected: '" + importTarget.displayName + "'.";
        return false;
    }

    auto clearInProgress = [&]() {
        cache.modulesInProgress.erase(importTarget.canonicalId);
    };

    AstImportedModuleInterface importedInterface;
    importedInterface.importTarget = importTarget;

    if (importTarget.kind == ImportTargetKind::NATIVE_PACKAGE) {
        NativePackageDescriptor packageDescriptor;
        if (!loadNativePackageDescriptor(importTarget.resolvedPath,
                                         packageDescriptor, outError, false,
                                         nullptr) ||
            !validateNativePackageImport(importTarget, packageDescriptor,
                                         outError)) {
            clearInProgress();
            return false;
        }

        importedInterface.exportTypes = std::move(packageDescriptor.exportTypes);
    } else {
        std::ifstream file(importTarget.resolvedPath);
        if (!file) {
            outError = "Failed to open module '" + importTarget.resolvedPath + "'.";
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
        importedOptions.importCache = &cache;
        const AstFrontendBuildStatus status =
            buildAstFrontend(source, importedOptions, importedMode, importedErrors,
                             importedFrontend);
        if (status != AstFrontendBuildStatus::Success) {
            if (!importedErrors.empty()) {
                outError = importedErrors.front().message;
            } else {
                outError = "Failed to type-check imported module '" +
                           importTarget.resolvedPath + "'.";
            }
            clearInProgress();
            return false;
        }

        importedInterface.exportTypes =
            importedFrontend.semanticModel.exportedSymbolTypes;
    }

    cache.resolvedModules[importTarget.canonicalId] = importedInterface;
    outInterface = std::move(importedInterface);
    clearInProgress();
    return true;
}

class FrontendImportResolver {
   public:
    FrontendImportResolver(AstFrontendResult& frontend,
                           const AstFrontendOptions& options,
                           AstFrontendImportCache& cache,
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
    AstFrontendImportCache& m_cache;
    std::vector<TypeError>& m_errors;

    void addError(const SourceSpan& span, const std::string& message) {
        m_errors.push_back(TypeError{span, message});
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
                     "@import(...) is not allowed in interactive mode.");
            return;
        }

        std::string pathText(importExpr.path.start(), importExpr.path.length());
        if (pathText.length() < 2) {
            addError(importExpr.path.span(), "Invalid import path.");
            return;
        }

        const std::string rawPath = pathText.substr(1, pathText.length() - 2);
        ImportTarget importTarget;
        std::string resolveError;
        if (!resolveImportTarget(m_options.sourcePath, rawPath,
                                 m_options.packageSearchPaths, importTarget,
                                 resolveError)) {
            addError(importExpr.path.span(), resolveError);
            return;
        }

        AstImportedModuleInterface importedInterface;
        if (!buildImportedModuleInterface(importTarget, m_options,
                                          m_frontend.mode, m_cache,
                                          importedInterface, resolveError)) {
            addError(importExpr.path.span(), resolveError);
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
                                         std::vector<TypeError>& outErrors) {
    if (frontend.mode == AstFrontendMode::StrictChecked) {
        if (!bindAndCheckFrontend(frontend, outErrors, nullptr,
                                  &frontend.semanticModel,
                                  frontend.timings.initialBindMicros,
                                  frontend.timings.initialTypecheckMicros)) {
            return AstFrontendBuildStatus::SemanticError;
        }
    } else {
        std::vector<TypeError> ignoredErrors;
        bindAndCheckFrontend(frontend, ignoredErrors, nullptr,
                             &frontend.semanticModel,
                             frontend.timings.initialBindMicros,
                             frontend.timings.initialTypecheckMicros);
    }

    // The optimizer may read this semantic model while rewriting, but the
    // model becomes stale as soon as the AST mutates.
    frontend.timings.optimizationMicros +=
        measureMicros(
            [&]() { optimizeAst(frontend.module, frontend.semanticModel); });

    // The refreshed model below is the only semantic state lowering is allowed
    // to consume. No frontend code should rely on pre-optimization metadata
    // after the AST has been rewritten.
    frontend.semanticModel = AstSemanticModel{};
    frontend.bindings = AstBindResult{};
    outErrors.clear();
    bindAndCheckFrontend(frontend, outErrors, &frontend.bindings,
                         &frontend.semanticModel,
                         frontend.timings.refreshBindMicros,
                         frontend.timings.refreshTypecheckMicros);

    if (frontend.mode == AstFrontendMode::StrictChecked && !outErrors.empty()) {
        return AstFrontendBuildStatus::SemanticError;
    }

    frontend.hirModule = std::make_unique<HirModule>();
    frontend.timings.hirLowerMicros += measureMicros([&]() {
        lowerAstToHir(frontend, *frontend.hirModule);
    });

    collectExportedSymbolTypes(frontend);
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
    auto finalizeTotal = [&]() {
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

    AstFrontendImportCache localImportCache;
    AstFrontendImportCache& importCache =
        options.importCache ? *options.importCache : localImportCache;
    FrontendImportResolver importResolver(outFrontend, options, importCache,
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

    const AstFrontendBuildStatus status = runSemanticPhases(outFrontend, outErrors);
    finalizeTotal();
    return status;
}
