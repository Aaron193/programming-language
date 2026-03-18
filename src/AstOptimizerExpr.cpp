#include "AstOptimizerInternal.hpp"

#include <utility>
#include <variant>

namespace ast_optimizer_detail {

namespace {

bool isKnownBoolType(const ConstantEvaluator& evaluator, AstNodeId id) {
    TypeRef type = evaluator.typeOf(id);
    return type && type->kind == TypeKind::BOOL;
}

bool isKnownIntegerType(const ConstantEvaluator& evaluator, AstNodeId id) {
    TypeRef type = evaluator.typeOf(id);
    return type && type->isInteger();
}

bool replaceWithOperandIfTypeMatches(AstExpr& expr, AstExprPtr& operand,
                                     const ConstantEvaluator& evaluator) {
    if (!operand || !canReuseOperandAsResult(expr, *operand, evaluator)) {
        return false;
    }
    replaceExprPreservingNode(expr, std::move(operand));
    return true;
}

bool replaceWithLogicalNotOfOperandIfTypeMatches(
    AstExpr& expr, AstExprPtr& operand, const ConstantEvaluator& evaluator) {
    if (!operand || !isKnownBoolType(evaluator, expr.node.id) ||
        !isKnownBoolType(evaluator, operand->node.id) ||
        !canReuseOperandAsResult(expr, *operand, evaluator)) {
        return false;
    }

    replaceExprWithLogicalNotPreservingNode(expr, std::move(operand));
    return true;
}

bool simplifyUnaryExpr(AstExpr& expr, const ConstantEvaluator& evaluator) {
    auto* unary = std::get_if<AstUnaryExpr>(&expr.value);
    if (!unary || !unary->operand) {
        return false;
    }

    if (unary->op.type() == TokenType::BANG) {
        if (!isKnownBoolType(evaluator, expr.node.id)) {
            return false;
        }

        auto* nestedUnary = std::get_if<AstUnaryExpr>(&unary->operand->value);
        if (!nestedUnary || nestedUnary->op.type() != TokenType::BANG ||
            !nestedUnary->operand ||
            !isKnownBoolType(evaluator, nestedUnary->operand->node.id)) {
            return false;
        }

        return replaceWithOperandIfTypeMatches(expr, nestedUnary->operand,
                                               evaluator);
    }

    if (unary->op.type() != TokenType::TILDE ||
        !isKnownIntegerType(evaluator, expr.node.id)) {
        return false;
    }

    auto* nestedUnary = std::get_if<AstUnaryExpr>(&unary->operand->value);
    if (!nestedUnary || nestedUnary->op.type() != TokenType::TILDE ||
        !nestedUnary->operand ||
        !isKnownIntegerType(evaluator, nestedUnary->operand->node.id)) {
        return false;
    }

    return replaceWithOperandIfTypeMatches(expr, nestedUnary->operand,
                                           evaluator);
}

bool simplifyBooleanEqualityBinary(AstExpr& expr, AstBinaryExpr& binary,
                                   const ConstantEvaluator& evaluator) {
    if (!binary.left || !binary.right || !isKnownBoolType(evaluator, expr.node.id) ||
        !isKnownBoolType(evaluator, binary.left->node.id) ||
        !isKnownBoolType(evaluator, binary.right->node.id)) {
        return false;
    }

    const auto simplifyAgainstConstant = [&](AstExprPtr& operand,
                                             bool constantValue) {
        switch (binary.op.type()) {
            case TokenType::EQUAL_EQUAL:
                if (constantValue) {
                    return replaceWithOperandIfTypeMatches(expr, operand,
                                                           evaluator);
                }
                return replaceWithLogicalNotOfOperandIfTypeMatches(
                    expr, operand, evaluator);
            case TokenType::BANG_EQUAL:
                if (!constantValue) {
                    return replaceWithOperandIfTypeMatches(expr, operand,
                                                           evaluator);
                }
                return replaceWithLogicalNotOfOperandIfTypeMatches(
                    expr, operand, evaluator);
            default:
                return false;
        }
    };

    bool leftValue = false;
    if (tryEvaluateConditionBool(*binary.left, evaluator, leftValue)) {
        return simplifyAgainstConstant(binary.right, leftValue);
    }

    bool rightValue = false;
    if (tryEvaluateConditionBool(*binary.right, evaluator, rightValue)) {
        return simplifyAgainstConstant(binary.left, rightValue);
    }

    return false;
}

bool simplifyIdentityBinary(AstExpr& expr, AstBinaryExpr& binary,
                            const ConstantEvaluator& evaluator) {
    if (!binary.left || !binary.right) {
        return false;
    }

    TypeRef exprType = evaluator.typeOf(expr.node.id);
    if (!exprType) {
        return false;
    }

    ConstantValue leftConstant;
    ConstantValue rightConstant;
    const bool hasLeftConstant =
        tryEvaluateConstant(*binary.left, evaluator, leftConstant);
    const bool hasRightConstant =
        tryEvaluateConstant(*binary.right, evaluator, rightConstant);

    switch (binary.op.type()) {
        case TokenType::PLUS:
            if (!exprType->isNumeric()) {
                return false;
            }
            if (hasRightConstant && isZeroConstant(rightConstant)) {
                return replaceWithOperandIfTypeMatches(expr, binary.left,
                                                       evaluator);
            }
            if (hasLeftConstant && isZeroConstant(leftConstant)) {
                return replaceWithOperandIfTypeMatches(expr, binary.right,
                                                       evaluator);
            }
            return false;
        case TokenType::MINUS:
            return exprType->isNumeric() && hasRightConstant &&
                   isZeroConstant(rightConstant) &&
                   replaceWithOperandIfTypeMatches(expr, binary.left, evaluator);
        case TokenType::STAR:
            if (!exprType->isNumeric()) {
                return false;
            }
            if (exprType->isInteger()) {
                if (hasRightConstant && isZeroConstant(rightConstant) &&
                    isDefinitelyPure(*binary.left)) {
                    return replaceWithOperandIfTypeMatches(expr, binary.right,
                                                           evaluator);
                }
                if (hasLeftConstant && isZeroConstant(leftConstant) &&
                    isDefinitelyPure(*binary.right)) {
                    return replaceWithOperandIfTypeMatches(expr, binary.left,
                                                           evaluator);
                }
            }
            if (hasRightConstant && isOneConstant(rightConstant)) {
                return replaceWithOperandIfTypeMatches(expr, binary.left,
                                                       evaluator);
            }
            if (hasLeftConstant && isOneConstant(leftConstant)) {
                return replaceWithOperandIfTypeMatches(expr, binary.right,
                                                       evaluator);
            }
            return false;
        case TokenType::SLASH:
            return exprType->isNumeric() && hasRightConstant &&
                   isOneConstant(rightConstant) &&
                   replaceWithOperandIfTypeMatches(expr, binary.left, evaluator);
        case TokenType::AMPERSAND:
            if (!exprType->isInteger()) {
                return false;
            }
            if (hasRightConstant && isZeroConstant(rightConstant) &&
                isDefinitelyPure(*binary.left)) {
                return replaceWithOperandIfTypeMatches(expr, binary.right,
                                                       evaluator);
            }
            if (hasLeftConstant && isZeroConstant(leftConstant) &&
                isDefinitelyPure(*binary.right)) {
                return replaceWithOperandIfTypeMatches(expr, binary.left,
                                                       evaluator);
            }
            return false;
        case TokenType::PIPE:
        case TokenType::CARET:
            if (!exprType->isInteger()) {
                return false;
            }
            if (hasRightConstant && isZeroConstant(rightConstant)) {
                return replaceWithOperandIfTypeMatches(expr, binary.left,
                                                       evaluator);
            }
            if (hasLeftConstant && isZeroConstant(leftConstant)) {
                return replaceWithOperandIfTypeMatches(expr, binary.right,
                                                       evaluator);
            }
            return false;
        case TokenType::SHIFT_LEFT_TOKEN:
        case TokenType::SHIFT_RIGHT_TOKEN:
            return exprType->isInteger() && hasRightConstant &&
                   isZeroConstant(rightConstant) &&
                   replaceWithOperandIfTypeMatches(expr, binary.left, evaluator);
        default:
            return false;
    }
}

bool simplifyLogicalBinary(AstExpr& expr, AstBinaryExpr& binary,
                           const ConstantEvaluator& evaluator) {
    if (!binary.left || !binary.right) {
        return false;
    }

    bool leftValue = false;
    bool rightValue = false;
    const bool hasLeftConstant =
        tryEvaluateConditionBool(*binary.left, evaluator, leftValue);
    const bool hasRightConstant =
        tryEvaluateConditionBool(*binary.right, evaluator, rightValue);

    auto replaceWithBoolLiteral = [&](bool value) {
        expr = evaluator.makeLiteralExpr(
            expr, ConstantValue{ConstantValue::Kind::Boolean, 0, 0, 0.0,
                                value});
        return true;
    };

    switch (binary.op.type()) {
        case TokenType::LOGICAL_AND:
            if (hasLeftConstant) {
                if (leftValue) {
                    return replaceWithOperandIfTypeMatches(expr, binary.right,
                                                           evaluator);
                }
                if (!isDefinitelyPure(*binary.right)) {
                    return false;
                }
                return replaceWithBoolLiteral(false);
            }

            if (hasRightConstant) {
                if (rightValue) {
                    return replaceWithOperandIfTypeMatches(expr, binary.left,
                                                           evaluator);
                }
                if (!isDefinitelyPure(*binary.left)) {
                    return false;
                }
                return replaceWithBoolLiteral(false);
            }
            return false;
        case TokenType::LOGICAL_OR:
            if (hasLeftConstant) {
                if (!leftValue) {
                    return replaceWithOperandIfTypeMatches(expr, binary.right,
                                                           evaluator);
                }
                if (!isDefinitelyPure(*binary.right)) {
                    return false;
                }
                return replaceWithBoolLiteral(true);
            }

            if (hasRightConstant) {
                if (!rightValue) {
                    return replaceWithOperandIfTypeMatches(expr, binary.left,
                                                           evaluator);
                }
                if (!isDefinitelyPure(*binary.left)) {
                    return false;
                }
                return replaceWithBoolLiteral(true);
            }
            return false;
        default:
            return false;
    }
}

bool simplifyBinaryExpr(AstExpr& expr, const ConstantEvaluator& evaluator) {
    auto* binary = std::get_if<AstBinaryExpr>(&expr.value);
    if (!binary) {
        return false;
    }

    if (simplifyLogicalBinary(expr, *binary, evaluator)) {
        return true;
    }

    binary = std::get_if<AstBinaryExpr>(&expr.value);
    if (binary && simplifyBooleanEqualityBinary(expr, *binary, evaluator)) {
        return true;
    }

    binary = std::get_if<AstBinaryExpr>(&expr.value);
    return binary && simplifyIdentityBinary(expr, *binary, evaluator);
}

}  // namespace

void optimizeExprTree(AstExpr& expr, const ConstantEvaluator& evaluator) {
    std::visit(
        [&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, AstGroupingExpr>) {
                if (value.expression) {
                    optimizeExprTree(*value.expression, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstUnaryExpr>) {
                if (value.operand) {
                    optimizeExprTree(*value.operand, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstBinaryExpr>) {
                if (value.left) {
                    optimizeExprTree(*value.left, evaluator);
                }
                if (value.right) {
                    optimizeExprTree(*value.right, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstAssignmentExpr>) {
                if (value.target) {
                    optimizeExprTree(*value.target, evaluator);
                }
                if (value.value) {
                    optimizeExprTree(*value.value, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstCallExpr>) {
                if (value.callee) {
                    optimizeExprTree(*value.callee, evaluator);
                }
                for (auto& argument : value.arguments) {
                    if (argument) {
                        optimizeExprTree(*argument, evaluator);
                    }
                }
            } else if constexpr (std::is_same_v<T, AstMemberExpr>) {
                if (value.object) {
                    optimizeExprTree(*value.object, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstIndexExpr>) {
                if (value.object) {
                    optimizeExprTree(*value.object, evaluator);
                }
                if (value.index) {
                    optimizeExprTree(*value.index, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstCastExpr>) {
                if (value.expression) {
                    optimizeExprTree(*value.expression, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstFunctionExpr>) {
                if (value.blockBody) {
                    optimizeStmtTree(*value.blockBody, evaluator);
                }
                if (value.expressionBody) {
                    optimizeExprTree(*value.expressionBody, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstArrayLiteralExpr>) {
                for (auto& element : value.elements) {
                    if (element) {
                        optimizeExprTree(*element, evaluator);
                    }
                }
            } else if constexpr (std::is_same_v<T, AstDictLiteralExpr>) {
                for (auto& entry : value.entries) {
                    if (entry.key) {
                        optimizeExprTree(*entry.key, evaluator);
                    }
                    if (entry.value) {
                        optimizeExprTree(*entry.value, evaluator);
                    }
                }
            }
        },
        expr.value);

    simplifyUnaryExpr(expr, evaluator);

    // Keep the rewrite order explicit: simplify local algebra/logical
    // identities first, then collapse any expression that is now constant.
    simplifyBinaryExpr(expr, evaluator);

    ConstantValue constant;
    if (tryEvaluateConstant(expr, evaluator, constant)) {
        expr = evaluator.makeLiteralExpr(expr, constant);
    }
}

}  // namespace ast_optimizer_detail
