#pragma once

#include <string>
#include <string_view>
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

struct ToolingAnalyzeOptions {
    std::string sourcePath;
    std::vector<std::string> packageSearchPaths;
    AstFrontendModuleGraphCache* moduleGraphCache = nullptr;
    bool strictMode = false;
};

struct ToolingDocumentAnalysis {
    AstFrontendBuildStatus status = AstFrontendBuildStatus::ParseFailed;
    bool strictMode = false;
    bool hasFrontend = false;
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
