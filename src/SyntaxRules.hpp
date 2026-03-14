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
