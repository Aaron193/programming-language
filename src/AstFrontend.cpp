#include "AstFrontend.hpp"

#include <algorithm>

#include "AstParser.hpp"
#include "AstSymbolCollector.hpp"

AstFrontendBuildStatus buildAstFrontend(std::string_view source,
                                        AstFrontendMode mode,
                                        std::vector<TypeError>& outErrors,
                                        AstFrontendResult& outFrontend) {
    AstModule module;
    AstParser parser(source);
    if (!parser.parseModule(module)) {
        outErrors.clear();
        for (const auto& error : parser.errors()) {
            outErrors.push_back(TypeError{error.line, error.message});
        }
        return AstFrontendBuildStatus::ParseFailed;
    }

    outFrontend = AstFrontendResult{};
    outFrontend.mode = mode;
    outFrontend.terminalLine =
        1 + static_cast<size_t>(std::count(source.begin(), source.end(), '\n'));
    outFrontend.module = std::move(module);
    collectSymbolsFromAst(outFrontend.module, outFrontend.classNames,
                          outFrontend.functionSignatures,
                          &outFrontend.typeAliases);

    if (mode == AstFrontendMode::StrictChecked) {
        analyzeAstSemantics(outFrontend.module, outFrontend.classNames,
                            outFrontend.typeAliases,
                            outFrontend.functionSignatures, outErrors,
                            &outFrontend.semanticModel);
        return outErrors.empty() ? AstFrontendBuildStatus::Success
                                 : AstFrontendBuildStatus::SemanticError;
    }

    std::vector<TypeError> ignoredErrors;
    analyzeAstSemantics(outFrontend.module, outFrontend.classNames,
                        outFrontend.typeAliases,
                        outFrontend.functionSignatures, ignoredErrors,
                        &outFrontend.semanticModel);
    outErrors.clear();
    return AstFrontendBuildStatus::Success;
}
