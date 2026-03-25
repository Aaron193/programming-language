#include "AstOptimizerInternal.hpp"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <variant>

#include "NumericLiteral.hpp"
#include "Token.hpp"

namespace ast_optimizer_detail {

namespace {

constexpr uint64_t kMaxExactIntegerInDouble = 9007199254740992ULL;

bool addOverflow(int64_t lhs, int64_t rhs, int64_t& out) {
    return __builtin_add_overflow(lhs, rhs, &out);
}

bool subOverflow(int64_t lhs, int64_t rhs, int64_t& out) {
    return __builtin_sub_overflow(lhs, rhs, &out);
}

bool mulOverflow(int64_t lhs, int64_t rhs, int64_t& out) {
    return __builtin_mul_overflow(lhs, rhs, &out);
}

bool addOverflow(uint64_t lhs, uint64_t rhs, uint64_t& out) {
    return __builtin_add_overflow(lhs, rhs, &out);
}

bool subOverflow(uint64_t lhs, uint64_t rhs, uint64_t& out) {
    return __builtin_sub_overflow(lhs, rhs, &out);
}

bool mulOverflow(uint64_t lhs, uint64_t rhs, uint64_t& out) {
    return __builtin_mul_overflow(lhs, rhs, &out);
}

std::string formatFloatCore(double value) {
    if (value == 0.0) {
        value = 0.0;
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision(15) << value;
    std::string text = out.str();
    while (!text.empty() && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.push_back('0');
    }
    if (text.empty()) {
        return "0.0";
    }
    return text;
}

bool blockIsDefinitelyTerminal(const AstBlockStmt& block) {
    for (auto it = block.items.rbegin(); it != block.items.rend(); ++it) {
        if (*it) {
            return isDefinitelyTerminal(**it);
        }
    }
    return false;
}

}  // namespace

ConstantEvaluator::ConstantEvaluator(const AstSemanticModel& semanticModel)
    : m_semanticModel(semanticModel) {}

bool ConstantEvaluator::evaluate(const AstExpr& expr, ConstantValue& out) const {
    return std::visit(
        [&](const auto& value) -> bool {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, AstLiteralExpr>) {
                return evaluateLiteral(expr, value, out);
            } else if constexpr (std::is_same_v<T, AstGroupingExpr>) {
                return value.expression && evaluate(*value.expression, out);
            } else if constexpr (std::is_same_v<T, AstUnaryExpr>) {
                return evaluateUnary(expr, value, out);
            } else if constexpr (std::is_same_v<T, AstBinaryExpr>) {
                return evaluateBinary(expr, value, out);
            } else {
                return false;
            }
        },
        expr.value);
}

AstExpr ConstantEvaluator::makeLiteralExpr(const AstExpr& original,
                                           const ConstantValue& value) const {
    AstExpr expr;
    expr.node = original.node;

    AstLiteralExpr literal;
    switch (value.kind) {
        case ConstantValue::Kind::Boolean:
            literal.token = Token::synthetic(value.boolValue ? TokenType::TRUE
                                                             : TokenType::FALSE,
                                             value.boolValue ? "true" : "false",
                                             original.node.span);
            break;
        case ConstantValue::Kind::Null:
            literal.token =
                Token::synthetic(TokenType::_NULL, "null", original.node.span);
            break;
        case ConstantValue::Kind::SignedInteger: {
            TypeRef type = typeOf(original.node.id);
            std::string text = std::to_string(value.signedValue);
            if (type && type->isInteger()) {
                text += integerSuffix(type->kind);
            }
            literal.token =
                Token::synthetic(TokenType::NUMBER, std::move(text),
                                 original.node.span);
            break;
        }
        case ConstantValue::Kind::UnsignedInteger: {
            TypeRef type = typeOf(original.node.id);
            std::string text = std::to_string(value.unsignedValue);
            if (type && type->isInteger()) {
                text += integerSuffix(type->kind);
            }
            literal.token =
                Token::synthetic(TokenType::NUMBER, std::move(text),
                                 original.node.span);
            break;
        }
        case ConstantValue::Kind::Float: {
            TypeRef type = typeOf(original.node.id);
            std::string text = formatFloatCore(value.floatValue);
            if (type && type->kind == TypeKind::F32) {
                text += "f32";
            } else {
                text += "f64";
            }
            literal.token =
                Token::synthetic(TokenType::NUMBER, std::move(text),
                                 original.node.span);
            break;
        }
    }

    expr.value = std::move(literal);
    return expr;
}

bool ConstantEvaluator::evaluateConditionBool(const AstExpr& expr,
                                              bool& outValue) const {
    ConstantValue constant;
    if (!evaluate(expr, constant) ||
        constant.kind != ConstantValue::Kind::Boolean) {
        return false;
    }
    outValue = constant.boolValue;
    return true;
}

TypeRef ConstantEvaluator::typeOf(AstNodeId id) const {
    auto it = m_semanticModel.nodeTypes.find(id);
    if (it == m_semanticModel.nodeTypes.end()) {
        return nullptr;
    }
    return it->second;
}

bool ConstantEvaluator::constantToDouble(const ConstantValue& value,
                                         double& outValue) const {
    switch (value.kind) {
        case ConstantValue::Kind::Float:
            outValue = value.floatValue;
            return std::isfinite(outValue);
        case ConstantValue::Kind::SignedInteger:
            if (value.signedValue < 0 &&
                static_cast<uint64_t>(-(value.signedValue + 1)) + 1 >
                    kMaxExactIntegerInDouble) {
                return false;
            }
            if (value.signedValue >= 0 &&
                static_cast<uint64_t>(value.signedValue) >
                    kMaxExactIntegerInDouble) {
                return false;
            }
            outValue = static_cast<double>(value.signedValue);
            return true;
        case ConstantValue::Kind::UnsignedInteger:
            if (value.unsignedValue > kMaxExactIntegerInDouble) {
                return false;
            }
            outValue = static_cast<double>(value.unsignedValue);
            return true;
        default:
            return false;
    }
}

bool ConstantEvaluator::constantToBitwiseUnsigned(const ConstantValue& value,
                                                  uint64_t& outValue) const {
    switch (value.kind) {
        case ConstantValue::Kind::SignedInteger:
            outValue = static_cast<uint64_t>(value.signedValue);
            return true;
        case ConstantValue::Kind::UnsignedInteger:
            outValue = value.unsignedValue;
            return true;
        default:
            return false;
    }
}

bool ConstantEvaluator::evaluateLiteral(const AstExpr& expr,
                                        const AstLiteralExpr& literal,
                                        ConstantValue& out) const {
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
        case TokenType::NUMBER: {
            TypeRef literalType = typeOf(expr.node.id);
            if (!literalType || !literalType->isNumeric()) {
                return false;
            }

            if (literalType->isFloat()) {
                out.kind = ConstantValue::Kind::Float;
                return parseFloatLiteral(literal.token, out.floatValue);
            }

            if (literalType->isUnsigned()) {
                out.kind = ConstantValue::Kind::UnsignedInteger;
                return parseUnsignedIntegerLiteral(literal.token,
                                                   out.unsignedValue);
            }

            if (literalType->isInteger()) {
                out.kind = ConstantValue::Kind::SignedInteger;
                return parseSignedIntegerLiteral(literal.token,
                                                 out.signedValue);
            }
            return false;
        }
        default:
            return false;
    }
}

bool ConstantEvaluator::evaluateUnary(const AstExpr& expr,
                                      const AstUnaryExpr& unary,
                                      ConstantValue& out) const {
    ConstantValue operand;
    if (!unary.operand || !evaluate(*unary.operand, operand)) {
        return false;
    }

    switch (unary.op.type()) {
        case TokenType::BANG:
            if (operand.kind != ConstantValue::Kind::Boolean) {
                return false;
            }
            out.kind = ConstantValue::Kind::Boolean;
            out.boolValue = !operand.boolValue;
            return true;
        case TokenType::MINUS:
            if (operand.kind == ConstantValue::Kind::SignedInteger) {
                if (operand.signedValue ==
                    std::numeric_limits<int64_t>::min()) {
                    return false;
                }
                out.kind = ConstantValue::Kind::SignedInteger;
                out.signedValue = -operand.signedValue;
                return true;
            }
            if (operand.kind == ConstantValue::Kind::Float) {
                out.kind = ConstantValue::Kind::Float;
                out.floatValue = -operand.floatValue;
                return std::isfinite(out.floatValue);
            }
            return false;
        case TokenType::TILDE:
            if (operand.kind == ConstantValue::Kind::SignedInteger) {
                out.kind = ConstantValue::Kind::SignedInteger;
                out.signedValue = ~operand.signedValue;
                return true;
            }
            if (operand.kind == ConstantValue::Kind::UnsignedInteger) {
                out.kind = ConstantValue::Kind::UnsignedInteger;
                out.unsignedValue = ~operand.unsignedValue;
                return true;
            }
            return false;
        default:
            return false;
    }
}

bool ConstantEvaluator::evaluateNumericBinary(const AstBinaryExpr& binary,
                                              const ConstantValue& left,
                                              const ConstantValue& right,
                                              ConstantValue& out) const {
    TypeRef leftType = typeOf(binary.left->node.id);
    TypeRef rightType = typeOf(binary.right->node.id);
    TypeRef promoted = numericPromotion(leftType, rightType);
    if (!promoted) {
        return false;
    }

    if (promoted->isFloat()) {
        double lhs = 0.0;
        double rhs = 0.0;
        if (!constantToDouble(left, lhs) || !constantToDouble(right, rhs)) {
            return false;
        }

        switch (binary.op.type()) {
            case TokenType::PLUS:
                out.kind = ConstantValue::Kind::Float;
                out.floatValue = lhs + rhs;
                return std::isfinite(out.floatValue);
            case TokenType::MINUS:
                out.kind = ConstantValue::Kind::Float;
                out.floatValue = lhs - rhs;
                return std::isfinite(out.floatValue);
            case TokenType::STAR:
                out.kind = ConstantValue::Kind::Float;
                out.floatValue = lhs * rhs;
                return std::isfinite(out.floatValue);
            case TokenType::SLASH:
                if (rhs == 0.0) {
                    return false;
                }
                out.kind = ConstantValue::Kind::Float;
                out.floatValue = lhs / rhs;
                return std::isfinite(out.floatValue);
            case TokenType::GREATER:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = lhs > rhs;
                return true;
            case TokenType::GREATER_EQUAL:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = lhs >= rhs;
                return true;
            case TokenType::LESS:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = lhs < rhs;
                return true;
            case TokenType::LESS_EQUAL:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = lhs <= rhs;
                return true;
            case TokenType::EQUAL_EQUAL:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = lhs == rhs;
                return true;
            case TokenType::BANG_EQUAL:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = lhs != rhs;
                return true;
            default:
                return false;
        }
    }

    if (promoted->isSigned() &&
        left.kind == ConstantValue::Kind::SignedInteger &&
        right.kind == ConstantValue::Kind::SignedInteger) {
        int64_t numericResult = 0;
        switch (binary.op.type()) {
            case TokenType::PLUS:
                if (addOverflow(left.signedValue, right.signedValue,
                                numericResult)) {
                    return false;
                }
                out.kind = ConstantValue::Kind::SignedInteger;
                out.signedValue = numericResult;
                return true;
            case TokenType::MINUS:
                if (subOverflow(left.signedValue, right.signedValue,
                                numericResult)) {
                    return false;
                }
                out.kind = ConstantValue::Kind::SignedInteger;
                out.signedValue = numericResult;
                return true;
            case TokenType::STAR:
                if (mulOverflow(left.signedValue, right.signedValue,
                                numericResult)) {
                    return false;
                }
                out.kind = ConstantValue::Kind::SignedInteger;
                out.signedValue = numericResult;
                return true;
            case TokenType::SLASH:
                if (right.signedValue == 0 ||
                    (left.signedValue == std::numeric_limits<int64_t>::min() &&
                     right.signedValue == -1)) {
                    return false;
                }
                out.kind = ConstantValue::Kind::SignedInteger;
                out.signedValue = left.signedValue / right.signedValue;
                return true;
            case TokenType::GREATER:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = left.signedValue > right.signedValue;
                return true;
            case TokenType::GREATER_EQUAL:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = left.signedValue >= right.signedValue;
                return true;
            case TokenType::LESS:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = left.signedValue < right.signedValue;
                return true;
            case TokenType::LESS_EQUAL:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = left.signedValue <= right.signedValue;
                return true;
            case TokenType::EQUAL_EQUAL:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = left.signedValue == right.signedValue;
                return true;
            case TokenType::BANG_EQUAL:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = left.signedValue != right.signedValue;
                return true;
            default:
                return false;
        }
    }

    if (promoted->isUnsigned() &&
        left.kind == ConstantValue::Kind::UnsignedInteger &&
        right.kind == ConstantValue::Kind::UnsignedInteger) {
        uint64_t numericResult = 0;
        switch (binary.op.type()) {
            case TokenType::PLUS:
                if (addOverflow(left.unsignedValue, right.unsignedValue,
                                numericResult)) {
                    return false;
                }
                out.kind = ConstantValue::Kind::UnsignedInteger;
                out.unsignedValue = numericResult;
                return true;
            case TokenType::MINUS:
                if (subOverflow(left.unsignedValue, right.unsignedValue,
                                numericResult)) {
                    return false;
                }
                out.kind = ConstantValue::Kind::UnsignedInteger;
                out.unsignedValue = numericResult;
                return true;
            case TokenType::STAR:
                if (mulOverflow(left.unsignedValue, right.unsignedValue,
                                numericResult)) {
                    return false;
                }
                out.kind = ConstantValue::Kind::UnsignedInteger;
                out.unsignedValue = numericResult;
                return true;
            case TokenType::SLASH:
                if (right.unsignedValue == 0) {
                    return false;
                }
                out.kind = ConstantValue::Kind::UnsignedInteger;
                out.unsignedValue = left.unsignedValue / right.unsignedValue;
                return true;
            case TokenType::GREATER:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = left.unsignedValue > right.unsignedValue;
                return true;
            case TokenType::GREATER_EQUAL:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = left.unsignedValue >= right.unsignedValue;
                return true;
            case TokenType::LESS:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = left.unsignedValue < right.unsignedValue;
                return true;
            case TokenType::LESS_EQUAL:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = left.unsignedValue <= right.unsignedValue;
                return true;
            case TokenType::EQUAL_EQUAL:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = left.unsignedValue == right.unsignedValue;
                return true;
            case TokenType::BANG_EQUAL:
                out.kind = ConstantValue::Kind::Boolean;
                out.boolValue = left.unsignedValue != right.unsignedValue;
                return true;
            default:
                return false;
        }
    }

    return false;
}

bool ConstantEvaluator::evaluateBitwiseBinary(const AstExpr& expr,
                                              const AstBinaryExpr& binary,
                                              const ConstantValue& left,
                                              const ConstantValue& right,
                                              ConstantValue& out) const {
    uint64_t lhs = 0;
    uint64_t rhs = 0;
    if (!constantToBitwiseUnsigned(left, lhs) ||
        !constantToBitwiseUnsigned(right, rhs)) {
        return false;
    }

    uint64_t result = 0;
    switch (binary.op.type()) {
        case TokenType::AMPERSAND:
            result = lhs & rhs;
            break;
        case TokenType::PIPE:
            result = lhs | rhs;
            break;
        case TokenType::CARET:
            result = lhs ^ rhs;
            break;
        default:
            return false;
    }

    TypeRef resultType = typeOf(expr.node.id);
    const bool signedResult =
        resultType ? resultType->isSigned()
                   : (left.kind == ConstantValue::Kind::SignedInteger &&
                      right.kind == ConstantValue::Kind::SignedInteger);
    if (signedResult) {
        out.kind = ConstantValue::Kind::SignedInteger;
        out.signedValue = static_cast<int64_t>(result);
    } else {
        out.kind = ConstantValue::Kind::UnsignedInteger;
        out.unsignedValue = result;
    }
    return true;
}

bool ConstantEvaluator::evaluateShiftBinary(const AstBinaryExpr& binary,
                                            const ConstantValue& left,
                                            const ConstantValue& right,
                                            ConstantValue& out) const {
    uint64_t rawAmount = 0;
    if (!constantToBitwiseUnsigned(right, rawAmount)) {
        return false;
    }
    const uint32_t amount = static_cast<uint32_t>(rawAmount) & 63u;

    if (left.kind == ConstantValue::Kind::SignedInteger) {
        out.kind = ConstantValue::Kind::SignedInteger;
        if (binary.op.type() == TokenType::SHIFT_LEFT_TOKEN) {
            out.signedValue = static_cast<int64_t>(
                static_cast<uint64_t>(left.signedValue) << amount);
            return true;
        }
        if (binary.op.type() == TokenType::SHIFT_RIGHT_TOKEN) {
            out.signedValue = left.signedValue >> amount;
            return true;
        }
        return false;
    }

    if (left.kind == ConstantValue::Kind::UnsignedInteger) {
        out.kind = ConstantValue::Kind::UnsignedInteger;
        if (binary.op.type() == TokenType::SHIFT_LEFT_TOKEN) {
            out.unsignedValue = left.unsignedValue << amount;
            return true;
        }
        if (binary.op.type() == TokenType::SHIFT_RIGHT_TOKEN) {
            out.unsignedValue = left.unsignedValue >> amount;
            return true;
        }
        return false;
    }

    return false;
}

bool ConstantEvaluator::evaluateBinary(const AstExpr& expr,
                                       const AstBinaryExpr& binary,
                                       ConstantValue& out) const {
    ConstantValue left;
    ConstantValue right;
    if (!binary.left || !binary.right || !evaluate(*binary.left, left) ||
        !evaluate(*binary.right, right)) {
        return false;
    }

    if (binary.op.type() == TokenType::AMPERSAND ||
        binary.op.type() == TokenType::PIPE ||
        binary.op.type() == TokenType::CARET) {
        return evaluateBitwiseBinary(expr, binary, left, right, out);
    }

    if (binary.op.type() == TokenType::SHIFT_LEFT_TOKEN ||
        binary.op.type() == TokenType::SHIFT_RIGHT_TOKEN) {
        return evaluateShiftBinary(binary, left, right, out);
    }

    switch (binary.op.type()) {
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

    if ((left.kind == ConstantValue::Kind::SignedInteger ||
         left.kind == ConstantValue::Kind::UnsignedInteger ||
         left.kind == ConstantValue::Kind::Float) &&
        (right.kind == ConstantValue::Kind::SignedInteger ||
         right.kind == ConstantValue::Kind::UnsignedInteger ||
         right.kind == ConstantValue::Kind::Float)) {
        return evaluateNumericBinary(binary, left, right, out);
    }

    if (left.kind == ConstantValue::Kind::Boolean &&
        right.kind == ConstantValue::Kind::Boolean) {
        out.kind = ConstantValue::Kind::Boolean;
        switch (binary.op.type()) {
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
        out.boolValue = (binary.op.type() == TokenType::EQUAL_EQUAL);
        return binary.op.type() == TokenType::EQUAL_EQUAL ||
               binary.op.type() == TokenType::BANG_EQUAL;
    }

    return false;
}

bool tryEvaluateConstant(const AstExpr& expr, const ConstantEvaluator& evaluator,
                         ConstantValue& out) {
    return evaluator.evaluate(expr, out);
}

bool tryEvaluateConditionBool(const AstExpr& expr,
                              const ConstantEvaluator& evaluator,
                              bool& outValue) {
    return evaluator.evaluateConditionBool(expr, outValue);
}

bool isDefinitelyTerminal(const AstStmt& stmt) {
    return std::visit(
        [&](const auto& value) -> bool {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, AstReturnStmt> ||
                          std::is_same_v<T, AstBreakStmt> ||
                          std::is_same_v<T, AstContinueStmt>) {
                return true;
            } else if constexpr (std::is_same_v<T, AstBlockStmt>) {
                return blockIsDefinitelyTerminal(value);
            } else if constexpr (std::is_same_v<T, AstIfStmt>) {
                return value.thenBranch && value.elseBranch &&
                       isDefinitelyTerminal(*value.thenBranch) &&
                       isDefinitelyTerminal(*value.elseBranch);
            } else {
                return false;
            }
        },
        stmt.value);
}

bool isDefinitelyTerminal(const AstItem& item) {
    return std::visit(
        [&](const auto& value) -> bool {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, AstStmtPtr>) {
                return value && isDefinitelyTerminal(*value);
            } else {
                return false;
            }
        },
        item.value);
}

bool isDefinitelyPure(const AstExpr& expr) {
    return std::visit(
        [&](const auto& value) -> bool {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, AstLiteralExpr> ||
                          std::is_same_v<T, AstIdentifierExpr> ||
                          std::is_same_v<T, AstThisExpr> ||
                          std::is_same_v<T, AstSuperExpr>) {
                return true;
            } else if constexpr (std::is_same_v<T, AstGroupingExpr>) {
                return value.expression && isDefinitelyPure(*value.expression);
            } else if constexpr (std::is_same_v<T, AstUnaryExpr>) {
                return value.operand && isDefinitelyPure(*value.operand);
            } else if constexpr (std::is_same_v<T, AstBinaryExpr>) {
                return value.left && value.right &&
                       isDefinitelyPure(*value.left) &&
                       isDefinitelyPure(*value.right);
            } else if constexpr (std::is_same_v<T, AstCastExpr>) {
                return value.expression && isDefinitelyPure(*value.expression);
            } else {
                return false;
            }
        },
        expr.value);
}

AstStmtPtr makeStmtPtr(AstStmt&& stmt) {
    return std::make_unique<AstStmt>(std::move(stmt));
}

AstItemPtr makeItemPtr(AstStmtPtr stmt) {
    auto item = std::make_unique<AstItem>();
    item->value = std::move(stmt);
    return item;
}

AstExprPtr makeExprPtr(AstExpr&& expr) {
    return std::make_unique<AstExpr>(std::move(expr));
}

bool isEmptyBlockStmt(const AstStmt& stmt) {
    const auto* block = std::get_if<AstBlockStmt>(&stmt.value);
    return block && block->items.empty();
}

void replaceStmtPreservingNode(AstStmt& target, AstStmtPtr replacement) {
    if (!replacement) {
        target.value = AstBlockStmt{};
        return;
    }
    target.value = std::move(replacement->value);
}

void replaceExprPreservingNode(AstExpr& target, AstExprPtr replacement) {
    if (!replacement) {
        return;
    }
    replacement->node = target.node;
    target = std::move(*replacement);
}

void replaceExprWithLogicalNotPreservingNode(AstExpr& target,
                                             AstExprPtr operand) {
    if (!operand) {
        return;
    }

    AstExpr replacement;
    AstUnaryExpr logicalNot;
    logicalNot.op = Token::synthetic(TokenType::BANG, "!", target.node.span);
    logicalNot.operand = std::move(operand);
    replacement.value = std::move(logicalNot);
    replaceExprPreservingNode(target, makeExprPtr(std::move(replacement)));
}

bool sameKnownType(const ConstantEvaluator& evaluator, AstNodeId lhs,
                   AstNodeId rhs) {
    TypeRef leftType = evaluator.typeOf(lhs);
    TypeRef rightType = evaluator.typeOf(rhs);
    return leftType && rightType && leftType == rightType;
}

bool canReuseOperandAsResult(const AstExpr& expr, const AstExpr& operand,
                             const ConstantEvaluator& evaluator) {
    return sameKnownType(evaluator, expr.node.id, operand.node.id);
}

bool isZeroConstant(const ConstantValue& value) {
    switch (value.kind) {
        case ConstantValue::Kind::SignedInteger:
            return value.signedValue == 0;
        case ConstantValue::Kind::UnsignedInteger:
            return value.unsignedValue == 0;
        case ConstantValue::Kind::Float:
            return value.floatValue == 0.0;
        default:
            return false;
    }
}

bool isOneConstant(const ConstantValue& value) {
    switch (value.kind) {
        case ConstantValue::Kind::SignedInteger:
            return value.signedValue == 1;
        case ConstantValue::Kind::UnsignedInteger:
            return value.unsignedValue == 1;
        case ConstantValue::Kind::Float:
            return value.floatValue == 1.0;
        default:
            return false;
    }
}

}  // namespace ast_optimizer_detail
