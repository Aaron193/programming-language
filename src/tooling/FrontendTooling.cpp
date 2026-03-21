#include "tooling/FrontendTooling.hpp"

#include <algorithm>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "Ast.hpp"
#include "AstBinder.hpp"
#include "FrontendDiagnostic.hpp"

namespace {

struct DeclarationSite {
    SourceSpan range;
    SourceSpan selectionRange;
};

ToolingDiagnostic toolingDiagnosticFromFrontend(
    const FrontendDiagnostic& diagnostic) {
    ToolingDiagnostic result;
    result.code = diagnostic.code;
    result.message = diagnostic.message;
    result.range = toolingRangeFromSourceSpan(diagnostic.span);

    result.notes.reserve(diagnostic.notes.size());
    for (const auto& note : diagnostic.notes) {
        result.notes.push_back(
            ToolingDiagnosticNote{toolingRangeFromSourceSpan(note.span),
                                  note.message});
    }

    result.importTrace.reserve(diagnostic.importTrace.size());
    for (const auto& frame : diagnostic.importTrace) {
        result.importTrace.push_back(ToolingImportTraceFrame{
            toolingRangeFromSourceSpan(frame.span), frame.importerPath,
            frame.rawSpecifier, frame.resolvedPath});
    }

    return result;
}

ToolingDocumentSymbol makeSymbol(std::string name, std::string kind,
                                 std::string detail, const SourceSpan& range,
                                 const SourceSpan& selectionRange) {
    return ToolingDocumentSymbol{std::move(name),
                                 std::move(kind),
                                 std::move(detail),
                                 toolingRangeFromSourceSpan(range),
                                 toolingRangeFromSourceSpan(selectionRange)};
}

void collectDocumentSymbols(
    const AstModule& module, std::vector<ToolingDocumentSymbol>& outSymbols) {
    for (const auto& item : module.items) {
        if (!item) {
            continue;
        }

        if (const auto* functionDecl =
                std::get_if<AstFunctionDecl>(&item->value)) {
            outSymbols.push_back(makeSymbol(tokenLexeme(functionDecl->name),
                                            "function", "", item->node.span,
                                            functionDecl->name.span()));
            continue;
        }

        if (const auto* classDecl = std::get_if<AstClassDecl>(&item->value)) {
            outSymbols.push_back(makeSymbol(tokenLexeme(classDecl->name),
                                            "class", "", item->node.span,
                                            classDecl->name.span()));
            continue;
        }

        if (const auto* aliasDecl =
                std::get_if<AstTypeAliasDecl>(&item->value)) {
            outSymbols.push_back(makeSymbol(tokenLexeme(aliasDecl->name), "type",
                                            "", item->node.span,
                                            aliasDecl->name.span()));
            continue;
        }

        const auto* stmtPtr = std::get_if<AstStmtPtr>(&item->value);
        if (stmtPtr == nullptr || !*stmtPtr) {
            continue;
        }

        AstStmt& stmt = **stmtPtr;
        if (const auto* varDecl = std::get_if<AstVarDeclStmt>(&stmt.value)) {
            outSymbols.push_back(makeSymbol(tokenLexeme(varDecl->name),
                                            varDecl->isConst ? "constant"
                                                             : "variable",
                                            "", stmt.node.span,
                                            varDecl->name.span()));
            continue;
        }

        if (const auto* importStmt =
                std::get_if<AstDestructuredImportStmt>(&stmt.value)) {
            for (const auto& binding : importStmt->bindings) {
                const Token& name =
                    binding.localName.has_value() ? *binding.localName
                                                  : binding.exportedName;
                outSymbols.push_back(makeSymbol(
                    tokenLexeme(name), "import", "", binding.node.span,
                    name.span()));
            }
        }
    }
}

bool containsPosition(const SourceSpan& span, const SourcePosition& position) {
    if (position.line < span.start.line || position.line > span.end.line) {
        return false;
    }

    if (position.line == span.start.line && position.column < span.start.column) {
        return false;
    }

    if (position.line == span.end.line && position.column > span.end.column) {
        return false;
    }

    return true;
}

bool bindAnalysisFrontend(ToolingDocumentAnalysis& analysis,
                         std::vector<TypeError>& outErrors) {
    outErrors.clear();
    AstBindResult bindings;
    bindAst(analysis.frontend.module, analysis.frontend.classNames,
            analysis.frontend.typeAliases, analysis.frontend.functionSignatures,
            analysis.frontend.importedModules, outErrors, &bindings);
    if (!outErrors.empty()) {
        return false;
    }

    analysis.frontend.bindings = std::move(bindings);
    analysis.hasBindings = true;
    return true;
}

void indexDeclarationSite(std::unordered_map<AstNodeId, DeclarationSite>& out,
                          AstNodeId nodeId, const SourceSpan& range,
                          const SourceSpan& selectionRange) {
    out[nodeId] = DeclarationSite{range, selectionRange};
}

void collectStmtDeclarationSites(
    const AstStmt& stmt, std::unordered_map<AstNodeId, DeclarationSite>& outSites);
const AstExpr* findDefinitionTargetStmt(const AstStmt& stmt,
                                        const SourcePosition& position);

void collectExprDeclarationSites(
    const AstExpr& expr, std::unordered_map<AstNodeId, DeclarationSite>& outSites) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, AstLiteralExpr> ||
                          std::is_same_v<T, AstImportExpr> ||
                          std::is_same_v<T, AstThisExpr> ||
                          std::is_same_v<T, AstSuperExpr>) {
                return;
            } else if constexpr (std::is_same_v<T, AstIdentifierExpr>) {
                return;
            } else if constexpr (std::is_same_v<T, AstGroupingExpr>) {
                collectExprDeclarationSites(*value.expression, outSites);
            } else if constexpr (std::is_same_v<T, AstUnaryExpr> ||
                                 std::is_same_v<T, AstUpdateExpr>) {
                collectExprDeclarationSites(*value.operand, outSites);
            } else if constexpr (std::is_same_v<T, AstBinaryExpr>) {
                collectExprDeclarationSites(*value.left, outSites);
                collectExprDeclarationSites(*value.right, outSites);
            } else if constexpr (std::is_same_v<T, AstAssignmentExpr>) {
                collectExprDeclarationSites(*value.target, outSites);
                collectExprDeclarationSites(*value.value, outSites);
            } else if constexpr (std::is_same_v<T, AstCallExpr>) {
                collectExprDeclarationSites(*value.callee, outSites);
                for (const auto& argument : value.arguments) {
                    collectExprDeclarationSites(*argument, outSites);
                }
            } else if constexpr (std::is_same_v<T, AstMemberExpr>) {
                collectExprDeclarationSites(*value.object, outSites);
            } else if constexpr (std::is_same_v<T, AstIndexExpr>) {
                collectExprDeclarationSites(*value.object, outSites);
                collectExprDeclarationSites(*value.index, outSites);
            } else if constexpr (std::is_same_v<T, AstCastExpr>) {
                collectExprDeclarationSites(*value.expression, outSites);
            } else if constexpr (std::is_same_v<T, AstFunctionExpr>) {
                for (const auto& param : value.params) {
                    indexDeclarationSite(outSites, param.node.id, param.node.span,
                                         param.name.span());
                }
                if (value.expressionBody) {
                    collectExprDeclarationSites(*value.expressionBody, outSites);
                }
                if (value.blockBody) {
                    collectStmtDeclarationSites(*value.blockBody, outSites);
                }
            } else if constexpr (std::is_same_v<T, AstArrayLiteralExpr>) {
                for (const auto& element : value.elements) {
                    collectExprDeclarationSites(*element, outSites);
                }
            } else if constexpr (std::is_same_v<T, AstDictLiteralExpr>) {
                for (const auto& entry : value.entries) {
                    collectExprDeclarationSites(*entry.key, outSites);
                    collectExprDeclarationSites(*entry.value, outSites);
                }
            }
        },
        expr.value);
}

void collectItemDeclarationSites(
    const AstItem& item, std::unordered_map<AstNodeId, DeclarationSite>& outSites) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, AstFunctionDecl>) {
                indexDeclarationSite(outSites, value.node.id, value.node.span,
                                     value.name.span());
                for (const auto& param : value.params) {
                    indexDeclarationSite(outSites, param.node.id, param.node.span,
                                         param.name.span());
                }
                if (value.body) {
                    collectStmtDeclarationSites(*value.body, outSites);
                }
            } else if constexpr (std::is_same_v<T, AstClassDecl>) {
                indexDeclarationSite(outSites, value.node.id, value.node.span,
                                     value.name.span());
                for (const auto& method : value.methods) {
                    for (const auto& param : method.params) {
                        indexDeclarationSite(outSites, param.node.id,
                                             param.node.span, param.name.span());
                    }
                    if (method.body) {
                        collectStmtDeclarationSites(*method.body, outSites);
                    }
                }
            } else if constexpr (std::is_same_v<T, AstTypeAliasDecl>) {
                indexDeclarationSite(outSites, value.node.id, value.node.span,
                                     value.name.span());
            } else if constexpr (std::is_same_v<T, AstStmtPtr>) {
                if (value) {
                    collectStmtDeclarationSites(*value, outSites);
                }
            }
        },
        item.value);
}

void collectStmtDeclarationSites(
    const AstStmt& stmt, std::unordered_map<AstNodeId, DeclarationSite>& outSites) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, AstBlockStmt>) {
                for (const auto& item : value.items) {
                    if (item) {
                        collectItemDeclarationSites(*item, outSites);
                    }
                }
            } else if constexpr (std::is_same_v<T, AstExprStmt>) {
                collectExprDeclarationSites(*value.expression, outSites);
            } else if constexpr (std::is_same_v<T, AstPrintStmt>) {
                collectExprDeclarationSites(*value.expression, outSites);
            } else if constexpr (std::is_same_v<T, AstReturnStmt>) {
                if (value.value) {
                    collectExprDeclarationSites(*value.value, outSites);
                }
            } else if constexpr (std::is_same_v<T, AstIfStmt>) {
                collectExprDeclarationSites(*value.condition, outSites);
                collectStmtDeclarationSites(*value.thenBranch, outSites);
                if (value.elseBranch) {
                    collectStmtDeclarationSites(*value.elseBranch, outSites);
                }
            } else if constexpr (std::is_same_v<T, AstWhileStmt>) {
                collectExprDeclarationSites(*value.condition, outSites);
                collectStmtDeclarationSites(*value.body, outSites);
            } else if constexpr (std::is_same_v<T, AstVarDeclStmt>) {
                indexDeclarationSite(outSites, stmt.node.id, stmt.node.span,
                                     value.name.span());
                if (value.initializer) {
                    collectExprDeclarationSites(*value.initializer, outSites);
                }
            } else if constexpr (std::is_same_v<T, AstDestructuredImportStmt>) {
                if (value.initializer) {
                    collectExprDeclarationSites(*value.initializer, outSites);
                }
                for (const auto& binding : value.bindings) {
                    const Token& name = binding.localName.has_value()
                                            ? *binding.localName
                                            : binding.exportedName;
                    indexDeclarationSite(outSites, binding.node.id,
                                         binding.node.span, name.span());
                }
            } else if constexpr (std::is_same_v<T, AstForStmt>) {
                if (const auto* initDecl =
                        std::get_if<std::unique_ptr<AstVarDeclStmt>>(
                            &value.initializer)) {
                    if (*initDecl) {
                        indexDeclarationSite(outSites, (*initDecl)->node.id,
                                             (*initDecl)->node.span,
                                             (*initDecl)->name.span());
                        if ((*initDecl)->initializer) {
                            collectExprDeclarationSites(
                                *(*initDecl)->initializer, outSites);
                        }
                    }
                } else if (const auto* initExpr =
                               std::get_if<AstExprPtr>(&value.initializer)) {
                    if (*initExpr) {
                        collectExprDeclarationSites(**initExpr, outSites);
                    }
                }
                if (value.condition) {
                    collectExprDeclarationSites(*value.condition, outSites);
                }
                if (value.increment) {
                    collectExprDeclarationSites(*value.increment, outSites);
                }
                collectStmtDeclarationSites(*value.body, outSites);
            } else if constexpr (std::is_same_v<T, AstForEachStmt>) {
                collectExprDeclarationSites(*value.iterable, outSites);
                indexDeclarationSite(outSites, stmt.node.id, stmt.node.span,
                                     value.name.span());
                collectStmtDeclarationSites(*value.body, outSites);
            }
        },
        stmt.value);
}

void collectDeclarationSites(
    const AstModule& module, std::unordered_map<AstNodeId, DeclarationSite>& outSites) {
    for (const auto& item : module.items) {
        if (item) {
            collectItemDeclarationSites(*item, outSites);
        }
    }
}

const AstExpr* findDefinitionTargetExpr(const AstExpr& expr,
                                        const SourcePosition& position) {
    if (!containsPosition(expr.node.span, position)) {
        return nullptr;
    }

    const AstExpr* best = nullptr;
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, AstIdentifierExpr>) {
                if (containsPosition(value.name.span(), position)) {
                    best = &expr;
                }
            } else if constexpr (std::is_same_v<T, AstGroupingExpr>) {
                best = findDefinitionTargetExpr(*value.expression, position);
            } else if constexpr (std::is_same_v<T, AstUnaryExpr> ||
                                 std::is_same_v<T, AstUpdateExpr>) {
                best = findDefinitionTargetExpr(*value.operand, position);
            } else if constexpr (std::is_same_v<T, AstBinaryExpr>) {
                best = findDefinitionTargetExpr(*value.left, position);
                if (best == nullptr) {
                    best = findDefinitionTargetExpr(*value.right, position);
                }
            } else if constexpr (std::is_same_v<T, AstAssignmentExpr>) {
                best = findDefinitionTargetExpr(*value.target, position);
                if (best == nullptr) {
                    best = findDefinitionTargetExpr(*value.value, position);
                }
            } else if constexpr (std::is_same_v<T, AstCallExpr>) {
                best = findDefinitionTargetExpr(*value.callee, position);
                if (best == nullptr) {
                    for (const auto& argument : value.arguments) {
                        best = findDefinitionTargetExpr(*argument, position);
                        if (best != nullptr) {
                            break;
                        }
                    }
                }
            } else if constexpr (std::is_same_v<T, AstMemberExpr>) {
                best = findDefinitionTargetExpr(*value.object, position);
            } else if constexpr (std::is_same_v<T, AstIndexExpr>) {
                best = findDefinitionTargetExpr(*value.object, position);
                if (best == nullptr) {
                    best = findDefinitionTargetExpr(*value.index, position);
                }
            } else if constexpr (std::is_same_v<T, AstCastExpr>) {
                best = findDefinitionTargetExpr(*value.expression, position);
            } else if constexpr (std::is_same_v<T, AstFunctionExpr>) {
                if (value.expressionBody) {
                    best = findDefinitionTargetExpr(*value.expressionBody, position);
                }
                if (best == nullptr && value.blockBody) {
                    best = findDefinitionTargetStmt(*value.blockBody, position);
                }
            } else if constexpr (std::is_same_v<T, AstArrayLiteralExpr>) {
                for (const auto& element : value.elements) {
                    best = findDefinitionTargetExpr(*element, position);
                    if (best != nullptr) {
                        break;
                    }
                }
            } else if constexpr (std::is_same_v<T, AstDictLiteralExpr>) {
                for (const auto& entry : value.entries) {
                    best = findDefinitionTargetExpr(*entry.key, position);
                    if (best == nullptr) {
                        best = findDefinitionTargetExpr(*entry.value, position);
                    }
                    if (best != nullptr) {
                        break;
                    }
                }
            }
        },
        expr.value);

    return best;
}

const AstExpr* findDefinitionTargetItem(const AstItem& item,
                                        const SourcePosition& position) {
    const AstExpr* best = nullptr;
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, AstFunctionDecl>) {
                if (value.body) {
                    best = findDefinitionTargetStmt(*value.body, position);
                }
            } else if constexpr (std::is_same_v<T, AstClassDecl>) {
                for (const auto& method : value.methods) {
                    if (method.body) {
                        best = findDefinitionTargetStmt(*method.body, position);
                        if (best != nullptr) {
                            break;
                        }
                    }
                }
            } else if constexpr (std::is_same_v<T, AstStmtPtr>) {
                if (value) {
                    best = findDefinitionTargetStmt(*value, position);
                }
            }
        },
        item.value);
    return best;
}

const AstExpr* findDefinitionTargetStmt(const AstStmt& stmt,
                                        const SourcePosition& position) {
    const AstExpr* best = nullptr;
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, AstBlockStmt>) {
                for (const auto& item : value.items) {
                    if (!item) {
                        continue;
                    }
                    best = findDefinitionTargetItem(*item, position);
                    if (best != nullptr) {
                        break;
                    }
                }
            } else if constexpr (std::is_same_v<T, AstExprStmt>) {
                best = findDefinitionTargetExpr(*value.expression, position);
            } else if constexpr (std::is_same_v<T, AstPrintStmt>) {
                best = findDefinitionTargetExpr(*value.expression, position);
            } else if constexpr (std::is_same_v<T, AstReturnStmt>) {
                if (value.value) {
                    best = findDefinitionTargetExpr(*value.value, position);
                }
            } else if constexpr (std::is_same_v<T, AstIfStmt>) {
                best = findDefinitionTargetExpr(*value.condition, position);
                if (best == nullptr) {
                    best = findDefinitionTargetStmt(*value.thenBranch, position);
                }
                if (best == nullptr && value.elseBranch) {
                    best = findDefinitionTargetStmt(*value.elseBranch, position);
                }
            } else if constexpr (std::is_same_v<T, AstWhileStmt>) {
                best = findDefinitionTargetExpr(*value.condition, position);
                if (best == nullptr) {
                    best = findDefinitionTargetStmt(*value.body, position);
                }
            } else if constexpr (std::is_same_v<T, AstVarDeclStmt>) {
                if (value.initializer) {
                    best = findDefinitionTargetExpr(*value.initializer, position);
                }
            } else if constexpr (std::is_same_v<T, AstDestructuredImportStmt>) {
                if (value.initializer) {
                    best = findDefinitionTargetExpr(*value.initializer, position);
                }
            } else if constexpr (std::is_same_v<T, AstForStmt>) {
                if (const auto* initDecl =
                        std::get_if<std::unique_ptr<AstVarDeclStmt>>(
                            &value.initializer)) {
                    if (*initDecl && (*initDecl)->initializer) {
                        best = findDefinitionTargetExpr(*(*initDecl)->initializer,
                                                        position);
                    }
                } else if (const auto* initExpr =
                               std::get_if<AstExprPtr>(&value.initializer)) {
                    if (*initExpr) {
                        best = findDefinitionTargetExpr(**initExpr, position);
                    }
                }
                if (best == nullptr && value.condition) {
                    best = findDefinitionTargetExpr(*value.condition, position);
                }
                if (best == nullptr && value.increment) {
                    best = findDefinitionTargetExpr(*value.increment, position);
                }
                if (best == nullptr) {
                    best = findDefinitionTargetStmt(*value.body, position);
                }
            } else if constexpr (std::is_same_v<T, AstForEachStmt>) {
                best = findDefinitionTargetExpr(*value.iterable, position);
                if (best == nullptr) {
                    best = findDefinitionTargetStmt(*value.body, position);
                }
            }
        },
        stmt.value);
    return best;
}

const AstExpr* findDefinitionTarget(const AstModule& module,
                                    const SourcePosition& position) {
    for (const auto& item : module.items) {
        if (!item) {
            continue;
        }
        if (const AstExpr* match = findDefinitionTargetItem(*item, position)) {
            return match;
        }
    }
    return nullptr;
}

}  // namespace

bool toolingSourceStartsWithStrictDirective(std::string_view source) {
    constexpr std::string_view directive = "#!strict";
    if (source.size() < directive.size() ||
        source.substr(0, directive.size()) != directive) {
        return false;
    }

    if (source.size() == directive.size()) {
        return true;
    }

    const char boundary = source[directive.size()];
    return boundary == '\n' || boundary == '\r';
}

ToolingPosition toolingPositionFromSourcePosition(const SourcePosition& position) {
    ToolingPosition result;
    result.line = position.line == 0 ? 0 : position.line - 1;
    result.character = position.column == 0 ? 0 : position.column - 1;
    return result;
}

ToolingRange toolingRangeFromSourceSpan(const SourceSpan& span) {
    return ToolingRange{toolingPositionFromSourcePosition(span.start),
                        toolingPositionFromSourcePosition(span.end)};
}

SourcePosition sourcePositionFromToolingPosition(const ToolingPosition& position) {
    return makeSourcePosition(0, position.line + 1,
                              position.character + 1);
}

SourceSpan sourceSpanFromToolingRange(const ToolingRange& range) {
    return SourceSpan{sourcePositionFromToolingPosition(range.start),
                      sourcePositionFromToolingPosition(range.end)};
}

ToolingDocumentAnalysis analyzeDocumentForTooling(
    std::string_view source, const ToolingAnalyzeOptions& options) {
    ToolingDocumentAnalysis analysis;
    analysis.sourcePath = options.sourcePath;
    analysis.strictMode = options.strictMode;

    AstFrontendOptions frontendOptions;
    frontendOptions.sourcePath = options.sourcePath;
    frontendOptions.packageSearchPaths = options.packageSearchPaths;
    frontendOptions.moduleGraphCache = options.moduleGraphCache;

    std::vector<TypeError> errors;
    analysis.status = buildAstFrontend(
        source, frontendOptions,
        options.strictMode ? AstFrontendMode::StrictChecked
                           : AstFrontendMode::LoweringOnly,
        errors, analysis.frontend);
    analysis.hasParse = analysis.status != AstFrontendBuildStatus::ParseFailed;
    analysis.hasSemantics = analysis.status == AstFrontendBuildStatus::Success;
    analysis.hasFrontend = analysis.hasSemantics;

    analysis.diagnostics.reserve(errors.size());
    for (const auto& error : errors) {
        analysis.diagnostics.push_back(toolingDiagnosticFromFrontend(error));
    }

    if (analysis.hasParse) {
        collectDocumentSymbols(analysis.frontend.module, analysis.documentSymbols);
    }

    if (analysis.hasSemantics) {
        analysis.hasBindings = true;
    } else if (analysis.hasParse) {
        std::vector<TypeError> bindErrors;
        bindAnalysisFrontend(analysis, bindErrors);
    }

    return analysis;
}

std::optional<ToolingLocation> findDefinitionForTooling(
    const ToolingDocumentAnalysis& analysis, const ToolingPosition& position) {
    if (!analysis.hasParse || !analysis.hasBindings) {
        return std::nullopt;
    }

    const SourcePosition sourcePosition = sourcePositionFromToolingPosition(position);
    const AstExpr* targetExpr =
        findDefinitionTarget(analysis.frontend.module, sourcePosition);
    if (targetExpr == nullptr) {
        return std::nullopt;
    }

    const auto* identifierExpr = std::get_if<AstIdentifierExpr>(&targetExpr->value);
    if (identifierExpr == nullptr ||
        !containsPosition(identifierExpr->name.span(), sourcePosition)) {
        return std::nullopt;
    }

    const auto bindingIt =
        analysis.frontend.bindings.references.find(targetExpr->node.id);
    if (bindingIt == analysis.frontend.bindings.references.end() ||
        bindingIt->second.declarationNodeId == 0) {
        return std::nullopt;
    }

    std::unordered_map<AstNodeId, DeclarationSite> declarationSites;
    collectDeclarationSites(analysis.frontend.module, declarationSites);

    const auto declarationIt =
        declarationSites.find(bindingIt->second.declarationNodeId);
    if (declarationIt == declarationSites.end()) {
        return std::nullopt;
    }

    return ToolingLocation{analysis.sourcePath,
                           toolingRangeFromSourceSpan(declarationIt->second.range),
                           toolingRangeFromSourceSpan(
                               declarationIt->second.selectionRange)};
}
