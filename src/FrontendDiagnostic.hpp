#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "SourceLocation.hpp"

struct FrontendDiagnosticNote {
    SourceSpan span = makePointSpan(1, 1);
    std::string message;
};

struct FrontendImportTraceFrame {
    SourceSpan span = makePointSpan(1, 1);
    std::string importerPath;
    std::string rawSpecifier;
    std::string resolvedPath;
};

struct FrontendDiagnostic {
    std::string code = "frontend.error";
    std::string path;
    size_t line = 0;
    size_t column = 1;
    SourceSpan span = makePointSpan(1, 1);
    std::string message;
    std::vector<FrontendDiagnosticNote> notes;
    std::vector<FrontendImportTraceFrame> importTrace;

    FrontendDiagnostic() = default;
    FrontendDiagnostic(size_t lineValue, std::string messageValue,
                       std::string codeValue = "frontend.error")
        : code(std::move(codeValue)),
          path(),
          line(lineValue == 0 ? 1 : lineValue),
          column(1),
          span(makePointSpan(lineValue == 0 ? 1 : lineValue, 1)),
          message(std::move(messageValue)) {}
    FrontendDiagnostic(SourceSpan spanValue, std::string messageValue,
                       std::string codeValue = "frontend.error")
        : code(std::move(codeValue)),
          path(),
          line(spanValue.line()),
          column(spanValue.column()),
          span(std::move(spanValue)),
          message(std::move(messageValue)) {}

    void addNote(const SourceSpan& noteSpan, std::string noteMessage) {
        notes.push_back(
            FrontendDiagnosticNote{noteSpan, std::move(noteMessage)});
    }

    void addImportTrace(const FrontendImportTraceFrame& frame) {
        importTrace.push_back(frame);
    }
};

void printFrontendDiagnostic(std::ostream& out,
                             const FrontendDiagnostic& diagnostic,
                             const char* stage = "compile");
