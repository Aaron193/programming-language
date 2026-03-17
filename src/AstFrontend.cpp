#include "AstFrontend.hpp"

#include "AstParser.hpp"
#include "AstSymbolCollector.hpp"

bool buildAstFrontend(std::string_view source, std::vector<TypeError>& outErrors,
                      AstFrontendResult& outFrontend) {
    AstModule module;
    AstParser parser(source);
    if (!parser.parseModule(module)) {
        return false;
    }

    outFrontend = AstFrontendResult{};
    outFrontend.module = std::move(module);
    collectSymbolsFromAst(outFrontend.module, outFrontend.classNames,
                          outFrontend.functionSignatures,
                          &outFrontend.typeAliases);
    analyzeAstSemantics(outFrontend.module, outFrontend.classNames,
                        outFrontend.typeAliases,
                        outFrontend.functionSignatures, outErrors,
                        &outFrontend.semanticModel);
    return outErrors.empty();
}
