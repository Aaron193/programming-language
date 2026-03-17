#pragma once

#include "Ast.hpp"
#include "AstSemanticAnalyzer.hpp"

void optimizeAst(AstModule& module, const AstSemanticModel& semanticModel);
