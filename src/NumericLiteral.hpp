#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Token.hpp"
#include "TypeInfo.hpp"

struct NumericLiteralInfo {
    std::string core;
    TypeRef type;
    bool isUnsigned = false;
    bool isFloat = false;
    bool valid = false;
};

std::string stripNumericSuffix(std::string_view text);
NumericLiteralInfo parseNumericLiteralInfo(std::string_view literal);
bool parseSignedIntegerLiteral(const Token& token, int64_t& outValue);
bool parseUnsignedIntegerLiteral(const Token& token, uint64_t& outValue);
bool parseFloatLiteral(const Token& token, double& outValue);
std::string integerSuffix(TypeKind kind);
