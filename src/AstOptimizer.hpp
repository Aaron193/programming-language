#pragma once

#include "Ast.hpp"
#include "AstSemanticAnalyzer.hpp"

// The optimizer may consult the pre-optimization semantic model to make
// conservative rewrite decisions, but any AST mutation invalidates semantic data
// for affected nodes. Lowering must only consume a refreshed post-optimization
// AstSemanticModel.
void optimizeAst(AstModule& module, const AstSemanticModel& semanticModel);
