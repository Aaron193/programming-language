#include "AstFrontend.hpp"

#include <algorithm>

#include "AstOptimizer.hpp"
#include "AstParser.hpp"
#include "AstSymbolCollector.hpp"

namespace {

void appendParserErrors(const AstParser& parser,
                        std::vector<TypeError>& outErrors) {
    outErrors.clear();
    for (const auto& error : parser.errors()) {
        outErrors.push_back(TypeError{error.span, error.message});
    }
}

void analyzeFrontendSemantics(const AstFrontendResult& frontend,
                              std::vector<TypeError>& outErrors,
                              AstSemanticModel* outModel) {
    analyzeAstSemantics(frontend.module, frontend.classNames,
                        frontend.typeAliases, frontend.functionSignatures,
                        outErrors, outModel);
}

AstFrontendBuildStatus runSemanticPhases(AstFrontendResult& frontend,
                                         std::vector<TypeError>& outErrors) {
    if (frontend.mode == AstFrontendMode::StrictChecked) {
        analyzeFrontendSemantics(frontend, outErrors, &frontend.semanticModel);
        if (!outErrors.empty()) {
            return AstFrontendBuildStatus::SemanticError;
        }
    } else {
        std::vector<TypeError> ignoredErrors;
        analyzeFrontendSemantics(frontend, ignoredErrors, &frontend.semanticModel);
    }

    // The optimizer may read this semantic model while rewriting, but the
    // model becomes stale as soon as the AST mutates.
    optimizeAst(frontend.module, frontend.semanticModel);

    // The refreshed model below is the only semantic state lowering is allowed
    // to consume. No frontend code should rely on pre-optimization metadata
    // after the AST has been rewritten.
    frontend.semanticModel = AstSemanticModel{};
    outErrors.clear();
    analyzeFrontendSemantics(frontend, outErrors, &frontend.semanticModel);

    if (frontend.mode == AstFrontendMode::StrictChecked && !outErrors.empty()) {
        return AstFrontendBuildStatus::SemanticError;
    }

    outErrors.clear();
    return AstFrontendBuildStatus::Success;
}

}  // namespace

AstFrontendBuildStatus buildAstFrontend(std::string_view source,
                                        AstFrontendMode mode,
                                        std::vector<TypeError>& outErrors,
                                        AstFrontendResult& outFrontend) {
    AstModule module;
    AstParser parser(source);
    if (!parser.parseModule(module)) {
        appendParserErrors(parser, outErrors);
        return AstFrontendBuildStatus::ParseFailed;
    }

    outFrontend = AstFrontendResult{};
    outFrontend.mode = mode;
    outFrontend.terminalLine =
        1 + static_cast<size_t>(std::count(source.begin(), source.end(), '\n'));
    size_t terminalColumn = 1;
    if (!source.empty() && source.back() != '\n') {
        const size_t lastNewline = source.find_last_of('\n');
        terminalColumn =
            (lastNewline == std::string_view::npos)
                ? source.size() + 1
                : source.size() - lastNewline;
    }
    outFrontend.terminalPosition =
        makeSourcePosition(source.size(), outFrontend.terminalLine, terminalColumn);
    outFrontend.module = std::move(module);
    collectSymbolsFromAst(outFrontend.module, outFrontend.classNames,
                          outFrontend.functionSignatures,
                          &outFrontend.typeAliases);
    return runSemanticPhases(outFrontend, outErrors);
}
