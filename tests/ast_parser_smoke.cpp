#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>
#include <variant>

#include "Ast.hpp"
#include "AstParser.hpp"

namespace {

struct AstStats {
    size_t topLevelFunctions = 0;
    size_t classMethods = 0;
    size_t lambdaExprs = 0;
    size_t importExprs = 0;
    size_t forLoops = 0;
    size_t forEachLoops = 0;
    size_t whileLoops = 0;
    size_t ifStatements = 0;
};

void walkExpr(const AstExpr& expr, AstStats& stats);
void walkStmt(const AstStmt& stmt, AstStats& stats);
void walkItem(const AstItem& item, AstStats& stats);

void walkExpr(const AstExpr& expr, AstStats& stats) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, AstGroupingExpr>) {
                walkExpr(*value.expression, stats);
            } else if constexpr (std::is_same_v<T, AstUnaryExpr>) {
                walkExpr(*value.operand, stats);
            } else if constexpr (std::is_same_v<T, AstUpdateExpr>) {
                walkExpr(*value.operand, stats);
            } else if constexpr (std::is_same_v<T, AstBinaryExpr>) {
                walkExpr(*value.left, stats);
                walkExpr(*value.right, stats);
            } else if constexpr (std::is_same_v<T, AstAssignmentExpr>) {
                walkExpr(*value.target, stats);
                walkExpr(*value.value, stats);
            } else if constexpr (std::is_same_v<T, AstCallExpr>) {
                walkExpr(*value.callee, stats);
                for (const auto& arg : value.arguments) {
                    walkExpr(*arg, stats);
                }
            } else if constexpr (std::is_same_v<T, AstMemberExpr>) {
                walkExpr(*value.object, stats);
            } else if constexpr (std::is_same_v<T, AstIndexExpr>) {
                walkExpr(*value.object, stats);
                walkExpr(*value.index, stats);
            } else if constexpr (std::is_same_v<T, AstCastExpr>) {
                walkExpr(*value.expression, stats);
            } else if constexpr (std::is_same_v<T, AstFunctionExpr>) {
                ++stats.lambdaExprs;
                if (value.expressionBody) {
                    walkExpr(*value.expressionBody, stats);
                }
                if (value.blockBody) {
                    walkStmt(*value.blockBody, stats);
                }
            } else if constexpr (std::is_same_v<T, AstImportExpr>) {
                ++stats.importExprs;
            } else if constexpr (std::is_same_v<T, AstArrayLiteralExpr>) {
                for (const auto& element : value.elements) {
                    walkExpr(*element, stats);
                }
            } else if constexpr (std::is_same_v<T, AstDictLiteralExpr>) {
                for (const auto& entry : value.entries) {
                    walkExpr(*entry.key, stats);
                    walkExpr(*entry.value, stats);
                }
            }
        },
        expr.value);
}

void walkStmt(const AstStmt& stmt, AstStats& stats) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, AstBlockStmt>) {
                for (const auto& item : value.items) {
                    walkItem(*item, stats);
                }
            } else if constexpr (std::is_same_v<T, AstExprStmt>) {
                walkExpr(*value.expression, stats);
            } else if constexpr (std::is_same_v<T, AstPrintStmt>) {
                walkExpr(*value.expression, stats);
            } else if constexpr (std::is_same_v<T, AstReturnStmt>) {
                if (value.value) {
                    walkExpr(*value.value, stats);
                }
            } else if constexpr (std::is_same_v<T, AstIfStmt>) {
                ++stats.ifStatements;
                walkExpr(*value.condition, stats);
                walkStmt(*value.thenBranch, stats);
                if (value.elseBranch) {
                    walkStmt(*value.elseBranch, stats);
                }
            } else if constexpr (std::is_same_v<T, AstWhileStmt>) {
                ++stats.whileLoops;
                walkExpr(*value.condition, stats);
                walkStmt(*value.body, stats);
            } else if constexpr (std::is_same_v<T, AstVarDeclStmt>) {
                if (value.initializer) {
                    walkExpr(*value.initializer, stats);
                }
            } else if constexpr (std::is_same_v<T, AstDestructuredImportStmt>) {
                if (value.initializer) {
                    walkExpr(*value.initializer, stats);
                }
            } else if constexpr (std::is_same_v<T, AstForStmt>) {
                ++stats.forLoops;
                if (const auto* initDecl =
                        std::get_if<std::unique_ptr<AstVarDeclStmt>>(&value.initializer)) {
                    if (*initDecl && (*initDecl)->initializer) {
                        walkExpr(*(*initDecl)->initializer, stats);
                    }
                } else if (const auto* initExpr =
                               std::get_if<AstExprPtr>(&value.initializer)) {
                    if (*initExpr) {
                        walkExpr(**initExpr, stats);
                    }
                }
                if (value.condition) {
                    walkExpr(*value.condition, stats);
                }
                if (value.increment) {
                    walkExpr(*value.increment, stats);
                }
                walkStmt(*value.body, stats);
            } else if constexpr (std::is_same_v<T, AstForEachStmt>) {
                ++stats.forEachLoops;
                walkExpr(*value.iterable, stats);
                walkStmt(*value.body, stats);
            }
        },
        stmt.value);
}

void walkItem(const AstItem& item, AstStats& stats) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, AstTypeAliasDecl>) {
                return;
            } else if constexpr (std::is_same_v<T, AstClassDecl>) {
                for (const auto& method : value.methods) {
                    ++stats.classMethods;
                    if (method.body) {
                        walkStmt(*method.body, stats);
                    }
                }
            } else if constexpr (std::is_same_v<T, AstFunctionDecl>) {
                ++stats.topLevelFunctions;
                if (value.body) {
                    walkStmt(*value.body, stats);
                }
            } else if constexpr (std::is_same_v<T, AstStmtPtr>) {
                if (value) {
                    walkStmt(*value, stats);
                }
            }
        },
        item.value);
}

bool parseFile(const std::string& path, AstStats& stats) {
    std::ifstream input(path);
    if (!input) {
        std::cerr << "failed to open " << path << '\n';
        return false;
    }

    std::string source((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());

    AstModule module;
    AstParser parser(source);
    if (!parser.parseModule(module)) {
        std::cerr << "AST parse failed for " << path << '\n';
        return false;
    }

    for (const auto& item : module.items) {
        if (item == nullptr) {
            std::cerr << "null AST item in " << path << '\n';
            return false;
        }
        walkItem(*item, stats);
    }

    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: ast_parser_smoke <file> [file...]\n";
        return 1;
    }

    AstStats stats;
    for (int index = 1; index < argc; ++index) {
        if (!parseFile(argv[index], stats)) {
            return 1;
        }
    }

    if (stats.topLevelFunctions == 0 || stats.classMethods == 0 ||
        stats.lambdaExprs == 0 || stats.importExprs == 0 ||
        stats.forLoops == 0 || stats.forEachLoops == 0 ||
        stats.whileLoops == 0 || stats.ifStatements == 0) {
        std::cerr << "AST smoke coverage was incomplete:\n"
                  << "  topLevelFunctions=" << stats.topLevelFunctions << '\n'
                  << "  classMethods=" << stats.classMethods << '\n'
                  << "  lambdaExprs=" << stats.lambdaExprs << '\n'
                  << "  importExprs=" << stats.importExprs << '\n'
                  << "  forLoops=" << stats.forLoops << '\n'
                  << "  forEachLoops=" << stats.forEachLoops << '\n'
                  << "  whileLoops=" << stats.whileLoops << '\n'
                  << "  ifStatements=" << stats.ifStatements << '\n';
        return 1;
    }

    std::cout << "ast parser smoke passed\n";
    return 0;
}
