#pragma once

#include <cstdint>
#include <string>

#include "Ast.hpp"
#include "AstSemanticAnalyzer.hpp"

namespace ast_optimizer_detail {

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

class ConstantEvaluator {
   public:
    explicit ConstantEvaluator(const AstSemanticModel& semanticModel);

    bool evaluate(const AstExpr& expr, ConstantValue& out) const;
    AstExpr makeLiteralExpr(const AstExpr& original,
                            const ConstantValue& value) const;
    bool evaluateConditionBool(const AstExpr& expr, bool& outValue) const;
    TypeRef typeOf(AstNodeId id) const;

   private:
    const AstSemanticModel& m_semanticModel;

    bool constantToDouble(const ConstantValue& value, double& outValue) const;
    bool constantToBitwiseUnsigned(const ConstantValue& value,
                                   uint64_t& outValue) const;
    bool evaluateLiteral(const AstExpr& expr, const AstLiteralExpr& literal,
                         ConstantValue& out) const;
    bool evaluateUnary(const AstExpr& expr, const AstUnaryExpr& unary,
                       ConstantValue& out) const;
    bool evaluateNumericBinary(const AstBinaryExpr& binary,
                               const ConstantValue& left,
                               const ConstantValue& right,
                               ConstantValue& out) const;
    bool evaluateBitwiseBinary(const AstExpr& expr, const AstBinaryExpr& binary,
                               const ConstantValue& left,
                               const ConstantValue& right,
                               ConstantValue& out) const;
    bool evaluateShiftBinary(const AstBinaryExpr& binary,
                             const ConstantValue& left,
                             const ConstantValue& right,
                             ConstantValue& out) const;
    bool evaluateBinary(const AstExpr& expr, const AstBinaryExpr& binary,
                        ConstantValue& out) const;
};

bool tryEvaluateConstant(const AstExpr& expr, const ConstantEvaluator& evaluator,
                         ConstantValue& out);
bool tryEvaluateConditionBool(const AstExpr& expr,
                              const ConstantEvaluator& evaluator,
                              bool& outValue);

bool isDefinitelyTerminal(const AstStmt& stmt);
bool isDefinitelyTerminal(const AstItem& item);
bool isDefinitelyPure(const AstExpr& expr);

AstStmtPtr makeStmtPtr(AstStmt&& stmt);
AstItemPtr makeItemPtr(AstStmtPtr stmt);
bool isEmptyBlockStmt(const AstStmt& stmt);

// Statement rewrites keep the outer statement node so later phases continue to
// report diagnostics against the original source construct.
void replaceStmtPreservingNode(AstStmt& target, AstStmtPtr replacement);

// Expression rewrites preserve the replaced node id and line because refreshed
// post-optimization semantics are still keyed by the original expression node.
void replaceExprPreservingNode(AstExpr& target, AstExprPtr replacement);

bool sameKnownType(const ConstantEvaluator& evaluator, AstNodeId lhs,
                   AstNodeId rhs);
bool canReuseOperandAsResult(const AstExpr& expr, const AstExpr& operand,
                             const ConstantEvaluator& evaluator);
bool isZeroConstant(const ConstantValue& value);
bool isOneConstant(const ConstantValue& value);

void optimizeExprTree(AstExpr& expr, const ConstantEvaluator& evaluator);
void optimizeStmtTree(AstStmt& stmt, const ConstantEvaluator& evaluator);
void optimizeItemTree(AstItem& item, const ConstantEvaluator& evaluator);

}  // namespace ast_optimizer_detail
