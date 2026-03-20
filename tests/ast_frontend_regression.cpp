#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Ast.hpp"
#include "AstFrontend.hpp"
#include "AstOptimizer.hpp"
#include "AstParser.hpp"
#include "AstSemanticAnalyzer.hpp"
#include "AstSymbolCollector.hpp"
#include "Chunk.hpp"
#include "Compiler.hpp"
#include "GC.hpp"

namespace {

struct SemanticPipelineResult {
    AstModule module;
    std::unordered_set<std::string> classNames;
    std::unordered_map<std::string, TypeRef> typeAliases;
    std::unordered_map<std::string, TypeRef> functionSignatures;
    AstNodeId preIdentityExprId = 0;
    AstNodeId preFoldedExprId = 0;
    size_t preFoldedExprLine = 0;
    AstSemanticModel preOptimization;
    AstSemanticModel refreshed;
};

bool hasStrictDirective(std::string_view source) {
    return source.rfind("#!strict", 0) == 0;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        return {};
    }

    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
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

bool buildSemanticPipeline(std::string_view source,
                           SemanticPipelineResult& outResult,
                           std::string& outError) {
    outError.clear();
    AstParser parser(source);
    if (!parser.parseModule(outResult.module)) {
        outError = "failed to parse inline regression source";
        return false;
    }

    collectSymbolsFromAst(outResult.module, outResult.classNames,
                          outResult.functionSignatures, &outResult.typeAliases);

    AstExpr* preIdentityExpr = topLevelPrintExpr(outResult.module, 0);
    AstExpr* preFoldedExpr = topLevelPrintExpr(outResult.module, 1);
    if (preIdentityExpr == nullptr || preFoldedExpr == nullptr) {
        outError = "failed to locate inline regression print expressions";
        return false;
    }

    outResult.preIdentityExprId = preIdentityExpr->node.id;
    outResult.preFoldedExprId = preFoldedExpr->node.id;
    outResult.preFoldedExprLine = preFoldedExpr->node.line;

    std::vector<TypeError> errors;
    analyzeAstSemantics(outResult.module, outResult.classNames,
                        outResult.typeAliases, outResult.functionSignatures,
                        {}, errors, &outResult.preOptimization);
    if (!errors.empty()) {
        outError = errors.front().message;
        return false;
    }

    optimizeAst(outResult.module, outResult.preOptimization);

    errors.clear();
    analyzeAstSemantics(outResult.module, outResult.classNames,
                        outResult.typeAliases, outResult.functionSignatures,
                        {}, errors, &outResult.refreshed);
    if (!errors.empty()) {
        outError = errors.front().message;
        return false;
    }

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
    compiler.setStrictMode(hasStrictDirective(source));

    if (!compiler.compile(source, chunk, path.string())) {
        return false;
    }

    outDisassembly = captureChunkDisassembly(chunk);
    return true;
}

bool compileSourceCanonical(std::string_view source, bool strictMode,
                            CompilerEmitterMode emitterMode, GC& gc,
                            Chunk& chunk, std::string& outDisassembly) {
    Compiler compiler;
    compiler.setGC(&gc);
    compiler.setPackageSearchPaths(
        {std::filesystem::current_path().append("build/packages").string()});
    compiler.setEmitterMode(emitterMode);
    compiler.setStrictMode(strictMode);
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

    GC forcedGc;
    Chunk forcedChunk;
    std::string forcedDisassembly;
    if (!require(compileFileCanonical(path, CompilerEmitterMode::ForceAst,
                                      forcedGc, forcedChunk, forcedDisassembly),
                 description + " should compile in forced AST mode")) {
        return false;
    }

    return require(autoDisassembly == forcedDisassembly,
                   description +
                       " should lower identically in auto and forced AST modes");
}

bool checkStrictForcedAstHasNoAdd(const std::filesystem::path& path,
                                  const std::string& description) {
    const std::string source = readFile(path);
    if (!require(!source.empty(), description + " should be readable")) {
        return false;
    }

    GC strictGc;
    Chunk strictChunk;
    std::string strictDisassembly;
    if (!require(compileSourceCanonical(source, true, CompilerEmitterMode::ForceAst,
                                        strictGc, strictChunk, strictDisassembly),
                 description + " should compile in strict forced AST mode")) {
        return false;
    }

    return require(strictDisassembly.find("IADD") == std::string::npos &&
                       strictDisassembly.find("ADD") == std::string::npos,
                   description + " should not emit addition opcodes");
}

bool checkSemanticRefreshContract() {
    constexpr std::string_view kSource =
        "var value i32 = 41\n"
        "print(value + 0i32)\n"
        "print(6i32 * 7i32)\n";

    SemanticPipelineResult pipeline;
    std::string error;
    if (!buildSemanticPipeline(kSource, pipeline, error)) {
        std::cerr << "[FAIL] semantic refresh setup failed: " << error << '\n';
        return false;
    }

    AstExpr* identityExpr = topLevelPrintExpr(pipeline.module, 0);
    AstExpr* foldedExpr = topLevelPrintExpr(pipeline.module, 1);
    if (!require(identityExpr != nullptr,
                 "missing first print expression after optimization") ||
        !require(foldedExpr != nullptr,
                 "missing second print expression after optimization")) {
        return false;
    }

    const AstNodeId identityId = identityExpr->node.id;
    const AstNodeId foldedId = foldedExpr->node.id;
    const size_t foldedLine = foldedExpr->node.line;

    if (!require(identityId == pipeline.preIdentityExprId &&
                     foldedId == pipeline.preFoldedExprId,
                 "optimized expressions should preserve their original node ids")) {
        return false;
    }

    if (!require(std::holds_alternative<AstIdentifierExpr>(identityExpr->value),
                 "identity rewrite should preserve the result node and collapse to an identifier")) {
        return false;
    }

    const auto* literal = std::get_if<AstLiteralExpr>(&foldedExpr->value);
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

    auto identityType = pipeline.refreshed.nodeTypes.find(identityId);
    auto foldedType = pipeline.refreshed.nodeTypes.find(foldedId);
    if (!require(identityType != pipeline.refreshed.nodeTypes.end() &&
                     identityType->second && identityType->second->kind == TypeKind::I32,
                 "refreshed semantics should keep the preserved identity node typed as i32") ||
        !require(foldedType != pipeline.refreshed.nodeTypes.end() &&
                     foldedType->second && foldedType->second->kind == TypeKind::I32,
                 "refreshed semantics should type the folded literal node as i32")) {
        return false;
    }

    GC gc;
    Chunk chunk;
    Compiler compiler;
    compiler.setGC(&gc);
    compiler.setStrictMode(true);
    if (!require(compiler.compile(kSource, chunk, "<frontend-regression>"),
                 "compiler should lower successfully after semantic refresh")) {
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

    std::cout << "[PASS] semantic refresh contract\n";
    return true;
}

bool checkBooleanIdentityRefreshContract() {
    constexpr std::string_view kSource =
        "var yes bool = true\n"
        "print(yes == true)\n"
        "print(false != yes)\n";

    SemanticPipelineResult pipeline;
    std::string error;
    if (!buildSemanticPipeline(kSource, pipeline, error)) {
        std::cerr << "[FAIL] bool identity refresh setup failed: " << error
                  << '\n';
        return false;
    }

    AstExpr* firstExpr = topLevelPrintExpr(pipeline.module, 0);
    AstExpr* secondExpr = topLevelPrintExpr(pipeline.module, 1);
    if (!require(firstExpr != nullptr,
                 "missing first bool identity expression after optimization") ||
        !require(secondExpr != nullptr,
                 "missing second bool identity expression after optimization")) {
        return false;
    }

    if (!require(firstExpr->node.id == pipeline.preIdentityExprId &&
                     secondExpr->node.id == pipeline.preFoldedExprId,
                 "bool identity rewrites should preserve original node ids")) {
        return false;
    }

    if (!require(std::holds_alternative<AstIdentifierExpr>(firstExpr->value) &&
                     std::holds_alternative<AstIdentifierExpr>(secondExpr->value),
                 "bool identity rewrites should collapse to identifiers")) {
        return false;
    }

    auto firstType = pipeline.refreshed.nodeTypes.find(firstExpr->node.id);
    auto secondType = pipeline.refreshed.nodeTypes.find(secondExpr->node.id);
    if (!require(firstType != pipeline.refreshed.nodeTypes.end() &&
                     firstType->second &&
                     firstType->second->kind == TypeKind::BOOL,
                 "refreshed semantics should keep the first bool identity node typed as bool") ||
        !require(secondType != pipeline.refreshed.nodeTypes.end() &&
                     secondType->second &&
                     secondType->second->kind == TypeKind::BOOL,
                 "refreshed semantics should keep the second bool identity node typed as bool")) {
        return false;
    }

    GC gc;
    Chunk chunk;
    Compiler compiler;
    compiler.setGC(&gc);
    compiler.setStrictMode(true);
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

    std::cout << "[PASS] bool identity refresh contract\n";
    return true;
}

bool checkSyntheticNotSpanRegression() {
    constexpr std::string_view kSource =
        "var yes bool = true\n"
        "print(yes == false)\n"
        "print(true)\n";

    SemanticPipelineResult pipeline;
    std::string error;
    if (!buildSemanticPipeline(kSource, pipeline, error)) {
        std::cerr << "[FAIL] synthetic not span setup failed: " << error
                  << '\n';
        return false;
    }

    AstExpr* notExpr = topLevelPrintExpr(pipeline.module, 0);
    if (!require(notExpr != nullptr,
                 "missing synthesized not expression after optimization")) {
        return false;
    }

    if (!require(notExpr->node.id == pipeline.preIdentityExprId,
                 "synthesized not rewrite should preserve the original node id")) {
        return false;
    }

    const auto* unary = std::get_if<AstUnaryExpr>(&notExpr->value);
    if (!require(unary != nullptr && unary->operand != nullptr,
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

    auto notType = pipeline.refreshed.nodeTypes.find(notExpr->node.id);
    if (!require(notType != pipeline.refreshed.nodeTypes.end() &&
                     notType->second && notType->second->kind == TypeKind::BOOL,
                 "refreshed semantics should type the synthesized not node as bool")) {
        return false;
    }

    GC gc;
    Chunk chunk;
    Compiler compiler;
    compiler.setGC(&gc);
    compiler.setStrictMode(true);
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

    SemanticPipelineResult pipeline;
    std::string error;
    if (!buildSemanticPipeline(kSource, pipeline, error)) {
        std::cerr << "[FAIL] integer double-tilde setup failed: " << error
                  << '\n';
        return false;
    }

    AstExpr* firstExpr = topLevelPrintExpr(pipeline.module, 0);
    AstExpr* secondExpr = topLevelPrintExpr(pipeline.module, 1);
    if (!require(firstExpr != nullptr,
                 "missing first integer double-tilde expression after optimization") ||
        !require(secondExpr != nullptr,
                 "missing second integer double-tilde expression after optimization")) {
        return false;
    }

    if (!require(firstExpr->node.id == pipeline.preIdentityExprId &&
                     secondExpr->node.id == pipeline.preFoldedExprId,
                 "integer double-tilde rewrites should preserve original node ids")) {
        return false;
    }

    if (!require(std::holds_alternative<AstCallExpr>(firstExpr->value),
                 "integer double-tilde should collapse to the original call expression") ||
        !require(std::holds_alternative<AstLiteralExpr>(secondExpr->value),
                 "constant integer double-tilde should fold to a literal")) {
        return false;
    }

    auto firstType = pipeline.refreshed.nodeTypes.find(firstExpr->node.id);
    auto secondType = pipeline.refreshed.nodeTypes.find(secondExpr->node.id);
    if (!require(firstType != pipeline.refreshed.nodeTypes.end() &&
                     firstType->second &&
                     firstType->second->kind == TypeKind::I32,
                 "refreshed semantics should keep the first integer double-tilde node typed as i32") ||
        !require(secondType != pipeline.refreshed.nodeTypes.end() &&
                     secondType->second &&
                     secondType->second->kind == TypeKind::I32,
                 "refreshed semantics should keep the second integer double-tilde node typed as i32")) {
        return false;
    }

    GC gc;
    Chunk chunk;
    Compiler compiler;
    compiler.setGC(&gc);
    compiler.setStrictMode(true);
    if (!require(compiler.compile(kSource, chunk, "<frontend-int-double-tilde>"),
                 "compiler should lower successfully after integer double-tilde rewrites")) {
        return false;
    }

    const std::string disassembly = captureChunkDisassembly(chunk);
    if (!require(disassembly.find("BITWISE_NOT") == std::string::npos,
                 "lowered chunk should not emit bitwise not opcodes for integer double-tilde rewrites")) {
        return false;
    }

    std::cout << "[PASS] integer double-tilde refresh contract\n";
    return true;
}

bool checkIntegerMultiplyZeroRefreshContract() {
    constexpr std::string_view kSource =
        "var value i32 = 7i32\n"
        "print(value * 0i32)\n"
        "print(0i32 * value)\n";

    SemanticPipelineResult pipeline;
    std::string error;
    if (!buildSemanticPipeline(kSource, pipeline, error)) {
        std::cerr << "[FAIL] integer multiply-zero setup failed: " << error
                  << '\n';
        return false;
    }

    AstExpr* firstExpr = topLevelPrintExpr(pipeline.module, 0);
    AstExpr* secondExpr = topLevelPrintExpr(pipeline.module, 1);
    if (!require(firstExpr != nullptr,
                 "missing first integer multiply-zero expression after optimization") ||
        !require(secondExpr != nullptr,
                 "missing second integer multiply-zero expression after optimization")) {
        return false;
    }

    if (!require(firstExpr->node.id == pipeline.preIdentityExprId &&
                     secondExpr->node.id == pipeline.preFoldedExprId,
                 "integer multiply-zero rewrites should preserve original node ids")) {
        return false;
    }

    const auto* firstLiteral = std::get_if<AstLiteralExpr>(&firstExpr->value);
    const auto* secondLiteral = std::get_if<AstLiteralExpr>(&secondExpr->value);
    if (!require(firstLiteral != nullptr && secondLiteral != nullptr,
                 "integer multiply-zero rewrites should collapse to zero literals")) {
        return false;
    }

    auto firstType = pipeline.refreshed.nodeTypes.find(firstExpr->node.id);
    auto secondType = pipeline.refreshed.nodeTypes.find(secondExpr->node.id);
    if (!require(firstType != pipeline.refreshed.nodeTypes.end() &&
                     firstType->second &&
                     firstType->second->kind == TypeKind::I32,
                 "refreshed semantics should keep the first multiply-zero node typed as i32") ||
        !require(secondType != pipeline.refreshed.nodeTypes.end() &&
                     secondType->second &&
                     secondType->second->kind == TypeKind::I32,
                 "refreshed semantics should keep the second multiply-zero node typed as i32")) {
        return false;
    }

    GC gc;
    Chunk chunk;
    Compiler compiler;
    compiler.setGC(&gc);
    compiler.setStrictMode(true);
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

    std::cout << "[PASS] integer multiply-zero refresh contract\n";
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

bool checkSemanticDiagnosticSpans() {
    constexpr std::string_view kSource = "    var age i32 = \"str\"\n";

    AstFrontendResult frontend;
    std::vector<TypeError> errors;
    const AstFrontendOptions options;
    const AstFrontendBuildStatus status = buildAstFrontend(
        kSource, options, AstFrontendMode::StrictChecked, errors, frontend);
    if (!require(status == AstFrontendBuildStatus::SemanticError,
                 "indented strict semantic sample should fail semantic analysis")) {
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
            kSource, options, AstFrontendMode::StrictChecked, errors, frontend);
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
            kSource, options, AstFrontendMode::StrictChecked, errors, frontend);
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
            kSource, options, AstFrontendMode::StrictChecked, errors, frontend);
        if (!require(status == AstFrontendBuildStatus::SemanticError,
                     "lambda block-body sample should fail semantic analysis") ||
            !require(errors.size() == 1,
                     "lambda block-body sample should report one error") ||
            !require(errors.front().line == 1 && errors.front().column == 35,
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
            "nested strict importer sample")) {
        return false;
    }

    std::cout << "[PASS] imported frontend regression\n";
    return true;
}

bool checkTypedImportFrontendRegression(const std::filesystem::path& repoRoot) {
    const std::filesystem::path sourcePath =
        repoRoot / "tests/sample_import_frontend_typed.mog";
    const std::string source = readFile(sourcePath);

    AstFrontendImportCache importCache;
    AstFrontendOptions options;
    options.sourcePath = sourcePath.string();
    options.packageSearchPaths = {(repoRoot / "build/packages").string()};
    options.importCache = &importCache;

    AstFrontendResult frontend;
    std::vector<TypeError> errors;
    const AstFrontendBuildStatus status =
        buildAstFrontend(source, options, AstFrontendMode::StrictChecked, errors,
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

    AstFrontendImportCache importCache;
    AstFrontendOptions options;
    options.sourcePath = sourcePath.string();
    options.packageSearchPaths = {(repoRoot / "build/packages").string()};
    options.importCache = &importCache;

    AstFrontendResult frontend;
    std::vector<TypeError> errors;
    const AstFrontendBuildStatus status =
        buildAstFrontend(source, options, AstFrontendMode::StrictChecked, errors,
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
        AstFrontendImportCache importCache;
        AstFrontendOptions options;
        options.sourcePath = path.string();
        options.packageSearchPaths = {(repoRoot / "build/packages").string()};
        options.importCache = &importCache;

        AstFrontendResult frontend;
        std::vector<TypeError> errors;
        const AstFrontendBuildStatus status = buildAstFrontend(
            source, options, AstFrontendMode::StrictChecked, errors, frontend);
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
                           2, 9, "cannot assign imported value",
                           "typed import mismatch sample")) {
        return false;
    }

    if (!expectStrictError(repoRoot / "tests/types/errors/import_missing_export.mog",
                           2, 9, "has no export 'Missing'",
                           "missing export sample")) {
        return false;
    }

    if (!expectStrictError(
            repoRoot / "tests/types/errors/import_native_binding_type_mismatch.mog",
            2, 9, "function(i64, i64) -> i64",
            "native typed import mismatch sample")) {
        return false;
    }

    if (!expectStrictError(
            repoRoot /
                "tests/types/errors/import_native_handle_binding_type_mismatch.mog",
            2, 9, "function(i64) -> handle<examples:counter:CounterHandle>",
            "native handle typed import mismatch sample")) {
        return false;
    }

    if (!expectStrictError(repoRoot /
                               "tests/types/errors/import_cycle_frontend.mog",
                           2, 21, "Circular import detected",
                           "import cycle sample")) {
        return false;
    }

    std::cout << "[PASS] typed import diagnostic regression\n";
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

    if (!checkStrictForcedAstHasNoAdd(
            repoRoot / "tests/newline/sample_newline_operator_rhs.mog",
            "strict newline operator sample")) {
        return false;
    }

    if (!checkStrictForcedAstHasNoAdd(
            repoRoot / "tests/newline/sample_newline_call_suffix_folded_arg.mog",
            "strict newline folded call sample")) {
        return false;
    }

    std::cout << "[PASS] newline-sensitive optimization regression\n";
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
    if (!checkSemanticDiagnosticSpans()) {
        return 1;
    }
    if (!checkCallableDiagnosticSpans()) {
        return 1;
    }
    if (!checkImportedModuleRegression(repoRoot)) {
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
    if (!checkNewlineOptimizationRegression(repoRoot)) {
        return 1;
    }

    std::cout << "[PASS] AST frontend regression suite\n";
    return 0;
}
