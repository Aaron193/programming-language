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

const ToolingSemanticToken* findSemanticToken(
    const std::vector<ToolingSemanticToken>& tokens, size_t line,
    size_t character, const std::string& kind) {
    for (const auto& token : tokens) {
        if (token.range.start.line == line &&
            token.range.start.character == character &&
            token.kind == kind) {
            return &token;
        }
    }
    return nullptr;
}

bool tokenHasModifier(const ToolingSemanticToken& token,
                      const std::string& modifier) {
    for (const auto& item : token.modifiers) {
        if (item == modifier) {
            return true;
        }
    }
    return false;
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
                "type Box i32\n"
        "fn add(x i32) i32 {\n"
        "    return x\n"
        "}\n"
        "var broken i32 = \"oops\"\n";

    ToolingAnalyzeOptions options;
    options.sourcePath = "tooling_regression.mog";
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
    if (!require(diagnostic.range.start.line == 4,
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

    const std::string undefinedSource =
                "fn main() void {\n"
        "    app.update(1)\n"
        "    asdasdas.update(1)\n"
        "}\n";
    options.sourcePath = "tooling_undefined_identifier_regression.mog";
    ToolingDocumentAnalysis undefinedAnalysis =
        analyzeDocumentForTooling(undefinedSource, options);
    if (!require(undefinedAnalysis.status == AstFrontendBuildStatus::SemanticError,
                 "undefined identifier sample should return semantic error status") ||
        !require(undefinedAnalysis.hasBindings,
                 "undefined identifier sample should preserve bindings") ||
        !require(undefinedAnalysis.diagnostics.size() == 2,
                 "undefined identifier sample should expose two diagnostics")) {
        return false;
    }

    if (!require(undefinedAnalysis.diagnostics[0].message ==
                     "Type error: unknown identifier 'app'.",
                 "first undefined identifier tooling diagnostic should preserve frontend text") ||
        !require(undefinedAnalysis.diagnostics[0].range.start.line == 1 &&
                     undefinedAnalysis.diagnostics[0].range.start.character == 4,
                 "first undefined identifier tooling diagnostic should point at the receiver")) {
        return false;
    }

    if (!require(undefinedAnalysis.diagnostics[1].message ==
                     "Type error: unknown identifier 'asdasdas'.",
                 "second undefined identifier tooling diagnostic should preserve frontend text") ||
        !require(undefinedAnalysis.diagnostics[1].range.start.line == 2 &&
                     undefinedAnalysis.diagnostics[1].range.start.character == 4,
                 "second undefined identifier tooling diagnostic should point at the receiver")) {
        return false;
    }

    if (!require(undefinedAnalysis.documentSymbols.size() == 1 &&
                     undefinedAnalysis.documentSymbols[0].name == "main",
                 "undefined identifier diagnostics should not hide document symbols")) {
        return false;
    }

    const std::string specialBuiltinSource =
                "var keys Set<str> = Set()\n"
        "var text str = str(42)\n"
        "print(type(text))\n";
    options.sourcePath = "tooling_special_builtin_regression.mog";
    ToolingDocumentAnalysis specialBuiltinAnalysis =
        analyzeDocumentForTooling(specialBuiltinSource, options);
    if (!require(specialBuiltinAnalysis.status == AstFrontendBuildStatus::Success,
                 "special builtin identifier sample should succeed for tooling") ||
        !require(specialBuiltinAnalysis.diagnostics.empty(),
                 "special builtin identifier sample should stay diagnostics-free")) {
        return false;
    }

    return true;
}

bool testPackageApiDiagnosticsAndSymbols() {
    const std::filesystem::path apiPath =
        std::filesystem::current_path() / "packages" / "mog" / "window" /
        "package.api.mog";
    const auto source = readFileText(apiPath);
    if (!require(source.has_value(),
                 "package api regression should load package.api.mog")) {
        return false;
    }

    ToolingAnalyzeOptions options;
    options.sourcePath = apiPath.string();
    ToolingDocumentAnalysis analysis =
        analyzeDocumentForTooling(*source, options);

    if (!require(analysis.status == AstFrontendBuildStatus::Success,
                 "valid package api source should succeed in tooling")) {
        return false;
    }

    if (!require(analysis.diagnostics.empty(),
                 "valid package api source should not emit diagnostics")) {
        return false;
    }

    if (!require(analysis.hasParse,
                 "valid package api source should expose parsed symbols")) {
        return false;
    }

    bool sawWindowType = false;
    bool sawCreate = false;
    for (const auto& symbol : analysis.documentSymbols) {
        if (symbol.name == "Window" && symbol.kind == "type") {
            sawWindowType = true;
        }
        if (symbol.name == "create" && symbol.kind == "function") {
            sawCreate = true;
        }
    }

    if (!require(sawWindowType,
                 "package api source should expose opaque type symbols")) {
        return false;
    }

    if (!require(sawCreate,
                 "package api source should expose function symbols")) {
        return false;
    }

    return true;
}

bool testImportedDiagnosticPaths() {
    const std::filesystem::path tempRoot =
        std::filesystem::temp_directory_path() / "mog_tooling_import_diagnostics";
    std::error_code ec;
    std::filesystem::create_directories(tempRoot, ec);
    if (!require(!ec,
                 "import diagnostic regression should create its temporary workspace")) {
        return false;
    }

    const std::filesystem::path depPath = tempRoot / "dep.mog";
    const std::filesystem::path importerPath = tempRoot / "main.mog";
    if (!require(writeFile(depPath, "var broken i32 = 1;\n"),
                 "import diagnostic regression should write the dependency sample") ||
        !require(writeFile(importerPath,
                           "const { broken } = @import(\"./dep.mog\")\nprint(broken)\n"),
                 "import diagnostic regression should write the importer sample")) {
        return false;
    }

    ToolingAnalyzeOptions options;
    options.sourcePath = importerPath.string();
    AstFrontendModuleGraphCache cache;
    options.moduleGraphCache = &cache;

    const auto importerSource = readFileText(importerPath);
    ToolingDocumentAnalysis importerAnalysis =
        analyzeDocumentForTooling(importerSource.value_or(""), options);
    if (!require(importerAnalysis.status == AstFrontendBuildStatus::SemanticError,
                 "import diagnostic regression should fail through the importer")) {
        return false;
    }
    if (!require(!importerAnalysis.diagnostics.empty(),
                 "import diagnostic regression should report importer diagnostics")) {
        return false;
    }

    const ToolingDiagnostic& importedDiagnostic = importerAnalysis.diagnostics.front();
    const bool tracedFromImporter =
        std::any_of(importedDiagnostic.importTrace.begin(),
                    importedDiagnostic.importTrace.end(),
                    [&](const ToolingImportTraceFrame& frame) {
                        return frame.importerPath == importerPath.string();
                    });
    if (!require(importedDiagnostic.path == depPath.string(),
                 "imported parser diagnostic should keep the dependency path") ||
        !require(importedDiagnostic.message ==
                     "Semicolons are only allowed inside 'for (...)' clauses.",
                 "imported parser diagnostic should preserve the semicolon message") ||
        !require(tracedFromImporter,
                 "imported parser diagnostic should preserve import trace information")) {
        return false;
    }

    options.sourcePath = depPath.string();
    const auto depSource = readFileText(depPath);
    ToolingDocumentAnalysis depAnalysis =
        analyzeDocumentForTooling(depSource.value_or(""), options);
    if (!require(depAnalysis.status == AstFrontendBuildStatus::ParseFailed,
                 "dependency diagnostic regression should fail parsing directly") ||
        !require(depAnalysis.diagnostics.size() == 1,
                 "dependency diagnostic regression should report one direct parser diagnostic") ||
        !require(depAnalysis.diagnostics.front().path == depPath.string(),
                 "direct dependency diagnostic should use the dependency path")) {
        return false;
    }

    std::cout << "[PASS] imported diagnostic paths\n";
    return true;
}

bool testMalformedImportArgumentDiagnostic() {
    const std::string source =
        "const value = @import(@ASDAS\"./constants.mog\")\n";

    ToolingAnalyzeOptions options;
    options.sourcePath = "tooling_malformed_import_arg_regression.mog";
    ToolingDocumentAnalysis analysis =
        analyzeDocumentForTooling(source, options);
    if (!require(analysis.status == AstFrontendBuildStatus::ParseFailed,
                 "malformed import arg sample should fail parsing")) {
        return false;
    }
    if (!require(analysis.diagnostics.size() == 1,
                 "malformed import arg sample should emit one diagnostic")) {
        return false;
    }

    const ToolingDiagnostic& diagnostic = analysis.diagnostics.front();
    if (!require(diagnostic.code == "parse.expected_token",
                 "malformed import arg sample should keep the expected-token code") ||
        !require(diagnostic.message ==
                     "Expected string literal but found '@'.",
                 "malformed import arg sample should report the missing string literal") ||
        !require(diagnostic.range.start.line == 0 &&
                     diagnostic.range.start.character == 22,
                 "malformed import arg sample should point at the unexpected '@'")) {
        return false;
    }

    std::cout << "[PASS] malformed import argument diagnostic\n";
    return true;
}

bool testInlineSemicolonDiagnostics() {
    const std::string source = "const state GameState ;= GameState()\n";

    ToolingAnalyzeOptions options;
    options.sourcePath = "tooling_inline_semicolon_regression.mog";
    ToolingDocumentAnalysis analysis =
        analyzeDocumentForTooling(source, options);
    if (!require(analysis.status == AstFrontendBuildStatus::ParseFailed,
                 "inline semicolon sample should fail parsing")) {
        return false;
    }
    if (!require(analysis.diagnostics.size() == 1,
                 "inline semicolon sample should emit one diagnostic")) {
        return false;
    }

    const ToolingDiagnostic& diagnostic = analysis.diagnostics.front();
    if (!require(diagnostic.message ==
                     "Semicolons are only allowed inside 'for (...)' clauses.",
                 "inline semicolon sample should preserve the semicolon message") ||
        !require(diagnostic.range.start.line == 0 &&
                     diagnostic.range.start.character == 22,
                 "inline semicolon sample should point at the stray semicolon")) {
        return false;
    }

    std::cout << "[PASS] inline semicolon diagnostics\n";
    return true;
}

bool testLexerDiagnostics() {
    const std::string source = "const value i32 = 1$\n";

    ToolingAnalyzeOptions options;
    options.sourcePath = "tooling_lexer_regression.mog";
    ToolingDocumentAnalysis analysis =
        analyzeDocumentForTooling(source, options);
    if (!require(analysis.status == AstFrontendBuildStatus::ParseFailed,
                 "lexer regression sample should fail parsing")) {
        return false;
    }
    if (!require(analysis.diagnostics.size() == 1,
                 "lexer regression sample should emit one diagnostic")) {
        return false;
    }

    const ToolingDiagnostic& diagnostic = analysis.diagnostics.front();
    if (!require(diagnostic.code == "lex.invalid_token",
                 "lexer regression sample should keep the lexer diagnostic code") ||
        !require(diagnostic.message == "Unexpected Token.",
                 "lexer regression sample should keep the scanner message")) {
        return false;
    }

    std::cout << "[PASS] lexer diagnostics\n";
    return true;
}

bool testUnterminatedBlockCommentDiagnostic() {
    const std::string source =
        "const value i32 = 1\n"
        "/* unterminated block comment\n";

    ToolingAnalyzeOptions options;
    options.sourcePath = "tooling_unterminated_block_comment_regression.mog";
    ToolingDocumentAnalysis analysis =
        analyzeDocumentForTooling(source, options);
    if (!require(analysis.status == AstFrontendBuildStatus::ParseFailed,
                 "unterminated block comment sample should fail parsing")) {
        return false;
    }
    if (!require(analysis.diagnostics.size() == 1,
                 "unterminated block comment sample should emit one diagnostic")) {
        return false;
    }

    const ToolingDiagnostic& diagnostic = analysis.diagnostics.front();
    if (!require(diagnostic.code == "lex.unterminated_block_comment",
                 "unterminated block comment sample should keep the block comment diagnostic code") ||
        !require(diagnostic.message == "Unterminated block comment.",
                 "unterminated block comment sample should keep the scanner message") ||
        !require(diagnostic.range.start.line == 1 &&
                     diagnostic.range.start.character == 0,
                 "unterminated block comment sample should point at the comment start")) {
        return false;
    }

    std::cout << "[PASS] unterminated block comment diagnostics\n";
    return true;
}

bool testDefinitionLookup() {
    ToolingAnalyzeOptions options;
    options.sourcePath = "tooling_definition_regression.mog";
    const std::string sourceWithTypeError =
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
        findDefinitionForTooling(analysis, ToolingPosition{2, 11});
    if (!require(localDefinition.has_value(),
                 "local variable use should resolve to its declaration")) {
        return false;
    }

    if (!require(localDefinition->selectionRange.start.line == 1 &&
                     localDefinition->selectionRange.start.character == 8,
                 "local definition should point at the declared local name")) {
        return false;
    }

    const auto topLevelDefinition =
        findDefinitionForTooling(analysis, ToolingPosition{6, 6});
    if (!require(topLevelDefinition.has_value(),
                 "top-level identifier use should resolve to its declaration")) {
        return false;
    }

    if (!require(topLevelDefinition->selectionRange.start.line == 4 &&
                     topLevelDefinition->selectionRange.start.character == 6,
                 "top-level definition should point at the const name")) {
        return false;
    }

    const std::string importSource =
                "const { Answer, Get } = @import(\"./modules/frontend_identity_module.mog\")\n"
        "print(Get())\n"
        "print(Answer)\n";
    options.sourcePath = "tests/sample_import_frontend_identity.mog";
    ToolingDocumentAnalysis importAnalysis =
        analyzeDocumentForTooling(importSource, options);
    const auto importDefinition =
        findDefinitionForTooling(importAnalysis, ToolingPosition{2, 7});
    if (!require(importDefinition.has_value(),
                 "imported binding use should resolve to the imported module declaration")) {
        return false;
    }

    if (!require(importDefinition->path.find("frontend_identity_module.mog") !=
                     std::string::npos,
                 "import definition should resolve to the imported module path")) {
        return false;
    }

    if (!require(importDefinition->selectionRange.start.line == 4 &&
                     importDefinition->selectionRange.start.character == 6,
                 "import definition should point at the exported binding name")) {
        return false;
    }

    options.packageSearchPaths = {
        (std::filesystem::current_path() / "build" / "packages").string()};
    const std::string nativeMemberSource =
                "const counter = @import(\"counter\")\n"
        "print(counter.create)\n";
    options.sourcePath = "tooling_native_member_definition_regression.mog";
    ToolingDocumentAnalysis nativeMemberAnalysis =
        analyzeDocumentForTooling(nativeMemberSource, options);
    if (!require(nativeMemberAnalysis.status == AstFrontendBuildStatus::Success,
                 "native package member definition sample should succeed")) {
        return false;
    }
    const auto nativeMemberDefinition =
        findDefinitionForTooling(nativeMemberAnalysis, ToolingPosition{1, 15});
    if (!require(nativeMemberDefinition.has_value(),
                 "native package member use should resolve to package api declaration")) {
        return false;
    }
    if (!require(nativeMemberDefinition->path.find("package.api.mog") !=
                     std::string::npos,
                 "native package member definition should resolve to the package api file")) {
        return false;
    }
    options.packageSearchPaths.clear();

    const std::string memberSource =
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
        findDefinitionForTooling(memberAnalysis, ToolingPosition{4, 15});
    if (!require(memberDefinition.has_value(),
                 "member access should resolve to the same-module field declaration")) {
        return false;
    }

    if (!require(memberDefinition->selectionRange.start.line == 1 &&
                     memberDefinition->selectionRange.start.character == 4,
                 "member definition should point at the field name")) {
        return false;
    }

    const std::string typeUseSource =
                "type Pipe struct {\n"
        "    x f64\n"
        "}\n"
        "fn makePipe(x f64) Pipe {\n"
        "    return makePipe(x)\n"
        "}\n";
    options.sourcePath = "tooling_type_definition_regression.mog";
    ToolingDocumentAnalysis typeUseAnalysis =
        analyzeDocumentForTooling(typeUseSource, options);
    if (!require(typeUseAnalysis.hasParse,
                 "type annotation definition sample should preserve parse data")) {
        return false;
    }
    const auto typeUseDefinition =
        findDefinitionForTooling(typeUseAnalysis, ToolingPosition{3, 20});
    if (!require(typeUseDefinition.has_value(),
                 "type annotations should resolve to the same-module type declaration")) {
        return false;
    }

    if (!require(typeUseDefinition->selectionRange.start.line == 0 &&
                     typeUseDefinition->selectionRange.start.character == 5,
                 "type definition should point at the declared type name")) {
        return false;
    }

    return true;
}

bool testReferencesAndHover() {
    ToolingAnalyzeOptions options;
    options.sourcePath = "tooling_references_hover_regression.mog";
    const std::string source =
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
        findReferencesForTooling(analysis, ToolingPosition{5, 6});
    if (!require(references.size() == 2,
                 "top-level binding should return declaration and one usage")) {
        return false;
    }

    if (!require(references[0].selectionRange.start.line == 4 &&
                     references[0].selectionRange.start.character == 6,
                 "references should start with the declaration site")) {
        return false;
    }

    if (!require(references[1].selectionRange.start.line == 5 &&
                     references[1].selectionRange.start.character == 6,
                 "references should include same-file usages")) {
        return false;
    }

    const auto hover = findHoverForTooling(analysis, ToolingPosition{5, 6});
    if (!require(hover.has_value(), "hover should be available for bound identifiers")) {
        return false;
    }

    if (!require(hover->kind == "constant" && hover->role.empty() &&
                     hover->detail == "const Value i32",
                 "hover should include kind and formatted type detail")) {
        return false;
    }

    const std::string importSource =
                "const { Answer, Get } = @import(\"./modules/frontend_identity_module.mog\")\n"
        "print(Get())\n"
        "print(Answer)\n";
    options.sourcePath = "tests/sample_import_frontend_identity.mog";
    ToolingDocumentAnalysis importAnalysis =
        analyzeDocumentForTooling(importSource, options);
    const auto importHover =
        findHoverForTooling(importAnalysis, ToolingPosition{2, 7});
    if (!require(importHover.has_value(),
                 "hover should be available for imported bindings")) {
        return false;
    }

    if (!require(importHover->kind == "import" && importHover->role.empty() &&
                     importHover->detail == "const Answer i32",
                 "import hover should preserve imported type information")) {
        return false;
    }

    const auto importedFunctionHover =
        findHoverForTooling(importAnalysis, ToolingPosition{1, 7});
    if (!require(importedFunctionHover.has_value(),
                 "hover should be available for imported functions")) {
        return false;
    }

    if (!require(importedFunctionHover->kind == "import" &&
                     importedFunctionHover->role == "function" &&
                     importedFunctionHover->detail == "fn Get() i32",
                 "imported source functions should preserve Mog declaration syntax")) {
        return false;
    }

    options.packageSearchPaths = {
        (std::filesystem::current_path() / "build" / "packages").string()};
    const std::string nativeImportSource =
                "const counter = @import(\"counter\")\n"
        "print(counter)\n"
        "print(counter.create)\n";
    options.sourcePath = "tooling_native_import_hover_regression.mog";
    ToolingDocumentAnalysis nativeImportAnalysis =
        analyzeDocumentForTooling(nativeImportSource, options);
    if (!require(nativeImportAnalysis.status == AstFrontendBuildStatus::Success,
                 "native import hover sample should succeed")) {
        return false;
    }

    const auto nativeModuleHover =
        findHoverForTooling(nativeImportAnalysis, ToolingPosition{1, 7});
    if (!require(nativeModuleHover.has_value(),
                 "hover should be available for imported module bindings")) {
        return false;
    }
    if (!require(nativeModuleHover->role == "module" &&
                     nativeModuleHover->detail == "package counter",
                 "native import binding hover should identify the imported package")) {
        return false;
    }

    const auto nativeMemberHover =
        findHoverForTooling(nativeImportAnalysis, ToolingPosition{2, 15});
    if (!require(nativeMemberHover.has_value(),
                 "hover should be available for native package members")) {
        return false;
    }
    if (!require(nativeMemberHover->kind == "function" &&
                     nativeMemberHover->role == "function" &&
                     nativeMemberHover->detail ==
                         "fn create(i64) counter.Counter" &&
                     nativeMemberHover->documentation ==
                         "Create a new counter handle.",
                 "native package member hover should surface declaration docs")) {
        return false;
    }

    const std::string nativeTypeQualifierSource =
                "const counter = @import(\"counter\")\n"
        "fn make() void {\n"
        "    var value counter.Counter = counter.create(1i64)\n"
        "}\n";
    options.sourcePath = "tooling_native_import_type_qualifier_hover_regression.mog";
    ToolingDocumentAnalysis nativeTypeQualifierAnalysis =
        analyzeDocumentForTooling(nativeTypeQualifierSource, options);
    const auto nativeTypeQualifierHover =
        findHoverForTooling(nativeTypeQualifierAnalysis, ToolingPosition{2, 16});
    if (!require(nativeTypeQualifierHover.has_value(),
                 "hover should be available for imported type qualifiers")) {
        return false;
    }
    if (!require(nativeTypeQualifierHover->kind == "import" &&
                     nativeTypeQualifierHover->role == "module" &&
                     nativeTypeQualifierHover->detail == "package counter",
                 "imported type qualifier hover should identify the package")) {
        return false;
    }

    const auto nativeTypeHover =
        findHoverForTooling(nativeTypeQualifierAnalysis, ToolingPosition{2, 24});
    if (!require(nativeTypeHover.has_value(),
                 "hover should be available for imported package types")) {
        return false;
    }
    if (!require(nativeTypeHover->kind == "type" &&
                     nativeTypeHover->role == "type" &&
                     nativeTypeHover->detail == "type Counter" &&
                     nativeTypeHover->documentation ==
                         "GC-managed opaque counter handle.",
                 "native package type hover should surface declaration docs")) {
        return false;
    }

    const auto nativeTypeQualifierDefinition =
        findDefinitionForTooling(nativeTypeQualifierAnalysis, ToolingPosition{2, 16});
    if (!require(nativeTypeQualifierDefinition.has_value(),
                 "definition should be available for imported type qualifiers")) {
        return false;
    }
    if (!require(nativeTypeQualifierDefinition->path.find(
                         "packages/examples/counter/package.api.mog") !=
                         std::string::npos &&
                     nativeTypeQualifierDefinition->range.start.line == 0 &&
                     nativeTypeQualifierDefinition->range.start.character == 0,
                 "imported type qualifier definition should jump to the package API")) {
        return false;
    }
    options.packageSearchPaths.clear();

    const std::string memberSource =
                "type Box struct {\n"
        "    value i32\n"
        "}\n"
        "fn read(box Box) i32 {\n"
        "    return box.value\n"
        "}\n";
    options.sourcePath = "tooling_member_hover_regression.mog";
    ToolingDocumentAnalysis memberAnalysis =
        analyzeDocumentForTooling(memberSource, options);
    const auto memberHover = findHoverForTooling(memberAnalysis, ToolingPosition{4, 15});
    if (!require(memberHover.has_value(),
                 "member access hover should be available for same-module fields")) {
        return false;
    }

    if (!require(memberHover->kind == "field" &&
                     memberHover->role == "property" &&
                     memberHover->detail == "value i32",
                 "member hover should preserve field kind and type detail")) {
        return false;
    }

    const std::string parameterSource =
                "fn tick(dt f64) void {\n"
        "    print(dt)\n"
        "}\n";
    options.sourcePath = "tooling_parameter_hover_regression.mog";
    ToolingDocumentAnalysis parameterAnalysis =
        analyzeDocumentForTooling(parameterSource, options);
    const auto parameterHover =
        findHoverForTooling(parameterAnalysis, ToolingPosition{1, 10});
    if (!require(parameterHover.has_value(),
                 "hover should be available for parameters")) {
        return false;
    }

    if (!require(parameterHover->kind == "parameter" &&
                     parameterHover->role == "parameter" &&
                     parameterHover->detail == "dt f64",
                 "parameter hover should preserve its role and raw type detail")) {
        return false;
    }

    const std::string builtinSource =
                "const value f64 = sqrt(9.0)\n"
        "print(value)\n";
    options.sourcePath = "tooling_builtin_hover_regression.mog";
    ToolingDocumentAnalysis builtinAnalysis =
        analyzeDocumentForTooling(builtinSource, options);
    const auto builtinHover =
        findHoverForTooling(builtinAnalysis, ToolingPosition{0, 20});
    if (!require(builtinHover.has_value(),
                 "hover should be available for builtin stdlib functions")) {
        return false;
    }

    if (!require(builtinHover->kind == "function" &&
                     builtinHover->role == "function" &&
                     builtinHover->detail == "fn sqrt(f64) f64",
                 "builtin stdlib hover should preserve callable type detail")) {
        return false;
    }

    const std::string collectionBuiltinSource =
                "fn main() void {\n"
        "    var values Array<i32> = Array<i32>()\n"
        "    var players Dict<usize, i32> = Dict<usize, i32>()\n"
        "    var keys Set<str> = Set<str>()\n"
        "}\n";
    options.sourcePath = "tooling_collection_builtin_hover_regression.mog";
    ToolingDocumentAnalysis collectionBuiltinAnalysis =
        analyzeDocumentForTooling(collectionBuiltinSource, options);
    if (!require(collectionBuiltinAnalysis.status == AstFrontendBuildStatus::Success,
                 "collection builtin hover sample should succeed")) {
        return false;
    }

    const auto arrayBuiltinHover =
        findHoverForTooling(collectionBuiltinAnalysis, ToolingPosition{1, 16});
    if (!require(arrayBuiltinHover.has_value() &&
                     arrayBuiltinHover->kind == "function" &&
                     arrayBuiltinHover->role == "function" &&
                     arrayBuiltinHover->detail == "fn Array() Array<any>",
                 "Array should expose builtin hover information in type positions")) {
        return false;
    }

    const auto dictBuiltinHover =
        findHoverForTooling(collectionBuiltinAnalysis, ToolingPosition{2, 17});
    if (!require(dictBuiltinHover.has_value() &&
                     dictBuiltinHover->kind == "function" &&
                     dictBuiltinHover->role == "function" &&
                     dictBuiltinHover->detail == "fn Dict() Dict<any, any>",
                 "Dict should expose builtin hover information in type positions")) {
        return false;
    }

    const auto setBuiltinHover =
        findHoverForTooling(collectionBuiltinAnalysis, ToolingPosition{3, 14});
    if (!require(setBuiltinHover.has_value() &&
                     setBuiltinHover->kind == "function" &&
                     setBuiltinHover->role == "function" &&
                     setBuiltinHover->detail == "fn Set() Set<any>",
                 "Set should expose builtin hover information in type positions")) {
        return false;
    }

    const std::string constructorTypeSource =
                "type Player struct {}\n"
        "fn main() void {\n"
        "    var players Dict<usize, Player> = Dict<usize, Player>()\n"
        "}\n";
    options.sourcePath = "tooling_constructor_type_hover_regression.mog";
    ToolingDocumentAnalysis constructorTypeAnalysis =
        analyzeDocumentForTooling(constructorTypeSource, options);
    if (!require(constructorTypeAnalysis.status == AstFrontendBuildStatus::Success,
                 "constructor generic type hover sample should succeed")) {
        return false;
    }

    const auto constructorTypeHover =
        findHoverForTooling(constructorTypeAnalysis, ToolingPosition{2, 52});
    if (!require(constructorTypeHover.has_value() &&
                     constructorTypeHover->kind == "class" &&
                     constructorTypeHover->role.empty() &&
                     constructorTypeHover->detail == "type Player struct",
                 "generic constructor type arguments should expose the same hover as declaration types")) {
        return false;
    }

    const auto constructorTypeDefinition =
        findDefinitionForTooling(constructorTypeAnalysis, ToolingPosition{2, 52});
    if (!require(constructorTypeDefinition.has_value() &&
                     constructorTypeDefinition->selectionRange.start.line == 0 &&
                     constructorTypeDefinition->selectionRange.start.character == 5,
                 "generic constructor type arguments should resolve to the type declaration")) {
        return false;
    }

    return true;
}

bool testSemanticTokens() {
    ToolingAnalyzeOptions options;
    options.sourcePath = "tooling_semantic_tokens_regression.mog";
    const std::string source =
                "type Pipe struct {\n"
        "    value f64\n"
        "\n"
        "    fn push(pipe Pipe) Pipe {\n"
        "        var next Pipe = pipe\n"
        "        return next\n"
        "    }\n"
        "}\n"
        "fn mix(pipe Pipe) Pipe {\n"
        "    const pushed Pipe = pipe.push(pipe)\n"
        "    return pushed\n"
        "}\n";

    ToolingDocumentAnalysis analysis =
        analyzeDocumentForTooling(source, options);
    if (!require(analysis.status == AstFrontendBuildStatus::Success,
                 "semantic token sample should succeed")) {
        return false;
    }

    const auto tokens = findSemanticTokensForTooling(analysis);

    const auto* pipeDecl = findSemanticToken(tokens, 0, 5, "type");
    if (!require(pipeDecl != nullptr && tokenHasModifier(*pipeDecl, "declaration"),
                 "type declarations should emit declaration semantic tokens")) {
        return false;
    }

    if (!require(findSemanticToken(tokens, 1, 4, "property") != nullptr,
                 "field declarations should emit property semantic tokens")) {
        return false;
    }

    if (!require(findSemanticToken(tokens, 1, 10, "type") != nullptr,
                 "built-in types should emit type semantic tokens")) {
        return false;
    }

    const auto* methodDecl = findSemanticToken(tokens, 3, 7, "method");
    if (!require(methodDecl != nullptr &&
                     tokenHasModifier(*methodDecl, "declaration"),
                 "method declarations should emit method declaration tokens")) {
        return false;
    }

    if (!require(findSemanticToken(tokens, 3, 12, "parameter") != nullptr,
                 "parameters should emit parameter semantic tokens")) {
        return false;
    }

    const auto* localDecl = findSemanticToken(tokens, 4, 12, "variable");
    if (!require(localDecl != nullptr &&
                     tokenHasModifier(*localDecl, "declaration") &&
                     !tokenHasModifier(*localDecl, "readonly"),
                 "mutable locals should emit variable declaration tokens")) {
        return false;
    }

    if (!require(findSemanticToken(tokens, 4, 24, "parameter") != nullptr,
                 "parameter uses should keep the parameter semantic token kind")) {
        return false;
    }

    const auto* functionDecl = findSemanticToken(tokens, 8, 3, "function");
    if (!require(functionDecl != nullptr &&
                     tokenHasModifier(*functionDecl, "declaration"),
                 "free function declarations should emit function declaration tokens")) {
        return false;
    }

    const auto* constDecl = findSemanticToken(tokens, 9, 10, "variable");
    if (!require(constDecl != nullptr &&
                     tokenHasModifier(*constDecl, "declaration") &&
                     tokenHasModifier(*constDecl, "readonly"),
                 "const declarations should emit readonly variable tokens")) {
        return false;
    }

    if (!require(findSemanticToken(tokens, 9, 17, "type") != nullptr,
                 "custom type references should emit type semantic tokens")) {
        return false;
    }

    if (!require(findSemanticToken(tokens, 9, 24, "parameter") != nullptr,
                 "receiver variables should retain their semantic token kind")) {
        return false;
    }

    if (!require(findSemanticToken(tokens, 9, 29, "method") != nullptr,
                 "member calls should emit method semantic tokens")) {
        return false;
    }

    const std::string genericSource =
                "type Pipe struct {}\n"
        "fn reset() void {\n"
        "    var pipes Array<Pipe> = Array<Pipe>()\n"
        "}\n";
    options.sourcePath = "tooling_semantic_tokens_generics_regression.mog";
    ToolingDocumentAnalysis genericAnalysis =
        analyzeDocumentForTooling(genericSource, options);
    if (!require(genericAnalysis.status == AstFrontendBuildStatus::Success,
                 "generic semantic token sample should succeed")) {
        return false;
    }

    const auto genericTokens = findSemanticTokensForTooling(genericAnalysis);
    const auto* genericPipeRef = findSemanticToken(genericTokens, 2, 20, "type");
    if (!require(genericPipeRef != nullptr,
                 "generic type references should emit type semantic tokens")) {
        return false;
    }

    if (!require(genericPipeRef->range.end.character -
                         genericPipeRef->range.start.character ==
                     4,
                 "generic type semantic tokens should not extend past the type name")) {
        return false;
    }

    const auto* constructorPipeRef =
        findSemanticToken(genericTokens, 2, 34, "type");
    if (!require(constructorPipeRef != nullptr,
                 "generic constructor type arguments should emit type semantic tokens")) {
        return false;
    }

    const std::string ordinaryCallSource =
                "fn sqrtValue() i32 {\n"
        "    return 1\n"
        "}\n"
        "fn main() void {\n"
        "    sqrtValue()\n"
        "}\n";
    options.sourcePath = "tooling_semantic_tokens_ordinary_call_regression.mog";
    ToolingDocumentAnalysis ordinaryCallAnalysis =
        analyzeDocumentForTooling(ordinaryCallSource, options);
    if (!require(ordinaryCallAnalysis.status == AstFrontendBuildStatus::Success,
                 "ordinary call semantic token sample should succeed")) {
        return false;
    }

    const auto ordinaryCallTokens = findSemanticTokensForTooling(ordinaryCallAnalysis);
    if (!require(findSemanticToken(ordinaryCallTokens, 4, 4, "function") != nullptr,
                 "ordinary call identifiers should keep function semantic tokens")) {
        return false;
    }
    if (!require(findSemanticToken(ordinaryCallTokens, 4, 4, "type") == nullptr,
                 "ordinary call identifiers should not also emit type semantic tokens")) {
        return false;
    }

    const std::string importSource =
                "const { Answer, Get } = @import(\"./modules/frontend_identity_module.mog\")\n"
        "const value i32 = Get()\n"
        "print(Answer)\n";
    options.sourcePath = "tests/sample_import_frontend_identity.mog";
    ToolingDocumentAnalysis importAnalysis =
        analyzeDocumentForTooling(importSource, options);
    if (!require(importAnalysis.status == AstFrontendBuildStatus::Success,
                 "import semantic token sample should succeed")) {
        return false;
    }

    const auto importTokens = findSemanticTokensForTooling(importAnalysis);
    const auto* importedAnswerDecl =
        findSemanticToken(importTokens, 0, 8, "variable");
    if (!require(importedAnswerDecl != nullptr &&
                     tokenHasModifier(*importedAnswerDecl, "declaration") &&
                     tokenHasModifier(*importedAnswerDecl, "readonly"),
                 "imported constants should emit readonly variable declaration tokens")) {
        return false;
    }

    const auto* importedGetDecl = findSemanticToken(importTokens, 0, 16, "function");
    if (!require(importedGetDecl != nullptr &&
                     tokenHasModifier(*importedGetDecl, "declaration"),
                 "imported functions should emit function declaration tokens")) {
        return false;
    }

    if (!require(findSemanticToken(importTokens, 1, 18, "function") != nullptr,
                 "imported function uses should stay classified as functions")) {
        return false;
    }

    const auto* importedAnswerUse =
        findSemanticToken(importTokens, 2, 6, "variable");
    if (!require(importedAnswerUse != nullptr &&
                     tokenHasModifier(*importedAnswerUse, "readonly"),
                 "imported constant uses should stay readonly variables")) {
        return false;
    }

    const std::string operatorAndCastSource =
                "type Pipe struct {\n"
        "    passed bool\n"
        "    x f64\n"
        "}\n"
        "const PIPE_WIDTH i64 = 80i64\n"
        "const BIRD_X i64 = 160i64\n"
        "fn check(pipe Pipe) void {\n"
        "    if (pipe.passed == false && (pipe.x + (PIPE_WIDTH as f64)) < (BIRD_X as f64)) {\n"
        "        return\n"
        "    }\n"
        "}\n";
    options.sourcePath = "tooling_semantic_tokens_operator_cast_regression.mog";
    ToolingDocumentAnalysis operatorAndCastAnalysis =
        analyzeDocumentForTooling(operatorAndCastSource, options);
    if (!require(operatorAndCastAnalysis.status == AstFrontendBuildStatus::Success,
                 "operator/cast semantic token sample should succeed")) {
        return false;
    }

    const auto operatorAndCastTokens =
        findSemanticTokensForTooling(operatorAndCastAnalysis);
    if (!require(findSemanticToken(operatorAndCastTokens, 7, 43, "variable") !=
                     nullptr,
                 "PIPE_WIDTH use should keep its exact start column after == and &&")) {
        return false;
    }
    const auto* firstCastType =
        findSemanticToken(operatorAndCastTokens, 7, 57, "type");
    if (!require(firstCastType != nullptr,
                 "first cast target should keep its exact start column")) {
        return false;
    }
    if (!require(firstCastType->range.end.character -
                         firstCastType->range.start.character ==
                     3,
                 "first cast target length should match 'f64'")) {
        return false;
    }
    if (!require(findSemanticToken(operatorAndCastTokens, 7, 66, "variable") !=
                     nullptr,
                 "BIRD_X use should keep its exact start column after prior operators")) {
        return false;
    }
    const auto* secondCastType =
        findSemanticToken(operatorAndCastTokens, 7, 76, "type");
    if (!require(secondCastType != nullptr,
                 "second cast target should keep its exact start column")) {
        return false;
    }
    if (!require(secondCastType->range.end.character -
                         secondCastType->range.start.character ==
                     3,
                 "second cast target length should match 'f64'")) {
        return false;
    }

    const std::string builtinSource =
                "const value f64 = sqrt(9.0)\n"
        "print(value)\n";
    options.sourcePath = "tooling_semantic_tokens_builtin_regression.mog";
    ToolingDocumentAnalysis builtinAnalysis =
        analyzeDocumentForTooling(builtinSource, options);
    if (!require(builtinAnalysis.status == AstFrontendBuildStatus::Success,
                 "builtin semantic token sample should succeed")) {
        return false;
    }

    const auto builtinTokens = findSemanticTokensForTooling(builtinAnalysis);
    if (!require(findSemanticToken(builtinTokens, 0, 18, "function") != nullptr,
                 "builtin stdlib calls should emit function semantic tokens")) {
        return false;
    }
    if (!require(findSemanticToken(builtinTokens, 1, 0, "function") != nullptr,
                 "print syntax should emit function semantic tokens")) {
        return false;
    }

    return true;
}

bool testCompletions() {
    ToolingAnalyzeOptions options;
    const std::string topLevelSource =
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
        findCompletionsForTooling(localAnalysis, ToolingPosition{5, 8});
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
        findCompletionsForTooling(localAnalysis, ToolingPosition{7, 4});
    const auto* outerValue = findCompletion(outerScope, "Value");
    if (!require(outerValue != nullptr && outerValue->kind == "parameter",
                 "after leaving the block, completions should restore the parameter binding")) {
        return false;
    }

    const std::string importSemanticErrorSource =
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
        findCompletionsForTooling(importAnalysis, ToolingPosition{3, 10});
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
                     findCompletion(parseFailCompletions, "while") != nullptr &&
                     findCompletion(parseFailCompletions, "sqrt") != nullptr &&
                     findCompletion(parseFailCompletions, "print") != nullptr,
                 "parse-failed completions should include keywords and builtin functions")) {
        return false;
    }
    if (!require(findCompletion(parseFailCompletions, "broken") == nullptr,
                 "parse-failed completions should not invent local bindings")) {
        return false;
    }

    const std::string shadowedBuiltinSource =
                "fn sqrt(text str) str {\n"
        "    return text\n"
        "}\n"
        "const value str = \"\"\n"
        "\n";
    options.sourcePath = "tooling_completion_builtin_shadow_regression.mog";
    ToolingDocumentAnalysis shadowedBuiltinAnalysis =
        analyzeDocumentForTooling(shadowedBuiltinSource, options);
    if (!require(shadowedBuiltinAnalysis.status == AstFrontendBuildStatus::Success,
                 "builtin shadowing completion sample should succeed")) {
        return false;
    }

    const auto shadowedBuiltinCompletions =
        findCompletionsForTooling(shadowedBuiltinAnalysis, ToolingPosition{4, 0});
    const auto* shadowedSqrt =
        findCompletion(shadowedBuiltinCompletions, "sqrt");
    if (!require(shadowedSqrt != nullptr &&
                     shadowedSqrt->detail == "fn sqrt(text str) str",
                 "user declarations should shadow builtin completion entries")) {
        return false;
    }

    const std::string memberSource =
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
        findCompletionsForTooling(memberAnalysis, ToolingPosition{8, 18});
    const auto* valueCompletion = findCompletion(memberCompletions, "value");
    const auto* getCompletion = findCompletion(memberCompletions, "get");
    if (!require(valueCompletion != nullptr &&
                     valueCompletion->kind == "field" &&
                     valueCompletion->detail == "value i32",
                 "member completions should expose field members")) {
        return false;
    }
    if (!require(getCompletion != nullptr &&
                     getCompletion->kind == "method" &&
                     getCompletion->detail == "fn get() i32",
                 "member completions should expose method members")) {
        return false;
    }
    if (!require(findCompletion(memberCompletions, "box") == nullptr &&
                     findCompletion(memberCompletions, "return") == nullptr,
                 "member completions should not mix in scope or keyword items")) {
        return false;
    }

    const std::string incompleteMemberSource =
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
        incompleteMemberAnalysis, incompleteMemberSource, ToolingPosition{8, 15});
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

    const std::string incompleteMemberBeforeCallSource =
                "type Pipe struct {}\n"
        "type GameState struct {\n"
        "    birdY f64\n"
        "    birdVelocity f64\n"
        "    pipes Array<Pipe>\n"
        "    spawnTimer f64\n"
        "    spawnIndex i32\n"
        "    score i32\n"
        "    running bool\n"
        "    dead bool\n"
        "}\n"
        "fn seedInitialPipes(state GameState) void {}\n"
        "fn resetGame() GameState {\n"
        "    var state GameState = GameState()\n"
        "    var pipes Array<Pipe> = []\n"
        "\n"
        "    state.birdY = 220.0\n"
        "    state.birdVelocity = 0.0\n"
        "    state.pipes = pipes\n"
        "    state.spawnTimer = 0.0\n"
        "    state.spawnIndex = 0\n"
        "    state.score = 0\n"
        "    state.running = true\n"
        "    state.dead = false\n"
        "\n"
        "    state.\n"
        "    seedInitialPipes(state)\n"
        "    return state\n"
        "}\n";
    options.sourcePath =
        "tooling_completion_member_before_call_regression.mog";
    ToolingDocumentAnalysis incompleteMemberBeforeCallAnalysis =
        analyzeDocumentForTooling(incompleteMemberBeforeCallSource, options);

    const auto incompleteMemberBeforeCallCompletions = findCompletionsForTooling(
        incompleteMemberBeforeCallAnalysis, incompleteMemberBeforeCallSource,
        ToolingPosition{26, 10});
    if (!require(findCompletion(incompleteMemberBeforeCallCompletions, "birdY") !=
                     nullptr &&
                     findCompletion(incompleteMemberBeforeCallCompletions,
                                    "dead") != nullptr,
                 "member completions before a following call should expose fields")) {
        return false;
    }
    if (!require(
            findCompletion(incompleteMemberBeforeCallCompletions,
                           "seedInitialPipes") == nullptr &&
                findCompletion(incompleteMemberBeforeCallCompletions, "state") ==
                    nullptr,
            "member completions before a following call should not fall back to scope items")) {
        return false;
    }

    const std::filesystem::path completionTempRoot =
        std::filesystem::temp_directory_path() / "mog_tooling_completion_regression";
    std::error_code completionEc;
    std::filesystem::create_directories(completionTempRoot, completionEc);
    if (!require(!completionEc,
                 "imported member completion regression should create its temporary workspace")) {
        return false;
    }

    const std::filesystem::path importedStatePath = completionTempRoot / "state.mog";
    const std::filesystem::path importedLogicPath = completionTempRoot / "logic.mog";
    const std::string importedStateSource =
                "type GameState struct {\n"
        "    birdY f64\n"
        "    spawnTimer f64\n"
        "}\n";
    const std::string importedLogicSource =
                "const { GameState } = @import(\"./state.mog\")\n"
        "fn update(state GameState) void {\n"
        "    state.\n"
        "}\n";
    if (!require(writeFile(importedStatePath, importedStateSource),
                 "imported member completion regression should write the state module") ||
        !require(writeFile(importedLogicPath, importedLogicSource),
                 "imported member completion regression should write the consumer module")) {
        return false;
    }

    options.sourcePath = importedLogicPath.string();
    ToolingDocumentAnalysis importedMemberAnalysis =
        analyzeDocumentForTooling(importedLogicSource, options);
    const auto importedMemberCompletions = findCompletionsForTooling(
        importedMemberAnalysis, importedLogicSource, ToolingPosition{2, 10});
    const auto* importedBirdY = findCompletion(importedMemberCompletions, "birdY");
    const auto* importedSpawnTimer =
        findCompletion(importedMemberCompletions, "spawnTimer");
    if (!require(importedBirdY != nullptr &&
                     importedBirdY->kind == "field" &&
                     importedBirdY->detail == "birdY f64" &&
                     importedSpawnTimer != nullptr &&
                     importedSpawnTimer->kind == "field" &&
                     importedSpawnTimer->detail == "spawnTimer f64",
                 "imported member completions should expose fields from source modules")) {
        return false;
    }
    if (!require(findCompletion(importedMemberCompletions, "update") == nullptr &&
                     findCompletion(importedMemberCompletions, "state") == nullptr,
                 "imported member completions should not fall back to scope items")) {
        return false;
    }

    const std::string moduleMemberSource =
                "const math = @import(\"./modules/math.mog\")\n"
        "print(math.Ad)\n";
    options.sourcePath = "tests/sample_import_basic.mog";
    ToolingDocumentAnalysis moduleMemberAnalysis =
        analyzeDocumentForTooling(moduleMemberSource, options);
    if (!require((moduleMemberAnalysis.status ==
                      AstFrontendBuildStatus::Success ||
                  moduleMemberAnalysis.status ==
                      AstFrontendBuildStatus::SemanticError) &&
                     moduleMemberAnalysis.hasBindings,
                 "module member completion sample should preserve bindings")) {
        return false;
    }

    const auto moduleMemberCompletions =
        findCompletionsForTooling(moduleMemberAnalysis, ToolingPosition{1, 13});
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
        findCompletionsForTooling(importExportAnalysis, ToolingPosition{0, 15});
    if (!require(findCompletion(importExportCompletions, "PI") != nullptr,
                 "destructured import completions should expose remaining exports")) {
        return false;
    }
    if (!require(findCompletion(importExportCompletions, "Add") == nullptr,
                 "destructured import completions should exclude already imported exports")) {
        return false;
    }

    const std::string typeContextSource =
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
        findCompletionsForTooling(typeContextAnalysis, ToolingPosition{2, 16});
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
        importedTypeContextAnalysis, ToolingPosition{2, 16});
    if (!require(findCompletion(importedTypeCompletions, "Counter") != nullptr,
                 "type-context completions should include imported class bindings")) {
        return false;
    }

    options.packageSearchPaths = {
        (std::filesystem::current_path() / "build" / "packages").string()};
    const std::string nativeMemberCompletionSource =
                "const counter = @import(\"counter\")\n"
        "print(counter.cre)\n";
    options.sourcePath = "tooling_completion_native_member_regression.mog";
    ToolingDocumentAnalysis nativeMemberCompletionAnalysis =
        analyzeDocumentForTooling(nativeMemberCompletionSource, options);
    const auto nativeMemberCompletions = findCompletionsForTooling(
        nativeMemberCompletionAnalysis, nativeMemberCompletionSource,
        ToolingPosition{1, 17});
    if (!require(findCompletion(nativeMemberCompletions, "create") != nullptr &&
                     findCompletion(nativeMemberCompletions, "Counter") == nullptr,
                 "value member completions should expose native package values only")) {
        return false;
    }

    const std::string nativeTypeCompletionSource =
                "const counter = @import(\"counter\")\n"
        "fn make() void {\n"
        "    var value counter.Cou = counter.create(1i64)\n"
        "}\n";
    options.sourcePath = "tooling_completion_native_type_regression.mog";
    ToolingDocumentAnalysis nativeTypeCompletionAnalysis =
        analyzeDocumentForTooling(nativeTypeCompletionSource, options);
    const auto nativeTypeCompletions = findCompletionsForTooling(
        nativeTypeCompletionAnalysis, nativeTypeCompletionSource,
        ToolingPosition{2, 25});
    if (!require(findCompletion(nativeTypeCompletions, "Counter") != nullptr &&
                     findCompletion(nativeTypeCompletions, "create") == nullptr,
                 "type-context member completions should expose native package types only")) {
        return false;
    }
    options.packageSearchPaths.clear();

    const auto normalCompletions =
        findCompletionsForTooling(typeContextAnalysis, ToolingPosition{4, 4});
    if (!require(findCompletion(normalCompletions, "LocalAlias") == nullptr &&
                     findCompletion(normalCompletions, "i32") == nullptr,
                 "ordinary completions should exclude type-only names")) {
        return false;
    }

    const std::string ordinaryCallCompletionSource =
                "fn sqrtValue() i32 {\n"
        "    return 1\n"
        "}\n"
        "fn main() void {\n"
        "    sq()\n"
        "}\n";
    options.sourcePath = "tooling_completion_ordinary_call_regression.mog";
    ToolingDocumentAnalysis ordinaryCallCompletionAnalysis =
        analyzeDocumentForTooling(ordinaryCallCompletionSource, options);
    if (!require(ordinaryCallCompletionAnalysis.status ==
                     AstFrontendBuildStatus::SemanticError &&
                     ordinaryCallCompletionAnalysis.hasParse,
                 "ordinary call completion sample should preserve parse data on semantic errors")) {
        return false;
    }

    const auto ordinaryCallCompletions = findCompletionsForTooling(
        ordinaryCallCompletionAnalysis, ToolingPosition{4, 6});
    if (!require(findCompletion(ordinaryCallCompletions, "sqrtValue") != nullptr &&
                     findCompletion(ordinaryCallCompletions, "print") != nullptr,
                 "ordinary call completions should keep function suggestions")) {
        return false;
    }
    if (!require(findCompletion(ordinaryCallCompletions, "i32") == nullptr &&
                     findCompletion(ordinaryCallCompletions, "LocalAlias") == nullptr,
                 "ordinary call completions should not switch into type-context suggestions")) {
        return false;
    }

    return true;
}

bool testSignatureHelp() {
    ToolingAnalyzeOptions options;
    const std::string directSource =
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
        directAnalysis, directSource, ToolingPosition{3, 27});
    if (!require(directHelp.has_value() &&
                     directHelp->activeParameter == 1 &&
                     !directHelp->signatures.empty() &&
                     directHelp->signatures.front().label ==
                         "fn Add(a i32, b i32) i32",
                 "direct calls should expose signature help and active parameter")) {
        return false;
    }
    if (!require(directHelp->signatures.front().parameters.size() == 2 &&
                     directHelp->signatures.front().parameters[0].label ==
                         "a i32" &&
                     directHelp->signatures.front().parameters[1].label ==
                         "b i32",
                 "signature help should preserve parameter labels in Mog syntax")) {
        return false;
    }

    const std::string moduleSource =
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
        moduleAnalysis, moduleSource, ToolingPosition{1, 31});
    if (!require(moduleHelp.has_value() &&
                     moduleHelp->activeParameter == 1 &&
                     !moduleHelp->signatures.empty() &&
                     moduleHelp->signatures.front().label ==
                         "fn Add(a i32, b i32) i32",
                 "module export calls should expose signature help")) {
        return false;
    }

    const std::string builtinSource =
                "const value f64 = sqrt(9.0)\n"
        "print(1)\n";
    options.sourcePath = "tooling_signature_help_builtin_regression.mog";
    ToolingDocumentAnalysis builtinAnalysis =
        analyzeDocumentForTooling(builtinSource, options);
    if (!require(builtinAnalysis.status == AstFrontendBuildStatus::Success,
                 "builtin signature help sample should succeed")) {
        return false;
    }

    const auto builtinHelp = findSignatureHelpForTooling(
        builtinAnalysis, builtinSource, ToolingPosition{0, 24});
    if (!require(builtinHelp.has_value() &&
                     builtinHelp->activeParameter == 0 &&
                     !builtinHelp->signatures.empty() &&
                     builtinHelp->signatures.front().label ==
                         "fn sqrt(f64) f64",
                 "builtin stdlib calls should expose signature help")) {
        return false;
    }

    const auto printHelp = findSignatureHelpForTooling(
        builtinAnalysis, builtinSource, ToolingPosition{1, 7});
    if (!require(printHelp.has_value() &&
                     printHelp->activeParameter == 0 &&
                     !printHelp->signatures.empty() &&
                     printHelp->signatures.front().label ==
                         "fn print(any) void",
                 "print syntax should expose signature help")) {
        return false;
    }

    const std::string parseFailSource =
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
        parseFailAnalysis, parseFailSource, ToolingPosition{3, 25});
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
    const std::string localSource =
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
        prepareRenameForTooling(localAnalysis, ToolingPosition{2, 11});
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

    if (!require(findEdit(localEdits, options.sourcePath, 1, 8) != nullptr &&
                     findEdit(localEdits, options.sourcePath, 2, 11) != nullptr &&
                     findEdit(localEdits, options.sourcePath, 2, 19) != nullptr,
                 "local rename should target only the bound local identifier spans")) {
        return false;
    }

    const std::string memberSource =
                "type Box struct {\n"
        "    value i32\n"
        "}\n"
        "fn read(box Box) i32 {\n"
        "    return box.value\n"
        "}\n";
    options.sourcePath = "tooling_prepare_rename_member_regression.mog";
    ToolingDocumentAnalysis memberAnalysis =
        analyzeDocumentForTooling(memberSource, options);
    if (!require(!prepareRenameForTooling(memberAnalysis, ToolingPosition{4, 15})
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
                                                      "fn Get() i32 {\n"
                           "    return 42\n"
                           "}\n"
                           "const Answer i32 = 42\n"
                           "const privateValue i32 = 1\n"),
                 "rename regression should write the dependency module") ||
        !require(writeFile(importerPath,
                                                      "const { Answer, Get } = @import(\"./dep.mog\")\n"
                           "print(Get())\n"
                           "print(Answer)\n"),
                 "rename regression should write the importer module") ||
        !require(writeFile(aliasPath,
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
        prepareRenameForTooling(importerAnalysis, ToolingPosition{2, 6});
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
        findEdit(importLocalEdits, importerPath.string(), 0, 8);
    if (!require(importBindingEdit != nullptr &&
                     importBindingEdit->newText == "Answer as LocalAnswer",
                 "unaliased import rename should insert a local alias")) {
        return false;
    }

    const auto* importUseEdit =
        findEdit(importLocalEdits, importerPath.string(), 2, 6);
    if (!require(importUseEdit != nullptr &&
                     importUseEdit->newText == "LocalAnswer",
                 "unaliased import rename should update same-file uses")) {
        return false;
    }

    const auto importAliasTarget =
        prepareRenameForTooling(aliasAnalysis, ToolingPosition{1, 6});
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

    if (!require(findEdit(importAliasEdits, aliasPath.string(), 0, 18) != nullptr &&
                     findEdit(importAliasEdits, aliasPath.string(), 1, 6) != nullptr,
                 "aliased import rename should target the alias spans only")) {
        return false;
    }

    const auto exportedTarget =
        prepareRenameForTooling(depAnalysis, ToolingPosition{3, 6});
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

    if (!require(findEdit(importerExportEdits, importerPath.string(), 0, 8) != nullptr &&
                     findEdit(importerExportEdits, importerPath.string(), 2, 6) != nullptr,
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
        findEdit(aliasExportEdits, aliasPath.string(), 0, 8);
    if (!require(aliasExportEdit != nullptr &&
                     aliasExportEdit->newText == "FinalAnswer",
                 "exported rename should still rewrite the imported exported name")) {
        return false;
    }

    return true;
}

bool testFormatting() {
    const std::string source =
        "// file header\n"
        "const { Answer as Result: i32 } = @import(\"./dep.mog\") // import note\n"
        "fn add(x i32,y i32)i32{\n"
        "var total i32= x+y // trailing\n"
        "return total\n"
        "}\n";

    ToolingAnalyzeOptions options;
    options.sourcePath = "tooling_format_regression.mog";
    ToolingDocumentAnalysis analysis =
        analyzeDocumentForTooling(source, options);
    if (!require(analysis.hasParse,
                 "format regression source should parse successfully")) {
        return false;
    }

    const auto formatted = formatDocumentForTooling(source, analysis);
    if (!require(formatted.has_value(),
                 "formatter should produce output for supported comments")) {
        return false;
    }

    const std::string expected =
        "// file header\n"
        "const { Answer as Result: i32 } = @import(\"./dep.mog\") // import note\n"
        "fn add(x i32, y i32) i32 {\n"
        "    var total i32 = x + y // trailing\n"
        "    return total\n"
        "}\n";
    if (!require(*formatted == expected,
                 "formatter should normalize Mog source to the canonical style")) {
        return false;
    }

    const std::string multilineCallSource =
        "fn main() void {\n"
        "    player.init(\n"
        "        0usize,\n"
        "        \"Aaron\",\n"
        "        Vec2().init(0f32, 0f32),\n"
        "        Vec2().init(0f32, 0f32),\n"
        "        Vec2().init(1920f32, 1080f32)\n"
        "    )\n"
        "}\n";
    ToolingAnalyzeOptions multilineOptions;
    multilineOptions.sourcePath = "tooling_multiline_call_regression.mog";
    ToolingDocumentAnalysis multilineAnalysis =
        analyzeDocumentForTooling(multilineCallSource, multilineOptions);
    if (!require(multilineAnalysis.hasParse,
                 "multiline call formatting source should parse successfully")) {
        return false;
    }

    const auto multilineFormatted =
        formatDocumentForTooling(multilineCallSource, multilineAnalysis);
    if (!require(multilineFormatted.has_value(),
                 "formatter should support multiline calls")) {
        return false;
    }
    if (!require(*multilineFormatted == multilineCallSource,
                 "formatter should preserve authored multiline call layout")) {
        return false;
    }

    const std::string blankLineCapSource =
        "fn main() void {\n"
        "    const first i32 = 1\n"
        "\n"
        "\n"
        "\n"
        "    const second i32 = 2\n"
        "}\n";
    ToolingAnalyzeOptions blankLineOptions;
    blankLineOptions.sourcePath = "tooling_blank_line_cap_regression.mog";
    ToolingDocumentAnalysis blankLineAnalysis =
        analyzeDocumentForTooling(blankLineCapSource, blankLineOptions);
    if (!require(blankLineAnalysis.hasParse,
                 "blank-line cap formatting source should parse successfully")) {
        return false;
    }

    const auto blankLineFormatted =
        formatDocumentForTooling(blankLineCapSource, blankLineAnalysis);
    if (!require(blankLineFormatted.has_value(),
                 "formatter should support blank-line cap formatting")) {
        return false;
    }
    const std::string blankLineExpected =
        "fn main() void {\n"
        "    const first i32 = 1\n"
        "\n"
        "    const second i32 = 2\n"
        "}\n";
    if (!require(*blankLineFormatted == blankLineExpected,
                 "formatter should cap blank lines to one empty line")) {
        return false;
    }

    const std::string widthWrappedCallSource =
        "fn main() void {\n"
        "    player.init(0usize, \"Aaron\", Vec2().init(0f32, 0f32), Vec2().init(0f32, 0f32), Vec2().init(1920f32, 1080f32))\n"
        "}\n";
    ToolingAnalyzeOptions widthWrappedCallOptions;
    widthWrappedCallOptions.sourcePath = "tooling_width_wrap_call_regression.mog";
    ToolingDocumentAnalysis widthWrappedCallAnalysis =
        analyzeDocumentForTooling(widthWrappedCallSource, widthWrappedCallOptions);
    if (!require(widthWrappedCallAnalysis.hasParse,
                 "width-wrap call source should parse successfully")) {
        return false;
    }

    const auto widthWrappedCallFormatted =
        formatDocumentForTooling(widthWrappedCallSource, widthWrappedCallAnalysis);
    if (!require(widthWrappedCallFormatted.has_value(),
                 "formatter should support width-wrapped calls")) {
        return false;
    }
    const std::string widthWrappedCallExpected =
        "fn main() void {\n"
        "    player.init(\n"
        "        0usize,\n"
        "        \"Aaron\",\n"
        "        Vec2().init(0f32, 0f32),\n"
        "        Vec2().init(0f32, 0f32),\n"
        "        Vec2().init(1920f32, 1080f32)\n"
        "    )\n"
        "}\n";
    if (!require(*widthWrappedCallFormatted == widthWrappedCallExpected,
                 "formatter should wrap long call sites to stay under 80 columns")) {
        return false;
    }

    const std::string widthWrappedSignatureSource =
        "fn init(id usize, name str, position Vec2, velocity Vec2, viewport Vec2) void {\n"
        "    print(name)\n"
        "}\n";
    ToolingAnalyzeOptions widthWrappedSignatureOptions;
    widthWrappedSignatureOptions.sourcePath =
        "tooling_width_wrap_signature_regression.mog";
    ToolingDocumentAnalysis widthWrappedSignatureAnalysis =
        analyzeDocumentForTooling(widthWrappedSignatureSource,
                                  widthWrappedSignatureOptions);
    if (!require(widthWrappedSignatureAnalysis.hasParse,
                 "width-wrap signature source should parse successfully")) {
        return false;
    }

    const auto widthWrappedSignatureFormatted = formatDocumentForTooling(
        widthWrappedSignatureSource, widthWrappedSignatureAnalysis);
    if (!require(widthWrappedSignatureFormatted.has_value(),
                 "formatter should support width-wrapped signatures")) {
        return false;
    }
    const std::string widthWrappedSignatureExpected =
        "fn init(\n"
        "    id usize,\n"
        "    name str,\n"
        "    position Vec2,\n"
        "    velocity Vec2,\n"
        "    viewport Vec2\n"
        ") void {\n"
        "    print(name)\n"
        "}\n";
    if (!require(*widthWrappedSignatureFormatted == widthWrappedSignatureExpected,
                 "formatter should wrap long function signatures to stay under 80 columns")) {
        return false;
    }

    const std::string widthWrappedAssignmentSource =
        "fn main() void {\n"
        "    connection.items[index] = connection.items[connection.items.size() - 1]\n"
        "}\n";
    ToolingAnalyzeOptions widthWrappedAssignmentOptions;
    widthWrappedAssignmentOptions.sourcePath =
        "tooling_width_wrap_assignment_regression.mog";
    ToolingDocumentAnalysis widthWrappedAssignmentAnalysis =
        analyzeDocumentForTooling(widthWrappedAssignmentSource,
                                  widthWrappedAssignmentOptions);
    if (!require(widthWrappedAssignmentAnalysis.hasParse,
                 "width-wrap assignment source should parse successfully")) {
        return false;
    }

    const auto widthWrappedAssignmentFormatted = formatDocumentForTooling(
        widthWrappedAssignmentSource, widthWrappedAssignmentAnalysis);
    if (!require(widthWrappedAssignmentFormatted.has_value(),
                 "formatter should support width-wrapped assignments")) {
        return false;
    }
    const std::string widthWrappedAssignmentExpected =
        "fn main() void {\n"
        "    connection.items[index] =\n"
        "        connection.items[connection.items.size() - 1]\n"
        "}\n";
    if (!require(*widthWrappedAssignmentFormatted == widthWrappedAssignmentExpected,
                 "formatter should wrap long assignment expressions to stay under 80 columns")) {
        return false;
    }

    const std::filesystem::path projectRoot =
        std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::filesystem::path fixtureSourcePath =
        projectRoot / "examples" / "test" / "main.mog";
    const std::filesystem::path fixtureExpectedPath =
        projectRoot / "examples" / "test" / "main-formatted.mog";
    const auto fixtureSource = readFileText(fixtureSourcePath);
    if (!require(fixtureSource.has_value(),
                 "formatter fixture source should be readable")) {
        return false;
    }
    const auto fixtureExpected = readFileText(fixtureExpectedPath);
    if (!require(fixtureExpected.has_value(),
                 "formatter fixture expectation should be readable")) {
        return false;
    }

    ToolingAnalyzeOptions fixtureOptions;
    fixtureOptions.sourcePath = fixtureSourcePath.string();
    ToolingDocumentAnalysis fixtureAnalysis =
        analyzeDocumentForTooling(*fixtureSource, fixtureOptions);
    if (!require(fixtureAnalysis.hasParse,
                 "formatter fixture source should parse successfully")) {
        return false;
    }

    const auto fixtureFormatted =
        formatDocumentForTooling(*fixtureSource, fixtureAnalysis);
    if (!require(fixtureFormatted.has_value(),
                 "formatter should support the main fixture")) {
        return false;
    }
    if (!require(*fixtureFormatted == *fixtureExpected,
                 "formatter should preserve fixture comment placement and blank lines")) {
        return false;
    }

    const std::string inlineCommentSource =
        "const value i32 = 1 /* inline */ + 2\n";
    ToolingDocumentAnalysis inlineCommentAnalysis =
        analyzeDocumentForTooling(inlineCommentSource, options);
    if (!require(inlineCommentAnalysis.hasParse,
                 "inline-comment formatting sample should parse")) {
        return false;
    }

    if (!require(!formatDocumentForTooling(inlineCommentSource,
                                           inlineCommentAnalysis)
                      .has_value(),
                 "formatter should safely reject unsupported inline comments")) {
        return false;
    }

    return true;
}

}  // namespace

int main() {
    if (!testRangeConversion()) {
        return 1;
    }

    if (!testDiagnosticsAndSymbols()) {
        return 1;
    }

    if (!testPackageApiDiagnosticsAndSymbols()) {
        return 1;
    }

    if (!testImportedDiagnosticPaths()) {
        return 1;
    }

    if (!testMalformedImportArgumentDiagnostic()) {
        return 1;
    }

    if (!testInlineSemicolonDiagnostics()) {
        return 1;
    }

    if (!testLexerDiagnostics()) {
        return 1;
    }
    if (!testUnterminatedBlockCommentDiagnostic()) {
        return 1;
    }

    if (!testDefinitionLookup()) {
        return 1;
    }

    if (!testReferencesAndHover()) {
        return 1;
    }

    if (!testSemanticTokens()) {
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

    if (!testFormatting()) {
        return 1;
    }

    std::cout << "[PASS] frontend_tooling_regression\n";
    return 0;
}
