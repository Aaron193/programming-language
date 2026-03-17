#include "AstOptimizer.hpp"

#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <variant>

#include "Token.hpp"

namespace {

struct ConstantValue {
    enum class Kind {
        Integer,
        Boolean,
        Null,
    };

    Kind kind = Kind::Null;
    int64_t integerValue = 0;
    bool boolValue = false;
};

bool addOverflow(int64_t lhs, int64_t rhs, int64_t& out) {
    return __builtin_add_overflow(lhs, rhs, &out);
}

bool subOverflow(int64_t lhs, int64_t rhs, int64_t& out) {
    return __builtin_sub_overflow(lhs, rhs, &out);
}

bool mulOverflow(int64_t lhs, int64_t rhs, int64_t& out) {
    return __builtin_mul_overflow(lhs, rhs, &out);
}

bool isUnsuffixedIntegerLiteral(const Token& token, int64_t& outValue) {
    if (token.type() != TokenType::NUMBER) {
        return false;
    }

    std::string text(token.start(), token.length());
    if (text.empty()) {
        return false;
    }

    for (char ch : text) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }

    try {
        size_t parsed = 0;
        long long value = std::stoll(text, &parsed, 10);
        if (parsed != text.size()) {
            return false;
        }
        outValue = static_cast<int64_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

bool evaluateConstant(const AstExpr& expr, ConstantValue& out);

bool evaluateLiteral(const AstLiteralExpr& literal, ConstantValue& out) {
    switch (literal.token.type()) {
        case TokenType::TRUE:
            out.kind = ConstantValue::Kind::Boolean;
            out.boolValue = true;
            return true;
        case TokenType::FALSE:
            out.kind = ConstantValue::Kind::Boolean;
            out.boolValue = false;
            return true;
        case TokenType::_NULL:
        case TokenType::TYPE_NULL_KW:
            out.kind = ConstantValue::Kind::Null;
            return true;
        case TokenType::NUMBER:
            out.kind = ConstantValue::Kind::Integer;
            return isUnsuffixedIntegerLiteral(literal.token, out.integerValue);
        default:
            return false;
    }
}

bool evaluateUnary(const AstUnaryExpr& expr, ConstantValue& out) {
    ConstantValue operand;
    if (!evaluateConstant(*expr.operand, operand)) {
        return false;
    }

    switch (expr.op.type()) {
        case TokenType::BANG:
            if (operand.kind != ConstantValue::Kind::Boolean) {
                return false;
            }
            out.kind = ConstantValue::Kind::Boolean;
            out.boolValue = !operand.boolValue;
            return true;
        case TokenType::MINUS:
            if (operand.kind != ConstantValue::Kind::Integer) {
                return false;
            }
            if (operand.integerValue == std::numeric_limits<int64_t>::min()) {
                return false;
            }
            out.kind = ConstantValue::Kind::Integer;
            out.integerValue = -operand.integerValue;
            return true;
        case TokenType::TILDE:
            if (operand.kind != ConstantValue::Kind::Integer) {
                return false;
            }
            out.kind = ConstantValue::Kind::Integer;
            out.integerValue = ~operand.integerValue;
            return true;
        default:
            return false;
    }
}

bool evaluateBinary(const AstBinaryExpr& expr, ConstantValue& out) {
    ConstantValue left;
    ConstantValue right;
    if (!evaluateConstant(*expr.left, left) || !evaluateConstant(*expr.right, right)) {
        return false;
    }

    switch (expr.op.type()) {
        case TokenType::PLUS:
        case TokenType::MINUS:
        case TokenType::STAR:
        case TokenType::SLASH:
        case TokenType::GREATER:
        case TokenType::GREATER_EQUAL:
        case TokenType::LESS:
        case TokenType::LESS_EQUAL:
        case TokenType::EQUAL_EQUAL:
        case TokenType::BANG_EQUAL:
        case TokenType::LOGICAL_AND:
        case TokenType::LOGICAL_OR:
            break;
        default:
            return false;
    }

    if (left.kind == ConstantValue::Kind::Integer &&
        right.kind == ConstantValue::Kind::Integer) {
        int64_t numericResult = 0;
        switch (expr.op.type()) {
            case TokenType::PLUS:
                if (addOverflow(left.integerValue, right.integerValue,
                                numericResult)) {
                    return false;
                }
                out.kind = ConstantValue::Kind::Integer;
                out.integerValue = numericResult;
                return true;
            case TokenType::MINUS:
                if (subOverflow(left.integerValue, right.integerValue,
                                numericResult)) {
                    return false;
                }
                out.kind = ConstantValue::Kind::Integer;
                out.integerValue = numericResult;
                return true;
            case TokenType::STAR:
                if (mulOverflow(left.integerValue, right.integerValue,
                                numericResult)) {
                    return false;
                }
                out.kind = ConstantValue::Kind::Integer;
                out.integerValue = numericResult;
                return true;
            case TokenType::SLASH:
                if (right.integerValue == 0 ||
                    (left.integerValue == std::numeric_limits<int64_t>::min() &&
                     right.integerValue == -1)) {
                    return false;
                }
                out.kind = ConstantValue::Kind::Integer;
                out.integerValue = left.integerValue / right.integerValue;
                return true;
            case TokenType::GREATER:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = left.integerValue > right.integerValue;
                return true;
            case TokenType::GREATER_EQUAL:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = left.integerValue >= right.integerValue;
                return true;
            case TokenType::LESS:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = left.integerValue < right.integerValue;
                return true;
            case TokenType::LESS_EQUAL:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = left.integerValue <= right.integerValue;
                return true;
            case TokenType::EQUAL_EQUAL:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = left.integerValue == right.integerValue;
                return true;
            case TokenType::BANG_EQUAL:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = left.integerValue != right.integerValue;
                return true;
            default:
                return false;
        }
    }

    if (left.kind == ConstantValue::Kind::Boolean &&
        right.kind == ConstantValue::Kind::Boolean) {
        out.kind = ConstantValue::Kind::Boolean;
        switch (expr.op.type()) {
            case TokenType::EQUAL_EQUAL:
                out.boolValue = left.boolValue == right.boolValue;
                return true;
            case TokenType::BANG_EQUAL:
                out.boolValue = left.boolValue != right.boolValue;
                return true;
            case TokenType::LOGICAL_AND:
                out.boolValue = left.boolValue && right.boolValue;
                return true;
            case TokenType::LOGICAL_OR:
                out.boolValue = left.boolValue || right.boolValue;
                return true;
            default:
                return false;
        }
    }

    if (left.kind == ConstantValue::Kind::Null &&
        right.kind == ConstantValue::Kind::Null) {
        out.kind = ConstantValue::Kind::Boolean;
        out.boolValue = (expr.op.type() == TokenType::EQUAL_EQUAL);
        return expr.op.type() == TokenType::EQUAL_EQUAL ||
               expr.op.type() == TokenType::BANG_EQUAL;
    }

    return false;
}

bool evaluateConstant(const AstExpr& expr, ConstantValue& out) {
    return std::visit(
        [&](const auto& value) -> bool {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, AstLiteralExpr>) {
                return evaluateLiteral(value, out);
            } else if constexpr (std::is_same_v<T, AstGroupingExpr>) {
                return value.expression && evaluateConstant(*value.expression, out);
            } else if constexpr (std::is_same_v<T, AstUnaryExpr>) {
                return evaluateUnary(value, out);
            } else if constexpr (std::is_same_v<T, AstBinaryExpr>) {
                return evaluateBinary(value, out);
            } else {
                return false;
            }
        },
        expr.value);
}

AstExpr makeLiteralExpr(const AstNodeInfo& node, const ConstantValue& value) {
    AstExpr expr;
    expr.node = node;
    AstLiteralExpr literal;
    switch (value.kind) {
        case ConstantValue::Kind::Boolean:
            literal.token = Token::synthetic(value.boolValue ? TokenType::TRUE
                                                             : TokenType::FALSE,
                                             value.boolValue ? "true" : "false",
                                             node.line);
            break;
        case ConstantValue::Kind::Integer:
            literal.token = Token::synthetic(TokenType::NUMBER,
                                             std::to_string(value.integerValue),
                                             node.line);
            break;
        case ConstantValue::Kind::Null:
            literal.token = Token::synthetic(TokenType::_NULL, "null", node.line);
            break;
    }
    expr.value = std::move(literal);
    return expr;
}

void optimizeExpr(AstExpr& expr);
void optimizeStmt(AstStmt& stmt);
void optimizeItem(AstItem& item);

void replaceStmtWith(AstStmt& target, AstStmtPtr replacement) {
    if (!replacement) {
        target.value = AstBlockStmt{};
        return;
    }
    target.value = std::move(replacement->value);
}

void optimizeExpr(AstExpr& expr) {
    std::visit(
        [&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, AstGroupingExpr>) {
                if (value.expression) {
                    optimizeExpr(*value.expression);
                }
            } else if constexpr (std::is_same_v<T, AstUnaryExpr>) {
                if (value.operand) {
                    optimizeExpr(*value.operand);
                }
            } else if constexpr (std::is_same_v<T, AstBinaryExpr>) {
                if (value.left) {
                    optimizeExpr(*value.left);
                }
                if (value.right) {
                    optimizeExpr(*value.right);
                }
            } else if constexpr (std::is_same_v<T, AstAssignmentExpr>) {
                if (value.target) {
                    optimizeExpr(*value.target);
                }
                if (value.value) {
                    optimizeExpr(*value.value);
                }
            } else if constexpr (std::is_same_v<T, AstCallExpr>) {
                if (value.callee) {
                    optimizeExpr(*value.callee);
                }
                for (auto& argument : value.arguments) {
                    if (argument) {
                        optimizeExpr(*argument);
                    }
                }
            } else if constexpr (std::is_same_v<T, AstMemberExpr>) {
                if (value.object) {
                    optimizeExpr(*value.object);
                }
            } else if constexpr (std::is_same_v<T, AstIndexExpr>) {
                if (value.object) {
                    optimizeExpr(*value.object);
                }
                if (value.index) {
                    optimizeExpr(*value.index);
                }
            } else if constexpr (std::is_same_v<T, AstCastExpr>) {
                if (value.expression) {
                    optimizeExpr(*value.expression);
                }
            } else if constexpr (std::is_same_v<T, AstFunctionExpr>) {
                if (value.blockBody) {
                    optimizeStmt(*value.blockBody);
                }
                if (value.expressionBody) {
                    optimizeExpr(*value.expressionBody);
                }
            } else if constexpr (std::is_same_v<T, AstArrayLiteralExpr>) {
                for (auto& element : value.elements) {
                    if (element) {
                        optimizeExpr(*element);
                    }
                }
            } else if constexpr (std::is_same_v<T, AstDictLiteralExpr>) {
                for (auto& entry : value.entries) {
                    if (entry.key) {
                        optimizeExpr(*entry.key);
                    }
                    if (entry.value) {
                        optimizeExpr(*entry.value);
                    }
                }
            }
        },
        expr.value);

    ConstantValue constant;
    if (evaluateConstant(expr, constant)) {
        expr = makeLiteralExpr(expr.node, constant);
    }
}

void optimizeStmt(AstStmt& stmt) {
    std::visit(
        [&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, AstBlockStmt>) {
                for (auto& item : value.items) {
                    if (item) {
                        optimizeItem(*item);
                    }
                }
            } else if constexpr (std::is_same_v<T, AstExprStmt>) {
                if (value.expression) {
                    optimizeExpr(*value.expression);
                }
            } else if constexpr (std::is_same_v<T, AstPrintStmt>) {
                if (value.expression) {
                    optimizeExpr(*value.expression);
                }
            } else if constexpr (std::is_same_v<T, AstReturnStmt>) {
                if (value.value) {
                    optimizeExpr(*value.value);
                }
            } else if constexpr (std::is_same_v<T, AstIfStmt>) {
                if (value.condition) {
                    optimizeExpr(*value.condition);
                }
                if (value.thenBranch) {
                    optimizeStmt(*value.thenBranch);
                }
                if (value.elseBranch) {
                    optimizeStmt(*value.elseBranch);
                }

                ConstantValue condition;
                if (!value.condition || !evaluateConstant(*value.condition, condition) ||
                    condition.kind != ConstantValue::Kind::Boolean) {
                    return;
                }

                if (condition.boolValue) {
                    replaceStmtWith(stmt, std::move(value.thenBranch));
                } else if (value.elseBranch) {
                    replaceStmtWith(stmt, std::move(value.elseBranch));
                } else {
                    stmt.value = AstBlockStmt{};
                }
            } else if constexpr (std::is_same_v<T, AstWhileStmt>) {
                if (value.condition) {
                    optimizeExpr(*value.condition);
                }
                if (value.body) {
                    optimizeStmt(*value.body);
                }

                ConstantValue condition;
                if (value.condition && evaluateConstant(*value.condition, condition) &&
                    condition.kind == ConstantValue::Kind::Boolean &&
                    !condition.boolValue) {
                    stmt.value = AstBlockStmt{};
                }
            } else if constexpr (std::is_same_v<T, AstVarDeclStmt>) {
                if (value.initializer) {
                    optimizeExpr(*value.initializer);
                }
            } else if constexpr (std::is_same_v<T, AstDestructuredImportStmt>) {
                if (value.initializer) {
                    optimizeExpr(*value.initializer);
                }
            } else if constexpr (std::is_same_v<T, AstForStmt>) {
                if (auto* initDecl =
                        std::get_if<std::unique_ptr<AstVarDeclStmt>>(&value.initializer)) {
                    if (*initDecl && (*initDecl)->initializer) {
                        optimizeExpr(*(*initDecl)->initializer);
                    }
                } else if (auto* initExpr = std::get_if<AstExprPtr>(&value.initializer)) {
                    if (*initExpr) {
                        optimizeExpr(**initExpr);
                    }
                }
                if (value.condition) {
                    optimizeExpr(*value.condition);
                }
                if (value.increment) {
                    optimizeExpr(*value.increment);
                }
                if (value.body) {
                    optimizeStmt(*value.body);
                }
            } else if constexpr (std::is_same_v<T, AstForEachStmt>) {
                if (value.iterable) {
                    optimizeExpr(*value.iterable);
                }
                if (value.body) {
                    optimizeStmt(*value.body);
                }
            }
        },
        stmt.value);
}

void optimizeItem(AstItem& item) {
    std::visit(
        [&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, AstFunctionDecl>) {
                if (value.body) {
                    optimizeStmt(*value.body);
                }
            } else if constexpr (std::is_same_v<T, AstClassDecl>) {
                for (auto& method : value.methods) {
                    if (method.body) {
                        optimizeStmt(*method.body);
                    }
                }
            } else if constexpr (std::is_same_v<T, AstStmtPtr>) {
                if (value) {
                    optimizeStmt(*value);
                }
            }
        },
        item.value);
}

}  // namespace

void optimizeAst(AstModule& module, const AstSemanticModel& semanticModel) {
    (void)semanticModel;
    for (auto& item : module.items) {
        if (item) {
            optimizeItem(*item);
        }
    }
}
