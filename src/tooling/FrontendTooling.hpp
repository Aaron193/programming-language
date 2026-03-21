#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <vector>

#include "AstFrontend.hpp"
#include "SourceLocation.hpp"

struct ToolingPosition {
    size_t line = 0;
    size_t character = 0;
};

struct ToolingRange {
    ToolingPosition start;
    ToolingPosition end;
};

struct ToolingDiagnosticNote {
    ToolingRange range;
    std::string message;
};

struct ToolingImportTraceFrame {
    ToolingRange range;
    std::string importerPath;
    std::string rawSpecifier;
    std::string resolvedPath;
};

struct ToolingDiagnostic {
    std::string code;
    std::string message;
    ToolingRange range;
    std::vector<ToolingDiagnosticNote> notes;
    std::vector<ToolingImportTraceFrame> importTrace;
};

struct ToolingDocumentSymbol {
    std::string name;
    std::string kind;
    std::string detail;
    ToolingRange range;
    ToolingRange selectionRange;
};

struct ToolingLocation {
    std::string path;
    ToolingRange range;
    ToolingRange selectionRange;
};

struct ToolingHover {
    ToolingRange range;
    std::string name;
    std::string kind;
    std::string detail;
};

struct ToolingCompletionItem {
    std::string label;
    std::string kind;
    std::string detail;
    std::string sortText;
};

struct ToolingSignatureParameter {
    std::string label;
};

struct ToolingSignatureInformation {
    std::string label;
    std::vector<ToolingSignatureParameter> parameters;
};

struct ToolingSignatureHelp {
    std::vector<ToolingSignatureInformation> signatures;
    size_t activeSignature = 0;
    size_t activeParameter = 0;
};

struct ToolingWorkspaceSymbol {
    std::string name;
    std::string kind;
    std::string detail;
    std::string path;
    ToolingRange range;
    ToolingRange selectionRange;
};

struct ToolingTextEdit {
    std::string path;
    ToolingRange range;
    std::string newText;
};

struct ToolingPrepareRename {
    ToolingRange range;
    std::string placeholder;
    std::string symbolKind;
    std::string strategy;
    AstNodeId declarationNodeId = 0;
    std::string sourcePath;
    std::string exportedName;
    std::string resolvedPath;
    bool importHasAlias = false;
};

struct ToolingAnalyzeOptions {
    std::string sourcePath;
    std::vector<std::string> packageSearchPaths;
    AstFrontendModuleGraphCache* moduleGraphCache = nullptr;
    bool strictMode = false;
};

struct ToolingDocumentAnalysis {
    AstFrontendBuildStatus status = AstFrontendBuildStatus::ParseFailed;
    std::string sourcePath;
    std::vector<std::string> packageSearchPaths;
    bool strictMode = false;
    bool hasFrontend = false;
    bool hasParse = false;
    bool hasBindings = false;
    bool hasSemantics = false;
    std::vector<ToolingDiagnostic> diagnostics;
    std::vector<ToolingDocumentSymbol> documentSymbols;
    AstFrontendResult frontend;
};

bool toolingSourceStartsWithStrictDirective(std::string_view source);
ToolingPosition toolingPositionFromSourcePosition(const SourcePosition& position);
ToolingRange toolingRangeFromSourceSpan(const SourceSpan& span);
SourcePosition sourcePositionFromToolingPosition(const ToolingPosition& position);
SourceSpan sourceSpanFromToolingRange(const ToolingRange& range);
ToolingDocumentAnalysis analyzeDocumentForTooling(
    std::string_view source, const ToolingAnalyzeOptions& options);
std::optional<ToolingLocation> findDefinitionForTooling(
    const ToolingDocumentAnalysis& analysis, const ToolingPosition& position);
std::vector<ToolingLocation> findTypeDeclarationReferencesForTooling(
    const ToolingDocumentAnalysis& analysis, const ToolingPosition& position);
std::vector<ToolingLocation> findReferencesForTooling(
    const ToolingDocumentAnalysis& analysis, const ToolingPosition& position);
std::optional<ToolingHover> findHoverForTooling(
    const ToolingDocumentAnalysis& analysis, const ToolingPosition& position);
std::vector<ToolingCompletionItem> findCompletionsForTooling(
    const ToolingDocumentAnalysis& analysis, const ToolingPosition& position);
std::vector<ToolingCompletionItem> findCompletionsForTooling(
    const ToolingDocumentAnalysis& analysis, std::string_view source,
    const ToolingPosition& position);
std::optional<ToolingSignatureHelp> findSignatureHelpForTooling(
    const ToolingDocumentAnalysis& analysis, std::string_view source,
    const ToolingPosition& position);
std::vector<ToolingWorkspaceSymbol> collectWorkspaceSymbolsForTooling(
    const ToolingDocumentAnalysis& analysis);
std::optional<ToolingPrepareRename> prepareRenameForTooling(
    const ToolingDocumentAnalysis& analysis, const ToolingPosition& position);
std::optional<std::string> validateRenameForTooling(
    const ToolingPrepareRename& target, std::string_view newName);
std::vector<ToolingTextEdit> findRenameEditsForTooling(
    const ToolingDocumentAnalysis& analysis, const ToolingPrepareRename& target,
    std::string_view newName);
std::vector<ToolingTextEdit> findImportRenameEditsForTooling(
    const ToolingDocumentAnalysis& analysis, const ToolingPrepareRename& target,
    std::string_view newName);
