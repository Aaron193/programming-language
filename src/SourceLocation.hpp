#pragma once

#include <cstddef>

struct SourcePosition {
    size_t offset = 0;
    size_t line = 1;
    size_t column = 1;
};

struct SourceSpan {
    SourcePosition start;
    SourcePosition end;

    size_t line() const { return start.line == 0 ? 1 : start.line; }
    size_t column() const { return start.column == 0 ? 1 : start.column; }
    bool valid() const { return end.offset >= start.offset; }
};

inline SourcePosition makeSourcePosition(size_t offset, size_t line,
                                         size_t column) {
    return SourcePosition{offset, line == 0 ? 1 : line, column == 0 ? 1 : column};
}

inline SourceSpan makePointSpan(size_t line, size_t column = 1,
                                size_t offset = 0) {
    const SourcePosition point =
        makeSourcePosition(offset, line, column);
    return SourceSpan{point, point};
}

inline SourceSpan makePointSpan(const SourcePosition& point) {
    return SourceSpan{point, point};
}

inline SourceSpan combineSourceSpans(const SourceSpan& start,
                                     const SourceSpan& end) {
    return SourceSpan{start.start, end.end};
}
