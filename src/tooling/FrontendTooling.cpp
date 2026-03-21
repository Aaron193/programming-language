#include "tooling/FrontendTooling.hpp"

#include <algorithm>
#include <utility>

#include "Ast.hpp"
#include "FrontendDiagnostic.hpp"

namespace {

ToolingDiagnostic toolingDiagnosticFromFrontend(
    const FrontendDiagnostic& diagnostic) {
    ToolingDiagnostic result;
    result.code = diagnostic.code;
    result.message = diagnostic.message;
    result.range = toolingRangeFromSourceSpan(diagnostic.span);

    result.notes.reserve(diagnostic.notes.size());
    for (const auto& note : diagnostic.notes) {
        result.notes.push_back(
            ToolingDiagnosticNote{toolingRangeFromSourceSpan(note.span),
                                  note.message});
    }

    result.importTrace.reserve(diagnostic.importTrace.size());
    for (const auto& frame : diagnostic.importTrace) {
        result.importTrace.push_back(ToolingImportTraceFrame{
            toolingRangeFromSourceSpan(frame.span), frame.importerPath,
            frame.rawSpecifier, frame.resolvedPath});
    }

    return result;
}

ToolingDocumentSymbol makeSymbol(std::string name, std::string kind,
                                 std::string detail, const SourceSpan& range,
                                 const SourceSpan& selectionRange) {
    return ToolingDocumentSymbol{std::move(name),
                                 std::move(kind),
                                 std::move(detail),
                                 toolingRangeFromSourceSpan(range),
                                 toolingRangeFromSourceSpan(selectionRange)};
}

void collectDocumentSymbols(
    const AstModule& module, std::vector<ToolingDocumentSymbol>& outSymbols) {
    for (const auto& item : module.items) {
        if (!item) {
            continue;
        }

        if (const auto* functionDecl =
                std::get_if<AstFunctionDecl>(&item->value)) {
            outSymbols.push_back(makeSymbol(tokenLexeme(functionDecl->name),
                                            "function", "", item->node.span,
                                            functionDecl->name.span()));
            continue;
        }

        if (const auto* classDecl = std::get_if<AstClassDecl>(&item->value)) {
            outSymbols.push_back(makeSymbol(tokenLexeme(classDecl->name),
                                            "class", "", item->node.span,
                                            classDecl->name.span()));
            continue;
        }

        if (const auto* aliasDecl =
                std::get_if<AstTypeAliasDecl>(&item->value)) {
            outSymbols.push_back(makeSymbol(tokenLexeme(aliasDecl->name), "type",
                                            "", item->node.span,
                                            aliasDecl->name.span()));
            continue;
        }

        const auto* stmtPtr = std::get_if<AstStmtPtr>(&item->value);
        if (stmtPtr == nullptr || !*stmtPtr) {
            continue;
        }

        AstStmt& stmt = **stmtPtr;
        if (const auto* varDecl = std::get_if<AstVarDeclStmt>(&stmt.value)) {
            outSymbols.push_back(makeSymbol(tokenLexeme(varDecl->name),
                                            varDecl->isConst ? "constant"
                                                             : "variable",
                                            "", stmt.node.span,
                                            varDecl->name.span()));
            continue;
        }

        if (const auto* importStmt =
                std::get_if<AstDestructuredImportStmt>(&stmt.value)) {
            for (const auto& binding : importStmt->bindings) {
                const Token& name =
                    binding.localName.has_value() ? *binding.localName
                                                  : binding.exportedName;
                outSymbols.push_back(makeSymbol(
                    tokenLexeme(name), "import", "", binding.node.span,
                    name.span()));
            }
        }
    }
}

}  // namespace

bool toolingSourceStartsWithStrictDirective(std::string_view source) {
    constexpr std::string_view directive = "#!strict";
    if (source.size() < directive.size() ||
        source.substr(0, directive.size()) != directive) {
        return false;
    }

    if (source.size() == directive.size()) {
        return true;
    }

    const char boundary = source[directive.size()];
    return boundary == '\n' || boundary == '\r';
}

ToolingPosition toolingPositionFromSourcePosition(const SourcePosition& position) {
    ToolingPosition result;
    result.line = position.line == 0 ? 0 : position.line - 1;
    result.character = position.column == 0 ? 0 : position.column - 1;
    return result;
}

ToolingRange toolingRangeFromSourceSpan(const SourceSpan& span) {
    return ToolingRange{toolingPositionFromSourcePosition(span.start),
                        toolingPositionFromSourcePosition(span.end)};
}

SourcePosition sourcePositionFromToolingPosition(const ToolingPosition& position) {
    return makeSourcePosition(0, position.line + 1,
                              position.character + 1);
}

SourceSpan sourceSpanFromToolingRange(const ToolingRange& range) {
    return SourceSpan{sourcePositionFromToolingPosition(range.start),
                      sourcePositionFromToolingPosition(range.end)};
}

ToolingDocumentAnalysis analyzeDocumentForTooling(
    std::string_view source, const ToolingAnalyzeOptions& options) {
    ToolingDocumentAnalysis analysis;
    analysis.strictMode = options.strictMode;

    AstFrontendOptions frontendOptions;
    frontendOptions.sourcePath = options.sourcePath;
    frontendOptions.packageSearchPaths = options.packageSearchPaths;
    frontendOptions.moduleGraphCache = options.moduleGraphCache;

    std::vector<TypeError> errors;
    analysis.status = buildAstFrontend(
        source, frontendOptions,
        options.strictMode ? AstFrontendMode::StrictChecked
                           : AstFrontendMode::LoweringOnly,
        errors, analysis.frontend);
    analysis.hasFrontend = analysis.status == AstFrontendBuildStatus::Success;

    analysis.diagnostics.reserve(errors.size());
    for (const auto& error : errors) {
        analysis.diagnostics.push_back(toolingDiagnosticFromFrontend(error));
    }

    if (analysis.hasFrontend) {
        collectDocumentSymbols(analysis.frontend.module, analysis.documentSymbols);
    }

    return analysis;
}
