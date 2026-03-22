#include "AstOptimizerInternal.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ast_optimizer_detail {

namespace {

void pruneUnreachableBlockItems(AstBlockStmt& block) {
    size_t reachableCount = block.items.size();
    for (size_t index = 0; index < block.items.size(); ++index) {
        if (block.items[index] && isDefinitelyTerminal(*block.items[index])) {
            reachableCount = index + 1;
            break;
        }
    }

    if (reachableCount < block.items.size()) {
        block.items.erase(block.items.begin() +
                              static_cast<ptrdiff_t>(reachableCount),
                          block.items.end());
    }
}

void removeNoOpBlockItems(AstBlockStmt& block) {
    std::vector<AstItemPtr> kept;
    kept.reserve(block.items.size());
    for (auto& item : block.items) {
        if (!item) {
            continue;
        }

        bool keepItem = true;
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, AstStmtPtr>) {
                    keepItem = value && !isEmptyBlockStmt(*value);
                }
            },
            item->value);

        if (keepItem) {
            kept.push_back(std::move(item));
        }
    }

    block.items = std::move(kept);
}

AstStmtPtr takeForInitializerStmt(AstForStmt& loop) {
    if (auto* initDecl =
            std::get_if<std::unique_ptr<AstVarDeclStmt>>(&loop.initializer)) {
        if (!*initDecl) {
            return nullptr;
        }

        AstStmt stmt;
        stmt.node = (*initDecl)->node;
        stmt.value = std::move(**initDecl);
        *initDecl = nullptr;
        loop.initializer = std::monostate{};
        return makeStmtPtr(std::move(stmt));
    }

    auto* initExpr = std::get_if<AstExprPtr>(&loop.initializer);
    if (!initExpr || !*initExpr) {
        return nullptr;
    }

    AstStmt stmt;
    stmt.node = (*initExpr)->node;
    AstExprStmt exprStmt;
    exprStmt.expression = std::move(*initExpr);
    stmt.value = std::move(exprStmt);
    loop.initializer = std::monostate{};
    return makeStmtPtr(std::move(stmt));
}

AstStmtPtr makeForFalseReplacement(AstForStmt& loop, const AstStmt& original) {
    AstBlockStmt block;
    if (AstStmtPtr initializer = takeForInitializerStmt(loop)) {
        block.items.push_back(makeItemPtr(std::move(initializer)));
    }

    AstStmt stmt;
    stmt.node = original.node;
    stmt.value = std::move(block);
    return makeStmtPtr(std::move(stmt));
}

}  // namespace

void optimizeStmtTree(AstStmt& stmt, const ConstantEvaluator& evaluator) {
    std::visit(
        [&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, AstBlockStmt>) {
                for (auto& item : value.items) {
                    if (item) {
                        optimizeItemTree(*item, evaluator);
                    }
                }

                removeNoOpBlockItems(value);
                pruneUnreachableBlockItems(value);
                removeNoOpBlockItems(value);
            } else if constexpr (std::is_same_v<T, AstExprStmt>) {
                if (value.expression) {
                    optimizeExprTree(*value.expression, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstPrintStmt>) {
                if (value.expression) {
                    optimizeExprTree(*value.expression, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstReturnStmt>) {
                if (value.value) {
                    optimizeExprTree(*value.value, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstIfStmt>) {
                if (value.condition) {
                    optimizeExprTree(*value.condition, evaluator);
                }
                if (value.thenBranch) {
                    optimizeStmtTree(*value.thenBranch, evaluator);
                }
                if (value.elseBranch) {
                    optimizeStmtTree(*value.elseBranch, evaluator);
                }

                bool condition = false;
                if (!value.condition ||
                    !tryEvaluateConditionBool(*value.condition, evaluator,
                                              condition)) {
                    return;
                }

                if (condition) {
                    replaceStmtPreservingNode(stmt, std::move(value.thenBranch));
                } else if (value.elseBranch) {
                    replaceStmtPreservingNode(stmt, std::move(value.elseBranch));
                } else {
                    stmt.value = AstBlockStmt{};
                }
            } else if constexpr (std::is_same_v<T, AstWhileStmt>) {
                if (value.condition) {
                    optimizeExprTree(*value.condition, evaluator);
                }
                if (value.body) {
                    optimizeStmtTree(*value.body, evaluator);
                }

                bool condition = false;
                if (value.condition &&
                    tryEvaluateConditionBool(*value.condition, evaluator,
                                             condition) &&
                    !condition) {
                    stmt.value = AstBlockStmt{};
                }
            } else if constexpr (std::is_same_v<T, AstVarDeclStmt>) {
                if (value.initializer) {
                    optimizeExprTree(*value.initializer, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstDestructuredImportStmt>) {
                if (value.initializer) {
                    optimizeExprTree(*value.initializer, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstForStmt>) {
                if (auto* initDecl =
                        std::get_if<std::unique_ptr<AstVarDeclStmt>>(
                            &value.initializer)) {
                    if (*initDecl && (*initDecl)->initializer) {
                        optimizeExprTree(*(*initDecl)->initializer, evaluator);
                    }
                } else if (auto* initExpr =
                               std::get_if<AstExprPtr>(&value.initializer)) {
                    if (*initExpr) {
                        optimizeExprTree(**initExpr, evaluator);
                    }
                }
                if (value.condition) {
                    optimizeExprTree(*value.condition, evaluator);
                }
                if (value.increment) {
                    optimizeExprTree(*value.increment, evaluator);
                }
                if (value.body) {
                    optimizeStmtTree(*value.body, evaluator);
                }

                bool condition = false;
                if (value.condition &&
                    tryEvaluateConditionBool(*value.condition, evaluator,
                                             condition) &&
                    !condition) {
                    replaceStmtPreservingNode(stmt,
                                              makeForFalseReplacement(value, stmt));
                }
            } else if constexpr (std::is_same_v<T, AstForEachStmt>) {
                if (value.iterable) {
                    optimizeExprTree(*value.iterable, evaluator);
                }
                if (value.body) {
                    optimizeStmtTree(*value.body, evaluator);
                }
            }
        },
        stmt.value);
}

void optimizeItemTree(AstItem& item, const ConstantEvaluator& evaluator) {
    std::visit(
        [&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, AstFunctionDecl>) {
                if (value.body) {
                    optimizeStmtTree(*value.body, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstClassDecl>) {
                for (auto& method : value.methods) {
                    if (method.body) {
                        optimizeStmtTree(*method.body, evaluator);
                    }
                }
            } else if constexpr (std::is_same_v<T, AstStmtPtr>) {
                if (value) {
                    optimizeStmtTree(*value, evaluator);
                }
            }
        },
        item.value);
}

}  // namespace ast_optimizer_detail
