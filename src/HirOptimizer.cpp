#include "HirOptimizer.hpp"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "FrontendTypeUtils.hpp"
#include "NumericLiteral.hpp"
#include "Token.hpp"

namespace hir_optimizer_detail {

struct ConstantValue {
    enum class Kind {
        SignedInteger,
        UnsignedInteger,
        Float,
        Boolean,
        Null,
    };

    Kind kind = Kind::Null;
    int64_t signedValue = 0;
    uint64_t unsignedValue = 0;
    double floatValue = 0.0;
    bool boolValue = false;
};

namespace {

constexpr uint64_t kMaxExactIntegerInDouble = 9007199254740992ULL;
HirModule* g_module = nullptr;

HirExpr& exprRef(HirExprId id) {
    return g_module->expr(id);
}

HirStmt& stmtRef(HirStmtId id) {
    return g_module->stmt(id);
}

HirItem& itemRef(HirItemId id) {
    return g_module->item(id);
}

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

struct ConstantEvaluator {
    bool evaluate(const HirExpr& expr, ConstantValue& out) const {
        return std::visit(
            [&](const auto& value) -> bool {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, HirLiteralExpr>) {
                    return evaluateLiteral(expr, value, out);
                } else if constexpr (std::is_same_v<T, HirUnaryExpr>) {
                    return evaluateUnary(value, out);
                } else if constexpr (std::is_same_v<T, HirBinaryExpr>) {
                    return evaluateBinary(expr, value, out);
                } else if constexpr (std::is_same_v<T, HirCastExpr>) {
                    return value.expression &&
                           evaluate(exprRef(*value.expression), out);
                } else {
                    return false;
                }
            },
            expr.value);
    }

    bool evaluateConditionBool(const HirExpr& expr, bool& outValue) const {
        ConstantValue constant;
        if (!evaluate(expr, constant) ||
            constant.kind != ConstantValue::Kind::Boolean) {
            return false;
        }
        outValue = constant.boolValue;
        return true;
    }

    TypeRef typeOf(const HirNodeInfo& node) const {
        return node.type;
    }

    HirExpr makeLiteralExpr(const HirExpr& original,
                            const ConstantValue& value) const {
        HirExpr expr;
        expr.node = original.node;

        HirLiteralExpr literal;
        switch (value.kind) {
            case ConstantValue::Kind::Boolean:
                literal.token = Token::synthetic(
                    value.boolValue ? TokenType::TRUE : TokenType::FALSE,
                    value.boolValue ? "true" : "false", original.node.span);
                break;
            case ConstantValue::Kind::Null:
                literal.token =
                    Token::synthetic(TokenType::_NULL, "null", original.node.span);
                break;
            case ConstantValue::Kind::SignedInteger: {
                std::string text = std::to_string(value.signedValue);
                TypeRef type = typeOf(original.node);
                if (type && type->isInteger()) {
                    text += integerSuffix(type->kind);
                }
                literal.token = Token::synthetic(TokenType::NUMBER, std::move(text),
                                                 original.node.span);
                break;
            }
            case ConstantValue::Kind::UnsignedInteger: {
                std::string text = std::to_string(value.unsignedValue);
                TypeRef type = typeOf(original.node);
                if (type && type->isInteger()) {
                    text += integerSuffix(type->kind);
                }
                literal.token = Token::synthetic(TokenType::NUMBER, std::move(text),
                                                 original.node.span);
                break;
            }
            case ConstantValue::Kind::Float: {
                std::string text = formatFloatCore(value.floatValue);
                TypeRef type = typeOf(original.node);
                text += (type && type->kind == TypeKind::F32) ? "f32" : "f64";
                literal.token = Token::synthetic(TokenType::NUMBER, std::move(text),
                                                 original.node.span);
                break;
            }
        }

        expr.value = std::move(literal);
        return expr;
    }

   private:
    bool constantToDouble(const ConstantValue& value, double& outValue) const {
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

    bool constantToBitwiseUnsigned(const ConstantValue& value,
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

    bool evaluateLiteral(const HirExpr& expr, const HirLiteralExpr& literal,
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
                TypeRef literalType = typeOf(expr.node);
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

    bool evaluateUnary(const HirUnaryExpr& unary, ConstantValue& out) const {
        ConstantValue operand;
        if (!unary.operand || !evaluate(exprRef(*unary.operand), operand)) {
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

    bool evaluateNumericBinary(const HirExpr& expr, const HirBinaryExpr& binary,
                               const ConstantValue& left,
                               const ConstantValue& right,
                               ConstantValue& out) const {
        if (!binary.left || !binary.right) {
            return false;
        }

        TypeRef promoted = numericPromotion(typeOf(exprRef(*binary.left).node),
                                            typeOf(exprRef(*binary.right).node));
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
                        (left.signedValue ==
                             std::numeric_limits<int64_t>::min() &&
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

    bool evaluateBitwiseBinary(const HirExpr& expr, const HirBinaryExpr& binary,
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

        TypeRef resultType = typeOf(expr.node);
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

    bool evaluateShiftBinary(const ConstantValue& left,
                             const ConstantValue& right,
                             const HirBinaryExpr& binary,
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

    bool evaluateBinary(const HirExpr& expr, const HirBinaryExpr& binary,
                        ConstantValue& out) const {
        ConstantValue left;
        ConstantValue right;
        if (!binary.left || !binary.right ||
            !evaluate(exprRef(*binary.left), left) ||
            !evaluate(exprRef(*binary.right), right)) {
            return false;
        }

        if (binary.op.type() == TokenType::AMPERSAND ||
            binary.op.type() == TokenType::PIPE ||
            binary.op.type() == TokenType::CARET) {
            return evaluateBitwiseBinary(expr, binary, left, right, out);
        }

        if (binary.op.type() == TokenType::SHIFT_LEFT_TOKEN ||
            binary.op.type() == TokenType::SHIFT_RIGHT_TOKEN) {
            return evaluateShiftBinary(left, right, binary, out);
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
            return evaluateNumericBinary(expr, binary, left, right, out);
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
};

bool tryEvaluateConstant(const HirExpr& expr, const ConstantEvaluator& evaluator,
                         ConstantValue& out) {
    return evaluator.evaluate(expr, out);
}

bool tryEvaluateConditionBool(const HirExpr& expr,
                              const ConstantEvaluator& evaluator,
                              bool& outValue) {
    return evaluator.evaluateConditionBool(expr, outValue);
}

bool isDefinitelyTerminal(const HirStmt& stmt);
bool isDefinitelyTerminal(const HirItem& item);
void optimizeExprTree(HirExpr& expr, const ConstantEvaluator& evaluator);
void optimizeStmtTree(HirStmt& stmt, const ConstantEvaluator& evaluator);
void optimizeItemTree(HirItem& item, const ConstantEvaluator& evaluator);

bool blockIsDefinitelyTerminal(const HirBlockStmt& block) {
    for (auto it = block.items.rbegin(); it != block.items.rend(); ++it) {
        return isDefinitelyTerminal(itemRef(*it));
    }
    return false;
}

bool isDefinitelyTerminal(const HirStmt& stmt) {
    return std::visit(
        [&](const auto& value) -> bool {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, HirReturnStmt>) {
                return true;
            } else if constexpr (std::is_same_v<T, HirBlockStmt>) {
                return blockIsDefinitelyTerminal(value);
            } else if constexpr (std::is_same_v<T, HirIfStmt>) {
                return value.thenBranch && value.elseBranch &&
                       isDefinitelyTerminal(stmtRef(*value.thenBranch)) &&
                       isDefinitelyTerminal(stmtRef(*value.elseBranch));
            } else {
                return false;
            }
        },
        stmt.value);
}

bool isDefinitelyTerminal(const HirItem& item) {
    return std::visit(
        [&](const auto& value) -> bool {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, HirStmtId>) {
                return isDefinitelyTerminal(stmtRef(value));
            } else {
                return false;
            }
        },
        item.value);
}

bool isDefinitelyPure(const HirExpr& expr) {
    return std::visit(
        [&](const auto& value) -> bool {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, HirLiteralExpr> ||
                          std::is_same_v<T, HirBindingExpr> ||
                          std::is_same_v<T, HirThisExpr> ||
                          std::is_same_v<T, HirSuperExpr>) {
                return true;
            } else if constexpr (std::is_same_v<T, HirUnaryExpr>) {
                return value.operand && isDefinitelyPure(exprRef(*value.operand));
            } else if constexpr (std::is_same_v<T, HirBinaryExpr>) {
                return value.left && value.right &&
                       isDefinitelyPure(exprRef(*value.left)) &&
                       isDefinitelyPure(exprRef(*value.right));
            } else if constexpr (std::is_same_v<T, HirCastExpr>) {
                return value.expression &&
                       isDefinitelyPure(exprRef(*value.expression));
            } else {
                return false;
            }
        },
        expr.value);
}

HirStmtId makeStmtId(HirStmt&& stmt) {
    return g_module->addStmt(std::move(stmt));
}

HirItemId makeItemId(HirStmtId stmtId) {
    HirItem item;
    item.value = stmtId;
    return g_module->addItem(std::move(item));
}

HirExprId makeExprId(HirExpr&& expr) {
    return g_module->addExpr(std::move(expr));
}

bool isEmptyBlockStmt(const HirStmt& stmt) {
    const auto* block = std::get_if<HirBlockStmt>(&stmt.value);
    return block && block->items.empty();
}

void replaceStmtPreservingNode(HirStmt& target,
                               const std::optional<HirStmtId>& replacement) {
    if (!replacement) {
        target.value = HirBlockStmt{};
        return;
    }
    target.value = stmtRef(*replacement).value;
}

void replaceExprPreservingNode(HirExpr& target,
                               const std::optional<HirExprId>& replacement) {
    if (!replacement) {
        return;
    }
    HirExpr replacementExpr = exprRef(*replacement);
    replacementExpr.node = target.node;
    target = std::move(replacementExpr);
}

void replaceExprWithLogicalNotPreservingNode(HirExpr& target,
                                             const std::optional<HirExprId>& operand) {
    if (!operand) {
        return;
    }

    HirExpr replacement;
    replacement.node = target.node;
    HirUnaryExpr logicalNot;
    logicalNot.op = Token::synthetic(TokenType::BANG, "!", target.node.span);
    logicalNot.operand = operand;
    replacement.value = std::move(logicalNot);
    replaceExprPreservingNode(target, makeExprId(std::move(replacement)));
}

bool sameKnownType(const ConstantEvaluator& evaluator, const HirNodeInfo& lhs,
                   const HirNodeInfo& rhs) {
    TypeRef leftType = evaluator.typeOf(lhs);
    TypeRef rightType = evaluator.typeOf(rhs);
    return leftType && rightType && leftType == rightType;
}

bool canReuseOperandAsResult(const HirExpr& expr, const HirExpr& operand,
                             const ConstantEvaluator& evaluator) {
    return sameKnownType(evaluator, expr.node, operand.node);
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

bool isKnownBoolType(const ConstantEvaluator& evaluator, const HirNodeInfo& node) {
    TypeRef type = evaluator.typeOf(node);
    return type && type->kind == TypeKind::BOOL;
}

bool isKnownIntegerType(const ConstantEvaluator& evaluator,
                        const HirNodeInfo& node) {
    TypeRef type = evaluator.typeOf(node);
    return type && type->isInteger();
}

bool replaceWithOperandIfTypeMatches(HirExpr& expr,
                                     std::optional<HirExprId>& operand,
                                     const ConstantEvaluator& evaluator) {
    if (!operand || !canReuseOperandAsResult(expr, exprRef(*operand), evaluator)) {
        return false;
    }
    replaceExprPreservingNode(expr, operand);
    return true;
}

bool replaceWithLogicalNotOfOperandIfTypeMatches(
    HirExpr& expr, std::optional<HirExprId>& operand,
    const ConstantEvaluator& evaluator) {
    if (!operand || !isKnownBoolType(evaluator, expr.node) ||
        !isKnownBoolType(evaluator, exprRef(*operand).node) ||
        !canReuseOperandAsResult(expr, exprRef(*operand), evaluator)) {
        return false;
    }

    replaceExprWithLogicalNotPreservingNode(expr, operand);
    return true;
}

bool simplifyUnaryExpr(HirExpr& expr, const ConstantEvaluator& evaluator) {
    auto* unary = std::get_if<HirUnaryExpr>(&expr.value);
    if (!unary || !unary->operand) {
        return false;
    }

    if (unary->op.type() == TokenType::BANG) {
        if (!isKnownBoolType(evaluator, expr.node)) {
            return false;
        }

        auto* nestedUnary =
            std::get_if<HirUnaryExpr>(&exprRef(*unary->operand).value);
        if (!nestedUnary || nestedUnary->op.type() != TokenType::BANG ||
            !nestedUnary->operand ||
            !isKnownBoolType(evaluator, exprRef(*nestedUnary->operand).node)) {
            return false;
        }

        return replaceWithOperandIfTypeMatches(expr, nestedUnary->operand,
                                               evaluator);
    }

    if (unary->op.type() != TokenType::TILDE ||
        !isKnownIntegerType(evaluator, expr.node)) {
        return false;
    }

    auto* nestedUnary =
        std::get_if<HirUnaryExpr>(&exprRef(*unary->operand).value);
    if (!nestedUnary || nestedUnary->op.type() != TokenType::TILDE ||
        !nestedUnary->operand ||
        !isKnownIntegerType(evaluator, exprRef(*nestedUnary->operand).node)) {
        return false;
    }

    return replaceWithOperandIfTypeMatches(expr, nestedUnary->operand,
                                           evaluator);
}

bool simplifyBooleanEqualityBinary(HirExpr& expr, HirBinaryExpr& binary,
                                   const ConstantEvaluator& evaluator) {
    if (!binary.left || !binary.right ||
        !isKnownBoolType(evaluator, expr.node) ||
        !isKnownBoolType(evaluator, exprRef(*binary.left).node) ||
        !isKnownBoolType(evaluator, exprRef(*binary.right).node)) {
        return false;
    }

    const auto simplifyAgainstConstant = [&](std::optional<HirExprId>& operand,
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
    if (tryEvaluateConditionBool(exprRef(*binary.left), evaluator, leftValue)) {
        return simplifyAgainstConstant(binary.right, leftValue);
    }

    bool rightValue = false;
    if (tryEvaluateConditionBool(exprRef(*binary.right), evaluator, rightValue)) {
        return simplifyAgainstConstant(binary.left, rightValue);
    }

    return false;
}

bool simplifyIdentityBinary(HirExpr& expr, HirBinaryExpr& binary,
                            const ConstantEvaluator& evaluator) {
    if (!binary.left || !binary.right) {
        return false;
    }

    TypeRef exprType = evaluator.typeOf(expr.node);
    if (!exprType) {
        return false;
    }

    ConstantValue leftConstant;
    ConstantValue rightConstant;
    const bool hasLeftConstant =
        tryEvaluateConstant(exprRef(*binary.left), evaluator, leftConstant);
    const bool hasRightConstant =
        tryEvaluateConstant(exprRef(*binary.right), evaluator, rightConstant);

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
                    isDefinitelyPure(exprRef(*binary.left))) {
                    return replaceWithOperandIfTypeMatches(expr, binary.right,
                                                           evaluator);
                }
                if (hasLeftConstant && isZeroConstant(leftConstant) &&
                    isDefinitelyPure(exprRef(*binary.right))) {
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
                isDefinitelyPure(exprRef(*binary.left))) {
                return replaceWithOperandIfTypeMatches(expr, binary.right,
                                                       evaluator);
            }
            if (hasLeftConstant && isZeroConstant(leftConstant) &&
                isDefinitelyPure(exprRef(*binary.right))) {
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

bool simplifyLogicalBinary(HirExpr& expr, HirBinaryExpr& binary,
                           const ConstantEvaluator& evaluator) {
    if (!binary.left || !binary.right) {
        return false;
    }

    bool leftValue = false;
    bool rightValue = false;
    const bool hasLeftConstant =
        tryEvaluateConditionBool(exprRef(*binary.left), evaluator, leftValue);
    const bool hasRightConstant =
        tryEvaluateConditionBool(exprRef(*binary.right), evaluator, rightValue);

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
                if (!isDefinitelyPure(exprRef(*binary.right))) {
                    return false;
                }
                return replaceWithBoolLiteral(false);
            }

            if (hasRightConstant) {
                if (rightValue) {
                    return replaceWithOperandIfTypeMatches(expr, binary.left,
                                                           evaluator);
                }
                if (!isDefinitelyPure(exprRef(*binary.left))) {
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
                if (!isDefinitelyPure(exprRef(*binary.right))) {
                    return false;
                }
                return replaceWithBoolLiteral(true);
            }

            if (hasRightConstant) {
                if (!rightValue) {
                    return replaceWithOperandIfTypeMatches(expr, binary.left,
                                                           evaluator);
                }
                if (!isDefinitelyPure(exprRef(*binary.left))) {
                    return false;
                }
                return replaceWithBoolLiteral(true);
            }
            return false;
        default:
            return false;
    }
}

bool simplifyBinaryExpr(HirExpr& expr, const ConstantEvaluator& evaluator) {
    auto* binary = std::get_if<HirBinaryExpr>(&expr.value);
    if (!binary) {
        return false;
    }

    if (simplifyLogicalBinary(expr, *binary, evaluator)) {
        return true;
    }

    binary = std::get_if<HirBinaryExpr>(&expr.value);
    if (binary && simplifyBooleanEqualityBinary(expr, *binary, evaluator)) {
        return true;
    }

    binary = std::get_if<HirBinaryExpr>(&expr.value);
    return binary && simplifyIdentityBinary(expr, *binary, evaluator);
}

void optimizeExprTree(HirExpr& expr, const ConstantEvaluator& evaluator) {
    std::visit(
        [&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, HirUnaryExpr>) {
                if (value.operand) {
                    optimizeExprTree(exprRef(*value.operand), evaluator);
                }
            } else if constexpr (std::is_same_v<T, HirUpdateExpr>) {
                if (value.operand) {
                    optimizeExprTree(exprRef(*value.operand), evaluator);
                }
            } else if constexpr (std::is_same_v<T, HirBinaryExpr>) {
                if (value.left) {
                    optimizeExprTree(exprRef(*value.left), evaluator);
                }
                if (value.right) {
                    optimizeExprTree(exprRef(*value.right), evaluator);
                }
            } else if constexpr (std::is_same_v<T, HirAssignmentExpr>) {
                if (value.target) {
                    optimizeExprTree(exprRef(*value.target), evaluator);
                }
                if (value.value) {
                    optimizeExprTree(exprRef(*value.value), evaluator);
                }
            } else if constexpr (std::is_same_v<T, HirCallExpr>) {
                if (value.callee) {
                    optimizeExprTree(exprRef(*value.callee), evaluator);
                }
                for (HirExprId argumentId : value.arguments) {
                    optimizeExprTree(exprRef(argumentId), evaluator);
                }
            } else if constexpr (std::is_same_v<T, HirMemberExpr>) {
                if (value.object) {
                    optimizeExprTree(exprRef(*value.object), evaluator);
                }
            } else if constexpr (std::is_same_v<T, HirIndexExpr>) {
                if (value.object) {
                    optimizeExprTree(exprRef(*value.object), evaluator);
                }
                if (value.index) {
                    optimizeExprTree(exprRef(*value.index), evaluator);
                }
            } else if constexpr (std::is_same_v<T, HirCastExpr>) {
                if (value.expression) {
                    optimizeExprTree(exprRef(*value.expression), evaluator);
                }
            } else if constexpr (std::is_same_v<T, HirFunctionExpr>) {
                if (value.blockBody) {
                    optimizeStmtTree(stmtRef(*value.blockBody), evaluator);
                }
                if (value.expressionBody) {
                    optimizeExprTree(exprRef(*value.expressionBody), evaluator);
                }
            } else if constexpr (std::is_same_v<T, HirArrayLiteralExpr>) {
                for (HirExprId elementId : value.elements) {
                    optimizeExprTree(exprRef(elementId), evaluator);
                }
            } else if constexpr (std::is_same_v<T, HirDictLiteralExpr>) {
                for (auto& entry : value.entries) {
                    if (entry.key) {
                        optimizeExprTree(exprRef(*entry.key), evaluator);
                    }
                    if (entry.value) {
                        optimizeExprTree(exprRef(*entry.value), evaluator);
                    }
                }
            }
        },
        expr.value);

    simplifyUnaryExpr(expr, evaluator);
    simplifyBinaryExpr(expr, evaluator);

    ConstantValue constant;
    if (tryEvaluateConstant(expr, evaluator, constant)) {
        expr = evaluator.makeLiteralExpr(expr, constant);
    }
}

void pruneUnreachableBlockItems(HirBlockStmt& block) {
    size_t reachableCount = block.items.size();
    for (size_t index = 0; index < block.items.size(); ++index) {
        if (isDefinitelyTerminal(itemRef(block.items[index]))) {
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

void removeNoOpBlockItems(HirBlockStmt& block) {
    std::vector<HirItemId> kept;
    kept.reserve(block.items.size());
    for (HirItemId itemId : block.items) {
        bool keepItem = true;
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, HirStmtId>) {
                    keepItem = !isEmptyBlockStmt(stmtRef(value));
                }
            },
            itemRef(itemId).value);

        if (keepItem) {
            kept.push_back(itemId);
        }
    }

    block.items = std::move(kept);
}

std::optional<HirStmtId> takeForInitializerStmt(HirForStmt& loop) {
    if (!loop.initializer) {
        return std::nullopt;
    }

    const HirStmtId initializer = *loop.initializer;
    loop.initializer.reset();
    return initializer;
}

HirStmtId makeForFalseReplacement(HirForStmt& loop, const HirStmt& original) {
    HirBlockStmt block;
    if (std::optional<HirStmtId> initializer = takeForInitializerStmt(loop)) {
        block.items.push_back(makeItemId(*initializer));
    }

    HirStmt stmt;
    stmt.node = original.node;
    stmt.value = std::move(block);
    return makeStmtId(std::move(stmt));
}

void optimizeStmtTree(HirStmt& stmt, const ConstantEvaluator& evaluator) {
    std::optional<HirStmtId> replacementStmt;
    bool replaceWithEmptyBlock = false;

    std::visit(
        [&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, HirBlockStmt>) {
                for (HirItemId itemId : value.items) {
                    optimizeItemTree(itemRef(itemId), evaluator);
                }

                removeNoOpBlockItems(value);
                pruneUnreachableBlockItems(value);
                removeNoOpBlockItems(value);
            } else if constexpr (std::is_same_v<T, HirExprStmt>) {
                if (value.expression) {
                    optimizeExprTree(exprRef(*value.expression), evaluator);
                }
            } else if constexpr (std::is_same_v<T, HirPrintStmt>) {
                if (value.expression) {
                    optimizeExprTree(exprRef(*value.expression), evaluator);
                }
            } else if constexpr (std::is_same_v<T, HirReturnStmt>) {
                if (value.value) {
                    optimizeExprTree(exprRef(*value.value), evaluator);
                }
            } else if constexpr (std::is_same_v<T, HirIfStmt>) {
                if (value.condition) {
                    optimizeExprTree(exprRef(*value.condition), evaluator);
                }
                if (value.thenBranch) {
                    optimizeStmtTree(stmtRef(*value.thenBranch), evaluator);
                }
                if (value.elseBranch) {
                    optimizeStmtTree(stmtRef(*value.elseBranch), evaluator);
                }

                bool condition = false;
                if (!value.condition ||
                    !tryEvaluateConditionBool(exprRef(*value.condition), evaluator,
                                              condition)) {
                    return;
                }

                if (condition) {
                    replacementStmt = value.thenBranch;
                } else if (value.elseBranch) {
                    replacementStmt = value.elseBranch;
                } else {
                    replaceWithEmptyBlock = true;
                }
            } else if constexpr (std::is_same_v<T, HirWhileStmt>) {
                if (value.condition) {
                    optimizeExprTree(exprRef(*value.condition), evaluator);
                }
                if (value.body) {
                    optimizeStmtTree(stmtRef(*value.body), evaluator);
                }

                bool condition = false;
                if (value.condition &&
                    tryEvaluateConditionBool(exprRef(*value.condition), evaluator,
                                             condition) &&
                    !condition) {
                    replaceWithEmptyBlock = true;
                }
            } else if constexpr (std::is_same_v<T, HirVarDeclStmt>) {
                if (value.initializer) {
                    optimizeExprTree(exprRef(*value.initializer), evaluator);
                }
            } else if constexpr (std::is_same_v<T, HirDestructuredImportStmt>) {
                if (value.initializer) {
                    optimizeExprTree(exprRef(*value.initializer), evaluator);
                }
            } else if constexpr (std::is_same_v<T, HirForStmt>) {
                if (value.initializer) {
                    HirStmt& initializer = stmtRef(*value.initializer);
                    if (auto* initDecl =
                            std::get_if<HirVarDeclStmt>(&initializer.value)) {
                        if (initDecl->initializer) {
                            optimizeExprTree(exprRef(*initDecl->initializer),
                                             evaluator);
                        }
                    } else if (auto* initExpr =
                                   std::get_if<HirExprStmt>(&initializer.value);
                               initExpr && initExpr->expression) {
                        optimizeExprTree(exprRef(*initExpr->expression),
                                         evaluator);
                    }
                }
                if (value.condition) {
                    optimizeExprTree(exprRef(*value.condition), evaluator);
                }
                if (value.increment) {
                    optimizeExprTree(exprRef(*value.increment), evaluator);
                }
                if (value.body) {
                    optimizeStmtTree(stmtRef(*value.body), evaluator);
                }

                bool condition = false;
                if (value.condition &&
                    tryEvaluateConditionBool(exprRef(*value.condition), evaluator,
                                             condition) &&
                    !condition) {
                    return;
                }
            } else if constexpr (std::is_same_v<T, HirForEachStmt>) {
                if (value.iterable) {
                    optimizeExprTree(exprRef(*value.iterable), evaluator);
                }
                if (value.body) {
                    optimizeStmtTree(stmtRef(*value.body), evaluator);
                }
            }
        },
        stmt.value);

    if (replacementStmt) {
        replaceStmtPreservingNode(stmt, replacementStmt);
    } else if (replaceWithEmptyBlock) {
        stmt.value = HirBlockStmt{};
    }
}

void optimizeItemTree(HirItem& item, const ConstantEvaluator& evaluator) {
    std::visit(
        [&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, HirFunctionDecl>) {
                if (value.body) {
                    optimizeStmtTree(stmtRef(*value.body), evaluator);
                }
            } else if constexpr (std::is_same_v<T, HirClassDecl>) {
                for (auto& method : value.methods) {
                    if (method.body) {
                        optimizeStmtTree(stmtRef(*method.body), evaluator);
                    }
                }
            } else if constexpr (std::is_same_v<T, HirStmtId>) {
                optimizeStmtTree(stmtRef(value), evaluator);
            }
        },
        item.value);
}

}  // namespace

}  // namespace hir_optimizer_detail

void optimizeHir(HirModule& module) {
    hir_optimizer_detail::g_module = &module;
    hir_optimizer_detail::ConstantEvaluator evaluator;
    for (HirItemId itemId : module.items) {
        hir_optimizer_detail::optimizeItemTree(module.item(itemId), evaluator);
    }
}
