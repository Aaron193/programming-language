#include "tooling/FrontendTooling.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "SyntaxRules.hpp"

namespace {

constexpr int kFormatIndentWidth = 4;
constexpr size_t kMaxPreservedBlankLines = 1;

struct FormatComment {
    size_t startOffset = 0;
    size_t endOffset = 0;
    size_t startLine = 1;
    size_t endLine = 1;
    std::string text;
    bool hasCodeBeforeSameLine = false;
    bool hasCodeAfterSameLine = false;
};

bool isWhitespace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
           ch == '\v' || ch == '\f';
}

bool lineHasCodeBefore(std::string_view source, size_t offset) {
    size_t index = offset;
    while (index > 0) {
        const char ch = source[index - 1];
        if (ch == '\n') {
            break;
        }
        if (!isWhitespace(ch)) {
            return true;
        }
        --index;
    }
    return false;
}

bool lineHasCodeAfter(std::string_view source, size_t offset) {
    for (size_t index = offset; index < source.size(); ++index) {
        const char ch = source[index];
        if (ch == '\n') {
            break;
        }
        if (!isWhitespace(ch)) {
            return true;
        }
    }
    return false;
}

std::vector<FormatComment> collectComments(std::string_view source,
                                           bool& outSupported) {
    std::vector<FormatComment> comments;
    outSupported = true;
    size_t line = 1;

    for (size_t index = 0; index < source.size();) {
        if (source[index] == '\n') {
            ++line;
            ++index;
            continue;
        }

        if (source[index] != '/' || index + 1 >= source.size()) {
            ++index;
            continue;
        }

        if (source[index + 1] == '/') {
            const size_t start = index;
            const size_t startLine = line;
            index += 2;
            while (index < source.size() && source[index] != '\n') {
                ++index;
            }
            const size_t end = index;
            comments.push_back(FormatComment{
                start,
                end,
                startLine,
                startLine,
                std::string(source.substr(start, end - start)),
                lineHasCodeBefore(source, start),
                false,
            });
            continue;
        }

        if (source[index + 1] != '*') {
            ++index;
            continue;
        }

        const size_t start = index;
        const size_t startLine = line;
        index += 2;
        bool closed = false;
        while (index + 1 < source.size()) {
            if (source[index] == '\n') {
                ++line;
                ++index;
                continue;
            }
            if (source[index] == '*' && source[index + 1] == '/') {
                index += 2;
                closed = true;
                break;
            }
            ++index;
        }

        if (!closed) {
            outSupported = false;
            return comments;
        }

        const size_t end = index;
        const size_t endLine = line;
        const bool hasCodeBefore = lineHasCodeBefore(source, start);
        const bool hasCodeAfter = lineHasCodeAfter(source, end);
        if (hasCodeAfter) {
            outSupported = false;
        }
        comments.push_back(FormatComment{start,
                                         end,
                                         startLine,
                                         endLine,
                                         std::string(source.substr(start, end - start)),
                                         hasCodeBefore,
                                         hasCodeAfter});
    }

    return comments;
}

std::string indentText(int indent) {
    return std::string(static_cast<size_t>(indent * kFormatIndentWidth), ' ');
}

std::string annotationOperatorText(int op) {
    switch (op) {
        case TokenType::PLUS:
            return "+";
        case TokenType::MINUS:
            return "-";
        case TokenType::STAR:
            return "*";
        case TokenType::SLASH:
            return "/";
        case TokenType::EQUAL_EQUAL:
            return "==";
        case TokenType::BANG_EQUAL:
            return "!=";
        case TokenType::LESS:
            return "<";
        case TokenType::LESS_EQUAL:
            return "<=";
        case TokenType::GREATER:
            return ">";
        case TokenType::GREATER_EQUAL:
            return ">=";
        default:
            return "";
    }
}

struct PlainFormatter;

struct Formatter {
    struct Boundary {
        size_t startOffset = 0;
        size_t startLine = 1;
        size_t endOffset = 0;
        size_t endLine = 1;
    };

    std::string_view source;
    const ToolingDocumentAnalysis& analysis;
    std::vector<FormatComment> comments;
    size_t nextComment = 0;
    std::string output;
    bool lineStart = true;
    bool failed = false;

    bool run() {
        bool commentsSupported = true;
        comments = collectComments(source, commentsSupported);
        if (!commentsSupported || !analysis.hasParse) {
            return false;
        }

        if (!formatModule(analysis.frontend.module)) {
            return false;
        }

        if (nextComment != comments.size()) {
            return false;
        }

        if (!lineStart) {
            appendChar('\n');
        }
        while (!output.empty() && output.back() == '\n' &&
               output.size() >= 2 && output[output.size() - 2] == '\n') {
            break;
        }
        return true;
    }

    void appendChar(char ch) {
        output.push_back(ch);
        lineStart = ch == '\n';
    }

    void append(std::string_view text) {
        for (char ch : text) {
            appendChar(ch);
        }
    }

    void ensureIndent(int indent) {
        if (!lineStart) {
            return;
        }
        append(indentText(indent));
    }

    void appendNewline() { appendChar('\n'); }

    bool hasPendingCommentInRange(size_t startOffset, size_t endOffset) const {
        return nextComment < comments.size() &&
               comments[nextComment].startOffset >= startOffset &&
               comments[nextComment].startOffset < endOffset;
    }

    void advanceToNextContentLine(size_t blankLineCount) {
        if (!lineStart) {
            appendNewline();
        }
        const size_t cappedBlankLineCount =
            std::min(blankLineCount, kMaxPreservedBlankLines);
        for (size_t index = 0; index < cappedBlankLineCount; ++index) {
            appendNewline();
        }
    }

    bool emitGap(size_t startOffset, size_t endOffset, size_t startLine,
                 size_t endLine, int indent) {
        if (nextComment < comments.size() &&
            comments[nextComment].startOffset < startOffset) {
            failed = true;
            return false;
        }

        size_t currentLine = startLine;
        while (nextComment < comments.size() &&
               comments[nextComment].startOffset < endOffset) {
            const FormatComment& comment = comments[nextComment];
            if (comment.startOffset < startOffset) {
                failed = true;
                return false;
            }
            if (comment.hasCodeAfterSameLine) {
                failed = true;
                return false;
            }

            if (comment.hasCodeBeforeSameLine) {
                if (lineStart || comment.startLine != currentLine) {
                    failed = true;
                    return false;
                }
                append(" ");
                append(comment.text);
            } else {
                if (comment.startLine < currentLine) {
                    failed = true;
                    return false;
                }
                const size_t blankLines =
                    comment.startLine > currentLine
                        ? comment.startLine - currentLine - 1
                        : 0;
                if (comment.startLine > currentLine) {
                    advanceToNextContentLine(blankLines);
                }
                ensureIndent(indent);
                append(comment.text);
            }

            currentLine = comment.endLine;
            ++nextComment;
        }

        if (endLine > currentLine) {
            advanceToNextContentLine(endLine - currentLine - 1);
        }
        return true;
    }

    bool hasCommentsInRange(size_t startOffset, size_t endOffset) const {
        return std::any_of(
            comments.begin(), comments.end(), [&](const FormatComment& comment) {
                return comment.startOffset >= startOffset &&
                       comment.startOffset < endOffset;
            });
    }

    const SourceSpan& itemSpan(const AstItem& item) const {
        return std::visit(
            [&](const auto& value) -> const SourceSpan& {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, AstTypeAliasDecl> ||
                              std::is_same_v<T, AstClassDecl> ||
                              std::is_same_v<T, AstFunctionDecl>) {
                    return value.node.span;
                } else {
                    return value->node.span;
                }
            },
            item.value);
    }

    Boundary statementBoundary(const AstStmt& stmt) const {
        return std::visit(
            [&](const auto& value) -> Boundary {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, AstBlockStmt>) {
                    return Boundary{stmt.node.span.start.offset, stmt.node.span.start.line,
                                    stmt.node.span.end.offset, stmt.node.span.end.line};
                } else if constexpr (std::is_same_v<T, AstExprStmt>) {
                    return Boundary{value.expression->node.span.start.offset,
                                    value.expression->node.span.start.line,
                                    value.expression->node.span.end.offset,
                                    value.expression->node.span.end.line};
                } else if constexpr (std::is_same_v<T, AstPrintStmt>) {
                    return Boundary{value.keyword.span().start.offset,
                                    value.keyword.span().start.line,
                                    value.expression->node.span.end.offset,
                                    value.expression->node.span.end.line};
                } else if constexpr (std::is_same_v<T, AstReturnStmt> ||
                                     std::is_same_v<T, AstBreakStmt> ||
                                     std::is_same_v<T, AstContinueStmt> ||
                                     std::is_same_v<T, AstVarDeclStmt> ||
                                     std::is_same_v<T, AstDestructuredImportStmt>) {
                    return Boundary{stmt.node.span.start.offset, stmt.node.span.start.line,
                                    stmt.node.span.end.offset, stmt.node.span.end.line};
                } else if constexpr (std::is_same_v<T, AstIfStmt>) {
                    const Boundary thenBoundary = statementBoundary(*value.thenBranch);
                    const Boundary endBoundary = value.elseBranch
                                                    ? statementBoundary(*value.elseBranch)
                                                    : thenBoundary;
                    return Boundary{value.condition->node.span.start.offset,
                                    value.condition->node.span.start.line,
                                    endBoundary.endOffset,
                                    endBoundary.endLine};
                } else if constexpr (std::is_same_v<T, AstWhileStmt>) {
                    const Boundary bodyBoundary = statementBoundary(*value.body);
                    return Boundary{value.condition->node.span.start.offset,
                                    value.condition->node.span.start.line,
                                    bodyBoundary.endOffset,
                                    bodyBoundary.endLine};
                } else if constexpr (std::is_same_v<T, AstForStmt>) {
                    const Boundary bodyBoundary = statementBoundary(*value.body);
                    size_t startOffset = bodyBoundary.startOffset;
                    size_t startLine = bodyBoundary.startLine;
                    if (std::holds_alternative<std::unique_ptr<AstVarDeclStmt>>(
                            value.initializer)) {
                        const auto& initializer =
                            std::get<std::unique_ptr<AstVarDeclStmt>>(value.initializer);
                        startOffset = initializer->node.span.start.offset;
                        startLine = initializer->node.span.start.line;
                    } else if (std::holds_alternative<AstExprPtr>(value.initializer)) {
                        const auto& initializer = std::get<AstExprPtr>(value.initializer);
                        startOffset = initializer->node.span.start.offset;
                        startLine = initializer->node.span.start.line;
                    } else if (value.condition) {
                        startOffset = value.condition->node.span.start.offset;
                        startLine = value.condition->node.span.start.line;
                    } else if (value.increment) {
                        startOffset = value.increment->node.span.start.offset;
                        startLine = value.increment->node.span.start.line;
                    }
                    return Boundary{startOffset, startLine, bodyBoundary.endOffset,
                                    bodyBoundary.endLine};
                } else if constexpr (std::is_same_v<T, AstForEachStmt>) {
                    const Boundary bodyBoundary = statementBoundary(*value.body);
                    return Boundary{value.name.span().start.offset,
                                    value.name.span().start.line,
                                    bodyBoundary.endOffset,
                                    bodyBoundary.endLine};
                }
                return Boundary{stmt.node.span.start.offset, stmt.node.span.start.line,
                                stmt.node.span.end.offset, stmt.node.span.end.line};
            },
            stmt.value);
    }

    Boundary itemBoundary(const AstItem& item) const {
        return std::visit(
            [&](const auto& value) -> Boundary {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, AstTypeAliasDecl> ||
                              std::is_same_v<T, AstClassDecl> ||
                              std::is_same_v<T, AstFunctionDecl>) {
                    return Boundary{value.node.span.start.offset,
                                    value.node.span.start.line,
                                    value.node.span.end.offset,
                                    value.node.span.end.line};
                } else {
                    return statementBoundary(*value);
                }
            },
            item.value);
    }

    std::string formatType(const AstTypeExpr& type) {
        switch (type.kind) {
            case AstTypeKind::NAMED:
                return tokenLexeme(type.token);
            case AstTypeKind::FUNCTION: {
                std::string text = "fn(";
                for (size_t index = 0; index < type.paramTypes.size(); ++index) {
                    if (index != 0) {
                        text += ", ";
                    }
                    text += formatType(*type.paramTypes[index]);
                }
                text += ") ";
                text += formatType(*type.returnType);
                return text;
            }
            case AstTypeKind::ARRAY:
                return tokenLexeme(type.token) + "<" +
                       formatType(*type.elementType) + ">";
            case AstTypeKind::DICT:
                return tokenLexeme(type.token) + "<" +
                       formatType(*type.keyType) + ", " +
                       formatType(*type.valueType) + ">";
            case AstTypeKind::SET:
                return tokenLexeme(type.token) + "<" +
                       formatType(*type.elementType) + ">";
            case AstTypeKind::OPTIONAL:
                return formatType(*type.innerType) + "?";
            case AstTypeKind::NATIVE_HANDLE:
                return tokenLexeme(type.token) + "<" + type.packageNamespace +
                       ":" + type.packageName + ":" + type.nativeHandleTypeName +
                       ">";
        }
        return "";
    }

    std::string formatParameterList(const std::vector<AstParameter>& params) {
        std::string text = "(";
        for (size_t index = 0; index < params.size(); ++index) {
            if (index != 0) {
                text += ", ";
            }
            text += tokenLexeme(params[index].name);
            if (params[index].type) {
                text += " ";
                text += formatType(*params[index].type);
            }
        }
        text += ")";
        return text;
    }

    void appendPreservedLineGap(std::string& outText, size_t fromLine,
                                size_t toLine, int indent) const {
        if (toLine <= fromLine) {
            outText += "\n";
            outText += indentText(indent);
            return;
        }
        const size_t cappedToLine =
            std::min(toLine, fromLine + kMaxPreservedBlankLines + 1);
        for (size_t line = fromLine; line < cappedToLine; ++line) {
            outText += "\n";
        }
        outText += indentText(indent);
    }

    bool hasExpressionComments(const SourceSpan& span) const {
        return hasCommentsInRange(span.start.offset, span.end.offset);
    }

    bool callHasPreservedMultilineLayout(const AstCallExpr& callExpr,
                                         const SourceSpan& span) const {
        if (callExpr.arguments.empty()) {
            return span.end.line > callExpr.callee->node.span.end.line;
        }
        if (callExpr.arguments.front()->node.span.start.line >
            callExpr.callee->node.span.end.line) {
            return true;
        }
        for (size_t index = 1; index < callExpr.arguments.size(); ++index) {
            if (callExpr.arguments[index]->node.span.start.line >
                callExpr.arguments[index - 1]->node.span.end.line) {
                return true;
            }
        }
        return span.end.line > callExpr.arguments.back()->node.span.end.line;
    }

    bool arrayHasPreservedMultilineLayout(const AstArrayLiteralExpr& arrayExpr,
                                          const SourceSpan& span) const {
        if (arrayExpr.elements.empty()) {
            return span.end.line > span.start.line;
        }
        if (arrayExpr.elements.front()->node.span.start.line > span.start.line) {
            return true;
        }
        for (size_t index = 1; index < arrayExpr.elements.size(); ++index) {
            if (arrayExpr.elements[index]->node.span.start.line >
                arrayExpr.elements[index - 1]->node.span.end.line) {
                return true;
            }
        }
        return span.end.line > arrayExpr.elements.back()->node.span.end.line;
    }

    bool dictHasPreservedMultilineLayout(const AstDictLiteralExpr& dictExpr,
                                         const SourceSpan& span) const {
        if (dictExpr.entries.empty()) {
            return span.end.line > span.start.line;
        }
        if (dictExpr.entries.front().key->node.span.start.line > span.start.line) {
            return true;
        }
        for (size_t index = 1; index < dictExpr.entries.size(); ++index) {
            if (dictExpr.entries[index].key->node.span.start.line >
                dictExpr.entries[index - 1].value->node.span.end.line) {
                return true;
            }
        }
        return span.end.line > dictExpr.entries.back().value->node.span.end.line;
    }

    bool groupingHasPreservedMultilineLayout(const AstGroupingExpr& groupingExpr,
                                             const SourceSpan& span) const {
        return groupingExpr.expression &&
               (groupingExpr.expression->node.span.start.line > span.start.line ||
                span.end.line > groupingExpr.expression->node.span.end.line);
    }

    std::string formatMultilineCallExpression(const AstCallExpr& callExpr,
                                              const SourceSpan& span, int indent) {
        if (hasExpressionComments(span)) {
            failed = true;
            return std::string();
        }

        std::string result = formatExpression(*callExpr.callee, indent, 14);
        if (failed) {
            return std::string();
        }
        result += "(";
        if (callExpr.arguments.empty()) {
            if (span.end.line > callExpr.callee->node.span.end.line) {
                appendPreservedLineGap(result, callExpr.callee->node.span.end.line,
                                       span.end.line, indent);
            }
            result += ")";
            return result;
        }

        const size_t firstArgumentLine =
            callExpr.arguments.front()->node.span.start.line;
        if (firstArgumentLine > callExpr.callee->node.span.end.line) {
            appendPreservedLineGap(result, callExpr.callee->node.span.end.line,
                                   firstArgumentLine, indent + 1);
        }

        for (size_t index = 0; index < callExpr.arguments.size(); ++index) {
            if (index == 0 && firstArgumentLine <= callExpr.callee->node.span.end.line) {
                result += formatExpression(*callExpr.arguments[index], indent + 1);
            } else if (index != 0) {
                const auto& previousArgument = callExpr.arguments[index - 1];
                const auto& argument = callExpr.arguments[index];
                if (argument->node.span.start.line >
                    previousArgument->node.span.end.line) {
                    appendPreservedLineGap(result,
                                           previousArgument->node.span.end.line,
                                           argument->node.span.start.line,
                                           indent + 1);
                } else {
                    result += " ";
                }
                result += formatExpression(*argument, indent + 1);
            } else {
                result += formatExpression(*callExpr.arguments[index], indent + 1);
            }
            if (failed) {
                return std::string();
            }
            if (index + 1 < callExpr.arguments.size()) {
                result += ",";
            }
        }

        if (span.end.line > callExpr.arguments.back()->node.span.end.line) {
            appendPreservedLineGap(result,
                                   callExpr.arguments.back()->node.span.end.line,
                                   span.end.line, indent);
        }
        result += ")";
        return result;
    }

    std::string formatMultilineArrayLiteral(const AstArrayLiteralExpr& arrayExpr,
                                            const SourceSpan& span, int indent) {
        if (hasExpressionComments(span)) {
            failed = true;
            return std::string();
        }

        std::string result = "[";
        if (arrayExpr.elements.empty()) {
            if (span.end.line > span.start.line) {
                appendPreservedLineGap(result, span.start.line, span.end.line,
                                       indent);
            }
            result += "]";
            return result;
        }

        const size_t firstElementLine = arrayExpr.elements.front()->node.span.start.line;
        if (firstElementLine > span.start.line) {
            appendPreservedLineGap(result, span.start.line, firstElementLine,
                                   indent + 1);
        }

        for (size_t index = 0; index < arrayExpr.elements.size(); ++index) {
            if (index == 0 && firstElementLine <= span.start.line) {
                result += formatExpression(*arrayExpr.elements[index], indent + 1);
            } else if (index != 0) {
                const auto& previousElement = arrayExpr.elements[index - 1];
                const auto& element = arrayExpr.elements[index];
                if (element->node.span.start.line >
                    previousElement->node.span.end.line) {
                    appendPreservedLineGap(result,
                                           previousElement->node.span.end.line,
                                           element->node.span.start.line,
                                           indent + 1);
                } else {
                    result += " ";
                }
                result += formatExpression(*element, indent + 1);
            } else {
                result += formatExpression(*arrayExpr.elements[index], indent + 1);
            }
            if (failed) {
                return std::string();
            }
            if (index + 1 < arrayExpr.elements.size()) {
                result += ",";
            }
        }

        if (span.end.line > arrayExpr.elements.back()->node.span.end.line) {
            appendPreservedLineGap(result,
                                   arrayExpr.elements.back()->node.span.end.line,
                                   span.end.line, indent);
        }
        result += "]";
        return result;
    }

    std::string formatMultilineDictLiteral(const AstDictLiteralExpr& dictExpr,
                                           const SourceSpan& span, int indent) {
        if (hasExpressionComments(span)) {
            failed = true;
            return std::string();
        }

        std::string result = "{";
        if (dictExpr.entries.empty()) {
            if (span.end.line > span.start.line) {
                appendPreservedLineGap(result, span.start.line, span.end.line,
                                       indent);
            }
            result += "}";
            return result;
        }

        const size_t firstEntryLine = dictExpr.entries.front().key->node.span.start.line;
        if (firstEntryLine > span.start.line) {
            appendPreservedLineGap(result, span.start.line, firstEntryLine,
                                   indent + 1);
        }

        for (size_t index = 0; index < dictExpr.entries.size(); ++index) {
            if (index == 0 && firstEntryLine <= span.start.line) {
                result += formatExpression(*dictExpr.entries[index].key, indent + 1);
            } else if (index != 0) {
                const auto& previousEntry = dictExpr.entries[index - 1];
                const auto& entry = dictExpr.entries[index];
                if (entry.key->node.span.start.line >
                    previousEntry.value->node.span.end.line) {
                    appendPreservedLineGap(result,
                                           previousEntry.value->node.span.end.line,
                                           entry.key->node.span.start.line,
                                           indent + 1);
                } else {
                    result += " ";
                }
                result += formatExpression(*entry.key, indent + 1);
            } else {
                result += formatExpression(*dictExpr.entries[index].key, indent + 1);
            }
            if (failed) {
                return std::string();
            }
            result += ": ";
            result += formatExpression(*dictExpr.entries[index].value, indent + 1);
            if (failed) {
                return std::string();
            }
            if (index + 1 < dictExpr.entries.size()) {
                result += ",";
            }
        }

        if (span.end.line > dictExpr.entries.back().value->node.span.end.line) {
            appendPreservedLineGap(result,
                                   dictExpr.entries.back().value->node.span.end.line,
                                   span.end.line, indent);
        }
        result += "}";
        return result;
    }

    std::string formatMultilineGroupingExpression(const AstGroupingExpr& groupingExpr,
                                                  const SourceSpan& span,
                                                  int indent) {
        if (hasExpressionComments(span)) {
            failed = true;
            return std::string();
        }

        std::string result = "(";
        if (groupingExpr.expression->node.span.start.line > span.start.line) {
            appendPreservedLineGap(result, span.start.line,
                                   groupingExpr.expression->node.span.start.line,
                                   indent + 1);
        }
        result += formatExpression(*groupingExpr.expression, indent + 1);
        if (failed) {
            return std::string();
        }
        if (span.end.line > groupingExpr.expression->node.span.end.line) {
            appendPreservedLineGap(result,
                                   groupingExpr.expression->node.span.end.line,
                                   span.end.line, indent);
        }
        result += ")";
        return result;
    }

    int precedenceForBinaryOperator(TokenType type) const {
        switch (type) {
            case TokenType::LOGICAL_OR:
                return 2;
            case TokenType::LOGICAL_AND:
                return 3;
            case TokenType::EQUAL_EQUAL:
            case TokenType::BANG_EQUAL:
                return 4;
            case TokenType::LESS:
            case TokenType::LESS_EQUAL:
            case TokenType::GREATER:
            case TokenType::GREATER_EQUAL:
                return 5;
            case TokenType::PIPE:
                return 6;
            case TokenType::CARET:
                return 7;
            case TokenType::AMPERSAND:
                return 8;
            case TokenType::SHIFT_LEFT_TOKEN:
            case TokenType::SHIFT_RIGHT_TOKEN:
                return 9;
            case TokenType::PLUS:
            case TokenType::MINUS:
                return 10;
            case TokenType::STAR:
            case TokenType::SLASH:
                return 11;
            default:
                return 0;
        }
    }

    int precedenceForExpression(const AstExpr& expr) const {
        return std::visit(
            [&](const auto& value) -> int {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, AstAssignmentExpr>) {
                    return 1;
                } else if constexpr (std::is_same_v<T, AstBinaryExpr>) {
                    return precedenceForBinaryOperator(value.op.type());
                } else if constexpr (std::is_same_v<T, AstCastExpr>) {
                    return 12;
                } else if constexpr (std::is_same_v<T, AstUnaryExpr> ||
                                     std::is_same_v<T, AstUpdateExpr>) {
                    return 13;
                } else if constexpr (std::is_same_v<T, AstCallExpr> ||
                                     std::is_same_v<T, AstMemberExpr> ||
                                     std::is_same_v<T, AstIndexExpr>) {
                    return 14;
                }
                return 15;
            },
            expr.value);
    }

    bool expressionNeedsParens(const AstExpr& expr, int parentPrecedence,
                               bool rightChild, bool parentRightAssociative) const {
        const int childPrecedence = precedenceForExpression(expr);
        if (childPrecedence < parentPrecedence) {
            return true;
        }
        if (childPrecedence > parentPrecedence) {
            return false;
        }
        if (rightChild) {
            return !parentRightAssociative;
        }
        return parentRightAssociative;
    }

    std::string formatExpression(const AstExpr& expr, int indent,
                                 int parentPrecedence = 0,
                                 bool rightChild = false,
                                 bool parentRightAssociative = false);

    bool formatPlainStatement(const AstStmt& stmt, int indent, std::string& outText);
    bool formatPlainBlock(const AstStmt& stmt, int indent, std::string& outText);

    bool appendBlock(const AstBlockStmt& block, const SourceSpan& span, int indent) {
        append("{");
        const size_t firstOffset =
            block.items.empty() ? span.end.offset : itemBoundary(*block.items.front()).startOffset;
        const size_t firstLine =
            block.items.empty() ? span.end.line : itemBoundary(*block.items.front()).startLine;
        if (!emitGap(span.start.offset, firstOffset, span.start.line, firstLine,
                     indent + 1)) {
            return false;
        }

        for (size_t index = 0; index < block.items.size(); ++index) {
            if (!formatItem(*block.items[index], indent + 1, false)) {
                return false;
            }

            const Boundary currentSpan = itemBoundary(*block.items[index]);
            const size_t nextOffset =
                index + 1 < block.items.size()
                    ? itemBoundary(*block.items[index + 1]).startOffset
                    : span.end.offset;
            const size_t nextLine =
                index + 1 < block.items.size()
                    ? itemBoundary(*block.items[index + 1]).startLine
                    : span.end.line;
            if (!emitGap(currentSpan.endOffset, nextOffset, currentSpan.endLine,
                         nextLine, indent + 1)) {
                return false;
            }
        }

        ensureIndent(indent);
        append("}");
        return true;
    }

    bool formatEmbeddedStatement(const AstStmt& stmt, int indent,
                                 size_t boundaryOffset) {
        if (std::holds_alternative<AstBlockStmt>(stmt.value)) {
            append(" ");
            const auto* block = std::get_if<AstBlockStmt>(&stmt.value);
            if (block == nullptr || !appendBlock(*block, stmt.node.span, indent)) {
                failed = true;
                return false;
            }
            return true;
        }

        appendNewline();
        if (!formatStatement(stmt, indent + 1)) {
            return false;
        }
        (void)boundaryOffset;
        return true;
    }

    bool formatStatement(const AstStmt& stmt, int indent) {
        if (!formatStatement(stmt, indent, false)) {
            return false;
        }
        return true;
    }

    bool formatStatement(const AstStmt& stmt, int indent, bool preserveLeadingGap) {
        if (preserveLeadingGap) {
            if (!emitGap(stmt.node.span.start.offset, stmt.node.span.start.offset,
                         stmt.node.span.start.line, stmt.node.span.start.line,
                         indent)) {
                return false;
            }
        }
        ensureIndent(indent);
        return std::visit(
            [&](const auto& value) -> bool {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, AstBlockStmt>) {
                    return appendBlock(value, stmt.node.span, indent);
                } else if constexpr (std::is_same_v<T, AstExprStmt>) {
                    append(formatExpression(*value.expression, indent));
                    return true;
                } else if constexpr (std::is_same_v<T, AstPrintStmt>) {
                    append("print(");
                    append(formatExpression(*value.expression, indent));
                    append(")");
                    return true;
                } else if constexpr (std::is_same_v<T, AstReturnStmt>) {
                    append("return");
                    if (value.value) {
                        append(" ");
                        append(formatExpression(*value.value, indent));
                    }
                    return true;
                } else if constexpr (std::is_same_v<T, AstBreakStmt>) {
                    append("break");
                    if (value.label) {
                        append(" ");
                        append(tokenLexeme(*value.label));
                    }
                    return true;
                } else if constexpr (std::is_same_v<T, AstContinueStmt>) {
                    append("continue");
                    if (value.label) {
                        append(" ");
                        append(tokenLexeme(*value.label));
                    }
                    return true;
                } else if constexpr (std::is_same_v<T, AstVarDeclStmt>) {
                    append(value.isConst ? "const " : "var ");
                    append(tokenLexeme(value.name));
                    if (!value.omittedType && value.declaredType) {
                        append(" ");
                        append(formatType(*value.declaredType));
                    }
                    append(" = ");
                    append(formatExpression(*value.initializer, indent));
                    return true;
                } else if constexpr (std::is_same_v<T, AstDestructuredImportStmt>) {
                    append(value.isConst ? "const {" : "var {");
                    for (size_t index = 0; index < value.bindings.size(); ++index) {
                        if (index == 0) {
                            append(" ");
                        } else {
                            append(", ");
                        }
                        const auto& binding = value.bindings[index];
                        append(tokenLexeme(binding.exportedName));
                        if (binding.localName) {
                            append(" as ");
                            append(tokenLexeme(*binding.localName));
                        }
                        if (binding.expectedType) {
                            append(": ");
                            append(formatType(*binding.expectedType));
                        }
                    }
                    if (!value.bindings.empty()) {
                        append(" ");
                    }
                    append("} = ");
                    append(formatExpression(*value.initializer, indent));
                    return true;
                } else if constexpr (std::is_same_v<T, AstIfStmt>) {
                    append("if (");
                    append(formatExpression(*value.condition, indent));
                    append(")");
                    const Boundary thenBranchBoundary =
                        statementBoundary(*value.thenBranch);
                    const Boundary elseBranchBoundary =
                        value.elseBranch ? statementBoundary(*value.elseBranch)
                                         : thenBranchBoundary;
                    if (!formatEmbeddedStatement(*value.thenBranch, indent,
                                                 elseBranchBoundary.startOffset)) {
                        return false;
                    }
                    if (!value.elseBranch) {
                        return true;
                    }
                    if (elseBranchBoundary.startLine == thenBranchBoundary.endLine) {
                        append(" ");
                    } else if (elseBranchBoundary.startLine > thenBranchBoundary.endLine) {
                        advanceToNextContentLine(elseBranchBoundary.startLine -
                                                 thenBranchBoundary.endLine - 1);
                    }
                    ensureIndent(indent);
                    append("else");
                    return formatEmbeddedStatement(*value.elseBranch, indent,
                                                   elseBranchBoundary.endOffset);
                } else if constexpr (std::is_same_v<T, AstWhileStmt>) {
                    if (value.label) {
                        append(tokenLexeme(*value.label));
                        append(": ");
                    }
                    append("while (");
                    append(formatExpression(*value.condition, indent));
                    append(")");
                    return formatEmbeddedStatement(*value.body, indent,
                                                   stmt.node.span.end.offset);
                } else if constexpr (std::is_same_v<T, AstForStmt>) {
                    if (value.label) {
                        append(tokenLexeme(*value.label));
                        append(": ");
                    }
                    append("for (");
                    if (std::holds_alternative<std::unique_ptr<AstVarDeclStmt>>(
                            value.initializer)) {
                        const auto& initializer =
                            std::get<std::unique_ptr<AstVarDeclStmt>>(value.initializer);
                        append(initializer->isConst ? "const " : "var ");
                        append(tokenLexeme(initializer->name));
                        append(" ");
                        append(formatType(*initializer->declaredType));
                        append(" = ");
                        append(formatExpression(*initializer->initializer, indent));
                    } else if (std::holds_alternative<AstExprPtr>(
                                   value.initializer)) {
                        const auto& initializer =
                            std::get<AstExprPtr>(value.initializer);
                        append(formatExpression(*initializer, indent));
                    }
                    append(";");
                    if (value.condition) {
                        append(" ");
                        append(formatExpression(*value.condition, indent));
                    }
                    append(";");
                    if (value.increment) {
                        append(" ");
                        append(formatExpression(*value.increment, indent));
                    }
                    append(")");
                    return formatEmbeddedStatement(*value.body, indent,
                                                   stmt.node.span.end.offset);
                } else if constexpr (std::is_same_v<T, AstForEachStmt>) {
                    if (value.label) {
                        append(tokenLexeme(*value.label));
                        append(": ");
                    }
                    append("for (");
                    append(value.isConst ? "const " : "var ");
                    append(tokenLexeme(value.name));
                    append(" ");
                    append(formatType(*value.declaredType));
                    append(" : ");
                    append(formatExpression(*value.iterable, indent));
                    append(")");
                    return formatEmbeddedStatement(*value.body, indent,
                                                   stmt.node.span.end.offset);
                }

                return false;
            },
            stmt.value);
    }

    bool formatClassBody(const AstClassDecl& classDecl, int indent) {
        append("{");

        struct OrderedMember {
            size_t startOffset = 0;
            bool isField = false;
            const AstFieldDecl* field = nullptr;
            const AstMethodDecl* method = nullptr;
        };

        std::vector<OrderedMember> members;
        members.reserve(classDecl.fields.size() + classDecl.methods.size());
        for (const auto& field : classDecl.fields) {
            members.push_back(
                OrderedMember{field.node.span.start.offset, true, &field, nullptr});
        }
        for (const auto& method : classDecl.methods) {
            members.push_back(
                OrderedMember{method.node.span.start.offset, false, nullptr, &method});
        }
        std::sort(members.begin(), members.end(),
                  [](const OrderedMember& lhs, const OrderedMember& rhs) {
                      return lhs.startOffset < rhs.startOffset;
                  });

        if (members.empty()) {
            if (!emitGap(classDecl.node.span.start.offset, classDecl.node.span.end.offset,
                         classDecl.node.span.start.line, classDecl.node.span.end.line,
                         indent + 1)) {
                return false;
            }
            append("}");
            return true;
        }

        for (size_t index = 0; index < members.size(); ++index) {
            const OrderedMember& member = members[index];
            const size_t startOffset =
                member.isField ? member.field->node.span.start.offset
                               : member.method->node.span.start.offset;
            const size_t startLine =
                member.isField ? member.field->node.span.start.line
                               : member.method->node.span.start.line;
            const size_t previousOffset =
                index == 0 ? classDecl.node.span.start.offset
                           : (members[index - 1].isField
                                  ? members[index - 1].field->node.span.end.offset
                                  : members[index - 1].method->node.span.end.offset);
            const size_t previousLine =
                index == 0 ? classDecl.node.span.start.line
                           : (members[index - 1].isField
                                  ? members[index - 1].field->node.span.end.line
                                  : members[index - 1].method->node.span.end.line);
            if (!emitGap(previousOffset, startOffset, previousLine, startLine,
                         indent + 1)) {
                return false;
            }
            ensureIndent(indent + 1);

            if (member.isField) {
                append(tokenLexeme(member.field->name));
                append(" ");
                append(formatType(*member.field->type));
            } else {
                for (int op : member.method->annotatedOperators) {
                    const std::string opText = annotationOperatorText(op);
                    if (opText.empty()) {
                        continue;
                    }
                    append("@operator(\"");
                    append(opText);
                    append("\")");
                    appendNewline();
                    ensureIndent(indent + 1);
                }

                append("fn ");
                append(tokenLexeme(member.method->name));
                append(formatParameterList(member.method->params));
                if (member.method->returnType) {
                    append(" ");
                    append(formatType(*member.method->returnType));
                }
                append(" ");
                const auto* bodyBlock = std::get_if<AstBlockStmt>(&member.method->body->value);
                if (bodyBlock == nullptr ||
                    !appendBlock(*bodyBlock, member.method->body->node.span,
                                 indent + 1)) {
                    failed = true;
                    return false;
                }
            }

        }

        const OrderedMember& lastMember = members.back();
        const size_t lastOffset =
            lastMember.isField ? lastMember.field->node.span.end.offset
                               : lastMember.method->node.span.end.offset;
        const size_t lastLine =
            lastMember.isField ? lastMember.field->node.span.end.line
                               : lastMember.method->node.span.end.line;
        if (!emitGap(lastOffset, classDecl.node.span.end.offset, lastLine,
                     classDecl.node.span.end.line, indent + 1)) {
            return false;
        }

        ensureIndent(indent);
        append("}");
        return true;
    }

    bool formatItem(const AstItem& item, int indent) {
        return formatItem(item, indent, true);
    }

    bool formatItem(const AstItem& item, int indent, bool preserveLeadingGap) {
        const Boundary span = itemBoundary(item);
        if (preserveLeadingGap &&
            !emitGap(span.startOffset, span.startOffset, span.startLine,
                     span.startLine, indent)) {
            return false;
        }
        ensureIndent(indent);

        return std::visit(
            [&](const auto& value) -> bool {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, AstTypeAliasDecl>) {
                    append("type ");
                    append(tokenLexeme(value.name));
                    append(" ");
                    append(formatType(*value.aliasedType));
                    return true;
                } else if constexpr (std::is_same_v<T, AstClassDecl>) {
                    append("type ");
                    append(tokenLexeme(value.name));
                    append(" struct");
                    if (value.superclass) {
                        append(" < ");
                        append(tokenLexeme(*value.superclass));
                    }
                    append(" ");
                    return formatClassBody(value, indent);
                } else if constexpr (std::is_same_v<T, AstFunctionDecl>) {
                    append("fn ");
                    append(tokenLexeme(value.name));
                    append(formatParameterList(value.params));
                    if (value.returnType) {
                        append(" ");
                        append(formatType(*value.returnType));
                    }
                    append(" ");
                    const auto* bodyBlock = std::get_if<AstBlockStmt>(&value.body->value);
                    if (bodyBlock == nullptr) {
                        failed = true;
                        return false;
                    }
                    return appendBlock(*bodyBlock, value.body->node.span, indent);
                } else if constexpr (std::is_same_v<T, AstStmtPtr>) {
                    return value && formatStatement(*value, indent);
                }

                return false;
            },
            item.value);
    }

    bool formatModule(const AstModule& module) {
        if (module.items.empty()) {
            if (!emitGap(0, source.size(), 1, 1, 0)) {
                return false;
            }
            return true;
        }

        const Boundary firstSpan = itemBoundary(*module.items.front());
        if (!emitGap(0, firstSpan.startOffset, 1, firstSpan.startLine, 0)) {
            return false;
        }

        for (size_t index = 0; index < module.items.size(); ++index) {
            if (!formatItem(*module.items[index], 0, false)) {
                return false;
            }

            const Boundary currentSpan = itemBoundary(*module.items[index]);
            const size_t nextOffset =
                index + 1 < module.items.size()
                    ? itemBoundary(*module.items[index + 1]).startOffset
                    : source.size();
            const size_t nextLine =
                index + 1 < module.items.size()
                    ? itemBoundary(*module.items[index + 1]).startLine
                    : analysis.frontend.terminalLine;
            if (!emitGap(currentSpan.endOffset, nextOffset, currentSpan.endLine,
                         nextLine, 0)) {
                return false;
            }
        }

        return true;
    }
};

struct PlainFormatter {
    Formatter& parent;

    explicit PlainFormatter(Formatter& formatter) : parent(formatter) {}

    bool formatStatement(const AstStmt& stmt, int indent, std::string& outText) {
        return std::visit(
            [&](const auto& value) -> bool {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, AstBlockStmt>) {
                    return formatBlock(stmt.node.span, value, indent, outText);
                } else if constexpr (std::is_same_v<T, AstExprStmt>) {
                    outText += indentText(indent) +
                               parent.formatExpression(*value.expression, indent) + "\n";
                    return true;
                } else if constexpr (std::is_same_v<T, AstPrintStmt>) {
                    outText += indentText(indent) + "print(" +
                               parent.formatExpression(*value.expression, indent) + ")\n";
                    return true;
                } else if constexpr (std::is_same_v<T, AstReturnStmt>) {
                    outText += indentText(indent) + "return";
                    if (value.value) {
                        outText += " " +
                                   parent.formatExpression(*value.value, indent);
                    }
                    outText += "\n";
                    return true;
                } else if constexpr (std::is_same_v<T, AstBreakStmt>) {
                    outText += indentText(indent) + "break";
                    if (value.label) {
                        outText += " " + tokenLexeme(*value.label);
                    }
                    outText += "\n";
                    return true;
                } else if constexpr (std::is_same_v<T, AstContinueStmt>) {
                    outText += indentText(indent) + "continue";
                    if (value.label) {
                        outText += " " + tokenLexeme(*value.label);
                    }
                    outText += "\n";
                    return true;
                } else if constexpr (std::is_same_v<T, AstVarDeclStmt>) {
                    outText += indentText(indent) + (value.isConst ? "const " : "var ");
                    outText += tokenLexeme(value.name);
                    if (!value.omittedType && value.declaredType) {
                        outText += " " + parent.formatType(*value.declaredType);
                    }
                    outText += " = " +
                               parent.formatExpression(*value.initializer, indent) + "\n";
                    return true;
                } else if constexpr (std::is_same_v<T, AstDestructuredImportStmt>) {
                    outText += indentText(indent) + (value.isConst ? "const {" : "var {");
                    for (size_t index = 0; index < value.bindings.size(); ++index) {
                        if (index == 0) {
                            outText += " ";
                        } else {
                            outText += ", ";
                        }
                        const auto& binding = value.bindings[index];
                        outText += tokenLexeme(binding.exportedName);
                        if (binding.localName) {
                            outText += " as " + tokenLexeme(*binding.localName);
                        }
                        if (binding.expectedType) {
                            outText += ": " + parent.formatType(*binding.expectedType);
                        }
                    }
                    if (!value.bindings.empty()) {
                        outText += " ";
                    }
                    outText += "} = " +
                               parent.formatExpression(*value.initializer, indent) + "\n";
                    return true;
                } else if constexpr (std::is_same_v<T, AstIfStmt>) {
                    outText += indentText(indent) + "if (" +
                               parent.formatExpression(*value.condition, indent) + ")";
                    if (std::holds_alternative<AstBlockStmt>(value.thenBranch->value)) {
                        outText += " ";
                        return formatStatement(*value.thenBranch, indent, outText) &&
                               appendElse(value, indent, outText);
                    }
                    outText += "\n";
                    if (!formatStatement(*value.thenBranch, indent + 1, outText)) {
                        return false;
                    }
                    return appendElse(value, indent, outText);
                } else if constexpr (std::is_same_v<T, AstWhileStmt>) {
                    outText += indentText(indent);
                    if (value.label) {
                        outText += tokenLexeme(*value.label) + ": ";
                    }
                    outText += "while (" +
                               parent.formatExpression(*value.condition, indent) + ")";
                    if (std::holds_alternative<AstBlockStmt>(value.body->value)) {
                        outText += " ";
                        return formatStatement(*value.body, indent, outText);
                    }
                    outText += "\n";
                    return formatStatement(*value.body, indent + 1, outText);
                } else if constexpr (std::is_same_v<T, AstForStmt>) {
                    outText += indentText(indent);
                    if (value.label) {
                        outText += tokenLexeme(*value.label) + ": ";
                    }
                    outText += "for (";
                    if (std::holds_alternative<std::unique_ptr<AstVarDeclStmt>>(
                            value.initializer)) {
                        const auto& initializer =
                            std::get<std::unique_ptr<AstVarDeclStmt>>(value.initializer);
                        outText += initializer->isConst ? "const " : "var ";
                        outText += tokenLexeme(initializer->name) + " " +
                                   parent.formatType(*initializer->declaredType) +
                                   " = " +
                                   parent.formatExpression(*initializer->initializer, indent);
                    } else if (std::holds_alternative<AstExprPtr>(value.initializer)) {
                        outText += parent.formatExpression(
                            *std::get<AstExprPtr>(value.initializer), indent);
                    }
                    outText += ";";
                    if (value.condition) {
                        outText += " " +
                                   parent.formatExpression(*value.condition, indent);
                    }
                    outText += ";";
                    if (value.increment) {
                        outText += " " +
                                   parent.formatExpression(*value.increment, indent);
                    }
                    outText += ")";
                    if (std::holds_alternative<AstBlockStmt>(value.body->value)) {
                        outText += " ";
                        return formatStatement(*value.body, indent, outText);
                    }
                    outText += "\n";
                    return formatStatement(*value.body, indent + 1, outText);
                } else if constexpr (std::is_same_v<T, AstForEachStmt>) {
                    outText += indentText(indent);
                    if (value.label) {
                        outText += tokenLexeme(*value.label) + ": ";
                    }
                    outText += "for (";
                    outText += value.isConst ? "const " : "var ";
                    outText += tokenLexeme(value.name) + " " +
                               parent.formatType(*value.declaredType) + " : " +
                               parent.formatExpression(*value.iterable, indent) + ")";
                    if (std::holds_alternative<AstBlockStmt>(value.body->value)) {
                        outText += " ";
                        return formatStatement(*value.body, indent, outText);
                    }
                    outText += "\n";
                    return formatStatement(*value.body, indent + 1, outText);
                }

                return false;
            },
            stmt.value);
    }

    bool appendElse(const AstIfStmt& ifStmt, int indent, std::string& outText) {
        if (!ifStmt.elseBranch) {
            return true;
        }
        if (!outText.empty() && outText.back() == '\n') {
            outText += indentText(indent);
        } else {
            outText += " ";
        }
        outText += "else";
        if (std::holds_alternative<AstBlockStmt>(ifStmt.elseBranch->value)) {
            outText += " ";
            return formatStatement(*ifStmt.elseBranch, indent, outText);
        }
        outText += "\n";
        return formatStatement(*ifStmt.elseBranch, indent + 1, outText);
    }

    bool formatBlock(const SourceSpan& span, const AstBlockStmt& block, int indent,
                     std::string& outText) {
        (void)span;
        outText += "{";
        if (block.items.empty()) {
            outText += "}\n";
            return true;
        }
        outText += "\n";
        for (const auto& item : block.items) {
            if (!formatItem(*item, indent + 1, outText)) {
                return false;
            }
        }
        outText += indentText(indent) + "}\n";
        return true;
    }

    bool formatItem(const AstItem& item, int indent, std::string& outText) {
        return std::visit(
            [&](const auto& value) -> bool {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, AstTypeAliasDecl>) {
                    outText += indentText(indent) + "type " +
                               tokenLexeme(value.name) + " " +
                               parent.formatType(*value.aliasedType) + "\n";
                    return true;
                } else if constexpr (std::is_same_v<T, AstClassDecl>) {
                    outText += indentText(indent) + "type " +
                               tokenLexeme(value.name) + " struct";
                    if (value.superclass) {
                        outText += " < " + tokenLexeme(*value.superclass);
                    }
                    outText += " {\n";
                    std::vector<std::pair<size_t, std::string>> members;
                    for (const auto& field : value.fields) {
                        members.push_back({field.node.span.start.offset,
                                           indentText(indent + 1) +
                                               tokenLexeme(field.name) + " " +
                                               parent.formatType(*field.type) + "\n"});
                    }
                    for (const auto& method : value.methods) {
                        std::string methodText;
                        for (int op : method.annotatedOperators) {
                            const std::string opText = annotationOperatorText(op);
                            if (!opText.empty()) {
                                methodText += indentText(indent + 1) +
                                              "@operator(\"" + opText + "\")\n";
                            }
                        }
                        methodText += indentText(indent + 1) + "fn " +
                                      tokenLexeme(method.name) +
                                      parent.formatParameterList(method.params);
                        if (method.returnType) {
                            methodText += " " + parent.formatType(*method.returnType);
                        }
                        methodText += " ";
                        if (!formatStatement(*method.body, indent + 1, methodText)) {
                            return false;
                        }
                        members.push_back({method.node.span.start.offset,
                                           std::move(methodText)});
                    }
                    std::sort(members.begin(), members.end(),
                              [](const auto& lhs, const auto& rhs) {
                                  return lhs.first < rhs.first;
                              });
                    for (const auto& [offset, text] : members) {
                        (void)offset;
                        outText += text;
                    }
                    outText += indentText(indent) + "}\n";
                    return true;
                } else if constexpr (std::is_same_v<T, AstFunctionDecl>) {
                    outText += indentText(indent) + "fn " +
                               tokenLexeme(value.name) +
                               parent.formatParameterList(value.params);
                    if (value.returnType) {
                        outText += " " + parent.formatType(*value.returnType);
                    }
                    outText += " ";
                    return formatStatement(*value.body, indent, outText);
                } else if constexpr (std::is_same_v<T, AstStmtPtr>) {
                    return value && formatStatement(*value, indent, outText);
                }
                return false;
            },
            item.value);
    }
};

std::string Formatter::formatExpression(const AstExpr& expr, int indent,
                                        int parentPrecedence, bool rightChild,
                                        bool parentRightAssociative) {
    const bool needsParens =
        parentPrecedence != 0 &&
        expressionNeedsParens(expr, parentPrecedence, rightChild,
                              parentRightAssociative);

    std::string text = std::visit(
        [&](const auto& value) -> std::string {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, AstLiteralExpr>) {
                return tokenLexeme(value.token);
            } else if constexpr (std::is_same_v<T, AstIdentifierExpr>) {
                if (value.constructorType) {
                    return formatType(*value.constructorType);
                }
                return tokenLexeme(value.name);
            } else if constexpr (std::is_same_v<T, AstGroupingExpr>) {
                if (groupingHasPreservedMultilineLayout(value, expr.node.span)) {
                    return formatMultilineGroupingExpression(value, expr.node.span,
                                                             indent);
                }
                return "(" + formatExpression(*value.expression, indent) + ")";
            } else if constexpr (std::is_same_v<T, AstUnaryExpr>) {
                return tokenLexeme(value.op) +
                       formatExpression(*value.operand, indent, 13, true, true);
            } else if constexpr (std::is_same_v<T, AstUpdateExpr>) {
                if (value.isPrefix) {
                    return tokenLexeme(value.op) +
                           formatExpression(*value.operand, indent, 13, true, true);
                }
                return formatExpression(*value.operand, indent, 14) +
                       tokenLexeme(value.op);
            } else if constexpr (std::is_same_v<T, AstBinaryExpr>) {
                const int precedence = precedenceForBinaryOperator(value.op.type());
                return formatExpression(*value.left, indent, precedence, false, false) +
                       " " + tokenLexeme(value.op) + " " +
                       formatExpression(*value.right, indent, precedence, true, false);
            } else if constexpr (std::is_same_v<T, AstAssignmentExpr>) {
                return formatExpression(*value.target, indent, 1, false, true) +
                       " " + tokenLexeme(value.op) + " " +
                       formatExpression(*value.value, indent, 1, true, true);
            } else if constexpr (std::is_same_v<T, AstCallExpr>) {
                if (callHasPreservedMultilineLayout(value, expr.node.span)) {
                    return formatMultilineCallExpression(value, expr.node.span,
                                                         indent);
                }
                std::string result =
                    formatExpression(*value.callee, indent, 14);
                result += "(";
                for (size_t index = 0; index < value.arguments.size(); ++index) {
                    if (index != 0) {
                        result += ", ";
                    }
                    result += formatExpression(*value.arguments[index], indent);
                }
                result += ")";
                return result;
            } else if constexpr (std::is_same_v<T, AstMemberExpr>) {
                return formatExpression(*value.object, indent, 14) + "." +
                       tokenLexeme(value.member);
            } else if constexpr (std::is_same_v<T, AstIndexExpr>) {
                return formatExpression(*value.object, indent, 14) + "[" +
                       formatExpression(*value.index, indent) + "]";
            } else if constexpr (std::is_same_v<T, AstCastExpr>) {
                return formatExpression(*value.expression, indent, 12, false, false) +
                       " as " + formatType(*value.targetType);
            } else if constexpr (std::is_same_v<T, AstFunctionExpr>) {
                std::string result = "fn";
                result += formatParameterList(value.params);
                if (value.usesFatArrow) {
                    result += " => ";
                    if (value.blockBody) {
                        PlainFormatter plain(*this);
                        std::string blockText;
                        if (!plain.formatStatement(*value.blockBody, indent, blockText)) {
                            failed = true;
                            return std::string();
                        }
                        if (!blockText.empty() && blockText.back() == '\n') {
                            blockText.pop_back();
                        }
                        result += blockText;
                    } else if (value.expressionBody) {
                        result += formatExpression(*value.expressionBody, indent);
                    }
                    return result;
                }
                if (value.returnType) {
                    result += " ";
                    result += formatType(*value.returnType);
                }
                result += " ";
                if (!value.blockBody) {
                    failed = true;
                    return std::string();
                }
                PlainFormatter plain(*this);
                std::string blockText;
                if (!plain.formatStatement(*value.blockBody, indent, blockText)) {
                    failed = true;
                    return std::string();
                }
                if (!blockText.empty() && blockText.back() == '\n') {
                    blockText.pop_back();
                }
                result += blockText;
                return result;
            } else if constexpr (std::is_same_v<T, AstImportExpr>) {
                return "@import(" + tokenLexeme(value.path) + ")";
            } else if constexpr (std::is_same_v<T, AstThisExpr>) {
                return tokenLexeme(value.token);
            } else if constexpr (std::is_same_v<T, AstSuperExpr>) {
                return tokenLexeme(value.token);
            } else if constexpr (std::is_same_v<T, AstArrayLiteralExpr>) {
                if (arrayHasPreservedMultilineLayout(value, expr.node.span)) {
                    return formatMultilineArrayLiteral(value, expr.node.span,
                                                       indent);
                }
                std::string result = "[";
                for (size_t index = 0; index < value.elements.size(); ++index) {
                    if (index != 0) {
                        result += ", ";
                    }
                    result += formatExpression(*value.elements[index], indent);
                }
                result += "]";
                return result;
            } else if constexpr (std::is_same_v<T, AstDictLiteralExpr>) {
                if (dictHasPreservedMultilineLayout(value, expr.node.span)) {
                    return formatMultilineDictLiteral(value, expr.node.span,
                                                      indent);
                }
                std::string result = "{";
                for (size_t index = 0; index < value.entries.size(); ++index) {
                    if (index != 0) {
                        result += ", ";
                    }
                    result += formatExpression(*value.entries[index].key, indent);
                    result += ": ";
                    result += formatExpression(*value.entries[index].value, indent);
                }
                result += "}";
                return result;
            }

            return std::string();
        },
        expr.value);

    if (needsParens) {
        return "(" + text + ")";
    }
    return text;
}

}  // namespace

std::optional<std::string> formatDocumentForTooling(
    std::string_view source, const ToolingDocumentAnalysis& analysis) {
    Formatter formatter{source, analysis};
    if (!formatter.run() || formatter.failed) {
        return std::nullopt;
    }
    return formatter.output;
}
