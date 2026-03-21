#include "tooling/FrontendTooling.hpp"

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "Ast.hpp"
#include "AstBinder.hpp"
#include "FrontendDiagnostic.hpp"
#include "NativePackage.hpp"
#include "SyntaxRules.hpp"
#include "TypeInfo.hpp"

namespace {

struct DeclarationSite {
    AstNodeId nodeId = 0;
    std::string name;
    std::string kind;
    SourceSpan range;
    SourceSpan selectionRange;
};

struct ImportBindingSite {
    std::string exportedName;
    ImportTarget importTarget;
};

struct SymbolTarget {
    AstNodeId declarationNodeId = 0;
    SourceSpan occurrenceSpan;
};

bool positionLess(const SourcePosition& lhs, const SourcePosition& rhs) {
    if (lhs.line != rhs.line) {
        return lhs.line < rhs.line;
    }
    return lhs.column < rhs.column;
}

bool positionLessOrEqual(const SourcePosition& lhs, const SourcePosition& rhs) {
    return !positionLess(rhs, lhs);
}

std::string tokenText(const Token& token) { return tokenLexeme(token); }

DeclarationSite makeDeclarationSite(AstNodeId nodeId, const Token& name,
                                    std::string kind,
                                    const SourceSpan& range) {
    return DeclarationSite{nodeId, tokenText(name), std::move(kind), range,
                           name.span()};
}

DeclarationSite functionDeclarationSite(const AstFunctionDecl& decl) {
    return makeDeclarationSite(decl.node.id, decl.name, "function",
                               decl.node.span);
}

DeclarationSite classDeclarationSite(const AstClassDecl& decl) {
    return makeDeclarationSite(decl.node.id, decl.name, "class", decl.node.span);
}

DeclarationSite typeAliasDeclarationSite(const AstTypeAliasDecl& decl) {
    return makeDeclarationSite(decl.node.id, decl.name, "type", decl.node.span);
}

DeclarationSite parameterDeclarationSite(const AstParameter& param) {
    return makeDeclarationSite(param.node.id, param.name, "parameter",
                               param.node.span);
}

DeclarationSite variableDeclarationSite(const AstNodeInfo& node, const Token& name,
                                        bool isConst) {
    return makeDeclarationSite(node.id, name,
                               isConst ? "constant" : "variable", node.span);
}

DeclarationSite importBindingDeclarationSite(const AstImportBinding& binding) {
    const Token& name = binding.localName.has_value() ? *binding.localName
                                                      : binding.exportedName;
    return makeDeclarationSite(binding.node.id, name, "import", binding.node.span);
}

bool itemContainsPosition(const AstItem& item, const SourcePosition& position) {
    auto spanContainsPosition = [&](const SourceSpan& span) {
        return !positionLess(position, span.start) &&
               !positionLess(span.end, position);
    };

    bool contains = spanContainsPosition(item.node.span);
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, AstFunctionDecl>) {
                contains = contains ||
                           (value.body &&
                            spanContainsPosition(value.body->node.span));
            } else if constexpr (std::is_same_v<T, AstClassDecl>) {
                if (contains) {
                    return;
                }
                for (const auto& method : value.methods) {
                    if (method.body &&
                        spanContainsPosition(method.body->node.span)) {
                        contains = true;
                        return;
                    }
                }
            } else if constexpr (std::is_same_v<T, AstStmtPtr>) {
                contains = value && spanContainsPosition(value->node.span);
            }
        },
        item.value);
    return contains;
}

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
            outSymbols.push_back(makeSymbol(tokenText(functionDecl->name),
                                            "function", "", item->node.span,
                                            functionDecl->name.span()));
            continue;
        }

        if (const auto* classDecl = std::get_if<AstClassDecl>(&item->value)) {
            outSymbols.push_back(makeSymbol(tokenText(classDecl->name), "class",
                                            "", item->node.span,
                                            classDecl->name.span()));
            continue;
        }

        if (const auto* aliasDecl =
                std::get_if<AstTypeAliasDecl>(&item->value)) {
            outSymbols.push_back(makeSymbol(tokenText(aliasDecl->name), "type",
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
            outSymbols.push_back(makeSymbol(tokenText(varDecl->name),
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
                outSymbols.push_back(makeSymbol(tokenText(name), "import", "",
                                                binding.node.span,
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
                          AstNodeId nodeId, std::string name,
                          std::string kind, const SourceSpan& range,
                          const SourceSpan& selectionRange) {
    out[nodeId] = DeclarationSite{nodeId, std::move(name), std::move(kind),
                                  range, selectionRange};
}

void collectStmtDeclarationSites(
    const AstStmt& stmt,
    std::unordered_map<AstNodeId, DeclarationSite>& outSites);
void collectStmtReferenceSites(
    const AstStmt& stmt, std::unordered_map<AstNodeId, SourceSpan>& outSites);
void collectStmtImportBindingSites(
    const AstStmt& stmt,
    const std::unordered_map<AstNodeId, AstImportedModuleInterface>& importedModules,
    std::unordered_map<AstNodeId, ImportBindingSite>& outSites);
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
                          std::is_same_v<T, AstSuperExpr> ||
                          std::is_same_v<T, AstIdentifierExpr>) {
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
                    indexDeclarationSite(outSites, param.node.id,
                                         tokenText(param.name), "parameter",
                                         param.node.span, param.name.span());
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
                indexDeclarationSite(outSites, value.node.id, tokenText(value.name),
                                     "function", value.node.span,
                                     value.name.span());
                for (const auto& param : value.params) {
                    indexDeclarationSite(outSites, param.node.id,
                                         tokenText(param.name), "parameter",
                                         param.node.span, param.name.span());
                }
                if (value.body) {
                    collectStmtDeclarationSites(*value.body, outSites);
                }
            } else if constexpr (std::is_same_v<T, AstClassDecl>) {
                indexDeclarationSite(outSites, value.node.id, tokenText(value.name),
                                     "class", value.node.span, value.name.span());
                for (const auto& method : value.methods) {
                    for (const auto& param : method.params) {
                        indexDeclarationSite(outSites, param.node.id,
                                             tokenText(param.name), "parameter",
                                             param.node.span, param.name.span());
                    }
                    if (method.body) {
                        collectStmtDeclarationSites(*method.body, outSites);
                    }
                }
            } else if constexpr (std::is_same_v<T, AstTypeAliasDecl>) {
                indexDeclarationSite(outSites, value.node.id, tokenText(value.name),
                                     "type", value.node.span, value.name.span());
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
                indexDeclarationSite(outSites, stmt.node.id, tokenText(value.name),
                                     value.isConst ? "constant" : "variable",
                                     stmt.node.span, value.name.span());
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
                                         tokenText(name), "import",
                                         binding.node.span, name.span());
                }
            } else if constexpr (std::is_same_v<T, AstForStmt>) {
                if (const auto* initDecl =
                        std::get_if<std::unique_ptr<AstVarDeclStmt>>(
                            &value.initializer)) {
                    if (*initDecl) {
                        indexDeclarationSite(
                            outSites, (*initDecl)->node.id,
                            tokenText((*initDecl)->name),
                            (*initDecl)->isConst ? "constant" : "variable",
                            (*initDecl)->node.span, (*initDecl)->name.span());
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
                indexDeclarationSite(outSites, stmt.node.id, tokenText(value.name),
                                     value.isConst ? "constant" : "variable",
                                     stmt.node.span, value.name.span());
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

void collectExprReferenceSites(
    const AstExpr& expr, std::unordered_map<AstNodeId, SourceSpan>& outSites) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, AstLiteralExpr> ||
                          std::is_same_v<T, AstImportExpr> ||
                          std::is_same_v<T, AstThisExpr> ||
                          std::is_same_v<T, AstSuperExpr>) {
                return;
            } else if constexpr (std::is_same_v<T, AstIdentifierExpr>) {
                outSites[expr.node.id] = value.name.span();
            } else if constexpr (std::is_same_v<T, AstGroupingExpr>) {
                collectExprReferenceSites(*value.expression, outSites);
            } else if constexpr (std::is_same_v<T, AstUnaryExpr> ||
                                 std::is_same_v<T, AstUpdateExpr>) {
                collectExprReferenceSites(*value.operand, outSites);
            } else if constexpr (std::is_same_v<T, AstBinaryExpr>) {
                collectExprReferenceSites(*value.left, outSites);
                collectExprReferenceSites(*value.right, outSites);
            } else if constexpr (std::is_same_v<T, AstAssignmentExpr>) {
                collectExprReferenceSites(*value.target, outSites);
                collectExprReferenceSites(*value.value, outSites);
            } else if constexpr (std::is_same_v<T, AstCallExpr>) {
                collectExprReferenceSites(*value.callee, outSites);
                for (const auto& argument : value.arguments) {
                    collectExprReferenceSites(*argument, outSites);
                }
            } else if constexpr (std::is_same_v<T, AstMemberExpr>) {
                collectExprReferenceSites(*value.object, outSites);
            } else if constexpr (std::is_same_v<T, AstIndexExpr>) {
                collectExprReferenceSites(*value.object, outSites);
                collectExprReferenceSites(*value.index, outSites);
            } else if constexpr (std::is_same_v<T, AstCastExpr>) {
                collectExprReferenceSites(*value.expression, outSites);
            } else if constexpr (std::is_same_v<T, AstFunctionExpr>) {
                if (value.expressionBody) {
                    collectExprReferenceSites(*value.expressionBody, outSites);
                }
                if (value.blockBody) {
                    collectStmtReferenceSites(*value.blockBody, outSites);
                }
            } else if constexpr (std::is_same_v<T, AstArrayLiteralExpr>) {
                for (const auto& element : value.elements) {
                    collectExprReferenceSites(*element, outSites);
                }
            } else if constexpr (std::is_same_v<T, AstDictLiteralExpr>) {
                for (const auto& entry : value.entries) {
                    collectExprReferenceSites(*entry.key, outSites);
                    collectExprReferenceSites(*entry.value, outSites);
                }
            }
        },
        expr.value);
}

void collectItemReferenceSites(
    const AstItem& item, std::unordered_map<AstNodeId, SourceSpan>& outSites) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, AstFunctionDecl>) {
                if (value.body) {
                    collectStmtReferenceSites(*value.body, outSites);
                }
            } else if constexpr (std::is_same_v<T, AstClassDecl>) {
                for (const auto& method : value.methods) {
                    if (method.body) {
                        collectStmtReferenceSites(*method.body, outSites);
                    }
                }
            } else if constexpr (std::is_same_v<T, AstStmtPtr>) {
                if (value) {
                    collectStmtReferenceSites(*value, outSites);
                }
            }
        },
        item.value);
}

void collectStmtReferenceSites(
    const AstStmt& stmt, std::unordered_map<AstNodeId, SourceSpan>& outSites) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, AstBlockStmt>) {
                for (const auto& item : value.items) {
                    if (item) {
                        collectItemReferenceSites(*item, outSites);
                    }
                }
            } else if constexpr (std::is_same_v<T, AstExprStmt>) {
                collectExprReferenceSites(*value.expression, outSites);
            } else if constexpr (std::is_same_v<T, AstPrintStmt>) {
                collectExprReferenceSites(*value.expression, outSites);
            } else if constexpr (std::is_same_v<T, AstReturnStmt>) {
                if (value.value) {
                    collectExprReferenceSites(*value.value, outSites);
                }
            } else if constexpr (std::is_same_v<T, AstIfStmt>) {
                collectExprReferenceSites(*value.condition, outSites);
                collectStmtReferenceSites(*value.thenBranch, outSites);
                if (value.elseBranch) {
                    collectStmtReferenceSites(*value.elseBranch, outSites);
                }
            } else if constexpr (std::is_same_v<T, AstWhileStmt>) {
                collectExprReferenceSites(*value.condition, outSites);
                collectStmtReferenceSites(*value.body, outSites);
            } else if constexpr (std::is_same_v<T, AstVarDeclStmt>) {
                if (value.initializer) {
                    collectExprReferenceSites(*value.initializer, outSites);
                }
            } else if constexpr (std::is_same_v<T, AstDestructuredImportStmt>) {
                if (value.initializer) {
                    collectExprReferenceSites(*value.initializer, outSites);
                }
            } else if constexpr (std::is_same_v<T, AstForStmt>) {
                if (const auto* initDecl =
                        std::get_if<std::unique_ptr<AstVarDeclStmt>>(
                            &value.initializer)) {
                    if (*initDecl && (*initDecl)->initializer) {
                        collectExprReferenceSites(*(*initDecl)->initializer,
                                                  outSites);
                    }
                } else if (const auto* initExpr =
                               std::get_if<AstExprPtr>(&value.initializer)) {
                    if (*initExpr) {
                        collectExprReferenceSites(**initExpr, outSites);
                    }
                }
                if (value.condition) {
                    collectExprReferenceSites(*value.condition, outSites);
                }
                if (value.increment) {
                    collectExprReferenceSites(*value.increment, outSites);
                }
                collectStmtReferenceSites(*value.body, outSites);
            } else if constexpr (std::is_same_v<T, AstForEachStmt>) {
                collectExprReferenceSites(*value.iterable, outSites);
                collectStmtReferenceSites(*value.body, outSites);
            }
        },
        stmt.value);
}

void collectReferenceSites(
    const AstModule& module, std::unordered_map<AstNodeId, SourceSpan>& outSites) {
    for (const auto& item : module.items) {
        if (item) {
            collectItemReferenceSites(*item, outSites);
        }
    }
}

void collectItemImportBindingSites(
    const AstItem& item,
    const std::unordered_map<AstNodeId, AstImportedModuleInterface>& importedModules,
    std::unordered_map<AstNodeId, ImportBindingSite>& outSites) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, AstFunctionDecl>) {
                if (value.body) {
                    collectStmtImportBindingSites(*value.body, importedModules,
                                                  outSites);
                }
            } else if constexpr (std::is_same_v<T, AstClassDecl>) {
                for (const auto& method : value.methods) {
                    if (method.body) {
                        collectStmtImportBindingSites(*method.body,
                                                      importedModules, outSites);
                    }
                }
            } else if constexpr (std::is_same_v<T, AstStmtPtr>) {
                if (value) {
                    collectStmtImportBindingSites(*value, importedModules,
                                                  outSites);
                }
            }
        },
        item.value);
}

void collectStmtImportBindingSites(
    const AstStmt& stmt,
    const std::unordered_map<AstNodeId, AstImportedModuleInterface>& importedModules,
    std::unordered_map<AstNodeId, ImportBindingSite>& outSites) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, AstBlockStmt>) {
                for (const auto& item : value.items) {
                    if (item) {
                        collectItemImportBindingSites(*item, importedModules,
                                                      outSites);
                    }
                }
            } else if constexpr (std::is_same_v<T, AstIfStmt>) {
                collectStmtImportBindingSites(*value.thenBranch, importedModules,
                                              outSites);
                if (value.elseBranch) {
                    collectStmtImportBindingSites(*value.elseBranch,
                                                  importedModules, outSites);
                }
            } else if constexpr (std::is_same_v<T, AstWhileStmt>) {
                collectStmtImportBindingSites(*value.body, importedModules,
                                              outSites);
            } else if constexpr (std::is_same_v<T, AstDestructuredImportStmt>) {
                const AstImportedModuleInterface* importedModule =
                    value.initializer
                        ? [&]() -> const AstImportedModuleInterface* {
                              auto it =
                                  importedModules.find(value.initializer->node.id);
                              return it == importedModules.end() ? nullptr
                                                                 : &it->second;
                          }()
                        : nullptr;
                if (importedModule == nullptr) {
                    return;
                }

                for (const auto& binding : value.bindings) {
                    outSites[binding.node.id] = ImportBindingSite{
                        tokenText(binding.exportedName),
                        importedModule->importTarget,
                    };
                }
            } else if constexpr (std::is_same_v<T, AstForStmt>) {
                collectStmtImportBindingSites(*value.body, importedModules,
                                              outSites);
            } else if constexpr (std::is_same_v<T, AstForEachStmt>) {
                collectStmtImportBindingSites(*value.body, importedModules,
                                              outSites);
            }
        },
        stmt.value);
}

void collectImportBindingSites(
    const AstModule& module,
    const std::unordered_map<AstNodeId, AstImportedModuleInterface>& importedModules,
    std::unordered_map<AstNodeId, ImportBindingSite>& outSites) {
    for (const auto& item : module.items) {
        if (item) {
            collectItemImportBindingSites(*item, importedModules, outSites);
        }
    }
}

void collectExportedDeclarationSites(
    const AstModule& module,
    std::unordered_map<std::string, DeclarationSite>& outSites) {
    std::unordered_map<AstNodeId, DeclarationSite> declarations;
    collectDeclarationSites(module, declarations);

    for (const auto& item : module.items) {
        if (!item) {
            continue;
        }

        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;

                auto indexDeclaration = [&](AstNodeId nodeId) {
                    auto declarationIt = declarations.find(nodeId);
                    if (declarationIt == declarations.end() ||
                        !isPublicSymbolName(declarationIt->second.name)) {
                        return;
                    }
                    outSites[declarationIt->second.name] = declarationIt->second;
                };

                if constexpr (std::is_same_v<T, AstFunctionDecl> ||
                              std::is_same_v<T, AstClassDecl> ||
                              std::is_same_v<T, AstTypeAliasDecl>) {
                    indexDeclaration(value.node.id);
                } else if constexpr (std::is_same_v<T, AstStmtPtr>) {
                    if (!value) {
                        return;
                    }

                    const auto* varDecl = std::get_if<AstVarDeclStmt>(&value->value);
                    if (varDecl == nullptr) {
                        return;
                    }
                    indexDeclaration(value->node.id);
                }
            },
            item->value);
    }
}

std::optional<std::string> readFileText(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof()) {
        return std::nullopt;
    }

    return buffer.str();
}

std::optional<ToolingDocumentAnalysis> analyzeSourceModuleForTooling(
    const std::string& path, const ToolingDocumentAnalysis& analysis) {
    const auto text = readFileText(path);
    if (!text.has_value()) {
        return std::nullopt;
    }

    ToolingAnalyzeOptions options;
    options.sourcePath = path;
    options.packageSearchPaths = analysis.packageSearchPaths;
    options.moduleGraphCache = nullptr;
    options.strictMode = toolingSourceStartsWithStrictDirective(*text);
    return analyzeDocumentForTooling(*text, options);
}

std::optional<TypeRef> declarationTypeForTooling(
    const ToolingDocumentAnalysis& analysis, const DeclarationSite& declaration) {
    const auto nodeTypeIt =
        analysis.frontend.semanticModel.nodeTypes.find(declaration.nodeId);
    if (nodeTypeIt != analysis.frontend.semanticModel.nodeTypes.end() &&
        nodeTypeIt->second) {
        return nodeTypeIt->second;
    }

    if (declaration.kind == "function") {
        const auto it = analysis.frontend.functionSignatures.find(declaration.name);
        if (it != analysis.frontend.functionSignatures.end() && it->second) {
            return it->second;
        }
    } else if (declaration.kind == "type") {
        const auto it = analysis.frontend.typeAliases.find(declaration.name);
        if (it != analysis.frontend.typeAliases.end() && it->second) {
            return it->second;
        }
    } else if (declaration.kind == "class") {
        return TypeInfo::makeClass(declaration.name);
    }

    return std::nullopt;
}

std::string hoverDetailForDeclaration(const ToolingDocumentAnalysis& analysis,
                                      const DeclarationSite& declaration) {
    const auto declarationType = declarationTypeForTooling(analysis, declaration);
    const std::string typeText =
        declarationType.has_value() && *declarationType
            ? (*declarationType)->toString()
            : std::string("any");

    if (declaration.kind == "function") {
        return "fn " + declaration.name + ": " + typeText;
    }
    if (declaration.kind == "type") {
        return "type " + declaration.name + " = " + typeText;
    }
    if (declaration.kind == "class") {
        return "class " + declaration.name;
    }
    if (declaration.kind == "parameter") {
        return "parameter " + declaration.name + ": " + typeText;
    }
    if (declaration.kind == "constant") {
        return "const " + declaration.name + ": " + typeText;
    }
    if (declaration.kind == "import") {
        return "import " + declaration.name + ": " + typeText;
    }
    return "var " + declaration.name + ": " + typeText;
}

std::string completionSortText(int group, const std::string& label) {
    return std::to_string(group) + "-" + label;
}

const std::vector<ToolingCompletionItem>& keywordCompletionItems() {
    static const std::vector<ToolingCompletionItem> items = {
        {"@import", "keyword", "import module", completionSortText(1, "@import")},
        {"as", "keyword", "cast operator", completionSortText(1, "as")},
        {"bool", "type", "built-in type", completionSortText(1, "bool")},
        {"const", "keyword", "binding declaration", completionSortText(1, "const")},
        {"else", "keyword", "control flow", completionSortText(1, "else")},
        {"f32", "type", "built-in type", completionSortText(1, "f32")},
        {"f64", "type", "built-in type", completionSortText(1, "f64")},
        {"false", "keyword", "boolean literal", completionSortText(1, "false")},
        {"fn", "keyword", "function declaration", completionSortText(1, "fn")},
        {"for", "keyword", "loop", completionSortText(1, "for")},
        {"i16", "type", "built-in type", completionSortText(1, "i16")},
        {"i32", "type", "built-in type", completionSortText(1, "i32")},
        {"i64", "type", "built-in type", completionSortText(1, "i64")},
        {"i8", "type", "built-in type", completionSortText(1, "i8")},
        {"if", "keyword", "control flow", completionSortText(1, "if")},
        {"null", "keyword", "null literal", completionSortText(1, "null")},
        {"print", "keyword", "debug print", completionSortText(1, "print")},
        {"return", "keyword", "function return", completionSortText(1, "return")},
        {"str", "type", "built-in type", completionSortText(1, "str")},
        {"struct", "keyword", "type declaration", completionSortText(1, "struct")},
        {"super", "keyword", "super receiver", completionSortText(1, "super")},
        {"this", "keyword", "instance receiver", completionSortText(1, "this")},
        {"true", "keyword", "boolean literal", completionSortText(1, "true")},
        {"type", "keyword", "type alias declaration", completionSortText(1, "type")},
        {"u16", "type", "built-in type", completionSortText(1, "u16")},
        {"u32", "type", "built-in type", completionSortText(1, "u32")},
        {"u64", "type", "built-in type", completionSortText(1, "u64")},
        {"u8", "type", "built-in type", completionSortText(1, "u8")},
        {"usize", "type", "built-in type", completionSortText(1, "usize")},
        {"var", "keyword", "binding declaration", completionSortText(1, "var")},
        {"void", "type", "built-in type", completionSortText(1, "void")},
        {"while", "keyword", "loop", completionSortText(1, "while")},
    };
    return items;
}

ToolingCompletionItem completionItemForDeclaration(
    const ToolingDocumentAnalysis& analysis, const DeclarationSite& declaration) {
    return ToolingCompletionItem{declaration.name,
                                 declaration.kind,
                                 hoverDetailForDeclaration(analysis, declaration),
                                 completionSortText(0, declaration.name)};
}

class CompletionCollector {
   public:
    CompletionCollector(const ToolingDocumentAnalysis& analysis,
                        const ToolingPosition& position)
        : m_analysis(analysis),
          m_position(sourcePositionFromToolingPosition(position)) {}

    std::vector<ToolingCompletionItem> collect() {
        m_scopes.emplace_back();
        if (m_analysis.hasParse) {
            predeclareTopLevel(m_analysis.frontend.module);
            descendItems(m_analysis.frontend.module.items);
        }
        if (!m_captured) {
            captureVisible();
        }
        return m_results;
    }

   private:
    const ToolingDocumentAnalysis& m_analysis;
    SourcePosition m_position;
    std::vector<std::unordered_map<std::string, DeclarationSite>> m_scopes;
    std::vector<ToolingCompletionItem> m_results;
    bool m_captured = false;

    void beginScope() { m_scopes.emplace_back(); }

    void endScope() {
        if (m_scopes.size() > 1) {
            m_scopes.pop_back();
        }
    }

    void defineBinding(const DeclarationSite& declaration) {
        m_scopes.back()[declaration.name] = declaration;
    }

    void predeclareTopLevel(const AstModule& module) {
        for (const auto& item : module.items) {
            if (!item) {
                continue;
            }

            std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;

                    if constexpr (std::is_same_v<T, AstFunctionDecl>) {
                        defineBinding(functionDeclarationSite(value));
                    } else if constexpr (std::is_same_v<T, AstClassDecl>) {
                        defineBinding(classDeclarationSite(value));
                    } else if constexpr (std::is_same_v<T, AstTypeAliasDecl>) {
                        defineBinding(typeAliasDeclarationSite(value));
                    }
                },
                item->value);
        }
    }

    std::vector<ToolingCompletionItem> snapshotVisible() const {
        std::unordered_set<std::string> seen;
        std::vector<ToolingCompletionItem> items;

        for (auto scopeIt = m_scopes.rbegin(); scopeIt != m_scopes.rend();
             ++scopeIt) {
            std::vector<DeclarationSite> declarations;
            declarations.reserve(scopeIt->size());
            for (const auto& entry : *scopeIt) {
                declarations.push_back(entry.second);
            }
            std::sort(declarations.begin(), declarations.end(),
                      [](const DeclarationSite& lhs, const DeclarationSite& rhs) {
                          return lhs.name < rhs.name;
                      });

            for (const auto& declaration : declarations) {
                if (!seen.insert(declaration.name).second) {
                    continue;
                }
                items.push_back(
                    completionItemForDeclaration(m_analysis, declaration));
            }
        }

        for (const auto& keyword : keywordCompletionItems()) {
            if (seen.insert(keyword.label).second) {
                items.push_back(keyword);
            }
        }

        return items;
    }

    void captureVisible() {
        if (m_captured) {
            return;
        }
        m_results = snapshotVisible();
        m_captured = true;
    }

    void descendItems(const std::vector<AstItemPtr>& items) {
        for (const auto& item : items) {
            if (!item) {
                continue;
            }

            if (positionLess(m_position, item->node.span.start)) {
                captureVisible();
                return;
            }

            if (itemContainsPosition(*item, m_position)) {
                descendItem(*item);
                if (!m_captured) {
                    captureVisible();
                }
                return;
            }

            declareCompletedItem(*item);
        }

        captureVisible();
    }

    void descendFunctionBody(const AstStmt& body) {
        if (const auto* block = std::get_if<AstBlockStmt>(&body.value)) {
            descendItems(block->items);
            return;
        }

        descendStmt(body);
    }

    void descendItem(const AstItem& item) {
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, AstFunctionDecl>) {
                    if (!value.body) {
                        return;
                    }

                    defineBinding(functionDeclarationSite(value));
                    beginScope();
                    for (const auto& param : value.params) {
                        defineBinding(parameterDeclarationSite(param));
                    }
                    if (containsPosition(value.body->node.span, m_position)) {
                        descendFunctionBody(*value.body);
                    }
                    if (!m_captured) {
                        captureVisible();
                    }
                    endScope();
                } else if constexpr (std::is_same_v<T, AstClassDecl>) {
                    defineBinding(classDeclarationSite(value));
                    for (const auto& method : value.methods) {
                        if (!method.body ||
                            !containsPosition(method.body->node.span, m_position)) {
                            continue;
                        }

                        beginScope();
                        for (const auto& param : method.params) {
                            defineBinding(parameterDeclarationSite(param));
                        }
                        descendFunctionBody(*method.body);
                        if (!m_captured) {
                            captureVisible();
                        }
                        endScope();
                        return;
                    }
                } else if constexpr (std::is_same_v<T, AstTypeAliasDecl>) {
                    defineBinding(typeAliasDeclarationSite(value));
                } else if constexpr (std::is_same_v<T, AstStmtPtr>) {
                    if (value) {
                        descendStmt(*value);
                    }
                }
            },
            item.value);
    }

    void declareCompletedItem(const AstItem& item) {
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, AstFunctionDecl>) {
                    defineBinding(functionDeclarationSite(value));
                } else if constexpr (std::is_same_v<T, AstClassDecl>) {
                    defineBinding(classDeclarationSite(value));
                } else if constexpr (std::is_same_v<T, AstTypeAliasDecl>) {
                    defineBinding(typeAliasDeclarationSite(value));
                } else if constexpr (std::is_same_v<T, AstStmtPtr>) {
                    if (value) {
                        declareCompletedStmt(*value);
                    }
                }
            },
            item.value);
    }

    void declareCompletedStmt(const AstStmt& stmt) {
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, AstVarDeclStmt>) {
                    defineBinding(variableDeclarationSite(stmt.node, value.name,
                                                          value.isConst));
                } else if constexpr (std::is_same_v<T, AstDestructuredImportStmt>) {
                    for (const auto& binding : value.bindings) {
                        defineBinding(importBindingDeclarationSite(binding));
                    }
                }
            },
            stmt.value);
    }

    void descendStmt(const AstStmt& stmt) {
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, AstBlockStmt>) {
                    beginScope();
                    descendItems(value.items);
                    if (!m_captured) {
                        captureVisible();
                    }
                    endScope();
                } else if constexpr (std::is_same_v<T, AstExprStmt>) {
                    if (value.expression &&
                        containsPosition(value.expression->node.span, m_position)) {
                        descendExpr(*value.expression);
                    }
                } else if constexpr (std::is_same_v<T, AstPrintStmt>) {
                    if (value.expression &&
                        containsPosition(value.expression->node.span, m_position)) {
                        descendExpr(*value.expression);
                    }
                } else if constexpr (std::is_same_v<T, AstReturnStmt>) {
                    if (value.value &&
                        containsPosition(value.value->node.span, m_position)) {
                        descendExpr(*value.value);
                    }
                } else if constexpr (std::is_same_v<T, AstIfStmt>) {
                    if (value.condition &&
                        containsPosition(value.condition->node.span, m_position)) {
                        descendExpr(*value.condition);
                        return;
                    }
                    if (value.thenBranch &&
                        containsPosition(value.thenBranch->node.span, m_position)) {
                        descendStmt(*value.thenBranch);
                        return;
                    }
                    if (value.elseBranch &&
                        containsPosition(value.elseBranch->node.span, m_position)) {
                        descendStmt(*value.elseBranch);
                    }
                } else if constexpr (std::is_same_v<T, AstWhileStmt>) {
                    if (value.condition &&
                        containsPosition(value.condition->node.span, m_position)) {
                        descendExpr(*value.condition);
                        return;
                    }
                    if (value.body &&
                        containsPosition(value.body->node.span, m_position)) {
                        descendStmt(*value.body);
                    }
                } else if constexpr (std::is_same_v<T, AstVarDeclStmt>) {
                    if (value.initializer &&
                        containsPosition(value.initializer->node.span, m_position)) {
                        descendExpr(*value.initializer);
                    }
                } else if constexpr (std::is_same_v<T,
                                                    AstDestructuredImportStmt>) {
                    if (value.initializer &&
                        containsPosition(value.initializer->node.span, m_position)) {
                        descendExpr(*value.initializer);
                    }
                } else if constexpr (std::is_same_v<T, AstForStmt>) {
                    beginScope();

                    bool initializerContainsPosition = false;
                    if (const auto* initDecl =
                            std::get_if<std::unique_ptr<AstVarDeclStmt>>(
                                &value.initializer)) {
                        if (*initDecl && (*initDecl)->initializer &&
                            containsPosition((*initDecl)->initializer->node.span,
                                             m_position)) {
                            initializerContainsPosition = true;
                            descendExpr(*(*initDecl)->initializer);
                        } else if (*initDecl &&
                                   positionLessOrEqual(
                                       (*initDecl)->name.span().end, m_position)) {
                            defineBinding(variableDeclarationSite(
                                (*initDecl)->node, (*initDecl)->name,
                                (*initDecl)->isConst));
                        }
                    } else if (const auto* initExpr =
                                   std::get_if<AstExprPtr>(&value.initializer)) {
                        if (*initExpr &&
                            containsPosition((*initExpr)->node.span, m_position)) {
                            initializerContainsPosition = true;
                            descendExpr(**initExpr);
                        }
                    }

                    if (!initializerContainsPosition && value.condition &&
                        containsPosition(value.condition->node.span, m_position)) {
                        descendExpr(*value.condition);
                        endScope();
                        return;
                    }
                    if (!initializerContainsPosition && value.increment &&
                        containsPosition(value.increment->node.span, m_position)) {
                        descendExpr(*value.increment);
                        endScope();
                        return;
                    }
                    if (!initializerContainsPosition && value.body &&
                        containsPosition(value.body->node.span, m_position)) {
                        descendStmt(*value.body);
                    }

                    if (!m_captured) {
                        captureVisible();
                    }
                    endScope();
                } else if constexpr (std::is_same_v<T, AstForEachStmt>) {
                    beginScope();
                    if (value.iterable &&
                        containsPosition(value.iterable->node.span, m_position)) {
                        descendExpr(*value.iterable);
                        endScope();
                        return;
                    }

                    if (positionLessOrEqual(value.name.span().end, m_position)) {
                        defineBinding(variableDeclarationSite(stmt.node, value.name,
                                                              value.isConst));
                    }

                    if (value.body &&
                        containsPosition(value.body->node.span, m_position)) {
                        descendStmt(*value.body);
                    }

                    if (!m_captured) {
                        captureVisible();
                    }
                    endScope();
                }
            },
            stmt.value);
    }

    void descendExpr(const AstExpr& expr) {
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, AstGroupingExpr>) {
                    if (value.expression &&
                        containsPosition(value.expression->node.span, m_position)) {
                        descendExpr(*value.expression);
                    }
                } else if constexpr (std::is_same_v<T, AstUnaryExpr> ||
                                     std::is_same_v<T, AstUpdateExpr>) {
                    if (value.operand &&
                        containsPosition(value.operand->node.span, m_position)) {
                        descendExpr(*value.operand);
                    }
                } else if constexpr (std::is_same_v<T, AstBinaryExpr>) {
                    if (value.left &&
                        containsPosition(value.left->node.span, m_position)) {
                        descendExpr(*value.left);
                        return;
                    }
                    if (value.right &&
                        containsPosition(value.right->node.span, m_position)) {
                        descendExpr(*value.right);
                    }
                } else if constexpr (std::is_same_v<T, AstAssignmentExpr>) {
                    if (value.target &&
                        containsPosition(value.target->node.span, m_position)) {
                        descendExpr(*value.target);
                        return;
                    }
                    if (value.value &&
                        containsPosition(value.value->node.span, m_position)) {
                        descendExpr(*value.value);
                    }
                } else if constexpr (std::is_same_v<T, AstCallExpr>) {
                    if (value.callee &&
                        containsPosition(value.callee->node.span, m_position)) {
                        descendExpr(*value.callee);
                        return;
                    }
                    for (const auto& argument : value.arguments) {
                        if (argument &&
                            containsPosition(argument->node.span, m_position)) {
                            descendExpr(*argument);
                            return;
                        }
                    }
                } else if constexpr (std::is_same_v<T, AstMemberExpr>) {
                    if (value.object &&
                        containsPosition(value.object->node.span, m_position)) {
                        descendExpr(*value.object);
                    }
                } else if constexpr (std::is_same_v<T, AstIndexExpr>) {
                    if (value.object &&
                        containsPosition(value.object->node.span, m_position)) {
                        descendExpr(*value.object);
                        return;
                    }
                    if (value.index &&
                        containsPosition(value.index->node.span, m_position)) {
                        descendExpr(*value.index);
                    }
                } else if constexpr (std::is_same_v<T, AstCastExpr>) {
                    if (value.expression &&
                        containsPosition(value.expression->node.span, m_position)) {
                        descendExpr(*value.expression);
                    }
                } else if constexpr (std::is_same_v<T, AstFunctionExpr>) {
                    beginScope();
                    for (const auto& param : value.params) {
                        defineBinding(parameterDeclarationSite(param));
                    }

                    if (value.expressionBody &&
                        containsPosition(value.expressionBody->node.span,
                                         m_position)) {
                        descendExpr(*value.expressionBody);
                    } else if (value.blockBody &&
                               containsPosition(value.blockBody->node.span,
                                                m_position)) {
                        descendFunctionBody(*value.blockBody);
                    }

                    if (!m_captured) {
                        captureVisible();
                    }
                    endScope();
                } else if constexpr (std::is_same_v<T, AstArrayLiteralExpr>) {
                    for (const auto& element : value.elements) {
                        if (element &&
                            containsPosition(element->node.span, m_position)) {
                            descendExpr(*element);
                            return;
                        }
                    }
                } else if constexpr (std::is_same_v<T, AstDictLiteralExpr>) {
                    for (const auto& entry : value.entries) {
                        if (entry.key &&
                            containsPosition(entry.key->node.span, m_position)) {
                            descendExpr(*entry.key);
                            return;
                        }
                        if (entry.value &&
                            containsPosition(entry.value->node.span, m_position)) {
                            descendExpr(*entry.value);
                            return;
                        }
                    }
                }
            },
            expr.value);
    }
};

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

std::optional<SymbolTarget> resolveSymbolTarget(
    const ToolingDocumentAnalysis& analysis, const ToolingPosition& position) {
    if (!analysis.hasParse || !analysis.hasBindings) {
        return std::nullopt;
    }

    const SourcePosition sourcePosition = sourcePositionFromToolingPosition(position);

    std::unordered_map<AstNodeId, DeclarationSite> declarationSites;
    collectDeclarationSites(analysis.frontend.module, declarationSites);
    for (const auto& [nodeId, declaration] : declarationSites) {
        (void)nodeId;
        if (containsPosition(declaration.selectionRange, sourcePosition)) {
            return SymbolTarget{declaration.nodeId, declaration.selectionRange};
        }
    }

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

    return SymbolTarget{bindingIt->second.declarationNodeId,
                        identifierExpr->name.span()};
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
    return makeSourcePosition(0, position.line + 1, position.character + 1);
}

SourceSpan sourceSpanFromToolingRange(const ToolingRange& range) {
    return SourceSpan{sourcePositionFromToolingPosition(range.start),
                      sourcePositionFromToolingPosition(range.end)};
}

ToolingDocumentAnalysis analyzeDocumentForTooling(
    std::string_view source, const ToolingAnalyzeOptions& options) {
    ToolingDocumentAnalysis analysis;
    analysis.sourcePath = options.sourcePath;
    analysis.packageSearchPaths = options.packageSearchPaths;
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
    const auto target = resolveSymbolTarget(analysis, position);
    if (!target.has_value()) {
        return std::nullopt;
    }

    std::unordered_map<AstNodeId, DeclarationSite> declarationSites;
    collectDeclarationSites(analysis.frontend.module, declarationSites);

    const auto declarationIt = declarationSites.find(target->declarationNodeId);
    if (declarationIt == declarationSites.end()) {
        return std::nullopt;
    }

    std::unordered_map<AstNodeId, ImportBindingSite> importBindingSites;
    collectImportBindingSites(analysis.frontend.module,
                              analysis.frontend.bindings.importedModules,
                              importBindingSites);
    const auto importIt = importBindingSites.find(target->declarationNodeId);
    if (importIt != importBindingSites.end() &&
        importIt->second.importTarget.kind == ImportTargetKind::SOURCE_MODULE &&
        !importIt->second.importTarget.resolvedPath.empty()) {
        const auto importedAnalysis = analyzeSourceModuleForTooling(
            importIt->second.importTarget.resolvedPath, analysis);
        if (importedAnalysis.has_value() && importedAnalysis->hasParse) {
            std::unordered_map<std::string, DeclarationSite> exportedDeclarations;
            collectExportedDeclarationSites(importedAnalysis->frontend.module,
                                           exportedDeclarations);
            const auto exportedIt =
                exportedDeclarations.find(importIt->second.exportedName);
            if (exportedIt != exportedDeclarations.end()) {
                return ToolingLocation{
                    importIt->second.importTarget.resolvedPath,
                    toolingRangeFromSourceSpan(exportedIt->second.range),
                    toolingRangeFromSourceSpan(
                        exportedIt->second.selectionRange),
                };
            }
        }
    }

    return ToolingLocation{analysis.sourcePath,
                           toolingRangeFromSourceSpan(declarationIt->second.range),
                           toolingRangeFromSourceSpan(
                               declarationIt->second.selectionRange)};
}

std::vector<ToolingLocation> findReferencesForTooling(
    const ToolingDocumentAnalysis& analysis, const ToolingPosition& position) {
    std::vector<ToolingLocation> results;
    const auto target = resolveSymbolTarget(analysis, position);
    if (!target.has_value()) {
        return results;
    }

    std::unordered_map<AstNodeId, DeclarationSite> declarationSites;
    collectDeclarationSites(analysis.frontend.module, declarationSites);
    const auto declarationIt = declarationSites.find(target->declarationNodeId);
    if (declarationIt == declarationSites.end()) {
        return results;
    }

    results.push_back(ToolingLocation{
        analysis.sourcePath,
        toolingRangeFromSourceSpan(declarationIt->second.range),
        toolingRangeFromSourceSpan(declarationIt->second.selectionRange),
    });

    std::unordered_map<AstNodeId, SourceSpan> referenceSites;
    collectReferenceSites(analysis.frontend.module, referenceSites);

    std::vector<ToolingLocation> usageLocations;
    for (const auto& [nodeId, binding] : analysis.frontend.bindings.references) {
        if (binding.declarationNodeId != target->declarationNodeId) {
            continue;
        }

        const auto referenceIt = referenceSites.find(nodeId);
        if (referenceIt == referenceSites.end()) {
            continue;
        }

        usageLocations.push_back(ToolingLocation{
            analysis.sourcePath,
            toolingRangeFromSourceSpan(referenceIt->second),
            toolingRangeFromSourceSpan(referenceIt->second),
        });
    }

    std::sort(usageLocations.begin(), usageLocations.end(),
              [](const ToolingLocation& lhs, const ToolingLocation& rhs) {
                  if (lhs.path != rhs.path) {
                      return lhs.path < rhs.path;
                  }
                  if (lhs.selectionRange.start.line != rhs.selectionRange.start.line) {
                      return lhs.selectionRange.start.line <
                             rhs.selectionRange.start.line;
                  }
                  return lhs.selectionRange.start.character <
                         rhs.selectionRange.start.character;
              });
    results.insert(results.end(), usageLocations.begin(), usageLocations.end());
    return results;
}

std::optional<ToolingHover> findHoverForTooling(
    const ToolingDocumentAnalysis& analysis, const ToolingPosition& position) {
    const auto target = resolveSymbolTarget(analysis, position);
    if (!target.has_value()) {
        return std::nullopt;
    }

    std::unordered_map<AstNodeId, DeclarationSite> declarationSites;
    collectDeclarationSites(analysis.frontend.module, declarationSites);
    const auto declarationIt = declarationSites.find(target->declarationNodeId);
    if (declarationIt == declarationSites.end()) {
        return std::nullopt;
    }

    return ToolingHover{
        toolingRangeFromSourceSpan(target->occurrenceSpan),
        declarationIt->second.name,
        declarationIt->second.kind,
        hoverDetailForDeclaration(analysis, declarationIt->second),
    };
}

std::vector<ToolingCompletionItem> findCompletionsForTooling(
    const ToolingDocumentAnalysis& analysis, const ToolingPosition& position) {
    return CompletionCollector(analysis, position).collect();
}
