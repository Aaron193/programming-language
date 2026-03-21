#include <iostream>
#include <string>

#include "tooling/FrontendTooling.hpp"

namespace {

bool require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }
    return true;
}

bool testStrictDirectiveDetection() {
    if (!require(toolingSourceStartsWithStrictDirective("#!strict\nprint(1)\n"),
                 "strict directive should be detected at file start")) {
        return false;
    }

    if (!require(!toolingSourceStartsWithStrictDirective("print(1)\n#!strict\n"),
                 "strict directive should only be detected at file start")) {
        return false;
    }

    return true;
}

bool testRangeConversion() {
    SourceSpan span{
        makeSourcePosition(5, 3, 4),
        makeSourcePosition(9, 3, 8),
    };
    ToolingRange range = toolingRangeFromSourceSpan(span);
    if (!require(range.start.line == 2 && range.start.character == 3,
                 "tooling start position should be zero-based")) {
        return false;
    }

    if (!require(range.end.line == 2 && range.end.character == 7,
                 "tooling end position should be zero-based")) {
        return false;
    }

    return true;
}

bool testDiagnosticsAndSymbols() {
    const std::string source =
        "#!strict\n"
        "type Box i32\n"
        "fn add(x i32) i32 {\n"
        "    return x\n"
        "}\n"
        "var broken i32 = \"oops\"\n";

    ToolingAnalyzeOptions options;
    options.sourcePath = "tooling_regression.mog";
    options.strictMode = true;

    ToolingDocumentAnalysis analysis =
        analyzeDocumentForTooling(source, options);

    if (!require(analysis.status == AstFrontendBuildStatus::SemanticError,
                 "semantic error source should return semantic error status")) {
        return false;
    }

    if (!require(!analysis.diagnostics.empty(),
                 "semantic error source should produce diagnostics")) {
        return false;
    }

    const ToolingDiagnostic& diagnostic = analysis.diagnostics.front();
    if (!require(diagnostic.range.start.line == 5,
                 "diagnostic line should be converted to zero-based indexing")) {
        return false;
    }

    if (!require(diagnostic.message.find("cannot assign") != std::string::npos,
                 "diagnostic message should preserve frontend error text")) {
        return false;
    }

    if (!require(analysis.hasParse,
                 "semantic error source should still expose parse success")) {
        return false;
    }

    if (!require(analysis.hasBindings,
                 "semantic error source should preserve bindings when possible")) {
        return false;
    }

    if (!require(!analysis.documentSymbols.empty(),
                 "document symbols should remain available after type errors")) {
        return false;
    }

    const std::string validSource =
        "#!strict\n"
        "type Box i32\n"
        "fn add(x i32) i32 {\n"
        "    return x\n"
        "}\n"
        "const Value i32 = 1\n";

    ToolingDocumentAnalysis validAnalysis =
        analyzeDocumentForTooling(validSource, options);
    if (!require(validAnalysis.status == AstFrontendBuildStatus::Success,
                 "valid source should succeed")) {
        return false;
    }

    if (!require(validAnalysis.documentSymbols.size() == 3,
                 "top-level type, function, and const should appear as document symbols")) {
        return false;
    }

    if (!require(validAnalysis.documentSymbols[0].name == "Box" &&
                     validAnalysis.documentSymbols[0].kind == "type",
                 "type alias symbol should be exposed")) {
        return false;
    }

    if (!require(validAnalysis.documentSymbols[1].name == "add" &&
                     validAnalysis.documentSymbols[1].kind == "function",
                 "function symbol should be exposed")) {
        return false;
    }

    if (!require(validAnalysis.documentSymbols[2].name == "Value" &&
                     validAnalysis.documentSymbols[2].kind == "constant",
                 "const symbol should be exposed")) {
        return false;
    }

    return true;
}

bool testDefinitionLookup() {
    ToolingAnalyzeOptions options;
    options.sourcePath = "tooling_definition_regression.mog";
    options.strictMode = true;

    const std::string sourceWithTypeError =
        "#!strict\n"
        "fn add(x i32) i32 {\n"
        "    var local i32 = x\n"
        "    return local\n"
        "}\n"
        "const Value i32 = add(1)\n"
        "var broken i32 = \"oops\"\n"
        "print(Value)\n";

    ToolingDocumentAnalysis analysis =
        analyzeDocumentForTooling(sourceWithTypeError, options);
    if (!require(analysis.hasBindings,
                 "definition lookup should work when type checking fails")) {
        return false;
    }

    const auto localDefinition =
        findDefinitionForTooling(analysis, ToolingPosition{3, 11});
    if (!require(localDefinition.has_value(),
                 "local variable use should resolve to its declaration")) {
        return false;
    }

    if (!require(localDefinition->selectionRange.start.line == 2 &&
                     localDefinition->selectionRange.start.character == 8,
                 "local definition should point at the declared local name")) {
        return false;
    }

    const auto topLevelDefinition =
        findDefinitionForTooling(analysis, ToolingPosition{7, 6});
    if (!require(topLevelDefinition.has_value(),
                 "top-level identifier use should resolve to its declaration")) {
        return false;
    }

    if (!require(topLevelDefinition->selectionRange.start.line == 5 &&
                     topLevelDefinition->selectionRange.start.character == 6,
                 "top-level definition should point at the const name")) {
        return false;
    }

    const std::string importSource =
        "#!strict\n"
        "const { Answer, Get } = @import(\"./modules/frontend_identity_module.mog\")\n"
        "print(Get())\n"
        "print(Answer)\n";
    options.sourcePath = "tests/sample_import_frontend_identity.mog";
    ToolingDocumentAnalysis importAnalysis =
        analyzeDocumentForTooling(importSource, options);
    const auto importDefinition =
        findDefinitionForTooling(importAnalysis, ToolingPosition{3, 7});
    if (!require(importDefinition.has_value(),
                 "imported binding use should resolve to the local import declaration")) {
        return false;
    }

    if (!require(importDefinition->selectionRange.start.line == 1 &&
                     importDefinition->selectionRange.start.character == 8,
                 "import definition should point at the import binding name")) {
        return false;
    }

    const std::string memberSource =
        "#!strict\n"
        "type Box struct {\n"
        "    value i32\n"
        "}\n"
        "fn read(box Box) i32 {\n"
        "    return box.value\n"
        "}\n";
    options.sourcePath = "tooling_member_definition_regression.mog";
    ToolingDocumentAnalysis memberAnalysis =
        analyzeDocumentForTooling(memberSource, options);
    const auto memberDefinition =
        findDefinitionForTooling(memberAnalysis, ToolingPosition{5, 15});
    if (!require(!memberDefinition.has_value(),
                 "member access should remain unsupported for definition lookup")) {
        return false;
    }

    return true;
}

}  // namespace

int main() {
    if (!testStrictDirectiveDetection()) {
        return 1;
    }

    if (!testRangeConversion()) {
        return 1;
    }

    if (!testDiagnosticsAndSymbols()) {
        return 1;
    }

    if (!testDefinitionLookup()) {
        return 1;
    }

    std::cout << "[PASS] frontend_tooling_regression\n";
    return 0;
}
