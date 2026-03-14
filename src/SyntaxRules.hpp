#pragma once

#include <string_view>

#include "Token.hpp"

inline bool isCollectionTypeNameText(std::string_view name) {
    return name == "Array" || name == "Dict" || name == "Set";
}

inline bool isHandleTypeNameText(std::string_view name) {
    return name == "handle";
}

inline bool isPublicSymbolName(std::string_view name) {
    return !name.empty() && name.front() >= 'A' && name.front() <= 'Z';
}

inline int parseOperatorAnnotationToken(std::string_view op) {
    if (op == "+") return TokenType::PLUS;
    if (op == "-") return TokenType::MINUS;
    if (op == "*") return TokenType::STAR;
    if (op == "/") return TokenType::SLASH;
    if (op == "==") return TokenType::EQUAL_EQUAL;
    if (op == "!=") return TokenType::BANG_EQUAL;
    if (op == "<") return TokenType::LESS;
    if (op == "<=") return TokenType::LESS_EQUAL;
    if (op == ">") return TokenType::GREATER;
    if (op == ">=") return TokenType::GREATER_EQUAL;
    return -1;
}

inline bool isLineContinuationToken(TokenType type) {
    switch (type) {
        case TokenType::OPEN_PAREN:
        case TokenType::OPEN_BRACKET:
        case TokenType::DOT:
        case TokenType::AS_KW:
        case TokenType::PLUS:
        case TokenType::MINUS:
        case TokenType::STAR:
        case TokenType::SLASH:
        case TokenType::AMPERSAND:
        case TokenType::PIPE:
        case TokenType::CARET:
        case TokenType::LOGICAL_AND:
        case TokenType::LOGICAL_OR:
        case TokenType::EQUAL_EQUAL:
        case TokenType::BANG_EQUAL:
        case TokenType::LESS:
        case TokenType::LESS_EQUAL:
        case TokenType::GREATER:
        case TokenType::GREATER_EQUAL:
        case TokenType::SHIFT_LEFT_TOKEN:
        case TokenType::SHIFT_RIGHT_TOKEN:
        case TokenType::EQUAL:
        case TokenType::PLUS_EQUAL:
        case TokenType::MINUS_EQUAL:
        case TokenType::STAR_EQUAL:
        case TokenType::SLASH_EQUAL:
        case TokenType::AMPERSAND_EQUAL:
        case TokenType::CARET_EQUAL:
        case TokenType::PIPE_EQUAL:
        case TokenType::SHIFT_LEFT_EQUAL:
        case TokenType::SHIFT_RIGHT_EQUAL:
            return true;
        default:
            return false;
    }
}

inline std::string_view continuationTokenText(TokenType type) {
    switch (type) {
        case TokenType::OPEN_PAREN:
            return "(";
        case TokenType::OPEN_BRACKET:
            return "[";
        case TokenType::DOT:
            return ".";
        case TokenType::AS_KW:
            return "as";
        case TokenType::PLUS:
            return "+";
        case TokenType::MINUS:
            return "-";
        case TokenType::STAR:
            return "*";
        case TokenType::SLASH:
            return "/";
        case TokenType::AMPERSAND:
            return "&";
        case TokenType::PIPE:
            return "|";
        case TokenType::CARET:
            return "^";
        case TokenType::LOGICAL_AND:
            return "&&";
        case TokenType::LOGICAL_OR:
            return "||";
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
        case TokenType::SHIFT_LEFT_TOKEN:
            return "<<";
        case TokenType::SHIFT_RIGHT_TOKEN:
            return ">>";
        case TokenType::EQUAL:
            return "=";
        case TokenType::PLUS_EQUAL:
            return "+=";
        case TokenType::MINUS_EQUAL:
            return "-=";
        case TokenType::STAR_EQUAL:
            return "*=";
        case TokenType::SLASH_EQUAL:
            return "/=";
        case TokenType::AMPERSAND_EQUAL:
            return "&=";
        case TokenType::CARET_EQUAL:
            return "^=";
        case TokenType::PIPE_EQUAL:
            return "|=";
        case TokenType::SHIFT_LEFT_EQUAL:
            return "<<=";
        case TokenType::SHIFT_RIGHT_EQUAL:
            return ">>=";
        default:
            return "?";
    }
}
