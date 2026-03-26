#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Ast.hpp"
#include "AstFrontend.hpp"
#include "AstParser.hpp"
#include "Chunk.hpp"
#include "Compiler.hpp"
#include "GC.hpp"
#include "HirOptimizer.hpp"

namespace {

struct OptimizedFrontendResult {
    AstFrontendResult frontend;
    AstNodeId preIdentityExprId = 0;
    AstNodeId preFoldedExprId = 0;
    size_t preFoldedExprLine = 0;
};

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        return {};
    }

    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

bool writeFile(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream output(path);
    if (!output) {
        return false;
    }

    output << contents;
    return output.good();
}

std::string captureChunkDisassembly(const Chunk& chunk) {
    std::ostringstream output;
    std::streambuf* original = std::cout.rdbuf(output.rdbuf());
    Chunk& mutableChunk = const_cast<Chunk&>(chunk);

    int offset = 0;
    while (offset < chunk.count()) {
        offset = mutableChunk.disassembleInstruction(offset);
    }

    std::cout.rdbuf(original);
    return output.str();
}

bool require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }
    return true;
}

AstExpr* topLevelPrintExpr(AstModule& module, size_t printIndex) {
    size_t seen = 0;
    for (auto& item : module.items) {
        if (!item) {
            continue;
        }

        auto* stmtPtr = std::get_if<AstStmtPtr>(&item->value);
        if (!stmtPtr || !*stmtPtr) {
            continue;
        }

        auto* printStmt = std::get_if<AstPrintStmt>(&(*stmtPtr)->value);
        if (!printStmt || !printStmt->expression) {
            continue;
        }

        if (seen == printIndex) {
            return printStmt->expression.get();
        }
        ++seen;
    }

    return nullptr;
}

HirExpr* topLevelHirPrintExpr(HirModule& module, size_t printIndex) {
    size_t seen = 0;
    for (HirItemId itemId : module.items) {
        auto* stmtId = std::get_if<HirStmtId>(&module.item(itemId).value);
        if (!stmtId) {
            continue;
        }

        auto* printStmt = std::get_if<HirPrintStmt>(&module.stmt(*stmtId).value);
        if (!printStmt || !printStmt->expression) {
            continue;
        }

        if (seen == printIndex) {
            return &module.expr(*printStmt->expression);
        }
        ++seen;
    }

    return nullptr;
}

AstDestructuredImportStmt* topLevelDestructuredImport(AstModule& module) {
    for (auto& item : module.items) {
        if (!item) {
            continue;
        }

        auto* stmtPtr = std::get_if<AstStmtPtr>(&item->value);
        if (!stmtPtr || !*stmtPtr) {
            continue;
        }

        auto* importStmt =
            std::get_if<AstDestructuredImportStmt>(&(*stmtPtr)->value);
        if (importStmt) {
            return importStmt;
        }
    }

    return nullptr;
}

HirDestructuredImportStmt* topLevelHirDestructuredImport(HirModule& module) {
    for (HirItemId itemId : module.items) {
        auto* stmtId = std::get_if<HirStmtId>(&module.item(itemId).value);
        if (!stmtId) {
            continue;
        }

        auto* importStmt =
            std::get_if<HirDestructuredImportStmt>(&module.stmt(*stmtId).value);
        if (importStmt) {
            return importStmt;
        }
    }

    return nullptr;
}

bool buildOptimizedFrontend(std::string_view source,
                            OptimizedFrontendResult& outResult,
                            std::string& outError) {
    outError.clear();
    const AstFrontendOptions options;
    std::vector<TypeError> errors;
    const AstFrontendBuildStatus status = buildAstFrontend(
        source, options, errors,
        outResult.frontend);
    if (status != AstFrontendBuildStatus::Success) {
        outError = errors.empty() ? "failed to build optimized frontend"
                                  : errors.front().message;
        return false;
    }

    AstExpr* preIdentityExpr = topLevelPrintExpr(outResult.frontend.module, 0);
    AstExpr* preFoldedExpr = topLevelPrintExpr(outResult.frontend.module, 1);
    if (preIdentityExpr == nullptr || preFoldedExpr == nullptr) {
        outError = "failed to locate inline regression print expressions";
        return false;
    }

    outResult.preIdentityExprId = preIdentityExpr->node.id;
    outResult.preFoldedExprId = preFoldedExpr->node.id;
    outResult.preFoldedExprLine = preFoldedExpr->node.line;
    return true;
}

bool compileFileCanonical(const std::filesystem::path& path,
                          CompilerEmitterMode emitterMode, GC& gc, Chunk& chunk,
                          std::string& outDisassembly) {
    const std::string source = readFile(path);
    if (source.empty() && std::filesystem::file_size(path) != 0) {
        return false;
    }

    Compiler compiler;
    compiler.setGC(&gc);
    compiler.setPackageSearchPaths(
        {std::filesystem::current_path().append("build/packages").string()});
    compiler.setEmitterMode(emitterMode);

    if (!compiler.compile(source, chunk, path.string())) {
        return false;
    }

    outDisassembly = captureChunkDisassembly(chunk);
    return true;
}

bool compileSourceCanonical(std::string_view source,
                            CompilerEmitterMode emitterMode, GC& gc,
                            Chunk& chunk, std::string& outDisassembly) {
    Compiler compiler;
    compiler.setGC(&gc);
    compiler.setPackageSearchPaths(
        {std::filesystem::current_path().append("build/packages").string()});
    compiler.setEmitterMode(emitterMode);
    if (!compiler.compile(source, chunk, "<frontend-inline>")) {
        return false;
    }

    outDisassembly = captureChunkDisassembly(chunk);
    return true;
}

bool checkCanonicalLoweringStable(const std::filesystem::path& path,
                                  const std::string& description) {
    GC autoGc;
    Chunk autoChunk;
    std::string autoDisassembly;
    if (!require(compileFileCanonical(path, CompilerEmitterMode::Auto, autoGc,
                                      autoChunk, autoDisassembly),
                 description + " should compile in auto mode")) {
        return false;
    }

    GC forcedHirGc;
    Chunk forcedHirChunk;
    std::string forcedHirDisassembly;
    if (!require(compileFileCanonical(path, CompilerEmitterMode::ForceHir,
                                      forcedHirGc, forcedHirChunk,
                                      forcedHirDisassembly),
                 description + " should compile in forced HIR mode")) {
        return false;
    }

    return require(autoDisassembly == forcedHirDisassembly,
                   description +
                       " should lower identically in auto and forced HIR modes");
}

bool checkStrictHirHasNoAdd(const std::filesystem::path& path,
                            const std::string& description) {
    const std::string source = readFile(path);
    if (!require(!source.empty(), description + " should be readable")) {
        return false;
    }

    GC strictGc;
    Chunk strictChunk;
    std::string strictDisassembly;
    if (!require(compileSourceCanonical(source, CompilerEmitterMode::Auto,
                                        strictGc, strictChunk, strictDisassembly),
                 description + " should compile in default auto mode")) {
        return false;
    }

    GC strictHirGc;
    Chunk strictHirChunk;
    std::string strictHirDisassembly;
    if (!require(compileSourceCanonical(source, CompilerEmitterMode::ForceHir,
                                        strictHirGc, strictHirChunk,
                                        strictHirDisassembly),
                 description + " should compile in default forced HIR mode")) {
        return false;
    }

    return require(strictDisassembly == strictHirDisassembly &&
                       strictDisassembly.find("IADD") == std::string::npos &&
                       strictDisassembly.find("ADD") == std::string::npos,
                   description +
                       " should lower identically in default auto/HIR modes without addition opcodes");
}

bool checkSemanticRefreshContract() {
    constexpr std::string_view kSource =
        "var value i32 = 41\n"
        "print(value + 0i32)\n"
        "print(6i32 * 7i32)\n";

    OptimizedFrontendResult pipeline;
    std::string error;
    if (!buildOptimizedFrontend(kSource, pipeline, error)) {
        std::cerr << "[FAIL] semantic optimization setup failed: " << error
                  << '\n';
        return false;
    }

    if (!require(pipeline.frontend.hirModule != nullptr,
                 "frontend should lower the sample to HIR")) {
        return false;
    }

    HirExpr* identityExpr = topLevelHirPrintExpr(*pipeline.frontend.hirModule, 0);
    HirExpr* foldedExpr = topLevelHirPrintExpr(*pipeline.frontend.hirModule, 1);
    if (!require(identityExpr != nullptr,
                 "missing first print expression after optimization") ||
        !require(foldedExpr != nullptr,
                 "missing second print expression after optimization")) {
        return false;
    }

    const AstNodeId identityId = identityExpr->node.astNodeId;
    const AstNodeId foldedId = foldedExpr->node.astNodeId;
    const size_t foldedLine = foldedExpr->node.line;

    if (!require(identityId == pipeline.preIdentityExprId &&
                     foldedId == pipeline.preFoldedExprId,
                 "optimized expressions should preserve their original node ids")) {
        return false;
    }

    if (!require(std::holds_alternative<HirBindingExpr>(identityExpr->value),
                 "identity rewrite should preserve the result node and collapse to an identifier")) {
        return false;
    }

    const auto* literal = std::get_if<HirLiteralExpr>(&foldedExpr->value);
    if (!require(literal != nullptr,
                 "constant fold should replace the expression with a literal")) {
        return false;
    }

    if (!require(foldedLine == pipeline.preFoldedExprLine &&
                     literal->token.line() == pipeline.preFoldedExprLine &&
                     literal->token.span().start.offset ==
                         foldedExpr->node.span.start.offset &&
                     literal->token.span().end.offset ==
                         foldedExpr->node.span.end.offset,
                 "synthetic literal should keep the replaced expression span")) {
        return false;
    }

    if (!require(identityExpr->node.type &&
                     identityExpr->node.type->kind == TypeKind::I32,
                 "optimized HIR should keep the preserved identity node typed as i32") ||
        !require(foldedExpr->node.type &&
                     foldedExpr->node.type->kind == TypeKind::I32,
                 "optimized HIR should type the folded literal node as i32")) {
        return false;
    }

    GC gc;
    Chunk chunk;
    Compiler compiler;
    compiler.setGC(&gc);
    if (!require(compiler.compile(kSource, chunk, "<frontend-regression>"),
                 "compiler should lower successfully after semantic optimization")) {
        return false;
    }

    const std::string disassembly = captureChunkDisassembly(chunk);
    if (!require(disassembly.find("IADD") == std::string::npos &&
                     disassembly.find("ADD") == std::string::npos &&
                     disassembly.find("IMULT") == std::string::npos &&
                     disassembly.find("MULT") == std::string::npos,
                 "lowered chunk should reflect the optimized expression forms")) {
        return false;
    }

    std::cout << "[PASS] semantic optimization contract\n";
    return true;
}

bool checkBooleanIdentityRefreshContract() {
    constexpr std::string_view kSource =
        "var yes bool = true\n"
        "print(yes == true)\n"
        "print(false != yes)\n";

    OptimizedFrontendResult pipeline;
    std::string error;
    if (!buildOptimizedFrontend(kSource, pipeline, error)) {
        std::cerr << "[FAIL] bool identity optimization setup failed: " << error
                  << '\n';
        return false;
    }

    if (!require(pipeline.frontend.hirModule != nullptr,
                 "frontend should lower the bool identity sample to HIR")) {
        return false;
    }

    HirExpr* firstExpr = topLevelHirPrintExpr(*pipeline.frontend.hirModule, 0);
    HirExpr* secondExpr = topLevelHirPrintExpr(*pipeline.frontend.hirModule, 1);
    if (!require(firstExpr != nullptr,
                 "missing first bool identity expression after optimization") ||
        !require(secondExpr != nullptr,
                 "missing second bool identity expression after optimization")) {
        return false;
    }

    if (!require(firstExpr->node.astNodeId == pipeline.preIdentityExprId &&
                     secondExpr->node.astNodeId == pipeline.preFoldedExprId,
                 "bool identity rewrites should preserve original node ids")) {
        return false;
    }

    if (!require(std::holds_alternative<HirBindingExpr>(firstExpr->value) &&
                     std::holds_alternative<HirBindingExpr>(secondExpr->value),
                 "bool identity rewrites should collapse to identifiers")) {
        return false;
    }

    if (!require(firstExpr->node.type &&
                     firstExpr->node.type->kind == TypeKind::BOOL,
                 "optimized HIR should keep the first bool identity node typed as bool") ||
        !require(secondExpr->node.type &&
                     secondExpr->node.type->kind == TypeKind::BOOL,
                 "optimized HIR should keep the second bool identity node typed as bool")) {
        return false;
    }

    GC gc;
    Chunk chunk;
    Compiler compiler;
    compiler.setGC(&gc);
    if (!require(compiler.compile(kSource, chunk, "<frontend-bool-identity>"),
                 "compiler should lower successfully after bool identity rewrites")) {
        return false;
    }

    const std::string disassembly = captureChunkDisassembly(chunk);
    if (!require(disassembly.find("EQUAL") == std::string::npos &&
                     disassembly.find("NOT_EQUAL") == std::string::npos,
                 "lowered chunk should not emit equality opcodes for bool identity rewrites")) {
        return false;
    }

    std::cout << "[PASS] bool identity optimization contract\n";
    return true;
}

bool checkSyntheticNotSpanRegression() {
    constexpr std::string_view kSource =
        "var yes bool = true\n"
        "print(yes == false)\n"
        "print(true)\n";

    OptimizedFrontendResult pipeline;
    std::string error;
    if (!buildOptimizedFrontend(kSource, pipeline, error)) {
        std::cerr << "[FAIL] synthetic not span setup failed: " << error
                  << '\n';
        return false;
    }

    if (!require(pipeline.frontend.hirModule != nullptr,
                 "frontend should lower the synthetic not sample to HIR")) {
        return false;
    }

    HirExpr* notExpr = topLevelHirPrintExpr(*pipeline.frontend.hirModule, 0);
    if (!require(notExpr != nullptr,
                 "missing synthesized not expression after optimization")) {
        return false;
    }

    if (!require(notExpr->node.astNodeId == pipeline.preIdentityExprId,
                 "synthesized not rewrite should preserve the original node id")) {
        return false;
    }

    const auto* unary = std::get_if<HirUnaryExpr>(&notExpr->value);
    if (!require(unary != nullptr && unary->operand.has_value(),
                 "bool equality false rewrite should synthesize a unary not expression")) {
        return false;
    }

    if (!require(unary->op.type() == TokenType::BANG &&
                     unary->op.line() == notExpr->node.line &&
                     unary->op.span().start.offset == notExpr->node.span.start.offset &&
                     unary->op.span().end.offset == notExpr->node.span.end.offset,
                 "synthetic unary not should keep the replaced expression span")) {
        return false;
    }

    if (!require(notExpr->node.type &&
                     notExpr->node.type->kind == TypeKind::BOOL,
                 "optimized HIR should type the synthesized not node as bool")) {
        return false;
    }

    GC gc;
    Chunk chunk;
    Compiler compiler;
    compiler.setGC(&gc);
    if (!require(compiler.compile(kSource, chunk, "<frontend-synth-not>"),
                 "compiler should lower successfully after synthesized not rewrite")) {
        return false;
    }

    const std::string disassembly = captureChunkDisassembly(chunk);
    if (!require(disassembly.find("EQUAL") == std::string::npos &&
                     disassembly.find("NOT") != std::string::npos,
                 "lowered chunk should emit unary not instead of equality for the synthesized rewrite")) {
        return false;
    }

    std::cout << "[PASS] synthetic not spans\n";
    return true;
}

bool checkIntegerDoubleTildeRefreshContract() {
    constexpr std::string_view kSource =
        "fn value() i32 {\n"
        "  return 7i32\n"
        "}\n"
        "print(~~value())\n"
        "print(~~0i32)\n";

    OptimizedFrontendResult pipeline;
    std::string error;
    if (!buildOptimizedFrontend(kSource, pipeline, error)) {
        std::cerr << "[FAIL] integer double-tilde setup failed: " << error
                  << '\n';
        return false;
    }

    if (!require(pipeline.frontend.hirModule != nullptr,
                 "frontend should lower the integer double-tilde sample to HIR")) {
        return false;
    }

    HirExpr* firstExpr = topLevelHirPrintExpr(*pipeline.frontend.hirModule, 0);
    HirExpr* secondExpr = topLevelHirPrintExpr(*pipeline.frontend.hirModule, 1);
    if (!require(firstExpr != nullptr,
                 "missing first integer double-tilde expression after optimization") ||
        !require(secondExpr != nullptr,
                 "missing second integer double-tilde expression after optimization")) {
        return false;
    }

    if (!require(firstExpr->node.astNodeId == pipeline.preIdentityExprId &&
                     secondExpr->node.astNodeId == pipeline.preFoldedExprId,
                 "integer double-tilde rewrites should preserve original node ids")) {
        return false;
    }

    if (!require(std::holds_alternative<HirCallExpr>(firstExpr->value),
                 "integer double-tilde should collapse to the original call expression") ||
        !require(std::holds_alternative<HirLiteralExpr>(secondExpr->value),
                 "constant integer double-tilde should fold to a literal")) {
        return false;
    }

    if (!require(firstExpr->node.type &&
                     firstExpr->node.type->kind == TypeKind::I32,
                 "optimized HIR should keep the first integer double-tilde node typed as i32") ||
        !require(secondExpr->node.type &&
                     secondExpr->node.type->kind == TypeKind::I32,
                 "optimized HIR should keep the second integer double-tilde node typed as i32")) {
        return false;
    }

    GC gc;
    Chunk chunk;
    Compiler compiler;
    compiler.setGC(&gc);
    if (!require(compiler.compile(kSource, chunk, "<frontend-int-double-tilde>"),
                 "compiler should lower successfully after integer double-tilde rewrites")) {
        return false;
    }

    const std::string disassembly = captureChunkDisassembly(chunk);
    if (!require(disassembly.find("BITWISE_NOT") == std::string::npos,
                 "lowered chunk should not emit bitwise not opcodes for integer double-tilde rewrites")) {
        return false;
    }

    std::cout << "[PASS] integer double-tilde optimization contract\n";
    return true;
}

bool checkIntegerMultiplyZeroRefreshContract() {
    constexpr std::string_view kSource =
        "var value i32 = 7i32\n"
        "print(value * 0i32)\n"
        "print(0i32 * value)\n";

    OptimizedFrontendResult pipeline;
    std::string error;
    if (!buildOptimizedFrontend(kSource, pipeline, error)) {
        std::cerr << "[FAIL] integer multiply-zero setup failed: " << error
                  << '\n';
        return false;
    }

    if (!require(pipeline.frontend.hirModule != nullptr,
                 "frontend should lower the multiply-zero sample to HIR")) {
        return false;
    }

    HirExpr* firstExpr = topLevelHirPrintExpr(*pipeline.frontend.hirModule, 0);
    HirExpr* secondExpr = topLevelHirPrintExpr(*pipeline.frontend.hirModule, 1);
    if (!require(firstExpr != nullptr,
                 "missing first integer multiply-zero expression after optimization") ||
        !require(secondExpr != nullptr,
                 "missing second integer multiply-zero expression after optimization")) {
        return false;
    }

    if (!require(firstExpr->node.astNodeId == pipeline.preIdentityExprId &&
                     secondExpr->node.astNodeId == pipeline.preFoldedExprId,
                 "integer multiply-zero rewrites should preserve original node ids")) {
        return false;
    }

    const auto* firstLiteral = std::get_if<HirLiteralExpr>(&firstExpr->value);
    const auto* secondLiteral = std::get_if<HirLiteralExpr>(&secondExpr->value);
    if (!require(firstLiteral != nullptr && secondLiteral != nullptr,
                 "integer multiply-zero rewrites should collapse to zero literals")) {
        return false;
    }

    if (!require(firstExpr->node.type &&
                     firstExpr->node.type->kind == TypeKind::I32,
                 "optimized HIR should keep the first multiply-zero node typed as i32") ||
        !require(secondExpr->node.type &&
                     secondExpr->node.type->kind == TypeKind::I32,
                 "optimized HIR should keep the second multiply-zero node typed as i32")) {
        return false;
    }

    GC gc;
    Chunk chunk;
    Compiler compiler;
    compiler.setGC(&gc);
    if (!require(compiler.compile(kSource, chunk, "<frontend-mul-zero>"),
                 "compiler should lower successfully after integer multiply-zero rewrites")) {
        return false;
    }

    const std::string disassembly = captureChunkDisassembly(chunk);
    if (!require(disassembly.find("IMULT") == std::string::npos &&
                     disassembly.find("UMULT") == std::string::npos &&
                     disassembly.find("MULT") == std::string::npos,
                 "lowered chunk should not emit multiply opcodes for integer multiply-zero rewrites")) {
        return false;
    }

    std::cout << "[PASS] integer multiply-zero optimization contract\n";
    return true;
}

bool checkParserDiagnosticSpans() {
    constexpr std::string_view kSource =
        "print(\n"
        "  1\n"
        "  + 2)\n";

    AstParser parser(kSource);
    AstModule module;
    if (!require(!parser.parseModule(module),
                 "indented trailing-comma source should fail to parse")) {
        return false;
    }

    if (!require(!parser.errors().empty(),
                 "parser should report an error for the indented trailing-comma sample")) {
        return false;
    }

    const auto& error = parser.errors().front();
    if (!require(error.line == 3 && error.column == 3,
                 "parser diagnostic should point at the indented continuation token column")) {
        return false;
    }

    std::cout << "[PASS] parser diagnostic spans\n";
    return true;
}

bool checkStraySemicolonDiagnostic() {
    constexpr std::string_view kSource =
        "var x i32 = 1;\n"
        "print(x)\n";

    AstParser parser(kSource);
    AstModule module;
    if (!require(!parser.parseModule(module),
                 "semicolon sample should fail to parse")) {
        return false;
    }

    if (!require(!parser.errors().empty(),
                 "semicolon sample should report a parser diagnostic")) {
        return false;
    }

    const auto& error = parser.errors().front();
    if (!require(error.code == "parse.unexpected_semicolon",
                 "semicolon sample should use a stable diagnostic code") ||
        !require(error.message ==
                     "Semicolons are only allowed inside 'for (...)' clauses.",
                 "semicolon sample should report the semicolon-specific message") ||
        !require(error.line == 1 && error.column == 14,
                 "semicolon sample should point at the semicolon token")) {
        return false;
    }

    std::cout << "[PASS] semicolon parser diagnostic\n";
    return true;
}

bool checkSemicolonBeforeInitializerDiagnostic() {
    constexpr std::string_view kSource =
        "const state GameState ;= GameState()\n";

    AstParser parser(kSource);
    AstModule module;
    if (!require(!parser.parseModule(module),
                 "semicolon-before-initializer sample should fail to parse")) {
        return false;
    }

    if (!require(!parser.errors().empty(),
                 "semicolon-before-initializer sample should report a parser diagnostic")) {
        return false;
    }

    const auto& error = parser.errors().front();
    if (!require(error.code == "parse.unexpected_semicolon",
                 "semicolon-before-initializer sample should use the semicolon diagnostic code") ||
        !require(error.message ==
                     "Semicolons are only allowed inside 'for (...)' clauses.",
                 "semicolon-before-initializer sample should report the semicolon message") ||
        !require(error.line == 1 && error.column == 23,
                 "semicolon-before-initializer sample should point at the stray semicolon")) {
        return false;
    }

    std::cout << "[PASS] semicolon before initializer diagnostic\n";
    return true;
}

bool checkLexerDiagnosticPropagation() {
    constexpr std::string_view kSource = "const value i32 = 1$\n";

    AstParser parser(kSource);
    AstModule module;
    if (!require(!parser.parseModule(module),
                 "invalid-token sample should fail to parse")) {
        return false;
    }

    if (!require(!parser.errors().empty(),
                 "invalid-token sample should report a lexer diagnostic")) {
        return false;
    }

    const auto& error = parser.errors().front();
    if (!require(error.code == "lex.invalid_token",
                 "invalid-token sample should preserve the lexer diagnostic code") ||
        !require(error.message == "Unexpected Token.",
                 "invalid-token sample should preserve the scanner message")) {
        return false;
    }

    std::cout << "[PASS] lexer diagnostic propagation\n";
    return true;
}

bool checkBlockCommentParsing() {
    constexpr std::string_view kSource =
        "/* top-level block comment */\n"
        "const first i32 = /* inline block comment */ 1\n"
        "fn answer() i32 {\n"
        "    /* multiline block comment\n"
        "       // line comment text inside block comment */\n"
        "    return first\n"
        "}\n"
        "print(answer())\n";

    AstFrontendResult frontend;
    std::vector<TypeError> errors;
    const AstFrontendOptions options;
    const AstFrontendBuildStatus status = buildAstFrontend(
        kSource, options, errors, frontend);
    if (!require(status == AstFrontendBuildStatus::Success,
                 "block comment sample should build successfully") ||
        !require(errors.empty(),
                 "block comment sample should not emit diagnostics") ||
        !require(frontend.module.items.size() == 3,
                 "block comment sample should preserve the non-comment items")) {
        return false;
    }

    std::cout << "[PASS] block comment parsing\n";
    return true;
}

bool checkUnterminatedBlockCommentDiagnostic() {
    constexpr std::string_view kSource =
        "const value i32 = 1\n"
        "/* unterminated block comment\n";

    AstParser parser(kSource);
    AstModule module;
    if (!require(!parser.parseModule(module),
                 "unterminated block comment sample should fail to parse")) {
        return false;
    }

    if (!require(!parser.errors().empty(),
                 "unterminated block comment sample should report a lexer diagnostic")) {
        return false;
    }

    const auto& error = parser.errors().front();
    if (!require(error.code == "lex.unterminated_block_comment",
                 "unterminated block comment sample should use the block comment diagnostic code") ||
        !require(error.message == "Unterminated block comment.",
                 "unterminated block comment sample should preserve the scanner message") ||
        !require(error.line == 2 && error.column == 1,
                 "unterminated block comment sample should point at the comment start")) {
        return false;
    }

    std::cout << "[PASS] unterminated block comment diagnostic\n";
    return true;
}

bool checkSemanticDiagnosticSpans() {
    constexpr std::string_view kSource = "    var age i32 = \"str\"\n";

    AstFrontendResult frontend;
    std::vector<TypeError> errors;
    const AstFrontendOptions options;
    const AstFrontendBuildStatus status = buildAstFrontend(
        kSource, options, errors, frontend);
    if (!require(status == AstFrontendBuildStatus::SemanticError,
                 "indented semantic sample should fail semantic analysis")) {
        return false;
    }

    if (!require(errors.size() == 1,
                 "semantic diagnostic span sample should report one error")) {
        return false;
    }

    if (!require(errors.front().line == 1 && errors.front().column == 9,
                 "semantic diagnostic should point at the variable identifier column")) {
        return false;
    }

    std::cout << "[PASS] semantic diagnostic spans\n";
    return true;
}

bool checkCallableDiagnosticSpans() {
    {
        constexpr std::string_view kSource =
            "var bad fn(i32) i32 = fn(x i32, y i32) i32 {\n"
            "  return x + y\n"
            "}\n";

        AstFrontendResult frontend;
        std::vector<TypeError> errors;
        const AstFrontendOptions options;
        const AstFrontendBuildStatus status = buildAstFrontend(
            kSource, options, errors, frontend);
        if (!require(status == AstFrontendBuildStatus::SemanticError,
                     "closure mismatch sample should fail semantic analysis") ||
            !require(!errors.empty(),
                     "closure mismatch sample should report semantic errors") ||
            !require(errors.front().line == 1 && errors.front().column == 23,
                     "closure mismatch diagnostic should point at the closure literal")) {
            return false;
        }
    }

    {
        constexpr std::string_view kSource =
            "var addOne fn(i32) i32 = fn(x) => x + 1\n";

        AstFrontendResult frontend;
        std::vector<TypeError> errors;
        const AstFrontendOptions options;
        const AstFrontendBuildStatus status = buildAstFrontend(
            kSource, options, errors, frontend);
        if (!require(status == AstFrontendBuildStatus::SemanticError,
                     "lambda parameter sample should fail semantic analysis") ||
            !require(errors.size() == 1,
                     "lambda parameter sample should report one error") ||
            !require(errors.front().line == 1 && errors.front().column == 29,
                     "lambda parameter diagnostic should point at the omitted parameter")) {
            return false;
        }
    }

    {
        constexpr std::string_view kSource =
            "var bad fn(i32) i32 = fn(x i32) => {\n"
            "  return x + 1\n"
            "}\n";

        AstFrontendResult frontend;
        std::vector<TypeError> errors;
        const AstFrontendOptions options;
        const AstFrontendBuildStatus status = buildAstFrontend(
            kSource, options, errors, frontend);
        if (!require(status == AstFrontendBuildStatus::SemanticError,
                     "lambda block-body sample should fail semantic analysis") ||
            !require(errors.size() == 1,
                     "lambda block-body sample should report one error") ||
            !require(errors.front().line == 1 && errors.front().column == 36,
                     "lambda block-body diagnostic should point at the block body")) {
            return false;
        }
    }

    std::cout << "[PASS] callable diagnostic spans\n";
    return true;
}

bool checkImportedModuleRegression(const std::filesystem::path& repoRoot) {
    if (!checkCanonicalLoweringStable(
            repoRoot / "tests/sample_import_frontend_identity.mog",
            "optimized importer sample")) {
        return false;
    }

    if (!checkCanonicalLoweringStable(
            repoRoot / "tests/sample_import_frontend_nested_strict.mog",
            "nested importer sample")) {
        return false;
    }

    std::cout << "[PASS] imported frontend regression\n";
    return true;
}

bool checkImportedClassTypeFrontendRegression(
    const std::filesystem::path& repoRoot) {
    const std::filesystem::path sourcePath =
        repoRoot / "tests/sample_import_class_typed.mog";
    const std::string source = readFile(sourcePath);

    AstFrontendModuleGraphCache moduleGraphCache;
    AstFrontendOptions options;
    options.sourcePath = sourcePath.string();
    options.packageSearchPaths = {(repoRoot / "build/packages").string()};
    options.moduleGraphCache = &moduleGraphCache;

    AstFrontendResult frontend;
    std::vector<TypeError> errors;
    const AstFrontendBuildStatus status =
        buildAstFrontend(source, options, errors, frontend);
    if (!require(status == AstFrontendBuildStatus::Success,
                 "imported class type sample should build through the AST frontend")) {
        return false;
    }

    if (!require(frontend.typeAliases.find("ImportedCounter") !=
                     frontend.typeAliases.end() &&
                     frontend.typeAliases.at("ImportedCounter") &&
                     frontend.typeAliases.at("ImportedCounter")->toString() ==
                         "Counter",
                 "imported class type sample should register the aliased class name in type context")) {
        return false;
    }

    if (!require(frontend.hirModule != nullptr,
                 "imported class type sample should lower to HIR")) {
        return false;
    }

    if (!checkCanonicalLoweringStable(sourcePath, "imported class typed sample")) {
        return false;
    }

    const std::filesystem::path windowPackageSo =
        repoRoot / "build/packages/mog/window/package.so";
    const std::filesystem::path windowPackageDylib =
        repoRoot / "build/packages/mog/window/package.dylib";
    if (!std::filesystem::exists(windowPackageSo) &&
        !std::filesystem::exists(windowPackageDylib)) {
        std::cout << "[PASS] imported class type frontend regression\n";
        return true;
    }

    const std::filesystem::path flappyPath =
        repoRoot / "examples/game/flappy_bird.mog";
    const std::string flappySource = readFile(flappyPath);

    AstFrontendOptions flappyOptions;
    flappyOptions.sourcePath = flappyPath.string();
    flappyOptions.packageSearchPaths = {(repoRoot / "build/packages").string()};
    flappyOptions.moduleGraphCache = &moduleGraphCache;

    AstFrontendResult flappyFrontend;
    errors.clear();
    const AstFrontendBuildStatus flappyStatus =
        buildAstFrontend(flappySource, flappyOptions, errors, flappyFrontend);
    if (!require(flappyStatus == AstFrontendBuildStatus::Success,
                 "flappy example should build through the AST frontend after imported class typing support")) {
        return false;
    }

    std::cout << "[PASS] imported class type frontend regression\n";
    return true;
}

bool checkTypedImportFrontendRegression(const std::filesystem::path& repoRoot) {
    const std::filesystem::path sourcePath =
        repoRoot / "tests/sample_import_frontend_typed.mog";
    const std::string source = readFile(sourcePath);

    AstFrontendModuleGraphCache moduleGraphCache;
    AstFrontendOptions options;
    options.sourcePath = sourcePath.string();
    options.packageSearchPaths = {(repoRoot / "build/packages").string()};
    options.moduleGraphCache = &moduleGraphCache;

    AstFrontendResult frontend;
    std::vector<TypeError> errors;
    const AstFrontendBuildStatus status =
        buildAstFrontend(source, options, errors,
                         frontend);
    if (!require(status == AstFrontendBuildStatus::Success,
                 "typed import sample should build through the AST frontend")) {
        return false;
    }

    AstDestructuredImportStmt* importStmt =
        topLevelDestructuredImport(frontend.module);
    if (!require(importStmt != nullptr,
                 "typed import sample should contain a destructured import")) {
        return false;
    }
    if (!require(importStmt->bindings.size() == 2,
                 "typed import sample should contain two bindings")) {
        return false;
    }

    const TypeRef addType =
        frontend.semanticModel.nodeTypes.at(importStmt->bindings[0].node.id);
    const TypeRef piType =
        frontend.semanticModel.nodeTypes.at(importStmt->bindings[1].node.id);
    if (!require(addType && addType->toString() == "function(i32, i32) -> i32",
                 "typed import function binding should keep the declared type")) {
        return false;
    }
    if (!require(piType && piType->toString() == "f64",
                 "typed import constant binding should keep the declared type")) {
        return false;
    }
    if (!require(frontend.semanticModel.importedModules.count(
                     importStmt->initializer->node.id) == 1,
                 "typed import initializer should keep resolved import metadata")) {
        return false;
    }
    if (!require(frontend.bindings.importedModules.count(
                     importStmt->initializer->node.id) == 1,
                 "typed import initializer should keep final binding import metadata")) {
        return false;
    }
    if (!require(frontend.hirModule != nullptr,
                 "typed import sample should lower to HIR")) {
        return false;
    }

    HirDestructuredImportStmt* hirImportStmt =
        topLevelHirDestructuredImport(*frontend.hirModule);
    if (!require(hirImportStmt != nullptr,
                 "typed import sample should contain a HIR destructured import")) {
        return false;
    }
    if (!require(hirImportStmt->bindings.size() == 2,
                 "typed import sample should contain two HIR bindings")) {
        return false;
    }

    HirExpr& hirImportInitializer =
        frontend.hirModule->expr(*hirImportStmt->initializer);
    const auto* hirImportExpr =
        std::get_if<HirImportExpr>(&hirImportInitializer.value);
    if (!require(hirImportExpr != nullptr,
                 "typed import HIR initializer should stay an import expression")) {
        return false;
    }
    if (!require(!hirImportExpr->importedModule.importTarget.canonicalId.empty(),
                 "typed import HIR initializer should keep canonical import metadata")) {
        return false;
    }
    if (!require(hirImportStmt->bindings[0].bindingType &&
                     hirImportStmt->bindings[0].bindingType->toString() ==
                         "function(i32, i32) -> i32",
                 "typed import HIR function binding should keep the declared type")) {
        return false;
    }
    if (!require(hirImportStmt->bindings[1].bindingType &&
                     hirImportStmt->bindings[1].bindingType->toString() == "f64",
                 "typed import HIR constant binding should keep the declared type")) {
        return false;
    }
    if (!require(frontend.semanticModel.exportedSymbolTypes.empty(),
                 "typed import sample should not synthesize public exports")) {
        return false;
    }

    if (!checkCanonicalLoweringStable(sourcePath, "typed importer sample")) {
        return false;
    }

    std::cout << "[PASS] typed import frontend regression\n";
    return true;
}

bool checkNativeHandleTypeFrontendRegression(
    const std::filesystem::path& repoRoot) {
    const std::filesystem::path sourcePath =
        repoRoot / "tests/sample_import_native_handle_frontend_typed.mog";
    const std::string source = readFile(sourcePath);

    AstFrontendModuleGraphCache moduleGraphCache;
    AstFrontendOptions options;
    options.sourcePath = sourcePath.string();
    options.packageSearchPaths = {(repoRoot / "build/packages").string()};
    options.moduleGraphCache = &moduleGraphCache;

    AstFrontendResult frontend;
    std::vector<TypeError> errors;
    const AstFrontendBuildStatus status =
        buildAstFrontend(source, options, errors,
                         frontend);
    if (!require(status == AstFrontendBuildStatus::Success,
                 "native handle typed import sample should build through the AST frontend")) {
        return false;
    }

    AstDestructuredImportStmt* importStmt =
        topLevelDestructuredImport(frontend.module);
    if (!require(importStmt != nullptr,
                 "native handle typed import sample should contain a destructured import")) {
        return false;
    }
    if (!require(importStmt->bindings.size() == 3,
                 "native handle typed import sample should contain three bindings")) {
        return false;
    }

    const TypeRef createType =
        frontend.semanticModel.nodeTypes.at(importStmt->bindings[0].node.id);
    const TypeRef readType =
        frontend.semanticModel.nodeTypes.at(importStmt->bindings[1].node.id);
    const TypeRef addType =
        frontend.semanticModel.nodeTypes.at(importStmt->bindings[2].node.id);
    if (!require(createType &&
                     createType->toString() ==
                         "function(i64) -> handle<examples:counter:CounterHandle>",
                 "native handle create binding should keep the expected handle return type")) {
        return false;
    }
    if (!require(readType &&
                     readType->toString() ==
                         "function(handle<examples:counter:CounterHandle>) -> i64",
                 "native handle read binding should keep the expected handle parameter type")) {
        return false;
    }
    if (!require(addType &&
                     addType->toString() ==
                         "function(handle<examples:counter:CounterHandle>, i64) -> i64",
                 "native handle add binding should keep the expected handle parameter type")) {
        return false;
    }

    auto exportedForward =
        frontend.semanticModel.exportedSymbolTypes.find("Forward");
    if (!require(exportedForward != frontend.semanticModel.exportedSymbolTypes.end() &&
                     exportedForward->second &&
                     exportedForward->second->toString() ==
                         "function(handle<examples:counter:CounterHandle>) -> handle<examples:counter:CounterHandle>",
                 "native handle function export should preserve aliased handle parameter and return types")) {
        return false;
    }

    if (!checkCanonicalLoweringStable(sourcePath,
                                      "native handle typed importer sample")) {
        return false;
    }

    std::cout << "[PASS] native handle frontend regression\n";
    return true;
}

bool checkTypedImportDiagnosticRegression(const std::filesystem::path& repoRoot) {
    const auto expectStrictError = [&](const std::filesystem::path& path,
                                       size_t line, size_t column,
                                       const std::string& needle,
                                       const std::string& description) {
        const std::string source = readFile(path);
        AstFrontendModuleGraphCache moduleGraphCache;
        AstFrontendOptions options;
        options.sourcePath = path.string();
        options.packageSearchPaths = {(repoRoot / "build/packages").string()};
        options.moduleGraphCache = &moduleGraphCache;

        AstFrontendResult frontend;
        std::vector<TypeError> errors;
        const AstFrontendBuildStatus status = buildAstFrontend(
            source, options, errors, frontend);
        return require(status == AstFrontendBuildStatus::SemanticError,
                       description + " should fail semantic analysis") &&
               require(!errors.empty(),
                       description + " should report a semantic error") &&
               require(errors.front().line == line &&
                           errors.front().column == column,
                       description + " should report the expected location") &&
               require(errors.front().message.find(needle) != std::string::npos,
                       description + " should report the expected message");
    };

    if (!expectStrictError(repoRoot /
                               "tests/types/errors/import_binding_type_mismatch.mog",
                           1, 9, "cannot assign imported value",
                           "typed import mismatch sample")) {
        return false;
    }

    if (!expectStrictError(repoRoot / "tests/types/errors/import_missing_export.mog",
                           1, 9, "has no export 'Missing'",
                           "missing export sample")) {
        return false;
    }

    if (!expectStrictError(
            repoRoot / "tests/types/errors/import_native_binding_type_mismatch.mog",
            1, 9, "function(i64, i64) -> i64",
            "native typed import mismatch sample")) {
        return false;
    }

    if (!expectStrictError(
            repoRoot /
                "tests/types/errors/import_native_handle_binding_type_mismatch.mog",
            1, 9, "function(i64) -> handle<examples:counter:CounterHandle>",
            "native handle typed import mismatch sample")) {
        return false;
    }

    if (!expectStrictError(repoRoot /
                               "tests/types/errors/import_cycle_frontend.mog",
                           1, 21, "Circular import detected",
                           "import cycle sample")) {
        return false;
    }

    std::cout << "[PASS] typed import diagnostic regression\n";
    return true;
}

bool checkStructuredDiagnosticRegression(const std::filesystem::path& repoRoot) {
    const std::filesystem::path path =
        repoRoot / "tests/types/errors/import_cycle_frontend.mog";
    const std::string source = readFile(path);
    AstFrontendModuleGraphCache moduleGraphCache;
    AstFrontendOptions options;
    options.sourcePath = path.string();
    options.moduleGraphCache = &moduleGraphCache;

    AstFrontendResult frontend;
    std::vector<TypeError> errors;
    const AstFrontendBuildStatus status = buildAstFrontend(
        source, options, errors, frontend);
    const bool sawRootImportSpecifier =
        !errors.empty() &&
        std::any_of(errors.front().importTrace.begin(),
                    errors.front().importTrace.end(),
                    [](const FrontendImportTraceFrame& frame) {
                        return frame.rawSpecifier == "../../modules/cycle_a.mog";
                    });
    if (!require(status == AstFrontendBuildStatus::SemanticError,
                 "structured diagnostic sample should fail semantic analysis") ||
        !require(!errors.empty(),
                 "structured diagnostic sample should report errors") ||
        !require(errors.front().code == "import.cycle",
                 "structured diagnostic sample should keep the import cycle code") ||
        !require(errors.front().line == 1 && errors.front().column == 21,
                 "structured diagnostic sample should keep the import-site span") ||
        !require(!errors.front().importTrace.empty(),
                 "structured diagnostic sample should carry an import trace") ||
        !require(sawRootImportSpecifier,
                 "structured diagnostic sample should report the raw import specifier")) {
        return false;
    }

    std::cout << "[PASS] structured diagnostic regression\n";
    return true;
}

bool checkModuleGraphCacheRegression() {
    const std::filesystem::path tempRoot =
        std::filesystem::temp_directory_path() / "mog_frontend_cache_regression";
    std::error_code ec;
    std::filesystem::create_directories(tempRoot, ec);
    if (!require(!ec, "module graph cache regression should create its temp directory")) {
        return false;
    }

    const std::filesystem::path depPath = tempRoot / "dep.mog";
    const std::filesystem::path importerPath = tempRoot / "main.mog";
    if (!require(writeFile(depPath, "const Value i32 = 1i32\n"),
                 "module graph cache regression should write the dependency sample") ||
        !require(writeFile(importerPath,
                           "const { Value: i32 } = @import(\"./dep.mog\")\nprint(Value)\n"),
                 "module graph cache regression should write the importer sample")) {
        return false;
    }

    AstFrontendModuleGraphCache moduleGraphCache;
    AstFrontendOptions options;
    options.sourcePath = importerPath.string();
    options.moduleGraphCache = &moduleGraphCache;

    const std::string importerSource = readFile(importerPath);
    AstFrontendResult firstFrontend;
    std::vector<TypeError> errors;
    const AstFrontendBuildStatus firstStatus =
        buildAstFrontend(importerSource, options,
                         errors, firstFrontend);
    if (!require(firstStatus == AstFrontendBuildStatus::Success,
                 "module graph cache regression first build should succeed") ||
        !require(firstFrontend.timings.moduleCacheMisses >= 1,
                 "module graph cache regression first build should miss the cache")) {
        return false;
    }

    AstFrontendResult secondFrontend;
    errors.clear();
    const AstFrontendBuildStatus secondStatus =
        buildAstFrontend(importerSource, options,
                         errors, secondFrontend);
    if (!require(secondStatus == AstFrontendBuildStatus::Success,
                 "module graph cache regression second build should succeed") ||
        !require(secondFrontend.timings.moduleCacheHits >= 1,
                 "module graph cache regression second build should reuse the cache")) {
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (!require(writeFile(depPath, "const Value str = \"oops\"\n"),
                 "module graph cache regression should rewrite the dependency sample")) {
        return false;
    }

    AstFrontendResult thirdFrontend;
    errors.clear();
    const AstFrontendBuildStatus thirdStatus =
        buildAstFrontend(importerSource, options,
                         errors, thirdFrontend);
    if (!require(thirdStatus == AstFrontendBuildStatus::SemanticError,
                 "module graph cache regression third build should fail after invalidation") ||
        !require(thirdFrontend.timings.moduleCacheRebuilds >= 1,
                 "module graph cache regression third build should rebuild stale modules") ||
        !require(!errors.empty(),
                 "module graph cache regression third build should report errors") ||
        !require(errors.front().message.find("cannot assign imported value") !=
                     std::string::npos,
                 "module graph cache regression third build should report the updated type mismatch")) {
        return false;
    }

    std::cout << "[PASS] module graph cache regression\n";
    return true;
}

bool checkNewlineOptimizationRegression(const std::filesystem::path& repoRoot) {
    if (!checkCanonicalLoweringStable(
            repoRoot / "tests/newline/sample_newline_call_suffix.mog",
            "newline call-suffix sample")) {
        return false;
    }

    if (!checkCanonicalLoweringStable(
            repoRoot / "tests/newline/sample_newline_member_suffix.mog",
            "newline member-suffix sample")) {
        return false;
    }

    if (!checkCanonicalLoweringStable(
            repoRoot / "tests/newline/sample_newline_index_suffix.mog",
            "newline index-suffix sample")) {
        return false;
    }

    if (!checkCanonicalLoweringStable(
            repoRoot / "tests/newline/sample_newline_operator_rhs.mog",
            "newline operator sample")) {
        return false;
    }

    if (!checkCanonicalLoweringStable(
            repoRoot / "tests/newline/sample_newline_call_suffix_folded_arg.mog",
            "newline folded call sample")) {
        return false;
    }

    if (!checkStrictHirHasNoAdd(
            repoRoot / "tests/newline/sample_newline_operator_rhs.mog",
            "default newline operator sample")) {
        return false;
    }

    if (!checkStrictHirHasNoAdd(
            repoRoot / "tests/newline/sample_newline_call_suffix_folded_arg.mog",
            "default newline folded call sample")) {
        return false;
    }

    std::cout << "[PASS] newline-sensitive optimization regression\n";
    return true;
}

bool checkHirDeadForRewriteRegression() {
    HirModule module;

    HirStmt initializerStmt;
    initializerStmt.node.id = 1;
    initializerStmt.node.type = TypeInfo::makeAny();
    HirVarDeclStmt initializerDecl;
    initializerDecl.node.id = 2;
    initializerDecl.node.type = TypeInfo::makeAny();
    initializerDecl.name =
        Token::synthetic(TokenType::IDENTIFIER, "value", SourceSpan{});
    initializerDecl.declaredType = TypeInfo::makeI32();
    initializerDecl.omittedType = false;
    initializerStmt.value = std::move(initializerDecl);
    const HirStmtId initializerId = module.addStmt(std::move(initializerStmt));

    HirExpr conditionExpr;
    conditionExpr.node.id = 3;
    conditionExpr.node.type = TypeInfo::makeBool();
    conditionExpr.value = HirLiteralExpr{
        Token::synthetic(TokenType::FALSE, "false", SourceSpan{})};
    const HirExprId conditionId = module.addExpr(std::move(conditionExpr));

    HirStmt bodyStmt;
    bodyStmt.node.id = 4;
    bodyStmt.node.type = TypeInfo::makeAny();
    bodyStmt.value = HirBlockStmt{};
    const HirStmtId bodyId = module.addStmt(std::move(bodyStmt));

    HirStmt forStmt;
    forStmt.node.id = 5;
    forStmt.node.type = TypeInfo::makeAny();
    HirForStmt forLoop;
    forLoop.initializer = initializerId;
    forLoop.condition = conditionId;
    forLoop.body = bodyId;
    forStmt.value = std::move(forLoop);
    const HirStmtId forStmtId = module.addStmt(std::move(forStmt));

    HirItem topLevelItem;
    topLevelItem.value = forStmtId;
    module.items.push_back(module.addItem(std::move(topLevelItem)));

    optimizeHir(module);

    auto* block = std::get_if<HirBlockStmt>(&module.stmt(forStmtId).value);
    if (!require(block != nullptr,
                 "HIR dead-for rewrite should replace the loop with a block")) {
        return false;
    }

    if (!require(block->items.size() == 1,
                 "HIR dead-for rewrite should keep exactly one initializer item")) {
        return false;
    }

    auto* itemStmtId = std::get_if<HirStmtId>(&module.item(block->items.front()).value);
    if (!require(itemStmtId != nullptr && *itemStmtId == initializerId,
                 "HIR dead-for rewrite should preserve the original initializer statement")) {
        return false;
    }

    if (!require(module.stmt(forStmtId).node.id == 5,
                 "HIR dead-for rewrite should preserve the original statement node")) {
        return false;
    }

    std::cout << "[PASS] HIR dead-for rewrite regression\n";
    return true;
}

}  // namespace

int main() {
    const std::filesystem::path repoRoot = std::filesystem::current_path();

    if (!checkSemanticRefreshContract()) {
        return 1;
    }
    if (!checkBooleanIdentityRefreshContract()) {
        return 1;
    }
    if (!checkSyntheticNotSpanRegression()) {
        return 1;
    }
    if (!checkIntegerDoubleTildeRefreshContract()) {
        return 1;
    }
    if (!checkIntegerMultiplyZeroRefreshContract()) {
        return 1;
    }
    if (!checkParserDiagnosticSpans()) {
        return 1;
    }
    if (!checkStraySemicolonDiagnostic()) {
        return 1;
    }
    if (!checkSemicolonBeforeInitializerDiagnostic()) {
        return 1;
    }
    if (!checkLexerDiagnosticPropagation()) {
        return 1;
    }
    if (!checkBlockCommentParsing()) {
        return 1;
    }
    if (!checkUnterminatedBlockCommentDiagnostic()) {
        return 1;
    }
    if (!checkSemanticDiagnosticSpans()) {
        return 1;
    }
    if (!checkCallableDiagnosticSpans()) {
        return 1;
    }
    if (!checkImportedModuleRegression(repoRoot)) {
        return 1;
    }
    if (!checkImportedClassTypeFrontendRegression(repoRoot)) {
        return 1;
    }
    if (!checkTypedImportFrontendRegression(repoRoot)) {
        return 1;
    }
    if (!checkNativeHandleTypeFrontendRegression(repoRoot)) {
        return 1;
    }
    if (!checkTypedImportDiagnosticRegression(repoRoot)) {
        return 1;
    }
    if (!checkStructuredDiagnosticRegression(repoRoot)) {
        return 1;
    }
    if (!checkModuleGraphCacheRegression()) {
        return 1;
    }
    if (!checkNewlineOptimizationRegression(repoRoot)) {
        return 1;
    }
    if (!checkHirDeadForRewriteRegression()) {
        return 1;
    }

    std::cout << "[PASS] AST frontend regression suite\n";
    return 0;
}
