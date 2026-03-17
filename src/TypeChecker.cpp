#include "TypeChecker.hpp"

#include "AstParser.hpp"
#include "AstSemanticAnalyzer.hpp"
#include "AstSymbolCollector.hpp"

bool TypeChecker::collectSymbols(
    std::string_view source, std::unordered_set<std::string>& outClassNames,
    std::unordered_map<std::string, TypeRef>& outFunctionSignatures,
    std::unordered_map<std::string, TypeRef>* outTypeAliases) {
    return collectSymbolsFromAst(source, outClassNames, outFunctionSignatures,
                                 outTypeAliases);
}

bool TypeChecker::check(
    std::string_view source, const std::unordered_set<std::string>& classNames,
    const std::unordered_map<std::string, TypeRef>& typeAliases,
    const std::unordered_map<std::string, TypeRef>& functionSignatures,
    std::vector<TypeError>& out, TypeCheckerMetadata* outMetadata) {
    AstModule module;
    AstParser parser(source);
    out.clear();
    if (!parser.parseModule(module)) {
        for (const auto& error : parser.errors()) {
            out.push_back(TypeError{error.span, error.message});
        }
        if (outMetadata) {
            *outMetadata = TypeCheckerMetadata{};
        }
        return false;
    }

    AstSemanticModel semanticModel;
    analyzeAstSemantics(module, classNames, typeAliases, functionSignatures,
                        out, &semanticModel);
    if (outMetadata) {
        *outMetadata = semanticModel.metadata;
    }
    return out.empty();
}
