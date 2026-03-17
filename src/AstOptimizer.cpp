#include "AstOptimizer.hpp"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <variant>

#include "Token.hpp"

namespace {

constexpr uint64_t kMaxExactIntegerInDouble = 9007199254740992ULL;

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

std::string stripNumericSuffix(std::string text) {
    auto strip = [&](std::string_view suffix) -> bool {
        if (text.size() < suffix.size()) {
            return false;
        }
        if (text.compare(text.size() - suffix.size(), suffix.size(),
                         suffix.data()) != 0) {
            return false;
        }
        text.resize(text.size() - suffix.size());
        return true;
    };

    strip("usize") || strip("i16") || strip("i32") || strip("i64") ||
        strip("u16") || strip("u32") || strip("u64") || strip("f32") ||
        strip("f64") || strip("i8") || strip("u8") || strip("u");
    return text;
}

bool parseSignedIntegerLiteral(const Token& token, int64_t& outValue) {
    if (token.type() != TokenType::NUMBER) {
        return false;
    }

    try {
        size_t parsed = 0;
        std::string text = stripNumericSuffix(
            std::string(token.start(), token.length()));
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

bool parseUnsignedIntegerLiteral(const Token& token, uint64_t& outValue) {
    if (token.type() != TokenType::NUMBER) {
        return false;
    }

    try {
        size_t parsed = 0;
        std::string text = stripNumericSuffix(
            std::string(token.start(), token.length()));
        unsigned long long value = std::stoull(text, &parsed, 10);
        if (parsed != text.size()) {
            return false;
        }
        outValue = static_cast<uint64_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

bool parseFloatLiteral(const Token& token, double& outValue) {
    if (token.type() != TokenType::NUMBER) {
        return false;
    }

    try {
        size_t parsed = 0;
        std::string text = stripNumericSuffix(
            std::string(token.start(), token.length()));
        double value = std::stod(text, &parsed);
        if (parsed != text.size() || !std::isfinite(value)) {
            return false;
        }
        outValue = value;
        return true;
    } catch (...) {
        return false;
    }
}

std::string integerSuffix(TypeKind kind) {
    switch (kind) {
        case TypeKind::I8:
            return "i8";
        case TypeKind::I16:
            return "i16";
        case TypeKind::I32:
            return "i32";
        case TypeKind::I64:
            return "i64";
        case TypeKind::U8:
            return "u8";
        case TypeKind::U16:
            return "u16";
        case TypeKind::U32:
            return "u32";
        case TypeKind::U64:
            return "u64";
        case TypeKind::USIZE:
            return "usize";
        default:
            return "";
    }
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

class ConstantEvaluator {
   public:
    explicit ConstantEvaluator(const AstSemanticModel& semanticModel)
        : m_semanticModel(semanticModel) {}

    bool evaluate(const AstExpr& expr, ConstantValue& out) const {
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

    AstExpr makeLiteralExpr(const AstExpr& original,
                            const ConstantValue& value) const {
        AstExpr expr;
        expr.node = original.node;

        AstLiteralExpr literal;
        switch (value.kind) {
            case ConstantValue::Kind::Boolean:
                literal.token = Token::synthetic(value.boolValue ? TokenType::TRUE
                                                                 : TokenType::FALSE,
                                                 value.boolValue ? "true"
                                                                 : "false",
                                                 original.node.line);
                break;
            case ConstantValue::Kind::Null:
                literal.token =
                    Token::synthetic(TokenType::_NULL, "null", original.node.line);
                break;
            case ConstantValue::Kind::SignedInteger: {
                TypeRef type = typeOf(original.node.id);
                std::string text = std::to_string(value.signedValue);
                if (type && type->isInteger()) {
                    text += integerSuffix(type->kind);
                }
                literal.token =
                    Token::synthetic(TokenType::NUMBER, std::move(text),
                                     original.node.line);
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
                                     original.node.line);
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
                                     original.node.line);
                break;
            }
        }

        expr.value = std::move(literal);
        return expr;
    }

    bool evaluateConditionBool(const AstExpr& expr, bool& outValue) const {
        ConstantValue constant;
        if (!evaluate(expr, constant) ||
            constant.kind != ConstantValue::Kind::Boolean) {
            return false;
        }
        outValue = constant.boolValue;
        return true;
    }

    TypeRef typeOf(AstNodeId id) const {
        auto it = m_semanticModel.nodeTypes.find(id);
        if (it == m_semanticModel.nodeTypes.end()) {
            return nullptr;
        }
        return it->second;
    }

   private:
    const AstSemanticModel& m_semanticModel;

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

    bool evaluateLiteral(const AstExpr& expr, const AstLiteralExpr& literal,
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

    bool evaluateUnary(const AstExpr& expr, const AstUnaryExpr& unary,
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

    bool evaluateNumericBinary(const AstBinaryExpr& binary,
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

    bool evaluateBitwiseBinary(const AstExpr& expr, const AstBinaryExpr& binary,
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

    bool evaluateShiftBinary(const AstBinaryExpr& binary,
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

    bool evaluateBinary(const AstExpr& expr, const AstBinaryExpr& binary,
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
};

void optimizeExpr(AstExpr& expr, const ConstantEvaluator& evaluator);
void optimizeStmt(AstStmt& stmt, const ConstantEvaluator& evaluator);
void optimizeItem(AstItem& item, const ConstantEvaluator& evaluator);
bool isDefinitelyTerminal(const AstStmt& stmt);
bool isDefinitelyTerminal(const AstItem& item);

bool tryEvaluateConstant(const AstExpr& expr, const ConstantEvaluator& evaluator,
                         ConstantValue& out) {
    return evaluator.evaluate(expr, out);
}

bool tryEvaluateConditionBool(const AstExpr& expr,
                              const ConstantEvaluator& evaluator,
                              bool& outValue) {
    return evaluator.evaluateConditionBool(expr, outValue);
}

bool blockIsDefinitelyTerminal(const AstBlockStmt& block) {
    for (auto it = block.items.rbegin(); it != block.items.rend(); ++it) {
        if (*it) {
            return isDefinitelyTerminal(**it);
        }
    }
    return false;
}

bool isDefinitelyTerminal(const AstStmt& stmt) {
    return std::visit(
        [&](const auto& value) -> bool {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, AstReturnStmt>) {
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

bool isEmptyBlockStmt(const AstStmt& stmt) {
    const auto* block = std::get_if<AstBlockStmt>(&stmt.value);
    return block && block->items.empty();
}

void replaceStmtWith(AstStmt& target, AstStmtPtr replacement) {
    if (!replacement) {
        target.value = AstBlockStmt{};
        return;
    }
    target.value = std::move(replacement->value);
}

void replaceExprWith(AstExpr& target, AstExprPtr replacement) {
    if (!replacement) {
        return;
    }
    replacement->node = target.node;
    target = std::move(*replacement);
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

bool replaceWithOperandIfTypeMatches(AstExpr& expr, AstExprPtr& operand,
                                     const ConstantEvaluator& evaluator) {
    if (!operand || !canReuseOperandAsResult(expr, *operand, evaluator)) {
        return false;
    }
    replaceExprWith(expr, std::move(operand));
    return true;
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
    return binary && simplifyIdentityBinary(expr, *binary, evaluator);
}

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

void optimizeExpr(AstExpr& expr, const ConstantEvaluator& evaluator) {
    std::visit(
        [&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, AstGroupingExpr>) {
                if (value.expression) {
                    optimizeExpr(*value.expression, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstUnaryExpr>) {
                if (value.operand) {
                    optimizeExpr(*value.operand, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstBinaryExpr>) {
                if (value.left) {
                    optimizeExpr(*value.left, evaluator);
                }
                if (value.right) {
                    optimizeExpr(*value.right, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstAssignmentExpr>) {
                if (value.target) {
                    optimizeExpr(*value.target, evaluator);
                }
                if (value.value) {
                    optimizeExpr(*value.value, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstCallExpr>) {
                if (value.callee) {
                    optimizeExpr(*value.callee, evaluator);
                }
                for (auto& argument : value.arguments) {
                    if (argument) {
                        optimizeExpr(*argument, evaluator);
                    }
                }
            } else if constexpr (std::is_same_v<T, AstMemberExpr>) {
                if (value.object) {
                    optimizeExpr(*value.object, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstIndexExpr>) {
                if (value.object) {
                    optimizeExpr(*value.object, evaluator);
                }
                if (value.index) {
                    optimizeExpr(*value.index, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstCastExpr>) {
                if (value.expression) {
                    optimizeExpr(*value.expression, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstFunctionExpr>) {
                if (value.blockBody) {
                    optimizeStmt(*value.blockBody, evaluator);
                }
                if (value.expressionBody) {
                    optimizeExpr(*value.expressionBody, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstArrayLiteralExpr>) {
                for (auto& element : value.elements) {
                    if (element) {
                        optimizeExpr(*element, evaluator);
                    }
                }
            } else if constexpr (std::is_same_v<T, AstDictLiteralExpr>) {
                for (auto& entry : value.entries) {
                    if (entry.key) {
                        optimizeExpr(*entry.key, evaluator);
                    }
                    if (entry.value) {
                        optimizeExpr(*entry.value, evaluator);
                    }
                }
            }
        },
        expr.value);

    simplifyBinaryExpr(expr, evaluator);

    ConstantValue constant;
    if (tryEvaluateConstant(expr, evaluator, constant)) {
        expr = evaluator.makeLiteralExpr(expr, constant);
    }
}

void optimizeStmt(AstStmt& stmt, const ConstantEvaluator& evaluator) {
    std::visit(
        [&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, AstBlockStmt>) {
                for (auto& item : value.items) {
                    if (item) {
                        optimizeItem(*item, evaluator);
                    }
                }

                removeNoOpBlockItems(value);
                pruneUnreachableBlockItems(value);
                removeNoOpBlockItems(value);
            } else if constexpr (std::is_same_v<T, AstExprStmt>) {
                if (value.expression) {
                    optimizeExpr(*value.expression, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstPrintStmt>) {
                if (value.expression) {
                    optimizeExpr(*value.expression, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstReturnStmt>) {
                if (value.value) {
                    optimizeExpr(*value.value, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstIfStmt>) {
                if (value.condition) {
                    optimizeExpr(*value.condition, evaluator);
                }
                if (value.thenBranch) {
                    optimizeStmt(*value.thenBranch, evaluator);
                }
                if (value.elseBranch) {
                    optimizeStmt(*value.elseBranch, evaluator);
                }

                bool condition = false;
                if (!value.condition ||
                    !tryEvaluateConditionBool(*value.condition, evaluator,
                                              condition)) {
                    return;
                }

                if (condition) {
                    replaceStmtWith(stmt, std::move(value.thenBranch));
                } else if (value.elseBranch) {
                    replaceStmtWith(stmt, std::move(value.elseBranch));
                } else {
                    stmt.value = AstBlockStmt{};
                }
            } else if constexpr (std::is_same_v<T, AstWhileStmt>) {
                if (value.condition) {
                    optimizeExpr(*value.condition, evaluator);
                }
                if (value.body) {
                    optimizeStmt(*value.body, evaluator);
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
                    optimizeExpr(*value.initializer, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstDestructuredImportStmt>) {
                if (value.initializer) {
                    optimizeExpr(*value.initializer, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstForStmt>) {
                if (auto* initDecl =
                        std::get_if<std::unique_ptr<AstVarDeclStmt>>(
                            &value.initializer)) {
                    if (*initDecl && (*initDecl)->initializer) {
                        optimizeExpr(*(*initDecl)->initializer, evaluator);
                    }
                } else if (auto* initExpr =
                               std::get_if<AstExprPtr>(&value.initializer)) {
                    if (*initExpr) {
                        optimizeExpr(**initExpr, evaluator);
                    }
                }
                if (value.condition) {
                    optimizeExpr(*value.condition, evaluator);
                }
                if (value.increment) {
                    optimizeExpr(*value.increment, evaluator);
                }
                if (value.body) {
                    optimizeStmt(*value.body, evaluator);
                }

                bool condition = false;
                if (value.condition &&
                    tryEvaluateConditionBool(*value.condition, evaluator,
                                             condition) &&
                    !condition) {
                    replaceStmtWith(stmt, makeForFalseReplacement(value, stmt));
                }
            } else if constexpr (std::is_same_v<T, AstForEachStmt>) {
                if (value.iterable) {
                    optimizeExpr(*value.iterable, evaluator);
                }
                if (value.body) {
                    optimizeStmt(*value.body, evaluator);
                }
            }
        },
        stmt.value);
}

void optimizeItem(AstItem& item, const ConstantEvaluator& evaluator) {
    std::visit(
        [&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, AstFunctionDecl>) {
                if (value.body) {
                    optimizeStmt(*value.body, evaluator);
                }
            } else if constexpr (std::is_same_v<T, AstClassDecl>) {
                for (auto& method : value.methods) {
                    if (method.body) {
                        optimizeStmt(*method.body, evaluator);
                    }
                }
            } else if constexpr (std::is_same_v<T, AstStmtPtr>) {
                if (value) {
                    optimizeStmt(*value, evaluator);
                }
            }
        },
        item.value);
}

}  // namespace

void optimizeAst(AstModule& module, const AstSemanticModel& semanticModel) {
    ConstantEvaluator evaluator(semanticModel);
    for (auto& item : module.items) {
        if (item) {
            optimizeItem(*item, evaluator);
        }
    }
}
