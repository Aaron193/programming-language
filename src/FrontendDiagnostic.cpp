#include "FrontendDiagnostic.hpp"

#include <ostream>

namespace {

void printSpanPrefix(std::ostream& out, const char* level, const char* stage,
                     const SourceSpan& span) {
    out << "[" << level << "][" << stage << "][line " << span.line() << ":"
        << span.column() << "] ";
}

}  // namespace

void printFrontendDiagnostic(std::ostream& out,
                             const FrontendDiagnostic& diagnostic,
                             const char* stage) {
    printSpanPrefix(out, "error", stage, diagnostic.span);
    out << diagnostic.message << '\n';

    for (const auto& note : diagnostic.notes) {
        printSpanPrefix(out, "note", stage, note.span);
        out << note.message << '\n';
    }

    for (auto it = diagnostic.importTrace.rbegin();
         it != diagnostic.importTrace.rend(); ++it) {
        printSpanPrefix(out, "note", stage, it->span);
        out << "while importing '" << it->rawSpecifier << "'";
        if (!it->importerPath.empty()) {
            out << " from '" << it->importerPath << "'";
        }
        if (!it->resolvedPath.empty() && it->resolvedPath != it->rawSpecifier) {
            out << " -> '" << it->resolvedPath << "'";
        }
        out << '\n';
    }
}
