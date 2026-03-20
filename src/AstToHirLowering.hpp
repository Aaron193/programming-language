#pragma once

#include "AstFrontend.hpp"

bool lowerAstToHir(const AstFrontendResult& frontend, HirModule& outModule);
