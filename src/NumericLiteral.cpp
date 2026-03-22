#include "NumericLiteral.hpp"

#include <cmath>
#include <exception>

std::string stripNumericSuffix(std::string_view textView) {
    std::string text(textView);
    auto strip = [&](std::string_view suffix) -> bool {
        if (text.size() < suffix.size()) {
            return false;
        }
        if (text.compare(text.size() - suffix.size(), suffix.size(),
                         suffix.data()) != 0) {
            return false;
        }
        text.resize(text.size() - suffix.size());
        return true;
    };

    strip("usize") || strip("i16") || strip("i32") || strip("i64") ||
        strip("u16") || strip("u32") || strip("u64") || strip("f32") ||
        strip("f64") || strip("i8") || strip("u8") || strip("u");
    return text;
}

NumericLiteralInfo parseNumericLiteralInfo(std::string_view literalView) {
    const std::string literal(literalView);

    NumericLiteralInfo info;
    info.core = literal;
    info.valid = true;

    auto assignSuffix = [&](size_t suffixLength, const TypeRef& suffixType,
                            bool isUnsigned, bool isFloat) {
        info.type = suffixType;
        info.isUnsigned = isUnsigned;
        info.isFloat = isFloat;
        info.core = literal.substr(0, literal.length() - suffixLength);
    };

    if (literal.size() >= 5 &&
        literal.compare(literal.size() - 5, 5, "usize") == 0) {
        assignSuffix(5, TypeInfo::makeUSize(), true, false);
    } else if (literal.size() >= 3 &&
               literal.compare(literal.size() - 3, 3, "i16") == 0) {
        assignSuffix(3, TypeInfo::makeI16(), false, false);
    } else if (literal.size() >= 3 &&
               literal.compare(literal.size() - 3, 3, "i32") == 0) {
        assignSuffix(3, TypeInfo::makeI32(), false, false);
    } else if (literal.size() >= 3 &&
               literal.compare(literal.size() - 3, 3, "i64") == 0) {
        assignSuffix(3, TypeInfo::makeI64(), false, false);
    } else if (literal.size() >= 3 &&
               literal.compare(literal.size() - 3, 3, "u16") == 0) {
        assignSuffix(3, TypeInfo::makeU16(), true, false);
    } else if (literal.size() >= 3 &&
               literal.compare(literal.size() - 3, 3, "u32") == 0) {
        assignSuffix(3, TypeInfo::makeU32(), true, false);
    } else if (literal.size() >= 3 &&
               literal.compare(literal.size() - 3, 3, "u64") == 0) {
        assignSuffix(3, TypeInfo::makeU64(), true, false);
    } else if (literal.size() >= 3 &&
               literal.compare(literal.size() - 3, 3, "f32") == 0) {
        assignSuffix(3, TypeInfo::makeF32(), false, true);
    } else if (literal.size() >= 3 &&
               literal.compare(literal.size() - 3, 3, "f64") == 0) {
        assignSuffix(3, TypeInfo::makeF64(), false, true);
    } else if (literal.size() >= 2 &&
               literal.compare(literal.size() - 2, 2, "i8") == 0) {
        assignSuffix(2, TypeInfo::makeI8(), false, false);
    } else if (literal.size() >= 2 &&
               literal.compare(literal.size() - 2, 2, "u8") == 0) {
        assignSuffix(2, TypeInfo::makeU8(), true, false);
    } else if (!literal.empty() && literal.back() == 'u') {
        assignSuffix(1, TypeInfo::makeU32(), true, false);
    }

    if (info.core.empty()) {
        info.valid = false;
        return info;
    }

    const bool hasDecimalPoint = info.core.find('.') != std::string::npos;
    if (!info.type) {
        if (hasDecimalPoint) {
            info.type = TypeInfo::makeF64();
            info.isFloat = true;
        } else {
            info.type = TypeInfo::makeI32();
        }
    }

    if (info.type->isFloat()) {
        info.isFloat = true;
    }

    if (info.type->isInteger() && hasDecimalPoint) {
        info.valid = false;
    }

    if (info.isFloat && info.isUnsigned) {
        info.valid = false;
    }

    return info;
}

bool parseSignedIntegerLiteral(const Token& token, int64_t& outValue) {
    if (token.type() != TokenType::NUMBER) {
        return false;
    }

    try {
        const std::string text = stripNumericSuffix(tokenLexeme(token));
        size_t parsed = 0;
        const long long value = std::stoll(text, &parsed, 10);
        if (parsed != text.size()) {
            return false;
        }
        outValue = static_cast<int64_t>(value);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parseUnsignedIntegerLiteral(const Token& token, uint64_t& outValue) {
    if (token.type() != TokenType::NUMBER) {
        return false;
    }

    try {
        const std::string text = stripNumericSuffix(tokenLexeme(token));
        size_t parsed = 0;
        const unsigned long long value = std::stoull(text, &parsed, 10);
        if (parsed != text.size()) {
            return false;
        }
        outValue = static_cast<uint64_t>(value);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parseFloatLiteral(const Token& token, double& outValue) {
    if (token.type() != TokenType::NUMBER) {
        return false;
    }

    try {
        const std::string text = stripNumericSuffix(tokenLexeme(token));
        size_t parsed = 0;
        const double value = std::stod(text, &parsed);
        if (parsed != text.size() || !std::isfinite(value)) {
            return false;
        }
        outValue = value;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string integerSuffix(TypeKind kind) {
    switch (kind) {
        case TypeKind::I8:
            return "i8";
        case TypeKind::I16:
            return "i16";
        case TypeKind::I32:
            return "i32";
        case TypeKind::I64:
            return "i64";
        case TypeKind::U8:
            return "u8";
        case TypeKind::U16:
            return "u16";
        case TypeKind::U32:
            return "u32";
        case TypeKind::U64:
            return "u64";
        case TypeKind::USIZE:
            return "usize";
        default:
            return "";
    }
}
