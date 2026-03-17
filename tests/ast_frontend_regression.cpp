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

std::string stripStrictDirectiveLine(std::string_view source) {
    if (!hasStrictDirective(source)) {
        return std::string(source);
    }

    size_t newlinePos = source.find('\n');
    if (newlinePos == std::string_view::npos) {
        return std::string();
    }

    return std::string(source.substr(newlinePos + 1));
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
                        errors, &outResult.preOptimization);
    if (!errors.empty()) {
        outError = errors.front().message;
        return false;
    }

    optimizeAst(outResult.module, outResult.preOptimization);

    errors.clear();
    analyzeAstSemantics(outResult.module, outResult.classNames,
                        outResult.typeAliases, outResult.functionSignatures,
                        errors, &outResult.refreshed);
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

    const std::string strippedSource = stripStrictDirectiveLine(source);
    if (!compiler.compile(strippedSource, chunk, path.string())) {
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
    const AstFrontendBuildStatus status = buildAstFrontend(
        kSource, AstFrontendMode::StrictChecked, errors, frontend);
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

bool checkImportedModuleRegression(const std::filesystem::path& repoRoot) {
    const auto path = repoRoot / "tests/sample_import_frontend_identity.mog";

    GC autoGc;
    Chunk autoChunk;
    std::string autoDisassembly;
    if (!require(compileFileCanonical(path, CompilerEmitterMode::Auto, autoGc,
                                      autoChunk, autoDisassembly),
                 "optimized importer sample should compile in auto mode")) {
        return false;
    }

    GC forcedGc;
    Chunk forcedChunk;
    std::string forcedDisassembly;
    if (!require(compileFileCanonical(path, CompilerEmitterMode::ForceAst,
                                      forcedGc, forcedChunk, forcedDisassembly),
                 "optimized importer sample should compile in forced AST mode")) {
        return false;
    }

    if (!require(autoDisassembly == forcedDisassembly,
                 "optimized importer sample should have stable lowering in auto and forced AST modes")) {
        return false;
    }

    std::cout << "[PASS] imported frontend regression\n";
    return true;
}

bool checkNewlineOptimizationRegression(const std::filesystem::path& repoRoot) {
    const auto path = repoRoot / "tests/newline/sample_newline_operator_rhs.mog";
    const std::string source = readFile(path);
    if (!require(!source.empty(),
                 "newline-sensitive optimization sample should be readable")) {
        return false;
    }

    GC autoGc;
    Chunk autoChunk;
    std::string autoDisassembly;
    if (!require(compileFileCanonical(path, CompilerEmitterMode::Auto, autoGc,
                                      autoChunk, autoDisassembly),
                 "newline-sensitive optimization sample should compile in auto mode")) {
        return false;
    }

    GC forcedGc;
    Chunk forcedChunk;
    std::string forcedDisassembly;
    if (!require(compileFileCanonical(path, CompilerEmitterMode::ForceAst,
                                      forcedGc, forcedChunk, forcedDisassembly),
                 "newline-sensitive optimization sample should compile in forced AST mode")) {
        return false;
    }

    if (!require(autoDisassembly == forcedDisassembly,
                 "newline-sensitive sample should lower identically in auto and forced AST modes")) {
        return false;
    }

    GC strictGc;
    Chunk strictChunk;
    std::string strictDisassembly;
    if (!require(compileSourceCanonical(source, true, CompilerEmitterMode::ForceAst,
                                        strictGc, strictChunk, strictDisassembly),
                 "newline-sensitive optimization sample should compile in strict forced AST mode")) {
        return false;
    }

    if (!require(strictDisassembly.find("IADD") == std::string::npos &&
                     strictDisassembly.find("ADD") == std::string::npos,
                 "strict newline-sensitive optimized sample should not emit addition opcodes")) {
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
    if (!checkParserDiagnosticSpans()) {
        return 1;
    }
    if (!checkSemanticDiagnosticSpans()) {
        return 1;
    }
    if (!checkImportedModuleRegression(repoRoot)) {
        return 1;
    }
    if (!checkNewlineOptimizationRegression(repoRoot)) {
        return 1;
    }

    std::cout << "[PASS] AST frontend regression suite\n";
    return 0;
}
