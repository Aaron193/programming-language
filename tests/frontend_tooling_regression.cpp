#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
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

const ToolingCompletionItem* findCompletion(
    const std::vector<ToolingCompletionItem>& items, const std::string& label) {
    for (const auto& item : items) {
        if (item.label == label) {
            return &item;
        }
    }
    return nullptr;
}

const ToolingTextEdit* findEdit(const std::vector<ToolingTextEdit>& edits,
                                const std::string& path, size_t line,
                                size_t character) {
    for (const auto& edit : edits) {
        if (edit.path == path && edit.range.start.line == line &&
            edit.range.start.character == character) {
            return &edit;
        }
    }
    return nullptr;
}

bool writeFile(const std::filesystem::path& path, std::string_view text) {
    std::ofstream output(path);
    if (!output) {
        return false;
    }

    output << text;
    return output.good();
}

std::optional<std::string> readFileText(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        return std::nullopt;
    }

    std::string text((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
    if (!input.good() && !input.eof()) {
        return std::nullopt;
    }

    return text;
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

    const std::string importSource =
        "#!strict\n"
        "const {Answer as FinalAnswer: i32} = @import(\"./dep.mog\")\n";
    ToolingDocumentAnalysis importAnalysis =
        analyzeDocumentForTooling(importSource, options);
    if (!require(importAnalysis.documentSymbols.size() == 1,
                 "destructured import should appear as a document symbol")) {
        return false;
    }

    const ToolingDocumentSymbol& importSymbol = importAnalysis.documentSymbols[0];
    const bool startsInside =
        (importSymbol.range.start.line < importSymbol.selectionRange.start.line) ||
        (importSymbol.range.start.line == importSymbol.selectionRange.start.line &&
         importSymbol.range.start.character <=
             importSymbol.selectionRange.start.character);
    const bool endsInside =
        (importSymbol.selectionRange.end.line < importSymbol.range.end.line) ||
        (importSymbol.selectionRange.end.line == importSymbol.range.end.line &&
         importSymbol.selectionRange.end.character <= importSymbol.range.end.character);
    if (!require(startsInside && endsInside,
                 "document symbol selectionRange should be contained in range")) {
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
                 "imported binding use should resolve to the imported module declaration")) {
        return false;
    }

    if (!require(importDefinition->path.find("frontend_identity_module.mog") !=
                     std::string::npos,
                 "import definition should resolve to the imported module path")) {
        return false;
    }

    if (!require(importDefinition->selectionRange.start.line == 5 &&
                     importDefinition->selectionRange.start.character == 6,
                 "import definition should point at the exported binding name")) {
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
    if (!require(memberDefinition.has_value(),
                 "member access should resolve to the same-module field declaration")) {
        return false;
    }

    if (!require(memberDefinition->selectionRange.start.line == 2 &&
                     memberDefinition->selectionRange.start.character == 4,
                 "member definition should point at the field name")) {
        return false;
    }

    return true;
}

bool testReferencesAndHover() {
    ToolingAnalyzeOptions options;
    options.sourcePath = "tooling_references_hover_regression.mog";
    options.strictMode = true;

    const std::string source =
        "#!strict\n"
        "fn add(x i32) i32 {\n"
        "    var local i32 = x\n"
        "    return local + local\n"
        "}\n"
        "const Value i32 = add(1)\n"
        "print(Value)\n";

    ToolingDocumentAnalysis analysis =
        analyzeDocumentForTooling(source, options);
    if (!require(analysis.status == AstFrontendBuildStatus::Success,
                 "reference and hover sample should succeed")) {
        return false;
    }

    const auto references =
        findReferencesForTooling(analysis, ToolingPosition{6, 6});
    if (!require(references.size() == 2,
                 "top-level binding should return declaration and one usage")) {
        return false;
    }

    if (!require(references[0].selectionRange.start.line == 5 &&
                     references[0].selectionRange.start.character == 6,
                 "references should start with the declaration site")) {
        return false;
    }

    if (!require(references[1].selectionRange.start.line == 6 &&
                     references[1].selectionRange.start.character == 6,
                 "references should include same-file usages")) {
        return false;
    }

    const auto hover = findHoverForTooling(analysis, ToolingPosition{6, 6});
    if (!require(hover.has_value(), "hover should be available for bound identifiers")) {
        return false;
    }

    if (!require(hover->kind == "constant" &&
                     hover->detail == "const Value: i32",
                 "hover should include kind and formatted type detail")) {
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
    const auto importHover =
        findHoverForTooling(importAnalysis, ToolingPosition{3, 7});
    if (!require(importHover.has_value(),
                 "hover should be available for imported bindings")) {
        return false;
    }

    if (!require(importHover->kind == "import" &&
                     importHover->detail == "import Answer: i32",
                 "import hover should preserve imported type information")) {
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
    options.sourcePath = "tooling_member_hover_regression.mog";
    ToolingDocumentAnalysis memberAnalysis =
        analyzeDocumentForTooling(memberSource, options);
    const auto memberHover = findHoverForTooling(memberAnalysis, ToolingPosition{5, 15});
    if (!require(memberHover.has_value(),
                 "member access hover should be available for same-module fields")) {
        return false;
    }

    if (!require(memberHover->kind == "field" &&
                     memberHover->detail == "field value: i32",
                 "member hover should preserve field kind and type detail")) {
        return false;
    }

    return true;
}

bool testCompletions() {
    ToolingAnalyzeOptions options;
    options.strictMode = true;

    const std::string topLevelSource =
        "#!strict\n"
        "fn Helper() i32 {\n"
        "    return 1\n"
        "}\n"
        "\n"
        "const Later i32 = 2\n";
    options.sourcePath = "tooling_completion_top_level_regression.mog";
    ToolingDocumentAnalysis topLevelAnalysis =
        analyzeDocumentForTooling(topLevelSource, options);
    if (!require(topLevelAnalysis.status == AstFrontendBuildStatus::Success,
                 "top-level completion sample should succeed")) {
        return false;
    }

    const auto beforeLater =
        findCompletionsForTooling(topLevelAnalysis, ToolingPosition{4, 0});
    if (!require(findCompletion(beforeLater, "Helper") != nullptr,
                 "top-level functions should be visible before later declarations")) {
        return false;
    }
    if (!require(findCompletion(beforeLater, "Later") == nullptr,
                 "top-level const bindings should stay hidden before declaration")) {
        return false;
    }

    const auto afterLater =
        findCompletionsForTooling(topLevelAnalysis, ToolingPosition{6, 0});
    if (!require(findCompletion(afterLater, "Later") != nullptr,
                 "top-level const bindings should appear after declaration")) {
        return false;
    }

    const std::string localSource =
        "#!strict\n"
        "const Outer i32 = 1\n"
        "fn use(Value i32) i32 {\n"
        "    var local i32 = Value\n"
        "    {\n"
        "        const Value i32 = local\n"
        "\n"
        "    }\n"
        "\n"
        "}\n";
    options.sourcePath = "tooling_completion_local_regression.mog";
    ToolingDocumentAnalysis localAnalysis =
        analyzeDocumentForTooling(localSource, options);
    if (!require(localAnalysis.status == AstFrontendBuildStatus::Success,
                 "local completion sample should succeed")) {
        return false;
    }

    const auto innerScope =
        findCompletionsForTooling(localAnalysis, ToolingPosition{6, 8});
    const auto* innerValue = findCompletion(innerScope, "Value");
    if (!require(innerValue != nullptr && innerValue->kind == "constant",
                 "inner block completions should prefer the nearest shadowing binding")) {
        return false;
    }
    if (!require(findCompletion(innerScope, "local") != nullptr,
                 "inner block completions should include visible outer locals")) {
        return false;
    }

    const auto outerScope =
        findCompletionsForTooling(localAnalysis, ToolingPosition{8, 4});
    const auto* outerValue = findCompletion(outerScope, "Value");
    if (!require(outerValue != nullptr && outerValue->kind == "parameter",
                 "after leaving the block, completions should restore the parameter binding")) {
        return false;
    }

    const std::string importSemanticErrorSource =
        "#!strict\n"
        "const { Answer, Get } = @import(\"./modules/frontend_identity_module.mog\")\n"
        "fn use(x i32) i32 {\n"
        "    var local i32 = x\n"
        "    return local\n"
        "}\n"
        "var broken i32 = \"oops\"\n";
    options.sourcePath = "tests/sample_import_frontend_identity.mog";
    ToolingDocumentAnalysis importAnalysis =
        analyzeDocumentForTooling(importSemanticErrorSource, options);
    if (!require(importAnalysis.status == AstFrontendBuildStatus::SemanticError &&
                     importAnalysis.hasBindings,
                 "semantic-error completion sample should preserve bindings")) {
        return false;
    }

    const auto importCompletions =
        findCompletionsForTooling(importAnalysis, ToolingPosition{4, 10});
    if (!require(findCompletion(importCompletions, "Answer") != nullptr &&
                     findCompletion(importCompletions, "Get") != nullptr,
                 "imported bindings should appear in local completions")) {
        return false;
    }
    if (!require(findCompletion(importCompletions, "x") != nullptr &&
                     findCompletion(importCompletions, "local") != nullptr,
                 "local scope completions should survive semantic errors")) {
        return false;
    }

    const std::string parseFailSource =
        "#!strict\n"
        "fn broken(\n";
    options.sourcePath = "tooling_completion_parse_fail_regression.mog";
    ToolingDocumentAnalysis parseFailAnalysis =
        analyzeDocumentForTooling(parseFailSource, options);
    if (!require(parseFailAnalysis.status == AstFrontendBuildStatus::ParseFailed,
                 "parse-fail completion sample should fail parsing")) {
        return false;
    }

    const auto parseFailCompletions =
        findCompletionsForTooling(parseFailAnalysis, ToolingPosition{1, 10});
    if (!require(findCompletion(parseFailCompletions, "fn") != nullptr &&
                     findCompletion(parseFailCompletions, "while") != nullptr,
                 "parse-failed completions should still include keywords")) {
        return false;
    }
    if (!require(findCompletion(parseFailCompletions, "broken") == nullptr,
                 "parse-failed completions should not invent local bindings")) {
        return false;
    }

    const std::string memberSource =
        "#!strict\n"
        "type Box struct {\n"
        "    value i32\n"
        "\n"
        "    fn get() i32 {\n"
        "        return this.value\n"
        "    }\n"
        "}\n"
        "fn read(box Box) i32 {\n"
        "    return box.value + box.get()\n"
        "}\n";
    options.sourcePath = "tooling_completion_member_regression.mog";
    ToolingDocumentAnalysis memberAnalysis =
        analyzeDocumentForTooling(memberSource, options);
    if (!require(memberAnalysis.status == AstFrontendBuildStatus::Success,
                 "member completion sample should succeed")) {
        return false;
    }

    const auto memberCompletions =
        findCompletionsForTooling(memberAnalysis, ToolingPosition{9, 18});
    const auto* valueCompletion = findCompletion(memberCompletions, "value");
    const auto* getCompletion = findCompletion(memberCompletions, "get");
    if (!require(valueCompletion != nullptr &&
                     valueCompletion->kind == "field" &&
                     valueCompletion->detail == "field value: i32",
                 "member completions should expose field members")) {
        return false;
    }
    if (!require(getCompletion != nullptr &&
                     getCompletion->kind == "method" &&
                     getCompletion->detail == "method get: function() -> i32",
                 "member completions should expose method members")) {
        return false;
    }
    if (!require(findCompletion(memberCompletions, "box") == nullptr &&
                     findCompletion(memberCompletions, "return") == nullptr,
                 "member completions should not mix in scope or keyword items")) {
        return false;
    }

    const std::string incompleteMemberSource =
        "#!strict\n"
        "type Box struct {\n"
        "    value i32\n"
        "\n"
        "    fn get() i32 {\n"
        "        return this.value\n"
        "    }\n"
        "}\n"
        "fn read(box Box) i32 {\n"
        "    return box.\n"
        "}\n";
    options.sourcePath = "tooling_completion_member_incomplete_regression.mog";
    ToolingDocumentAnalysis incompleteMemberAnalysis =
        analyzeDocumentForTooling(incompleteMemberSource, options);

    const auto incompleteMemberCompletions = findCompletionsForTooling(
        incompleteMemberAnalysis, incompleteMemberSource, ToolingPosition{9, 15});
    const auto* incompleteValueCompletion =
        findCompletion(incompleteMemberCompletions, "value");
    const auto* incompleteGetCompletion =
        findCompletion(incompleteMemberCompletions, "get");
    if (!require(incompleteValueCompletion != nullptr &&
                     incompleteValueCompletion->kind == "field" &&
                     incompleteGetCompletion != nullptr &&
                     incompleteGetCompletion->kind == "method",
                 "incomplete member completions should use the receiver type")) {
        return false;
    }
    if (!require(findCompletion(incompleteMemberCompletions, "box") == nullptr &&
                     findCompletion(incompleteMemberCompletions, "return") == nullptr,
                 "incomplete member completions should stay locked to receiver members")) {
        return false;
    }

    const std::string moduleMemberSource =
        "#!strict\n"
        "const math = @import(\"./modules/math.mog\")\n"
        "print(math.Ad)\n";
    options.sourcePath = "tests/sample_import_basic.mog";
    ToolingDocumentAnalysis moduleMemberAnalysis =
        analyzeDocumentForTooling(moduleMemberSource, options);
    if (!require(moduleMemberAnalysis.status == AstFrontendBuildStatus::Success,
                 "module member completion sample should succeed")) {
        return false;
    }

    const auto moduleMemberCompletions =
        findCompletionsForTooling(moduleMemberAnalysis, ToolingPosition{2, 13});
    const auto* addCompletion = findCompletion(moduleMemberCompletions, "Add");
    const auto* piCompletion = findCompletion(moduleMemberCompletions, "PI");
    if (!require(addCompletion != nullptr &&
                     addCompletion->kind == "function" &&
                     piCompletion != nullptr &&
                     piCompletion->kind == "constant",
                 "module member completions should expose exported functions and constants")) {
        return false;
    }
    if (!require(findCompletion(moduleMemberCompletions, "print") == nullptr,
                 "module member completions should not mix in scope items")) {
        return false;
    }

    const std::string importExportSource =
        "#!strict\n"
        "const { Add, PI } = @import(\"./modules/math.mog\")\n";
    options.sourcePath = "tests/sample_import_named.mog";
    ToolingDocumentAnalysis importExportAnalysis =
        analyzeDocumentForTooling(importExportSource, options);
    if (!require((importExportAnalysis.status == AstFrontendBuildStatus::Success ||
                  importExportAnalysis.status ==
                      AstFrontendBuildStatus::SemanticError) &&
                     importExportAnalysis.hasBindings,
                 "destructured import completion sample should preserve parse-time bindings")) {
        return false;
    }

    const auto importExportCompletions =
        findCompletionsForTooling(importExportAnalysis, ToolingPosition{1, 15});
    if (!require(findCompletion(importExportCompletions, "PI") != nullptr,
                 "destructured import completions should expose remaining exports")) {
        return false;
    }
    if (!require(findCompletion(importExportCompletions, "Add") == nullptr,
                 "destructured import completions should exclude already imported exports")) {
        return false;
    }

    const std::string typeContextSource =
        "#!strict\n"
        "type LocalAlias i32\n"
        "type LocalBox struct {}\n"
        "fn use(value Loc) LocalA {\n"
        "    return 1\n"
        "}\n";
    options.sourcePath = "tooling_completion_type_context_regression.mog";
    ToolingDocumentAnalysis typeContextAnalysis =
        analyzeDocumentForTooling(typeContextSource, options);
    if (!require(typeContextAnalysis.status == AstFrontendBuildStatus::SemanticError,
                 "type-context completion sample should preserve parse data on semantic errors")) {
        return false;
    }

    const auto typeContextCompletions =
        findCompletionsForTooling(typeContextAnalysis, ToolingPosition{3, 16});
    if (!require(findCompletion(typeContextCompletions, "LocalAlias") != nullptr &&
                     findCompletion(typeContextCompletions, "LocalBox") != nullptr &&
                     findCompletion(typeContextCompletions, "i32") != nullptr,
                 "type-context completions should include aliases, classes, and built-in types")) {
        return false;
    }
    if (!require(findCompletion(typeContextCompletions, "value") == nullptr &&
                     findCompletion(typeContextCompletions, "use") == nullptr,
                 "type-context completions should exclude value bindings")) {
        return false;
    }

    const std::string importedTypeContextSource =
        "#!strict\n"
        "const { Counter } = @import(\"./modules/class_mod.mog\")\n"
        "fn make() void {\n"
        "    var value Cou = Counter()\n"
        "}\n";
    options.sourcePath = "tests/sample_import_class.mog";
    ToolingDocumentAnalysis importedTypeContextAnalysis =
        analyzeDocumentForTooling(importedTypeContextSource, options);
    if (!require(importedTypeContextAnalysis.status ==
                     AstFrontendBuildStatus::SemanticError,
                 "imported type-name completion sample should preserve parse data")) {
        return false;
    }

    const auto importedTypeCompletions = findCompletionsForTooling(
        importedTypeContextAnalysis, ToolingPosition{3, 16});
    if (!require(findCompletion(importedTypeCompletions, "Counter") != nullptr,
                 "type-context completions should include imported class bindings")) {
        return false;
    }

    const auto normalCompletions =
        findCompletionsForTooling(typeContextAnalysis, ToolingPosition{4, 4});
    if (!require(findCompletion(normalCompletions, "LocalAlias") == nullptr &&
                     findCompletion(normalCompletions, "i32") == nullptr,
                 "ordinary completions should exclude type-only names")) {
        return false;
    }

    return true;
}

bool testSignatureHelp() {
    ToolingAnalyzeOptions options;
    options.strictMode = true;

    const std::string directSource =
        "#!strict\n"
        "fn Add(a i32, b i32) i32 {\n"
        "    return a + b\n"
        "}\n"
        "const value i32 = Add(1, 2)\n";
    options.sourcePath = "tooling_signature_help_direct_regression.mog";
    ToolingDocumentAnalysis directAnalysis =
        analyzeDocumentForTooling(directSource, options);
    if (!require(directAnalysis.status == AstFrontendBuildStatus::Success,
                 "direct signature help sample should succeed")) {
        return false;
    }

    const auto directHelp = findSignatureHelpForTooling(
        directAnalysis, directSource, ToolingPosition{4, 27});
    if (!require(directHelp.has_value() &&
                     directHelp->activeParameter == 1 &&
                     !directHelp->signatures.empty() &&
                     directHelp->signatures.front().label ==
                         "function(i32, i32) -> i32",
                 "direct calls should expose signature help and active parameter")) {
        return false;
    }

    const std::string moduleSource =
        "#!strict\n"
        "const math = @import(\"./modules/math.mog\")\n"
        "const value i32 = math.Add(1, 2)\n";
    options.sourcePath = "tests/sample_import_basic.mog";
    ToolingDocumentAnalysis moduleAnalysis =
        analyzeDocumentForTooling(moduleSource, options);
    if (!require(moduleAnalysis.status == AstFrontendBuildStatus::Success,
                 "module-member signature help sample should succeed")) {
        return false;
    }

    const auto moduleHelp = findSignatureHelpForTooling(
        moduleAnalysis, moduleSource, ToolingPosition{2, 31});
    if (!require(moduleHelp.has_value() &&
                     moduleHelp->activeParameter == 1 &&
                     !moduleHelp->signatures.empty() &&
                     moduleHelp->signatures.front().label ==
                         "function(i32, i32) -> i32",
                 "module export calls should expose signature help")) {
        return false;
    }

    const std::string parseFailSource =
        "#!strict\n"
        "fn Add(a i32, b i32) i32 {\n"
        "    return a + b\n"
        "}\n"
        "const value i32 = Add(1,\n";
    options.sourcePath = "tooling_signature_help_parse_fail_regression.mog";
    ToolingDocumentAnalysis parseFailAnalysis =
        analyzeDocumentForTooling(parseFailSource, options);
    if (!require(parseFailAnalysis.status == AstFrontendBuildStatus::ParseFailed,
                 "parse-failed signature help sample should fail parsing")) {
        return false;
    }

    const auto parseFailHelp = findSignatureHelpForTooling(
        parseFailAnalysis, parseFailSource, ToolingPosition{4, 25});
    if (!require(parseFailHelp.has_value() &&
                     parseFailHelp->activeParameter == 1 &&
                     !parseFailHelp->signatures.empty(),
                 "signature help should recover across unterminated calls")) {
        return false;
    }

    return true;
}

bool testWorkspaceSymbolsAndRename() {
    ToolingAnalyzeOptions options;
    options.strictMode = true;

    const std::string localSource =
        "#!strict\n"
        "fn add(x i32) i32 {\n"
        "    var local i32 = x\n"
        "    return local + local\n"
        "}\n";
    options.sourcePath = "tooling_rename_local_regression.mog";
    ToolingDocumentAnalysis localAnalysis =
        analyzeDocumentForTooling(localSource, options);
    if (!require(localAnalysis.status == AstFrontendBuildStatus::Success,
                 "local rename sample should succeed")) {
        return false;
    }

    const auto localTarget =
        prepareRenameForTooling(localAnalysis, ToolingPosition{3, 11});
    if (!require(localTarget.has_value() &&
                     localTarget->strategy == "same-file",
                 "local variable rename should stay same-file")) {
        return false;
    }

    if (!require(!validateRenameForTooling(*localTarget, "renamedLocal").has_value(),
                 "local rename should accept valid identifiers")) {
        return false;
    }

    if (!require(validateRenameForTooling(*localTarget, "bad-name").has_value(),
                 "rename should reject invalid identifiers")) {
        return false;
    }

    const auto localEdits =
        findRenameEditsForTooling(localAnalysis, *localTarget, "renamedLocal");
    if (!require(localEdits.size() == 3,
                 "local rename should edit the declaration and both uses")) {
        return false;
    }

    if (!require(findEdit(localEdits, options.sourcePath, 2, 8) != nullptr &&
                     findEdit(localEdits, options.sourcePath, 3, 11) != nullptr &&
                     findEdit(localEdits, options.sourcePath, 3, 19) != nullptr,
                 "local rename should target only the bound local identifier spans")) {
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
    options.sourcePath = "tooling_prepare_rename_member_regression.mog";
    ToolingDocumentAnalysis memberAnalysis =
        analyzeDocumentForTooling(memberSource, options);
    if (!require(!prepareRenameForTooling(memberAnalysis, ToolingPosition{5, 15})
                      .has_value(),
                 "prepare rename should reject unsupported member access")) {
        return false;
    }

    const std::filesystem::path tempRoot =
        std::filesystem::temp_directory_path() / "mog_tooling_rename_regression";
    std::error_code ec;
    std::filesystem::create_directories(tempRoot, ec);
    if (!require(!ec,
                 "rename regression should create its temporary workspace")) {
        return false;
    }

    const std::filesystem::path depPath = tempRoot / "dep.mog";
    const std::filesystem::path importerPath = tempRoot / "importer.mog";
    const std::filesystem::path aliasPath = tempRoot / "alias_importer.mog";

    if (!require(writeFile(depPath,
                           "#!strict\n"
                           "fn Get() i32 {\n"
                           "    return 42\n"
                           "}\n"
                           "const Answer i32 = 42\n"
                           "const privateValue i32 = 1\n"),
                 "rename regression should write the dependency module") ||
        !require(writeFile(importerPath,
                           "#!strict\n"
                           "const { Answer, Get } = @import(\"./dep.mog\")\n"
                           "print(Get())\n"
                           "print(Answer)\n"),
                 "rename regression should write the importer module") ||
        !require(writeFile(aliasPath,
                           "#!strict\n"
                           "const { Answer as Alias } = @import(\"./dep.mog\")\n"
                           "print(Alias)\n"),
                 "rename regression should write the aliased importer module")) {
        return false;
    }

    options.sourcePath = depPath.string();
    const auto depSource = readFileText(depPath);
    ToolingDocumentAnalysis depAnalysis =
        analyzeDocumentForTooling(depSource.value_or(""), options);
    if (!require(depAnalysis.status == AstFrontendBuildStatus::Success,
                 "dependency rename sample should succeed")) {
        return false;
    }

    const auto depSymbols = collectWorkspaceSymbolsForTooling(depAnalysis);
    if (!require(depSymbols.size() == 3,
                 "workspace symbols should expose top-level declarations only")) {
        return false;
    }

    options.sourcePath = importerPath.string();
    const auto importerSource = readFileText(importerPath);
    ToolingDocumentAnalysis importerAnalysis =
        analyzeDocumentForTooling(importerSource.value_or(""), options);
    if (!require(importerAnalysis.status == AstFrontendBuildStatus::Success,
                 "importer rename sample should succeed")) {
        return false;
    }

    if (!require(collectWorkspaceSymbolsForTooling(importerAnalysis).empty(),
                 "workspace symbols should exclude destructured import bindings")) {
        return false;
    }

    options.sourcePath = aliasPath.string();
    const auto aliasSource = readFileText(aliasPath);
    ToolingDocumentAnalysis aliasAnalysis =
        analyzeDocumentForTooling(aliasSource.value_or(""), options);
    if (!require(aliasAnalysis.status == AstFrontendBuildStatus::Success,
                 "aliased importer rename sample should succeed")) {
        return false;
    }

    const auto importLocalTarget =
        prepareRenameForTooling(importerAnalysis, ToolingPosition{3, 6});
    if (!require(importLocalTarget.has_value() &&
                     importLocalTarget->strategy == "import-local" &&
                     !importLocalTarget->importHasAlias,
                 "unaliased imported binding rename should stay importer-local")) {
        return false;
    }

    const auto importLocalEdits = findRenameEditsForTooling(
        importerAnalysis, *importLocalTarget, "LocalAnswer");
    if (!require(importLocalEdits.size() == 2,
                 "unaliased imported binding rename should edit the import and usage")) {
        return false;
    }

    const auto* importBindingEdit =
        findEdit(importLocalEdits, importerPath.string(), 1, 8);
    if (!require(importBindingEdit != nullptr &&
                     importBindingEdit->newText == "Answer as LocalAnswer",
                 "unaliased import rename should insert a local alias")) {
        return false;
    }

    const auto* importUseEdit =
        findEdit(importLocalEdits, importerPath.string(), 3, 6);
    if (!require(importUseEdit != nullptr &&
                     importUseEdit->newText == "LocalAnswer",
                 "unaliased import rename should update same-file uses")) {
        return false;
    }

    const auto importAliasTarget =
        prepareRenameForTooling(aliasAnalysis, ToolingPosition{2, 6});
    if (!require(importAliasTarget.has_value() &&
                     importAliasTarget->strategy == "import-local" &&
                     importAliasTarget->importHasAlias,
                 "aliased imported binding rename should keep alias-local semantics")) {
        return false;
    }

    const auto importAliasEdits = findRenameEditsForTooling(
        aliasAnalysis, *importAliasTarget, "LocalAlias");
    if (!require(importAliasEdits.size() == 2,
                 "aliased import rename should update the alias declaration and usage")) {
        return false;
    }

    if (!require(findEdit(importAliasEdits, aliasPath.string(), 1, 18) != nullptr &&
                     findEdit(importAliasEdits, aliasPath.string(), 2, 6) != nullptr,
                 "aliased import rename should target the alias spans only")) {
        return false;
    }

    const auto exportedTarget =
        prepareRenameForTooling(depAnalysis, ToolingPosition{4, 6});
    if (!require(exportedTarget.has_value() &&
                     exportedTarget->strategy == "exported",
                 "public top-level rename should be marked as exported")) {
        return false;
    }

    if (!require(validateRenameForTooling(*exportedTarget, "answer").has_value(),
                 "exported rename should reject non-public replacement names")) {
        return false;
    }

    const auto exportedSameFileEdits = findRenameEditsForTooling(
        depAnalysis, *exportedTarget, "FinalAnswer");
    if (!require(exportedSameFileEdits.size() == 1,
                 "exported declaration rename should edit the defining declaration")) {
        return false;
    }

    const auto importerExportEdits = findImportRenameEditsForTooling(
        importerAnalysis, *exportedTarget, "FinalAnswer");
    if (!require(importerExportEdits.size() == 2,
                 "exported rename should update unaliased importer bindings and uses")) {
        return false;
    }

    if (!require(findEdit(importerExportEdits, importerPath.string(), 1, 8) != nullptr &&
                     findEdit(importerExportEdits, importerPath.string(), 3, 6) != nullptr,
                 "exported rename should update the imported binding and its uses")) {
        return false;
    }

    const auto aliasExportEdits = findImportRenameEditsForTooling(
        aliasAnalysis, *exportedTarget, "FinalAnswer");
    if (!require(aliasExportEdits.size() == 1,
                 "exported rename should leave aliased importer uses unchanged")) {
        return false;
    }

    const auto* aliasExportEdit =
        findEdit(aliasExportEdits, aliasPath.string(), 1, 8);
    if (!require(aliasExportEdit != nullptr &&
                     aliasExportEdit->newText == "FinalAnswer",
                 "exported rename should still rewrite the imported exported name")) {
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

    if (!testReferencesAndHover()) {
        return 1;
    }

    if (!testCompletions()) {
        return 1;
    }

    if (!testSignatureHelp()) {
        return 1;
    }

    if (!testWorkspaceSymbolsAndRename()) {
        return 1;
    }

    std::cout << "[PASS] frontend_tooling_regression\n";
    return 0;
}
