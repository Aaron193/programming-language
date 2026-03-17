#include "AstOptimizer.hpp"

#include "AstOptimizerInternal.hpp"

void optimizeAst(AstModule& module, const AstSemanticModel& semanticModel) {
    ast_optimizer_detail::ConstantEvaluator evaluator(semanticModel);
    for (auto& item : module.items) {
        if (item) {
            ast_optimizer_detail::optimizeItemTree(*item, evaluator);
        }
    }
}
