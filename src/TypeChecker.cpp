#include "TypeChecker.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "NativePackage.hpp"
#include "Scanner.hpp"
#include "SyntaxRules.hpp"

namespace {

bool isTypeToken(TokenType type) {
    switch (type) {
        case TokenType::TYPE_I8:
        case TokenType::TYPE_I16:
        case TokenType::TYPE_I32:
        case TokenType::TYPE_I64:
        case TokenType::TYPE_U8:
        case TokenType::TYPE_U16:
        case TokenType::TYPE_U32:
        case TokenType::TYPE_U64:
        case TokenType::TYPE_USIZE:
        case TokenType::TYPE_F32:
        case TokenType::TYPE_F64:
        case TokenType::TYPE_BOOL:
        case TokenType::TYPE_STR:
        case TokenType::TYPE_FN:
        case TokenType::TYPE_VOID:
        case TokenType::TYPE_NULL_KW:
            return true;
        default:
            return false;
    }
}

bool isAssignmentOperator(TokenType type) {
    switch (type) {
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
        case TokenType::PLUS_PLUS:
        case TokenType::MINUS_MINUS:
            return true;
        default:
            return false;
    }
}

bool isComparisonOperator(TokenType type) {
    switch (type) {
        case TokenType::GREATER:
        case TokenType::GREATER_EQUAL:
        case TokenType::LESS:
        case TokenType::LESS_EQUAL:
            return true;
        default:
            return false;
    }
}

bool isEqualityOperator(TokenType type) {
    return type == TokenType::EQUAL_EQUAL || type == TokenType::BANG_EQUAL;
}

std::string lineLeadingContinuationMessage(TokenType type) {
    return "Type error: Continuation token '" +
           std::string(continuationTokenText(type)) +
           "' must stay on the previous line.";
}

bool isArithmeticCompoundAssignment(TokenType type) {
    switch (type) {
        case TokenType::PLUS_EQUAL:
        case TokenType::MINUS_EQUAL:
        case TokenType::STAR_EQUAL:
        case TokenType::SLASH_EQUAL:
            return true;
        default:
            return false;
    }
}

bool isBitwiseCompoundAssignment(TokenType type) {
    switch (type) {
        case TokenType::AMPERSAND_EQUAL:
        case TokenType::CARET_EQUAL:
        case TokenType::PIPE_EQUAL:
            return true;
        default:
            return false;
    }
}

class CheckerImpl {
   private:
    struct ExprInfo {
        TypeRef type = TypeInfo::makeAny();
        bool isAssignable = false;
        bool isClassSymbol = false;
        std::string name;
        size_t line = 0;
        bool isConstSymbol = false;
    };

    struct SymbolInfo {
        TypeRef type = TypeInfo::makeAny();
        bool isConst = false;
    };

    struct FunctionCtx {
        TypeRef returnType;
    };

    struct ClassCtx {
        std::string className;
    };

    Scanner m_scanner;
    Token m_current;
    Token m_previous;
    std::deque<Token> m_bufferedTokens;

    const std::unordered_set<std::string>& m_classNames;
    std::unordered_map<std::string, TypeRef> m_typeAliases;
    const std::unordered_map<std::string, TypeRef>& m_functionSignatures;
    std::vector<TypeError>& m_errors;

    std::vector<std::unordered_map<std::string, SymbolInfo>> m_scopes;
    std::unordered_set<std::string> m_declaredGlobalSymbols;
    std::vector<TypeCheckerDeclarationType> m_declarationTypes;
    std::unordered_map<std::string, std::string> m_superclassOf;
    std::unordered_map<std::string, std::unordered_map<std::string, TypeRef>>
        m_classFieldTypes;
    std::unordered_map<std::string, std::unordered_map<std::string, TypeRef>>
        m_classMethodSignatures;
    std::unordered_map<std::string, std::unordered_map<int, std::string>>
        m_classOperatorMethods;
    std::vector<FunctionCtx> m_functionContexts;
    std::vector<ClassCtx> m_classContexts;

    std::string tokenText(const Token& token) const {
        return std::string(token.start(), token.length());
    }

    bool isCollectionTypeNameToken(const Token& token) const {
        if (token.type() != TokenType::IDENTIFIER) {
            return false;
        }
        return isCollectionTypeNameText(tokenText(token));
    }

    bool isTypedTypeAnnotationStart() {
        if (check(TokenType::TYPE_FN)) {
            return peekToken().type() == TokenType::OPEN_PAREN;
        }

        if (isTypeToken(m_current.type())) {
            return true;
        }

        if (!check(TokenType::IDENTIFIER)) {
            return false;
        }

        Token lookahead = peekToken();
        if (isHandleTypeNameText(tokenText(m_current))) {
            return lookahead.type() == TokenType::LESS;
        }

        if (isCollectionTypeNameToken(m_current)) {
            return lookahead.type() == TokenType::LESS;
        }

        std::string typeName = tokenText(m_current);
        return m_typeAliases.find(typeName) != m_typeAliases.end() ||
               m_classNames.find(typeName) != m_classNames.end();
    }

    TypeRef inferNumberLiteralType(const Token& token) {
        std::string literal = tokenText(token);
        std::string core = literal;

        TypeRef inferredType = nullptr;
        bool unsignedLiteral = false;
        bool floatLiteral = false;

        auto assignSuffix = [&](size_t suffixLength, const TypeRef& type,
                                bool isUnsigned, bool isFloat) {
            inferredType = type;
            unsignedLiteral = isUnsigned;
            floatLiteral = isFloat;
            core = literal.substr(0, literal.length() - suffixLength);
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

        if (core.empty()) {
            addError(token.line(),
                     "Type error: invalid numeric literal '" + literal + "'.");
            return TypeInfo::makeAny();
        }

        const bool hasDecimal = core.find('.') != std::string::npos;
        if (!inferredType) {
            if (hasDecimal) {
                inferredType = TypeInfo::makeF64();
                floatLiteral = true;
            } else {
                inferredType = TypeInfo::makeI32();
            }
        }

        if (inferredType->isFloat()) {
            floatLiteral = true;
        }

        if (inferredType->isInteger() && hasDecimal) {
            addError(token.line(), "Type error: integer literal '" + literal +
                                       "' cannot contain a decimal point.");
            return TypeInfo::makeAny();
        }

        try {
            if (floatLiteral) {
                (void)std::stod(core);
                return inferredType;
            }

            if (unsignedLiteral) {
                unsigned long long value = std::stoull(core);
                unsigned long long maxValue =
                    std::numeric_limits<uint64_t>::max();
                switch (inferredType->kind) {
                    case TypeKind::U8:
                        maxValue = std::numeric_limits<uint8_t>::max();
                        break;
                    case TypeKind::U16:
                        maxValue = std::numeric_limits<uint16_t>::max();
                        break;
                    case TypeKind::U32:
                        maxValue = std::numeric_limits<uint32_t>::max();
                        break;
                    case TypeKind::U64:
                    case TypeKind::USIZE:
                        maxValue = std::numeric_limits<uint64_t>::max();
                        break;
                    default:
                        break;
                }

                if (value > maxValue) {
                    addError(token.line(), "Type error: integer literal '" +
                                               literal +
                                               "' is out of range for type '" +
                                               inferredType->toString() + "'.");
                    return TypeInfo::makeAny();
                }

                return inferredType;
            }

            long long value = std::stoll(core);
            long long minValue = std::numeric_limits<int64_t>::min();
            long long maxValue = std::numeric_limits<int64_t>::max();
            switch (inferredType->kind) {
                case TypeKind::I8:
                    minValue = std::numeric_limits<int8_t>::min();
                    maxValue = std::numeric_limits<int8_t>::max();
                    break;
                case TypeKind::I16:
                    minValue = std::numeric_limits<int16_t>::min();
                    maxValue = std::numeric_limits<int16_t>::max();
                    break;
                case TypeKind::I32:
                    minValue = std::numeric_limits<int32_t>::min();
                    maxValue = std::numeric_limits<int32_t>::max();
                    break;
                case TypeKind::I64:
                    minValue = std::numeric_limits<int64_t>::min();
                    maxValue = std::numeric_limits<int64_t>::max();
                    break;
                default:
                    break;
            }

            if (value < minValue || value > maxValue) {
                addError(token.line(), "Type error: integer literal '" +
                                           literal +
                                           "' is out of range for type '" +
                                           inferredType->toString() + "'.");
                return TypeInfo::makeAny();
            }
        } catch (...) {
            addError(token.line(),
                     "Type error: invalid numeric literal '" + literal + "'.");
            return TypeInfo::makeAny();
        }

        return inferredType;
    }

    TypeRef mergeInferredTypes(const TypeRef& lhs, const TypeRef& rhs) const {
        if (!lhs) return rhs;
        if (!rhs) return lhs;
        if (lhs->isAny() || rhs->isAny()) {
            return TypeInfo::makeAny();
        }
        if (lhs->isNumeric() && rhs->isNumeric()) {
            TypeRef promoted = numericPromotion(lhs, rhs);
            return promoted ? promoted : TypeInfo::makeAny();
        }
        if (isAssignableType(rhs, lhs)) {
            return lhs;
        }
        if (isAssignableType(lhs, rhs)) {
            return rhs;
        }
        return TypeInfo::makeAny();
    }

    TypeRef bitwiseIntegerResultType(const TypeRef& lhs,
                                     const TypeRef& rhs) const {
        if (!lhs || !rhs || !lhs->isInteger() || !rhs->isInteger()) {
            return nullptr;
        }

        int width = std::max(lhs->bitWidth(), rhs->bitWidth());
        if (lhs->isSigned() && rhs->isSigned()) {
            switch (width) {
                case 8:
                    return TypeInfo::makeI8();
                case 16:
                    return TypeInfo::makeI16();
                case 32:
                    return TypeInfo::makeI32();
                default:
                    return TypeInfo::makeI64();
            }
        }

        switch (width) {
            case 8:
                return TypeInfo::makeU8();
            case 16:
                return TypeInfo::makeU16();
            case 32:
                return TypeInfo::makeU32();
            default:
                return TypeInfo::makeU64();
        }
    }

    void addError(size_t line, const std::string& message) {
        m_errors.push_back(TypeError{line, message});
    }

    Token nextToken() {
        if (!m_bufferedTokens.empty()) {
            Token token = m_bufferedTokens.front();
            m_bufferedTokens.pop_front();
            return token;
        }
        return m_scanner.nextToken();
    }

    Token peekToken(size_t offset = 1) {
        while (m_bufferedTokens.size() < offset) {
            m_bufferedTokens.push_back(m_scanner.nextToken());
        }
        return m_bufferedTokens[offset - 1];
    }

    Token tokenAt(size_t offset) {
        if (offset == 0) {
            return m_current;
        }
        return peekToken(offset);
    }

    bool parseTypeLookahead(size_t& offset) {
        auto consumeOptionalSuffix = [&]() {
            while (tokenAt(offset).type() == TokenType::QUESTION) {
                ++offset;
            }
        };

        Token current = tokenAt(offset);
        if (current.type() == TokenType::TYPE_FN) {
            ++offset;
            if (tokenAt(offset).type() != TokenType::OPEN_PAREN) {
                return false;
            }

            ++offset;
            if (tokenAt(offset).type() != TokenType::CLOSE_PAREN) {
                while (true) {
                    if (!parseTypeLookahead(offset)) {
                        return false;
                    }
                    if (tokenAt(offset).type() == TokenType::COMMA) {
                        ++offset;
                        continue;
                    }
                    if (tokenAt(offset).type() != TokenType::CLOSE_PAREN) {
                        return false;
                    }
                    break;
                }
            }

            ++offset;
            if (!parseTypeLookahead(offset)) {
                return false;
            }

            consumeOptionalSuffix();
            return true;
        }

        if (isTypeToken(current.type())) {
            ++offset;
            consumeOptionalSuffix();
            return true;
        }

        if (current.type() != TokenType::IDENTIFIER) {
            return false;
        }

        std::string_view name(current.start(), current.length());
        ++offset;

        if (isHandleTypeNameText(name)) {
            if (tokenAt(offset).type() != TokenType::LESS) {
                return false;
            }
            ++offset;

            if (tokenAt(offset).type() != TokenType::IDENTIFIER) {
                return false;
            }
            std::string_view packageNamespace(tokenAt(offset).start(),
                                              tokenAt(offset).length());
            ++offset;
            if (tokenAt(offset).type() != TokenType::COLON ||
                !isValidPackageIdPart(packageNamespace)) {
                return false;
            }
            ++offset;

            if (tokenAt(offset).type() != TokenType::IDENTIFIER) {
                return false;
            }
            std::string_view packageName(tokenAt(offset).start(),
                                         tokenAt(offset).length());
            ++offset;
            if (tokenAt(offset).type() != TokenType::COLON ||
                !isValidPackageIdPart(packageName)) {
                return false;
            }
            ++offset;

            if (tokenAt(offset).type() != TokenType::IDENTIFIER) {
                return false;
            }
            std::string_view typeName(tokenAt(offset).start(),
                                      tokenAt(offset).length());
            ++offset;
            if (tokenAt(offset).type() != TokenType::GREATER ||
                !isValidHandleTypeName(typeName)) {
                return false;
            }
            ++offset;
            consumeOptionalSuffix();
            return true;
        }

        if (isCollectionTypeNameText(name) &&
            tokenAt(offset).type() == TokenType::LESS) {
            ++offset;

            if (name == "Array" || name == "Set") {
                if (!parseTypeLookahead(offset) ||
                    tokenAt(offset).type() != TokenType::GREATER) {
                    return false;
                }
                ++offset;
                consumeOptionalSuffix();
                return true;
            }

            if (!parseTypeLookahead(offset) ||
                tokenAt(offset).type() != TokenType::COMMA) {
                return false;
            }
            ++offset;

            if (!parseTypeLookahead(offset) ||
                tokenAt(offset).type() != TokenType::GREATER) {
                return false;
            }
            ++offset;
            consumeOptionalSuffix();
            return true;
        }

        auto aliasIt = m_typeAliases.find(std::string(name));
        if (aliasIt != m_typeAliases.end() ||
            m_classNames.find(std::string(name)) != m_classNames.end()) {
            consumeOptionalSuffix();
            return true;
        }

        return false;
    }

    bool looksLikeFunctionTypeDeclarationStart() {
        if (!check(TokenType::TYPE_FN)) {
            return false;
        }

        size_t offset = 0;
        if (!parseTypeLookahead(offset)) {
            return false;
        }

        return tokenAt(offset).type() == TokenType::IDENTIFIER;
    }

    bool hasLineBreakBeforeCurrent() const {
        return m_previous.line() != 0 && m_current.line() > m_previous.line();
    }

    bool matchSameLine(TokenType type) {
        if (!check(type) || hasLineBreakBeforeCurrent()) {
            return false;
        }
        advance();
        return true;
    }

    bool rejectUnexpectedTrailingToken(
        std::initializer_list<TokenType> allowedTerminators = {}) {
        if (check(TokenType::END_OF_FILE) || hasLineBreakBeforeCurrent() ||
            check(TokenType::SEMI_COLON)) {
            return false;
        }

        for (TokenType terminator : allowedTerminators) {
            if (check(terminator)) {
                return false;
            }
        }

        addError(m_current.line(), "Type error: unexpected token.");

        while (!check(TokenType::END_OF_FILE) && !hasLineBreakBeforeCurrent()) {
            bool reachedTerminator = false;
            for (TokenType terminator : allowedTerminators) {
                if (check(terminator)) {
                    reachedTerminator = true;
                    break;
                }
            }

            if (reachedTerminator || check(TokenType::SEMI_COLON) ||
                check(TokenType::CLOSE_CURLY) ||
                isRecoveryBoundaryToken(m_current.type())) {
                break;
            }

            advance();
        }

        return true;
    }

    bool recoverLineLeadingContinuation(
        std::initializer_list<TokenType> terminators = {}) {
        if (!hasLineBreakBeforeCurrent() ||
            !isLineContinuationToken(m_current.type())) {
            return false;
        }

        addError(m_current.line(),
                 lineLeadingContinuationMessage(m_current.type()));

        int parenDepth = 0;
        int bracketDepth = 0;
        int braceDepth = 0;

        while (!check(TokenType::END_OF_FILE)) {
            if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
                for (TokenType terminator : terminators) {
                    if (check(terminator)) {
                        return true;
                    }
                }

                if (check(TokenType::SEMI_COLON) ||
                    check(TokenType::CLOSE_CURLY) ||
                    isRecoveryBoundaryToken(m_current.type())) {
                    return true;
                }
            }

            switch (m_current.type()) {
                case TokenType::OPEN_PAREN:
                    ++parenDepth;
                    break;
                case TokenType::CLOSE_PAREN:
                    if (parenDepth > 0) {
                        --parenDepth;
                    }
                    break;
                case TokenType::OPEN_BRACKET:
                    ++bracketDepth;
                    break;
                case TokenType::CLOSE_BRACKET:
                    if (bracketDepth > 0) {
                        --bracketDepth;
                    }
                    break;
                case TokenType::OPEN_CURLY:
                    ++braceDepth;
                    break;
                case TokenType::CLOSE_CURLY:
                    if (braceDepth > 0) {
                        --braceDepth;
                    }
                    break;
                default:
                    break;
            }

            advance();
        }

        return true;
    }

    bool isRecoveryBoundaryToken(TokenType type) const {
        switch (type) {
            case TokenType::TYPE:
            case TokenType::VAR:
            case TokenType::CONST:
            case TokenType::TYPE_FN:
            case TokenType::IMPORT:
            case TokenType::FOR:
            case TokenType::IF:
            case TokenType::WHILE:
            case TokenType::PRINT:
            case TokenType::_RETURN:
            case TokenType::CLOSE_CURLY:
            case TokenType::END_OF_FILE:
                return true;
            default:
                return false;
        }
    }

    void rejectStraySemicolon() {
        if (!check(TokenType::SEMI_COLON)) {
            return;
        }

        addError(m_current.line(),
                 "Type error: semicolons are only allowed inside 'for (...)' clauses.");
        advance();
    }

    void advance() {
        m_previous = m_current;
        m_current = nextToken();
    }

    bool check(TokenType type) const { return m_current.type() == type; }

    bool match(TokenType type) {
        if (!check(type)) return false;
        advance();
        return true;
    }

    bool consume(TokenType type, const std::string& message) {
        if (check(type)) {
            advance();
            return true;
        }
        addError(m_current.line(), message);
        if (!check(TokenType::END_OF_FILE)) {
            advance();
        }
        return false;
    }

    void beginScope() { m_scopes.emplace_back(); }

    void endScope() {
        if (m_scopes.size() > 1) {
            m_scopes.pop_back();
        }
    }

    void defineSymbol(const std::string& name, const TypeRef& type,
                      bool isConst = false) {
        m_scopes.back()[name] =
            SymbolInfo{type ? type : TypeInfo::makeAny(), isConst};
        if (m_scopes.size() == 1) {
            m_declaredGlobalSymbols.emplace(name);
        }
    }

    void recordDeclarationType(const std::string& name, const TypeRef& type,
                               size_t line, bool isConst = false) {
        TypeCheckerDeclarationType declaration;
        declaration.line = line;
        declaration.functionDepth = m_functionContexts.size();
        declaration.scopeDepth = m_scopes.empty() ? 0 : (m_scopes.size() - 1);
        declaration.name = name;
        declaration.type = type ? type : TypeInfo::makeAny();
        declaration.isConst = isConst;
        m_declarationTypes.push_back(std::move(declaration));
    }

    const SymbolInfo* resolveSymbolInfo(const std::string& name) const {
        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                return &found->second;
            }
        }

        return nullptr;
    }

    TypeRef resolveSymbol(const std::string& name) const {
        if (const SymbolInfo* symbol = resolveSymbolInfo(name)) {
            return symbol->type;
        }

        auto sigIt = m_functionSignatures.find(name);
        if (sigIt != m_functionSignatures.end()) {
            return sigIt->second;
        }

        return nullptr;
    }

    bool isSubtypeOf(const std::string& derived,
                     const std::string& base) const {
        if (derived == base) {
            return true;
        }

        std::string current = derived;
        std::unordered_set<std::string> visited;
        while (true) {
            if (visited.find(current) != visited.end()) {
                return false;
            }
            visited.insert(current);

            auto it = m_superclassOf.find(current);
            if (it == m_superclassOf.end() || it->second.empty()) {
                return false;
            }
            current = it->second;
            if (current == base) {
                return true;
            }
        }
    }

    bool isAssignableType(const TypeRef& from, const TypeRef& to) const {
        if (from && to && (from->isAny() || to->isAny())) {
            return true;
        }

        if (isAssignable(from, to)) {
            return true;
        }

        if (!from || !to) {
            return false;
        }

        if (from->kind == TypeKind::CLASS && to->kind == TypeKind::CLASS) {
            return isSubtypeOf(from->className, to->className);
        }

        return false;
    }

    bool isValidExplicitCast(const TypeRef& from, const TypeRef& to) const {
        if (!from || !to) {
            return false;
        }

        if (from->isAny() || to->isAny()) {
            return true;
        }

        if (isAssignableType(from, to) || isAssignableType(to, from)) {
            return true;
        }

        if (from->isNumeric() && to->isNumeric()) {
            return true;
        }

        if (from->isNumeric() && to->kind == TypeKind::STR) {
            return true;
        }

        return false;
    }

    TypeRef lookupClassFieldType(const std::string& className,
                                 const std::string& fieldName) const {
        std::string current = className;
        std::unordered_set<std::string> visited;

        while (!current.empty()) {
            if (visited.find(current) != visited.end()) {
                break;
            }
            visited.insert(current);

            auto classIt = m_classFieldTypes.find(current);
            if (classIt != m_classFieldTypes.end()) {
                auto fieldIt = classIt->second.find(fieldName);
                if (fieldIt != classIt->second.end()) {
                    return fieldIt->second;
                }
            }

            auto superIt = m_superclassOf.find(current);
            if (superIt == m_superclassOf.end()) {
                break;
            }
            current = superIt->second;
        }

        return nullptr;
    }

    TypeRef lookupClassMethodType(const std::string& className,
                                  const std::string& methodName) const {
        std::string current = className;
        std::unordered_set<std::string> visited;

        while (!current.empty()) {
            if (visited.find(current) != visited.end()) {
                break;
            }
            visited.insert(current);

            auto classIt = m_classMethodSignatures.find(current);
            if (classIt != m_classMethodSignatures.end()) {
                auto methodIt = classIt->second.find(methodName);
                if (methodIt != classIt->second.end()) {
                    return methodIt->second;
                }
            }

            auto superIt = m_superclassOf.find(current);
            if (superIt == m_superclassOf.end()) {
                break;
            }
            current = superIt->second;
        }

        return nullptr;
    }

    TypeRef lookupOperatorResultType(const TypeRef& receiverType, TokenType op,
                                     const TypeRef& rhs, size_t line) {
        if (!receiverType || receiverType->kind != TypeKind::CLASS) {
            return nullptr;
        }

        auto classIt = m_classOperatorMethods.find(receiverType->className);
        if (classIt == m_classOperatorMethods.end()) {
            return nullptr;
        }

        auto opIt = classIt->second.find(op);
        if (opIt == classIt->second.end()) {
            return nullptr;
        }

        TypeRef methodType =
            lookupClassMethodType(receiverType->className, opIt->second);
        if (!methodType || methodType->kind != TypeKind::FUNCTION) {
            addError(line, "Type error: operator method '" + opIt->second +
                               "' is not callable.");
            return TypeInfo::makeAny();
        }

        if (methodType->paramTypes.size() != 1) {
            addError(line, "Type error: operator method '" + opIt->second +
                               "' must take exactly one argument.");
            return methodType->returnType ? methodType->returnType
                                          : TypeInfo::makeAny();
        }

        TypeRef expected = methodType->paramTypes[0]
                               ? methodType->paramTypes[0]
                               : TypeInfo::makeAny();
        if (!isAssignableType(rhs, expected)) {
            addError(line, "Type error: operator '" + opIt->second +
                               "' expects '" + expected->toString() +
                               "', got '" + rhs->toString() + "'.");
        }

        return methodType->returnType ? methodType->returnType
                                      : TypeInfo::makeAny();
    }

    TypeRef collectionMemberType(const TypeRef& receiverType,
                                 const std::string& memberName, size_t line) {
        if (!receiverType) {
            return TypeInfo::makeAny();
        }

        if (receiverType->kind == TypeKind::ARRAY) {
            TypeRef element = receiverType->elementType
                                  ? receiverType->elementType
                                  : TypeInfo::makeAny();
            if (memberName == "push") {
                return TypeInfo::makeFunction({element}, TypeInfo::makeI64());
            }
            if (memberName == "pop") {
                return TypeInfo::makeFunction({}, element);
            }
            if (memberName == "size") {
                return TypeInfo::makeFunction({}, TypeInfo::makeI64());
            }
            if (memberName == "has") {
                return TypeInfo::makeFunction({element}, TypeInfo::makeBool());
            }
            if (memberName == "insert") {
                return TypeInfo::makeFunction({TypeInfo::makeI64(), element},
                                              element);
            }
            if (memberName == "remove") {
                return TypeInfo::makeFunction({TypeInfo::makeI64()}, element);
            }
            if (memberName == "clear") {
                return TypeInfo::makeFunction({}, TypeInfo::makeI64());
            }
            if (memberName == "isEmpty") {
                return TypeInfo::makeFunction({}, TypeInfo::makeBool());
            }
            if (memberName == "first" || memberName == "last") {
                return TypeInfo::makeFunction({}, element);
            }

            addError(line,
                     "Type error: array has no member '" + memberName + "'.");
            return TypeInfo::makeAny();
        }

        if (receiverType->kind == TypeKind::DICT) {
            TypeRef key = receiverType->keyType ? receiverType->keyType
                                                : TypeInfo::makeAny();
            TypeRef value = receiverType->valueType ? receiverType->valueType
                                                    : TypeInfo::makeAny();
            if (memberName == "get") {
                return TypeInfo::makeFunction({key}, value);
            }
            if (memberName == "set") {
                return TypeInfo::makeFunction({key, value}, value);
            }
            if (memberName == "has") {
                return TypeInfo::makeFunction({key}, TypeInfo::makeBool());
            }
            if (memberName == "keys") {
                return TypeInfo::makeFunction({}, TypeInfo::makeArray(key));
            }
            if (memberName == "values") {
                return TypeInfo::makeFunction({}, TypeInfo::makeArray(value));
            }
            if (memberName == "size") {
                return TypeInfo::makeFunction({}, TypeInfo::makeI64());
            }
            if (memberName == "remove") {
                return TypeInfo::makeFunction({key}, value);
            }
            if (memberName == "clear") {
                return TypeInfo::makeFunction({}, TypeInfo::makeI64());
            }
            if (memberName == "isEmpty") {
                return TypeInfo::makeFunction({}, TypeInfo::makeBool());
            }
            if (memberName == "getOr") {
                return TypeInfo::makeFunction({key, value}, value);
            }

            addError(line,
                     "Type error: dict has no member '" + memberName + "'.");
            return TypeInfo::makeAny();
        }

        if (receiverType->kind == TypeKind::SET) {
            TypeRef element = receiverType->elementType
                                  ? receiverType->elementType
                                  : TypeInfo::makeAny();
            TypeRef setType = TypeInfo::makeSet(element);

            if (memberName == "add") {
                return TypeInfo::makeFunction({element}, TypeInfo::makeBool());
            }
            if (memberName == "has") {
                return TypeInfo::makeFunction({element}, TypeInfo::makeBool());
            }
            if (memberName == "remove") {
                return TypeInfo::makeFunction({element}, TypeInfo::makeBool());
            }
            if (memberName == "size") {
                return TypeInfo::makeFunction({}, TypeInfo::makeI64());
            }
            if (memberName == "toArray") {
                return TypeInfo::makeFunction({}, TypeInfo::makeArray(element));
            }
            if (memberName == "clear") {
                return TypeInfo::makeFunction({}, TypeInfo::makeI64());
            }
            if (memberName == "isEmpty") {
                return TypeInfo::makeFunction({}, TypeInfo::makeBool());
            }
            if (memberName == "union" || memberName == "intersect" ||
                memberName == "difference") {
                return TypeInfo::makeFunction({setType}, setType);
            }

            addError(line,
                     "Type error: set has no member '" + memberName + "'.");
            return TypeInfo::makeAny();
        }

        return TypeInfo::makeAny();
    }

    TypeRef parseTypeExprType() {
        auto applyOptionalSuffix = [this](TypeRef baseType) -> TypeRef {
            if (!baseType) {
                return nullptr;
            }

            while (match(TokenType::QUESTION)) {
                baseType = TypeInfo::makeOptional(baseType);
            }

            return baseType;
        };

        if (check(TokenType::TYPE_FN)) {
            advance();
            consume(TokenType::OPEN_PAREN,
                    "Type error: expected '(' after 'fn'.");

            std::vector<TypeRef> paramTypes;
            if (!check(TokenType::CLOSE_PAREN)) {
                while (true) {
                    TypeRef paramType = parseTypeExprType();
                    if (!paramType) {
                        addError(
                            m_current.line(),
                            "Type error: expected parameter type in function "
                            "type.");
                        return nullptr;
                    }
                    if (paramType->isVoid()) {
                        addError(
                            m_current.line(),
                            "Type error: function type parameter cannot be "
                            "'void'.");
                        return nullptr;
                    }
                    paramTypes.push_back(paramType);

                    if (match(TokenType::COMMA)) {
                        continue;
                    }
                    break;
                }
            }

            consume(TokenType::CLOSE_PAREN,
                    "Type error: expected ')' after function type "
                    "parameters.");
            TypeRef returnType = parseTypeExprType();
            if (!returnType) {
                addError(m_current.line(),
                         "Type error: expected function return type.");
                return nullptr;
            }

            return applyOptionalSuffix(
                TypeInfo::makeFunction(paramTypes, returnType));
        }

        if (isTypeToken(m_current.type())) {
            Token token = m_current;
            advance();
            switch (token.type()) {
                case TokenType::TYPE_I8:
                    return applyOptionalSuffix(TypeInfo::makeI8());
                case TokenType::TYPE_I16:
                    return applyOptionalSuffix(TypeInfo::makeI16());
                case TokenType::TYPE_I32:
                    return applyOptionalSuffix(TypeInfo::makeI32());
                case TokenType::TYPE_I64:
                    return applyOptionalSuffix(TypeInfo::makeI64());
                case TokenType::TYPE_U8:
                    return applyOptionalSuffix(TypeInfo::makeU8());
                case TokenType::TYPE_U16:
                    return applyOptionalSuffix(TypeInfo::makeU16());
                case TokenType::TYPE_U32:
                    return applyOptionalSuffix(TypeInfo::makeU32());
                case TokenType::TYPE_U64:
                    return applyOptionalSuffix(TypeInfo::makeU64());
                case TokenType::TYPE_USIZE:
                    return applyOptionalSuffix(TypeInfo::makeUSize());
                case TokenType::TYPE_F32:
                    return applyOptionalSuffix(TypeInfo::makeF32());
                case TokenType::TYPE_F64:
                    return applyOptionalSuffix(TypeInfo::makeF64());
                case TokenType::TYPE_BOOL:
                    return applyOptionalSuffix(TypeInfo::makeBool());
                case TokenType::TYPE_STR:
                    return applyOptionalSuffix(TypeInfo::makeStr());
                case TokenType::TYPE_FN:
                    return nullptr;
                case TokenType::TYPE_VOID:
                    return applyOptionalSuffix(TypeInfo::makeVoid());
                case TokenType::TYPE_NULL_KW:
                    return applyOptionalSuffix(TypeInfo::makeNull());
                default:
                    return nullptr;
            }
        }

        if (check(TokenType::IDENTIFIER)) {
            std::string className = tokenText(m_current);

            if (isHandleTypeNameText(className)) {
                advance();
                consume(TokenType::LESS,
                        "Type error: expected '<' after 'handle'.");
                consume(TokenType::IDENTIFIER,
                        "Type error: expected package namespace in handle "
                        "type.");
                std::string packageNamespace = tokenText(m_previous);
                consume(TokenType::COLON,
                        "Type error: expected ':' after handle package "
                        "namespace.");
                consume(TokenType::IDENTIFIER,
                        "Type error: expected package name in handle type.");
                std::string packageName = tokenText(m_previous);
                consume(TokenType::COLON,
                        "Type error: expected ':' before handle type name.");
                consume(TokenType::IDENTIFIER,
                        "Type error: expected native handle type name.");
                std::string typeName = tokenText(m_previous);
                consume(TokenType::GREATER,
                        "Type error: expected '>' after handle type.");

                if (!isValidPackageIdPart(packageNamespace) ||
                    !isValidPackageIdPart(packageName) ||
                    !isValidHandleTypeName(typeName)) {
                    addError(
                        m_previous.line(),
                        "Type error: handle type must use "
                        "handle<namespace:name:Type> with lowercase package "
                        "IDs and an alphanumeric type name.");
                    return nullptr;
                }

                return applyOptionalSuffix(TypeInfo::makeNativeHandle(
                    makePackageId(packageNamespace, packageName), typeName));
            }

            if (isCollectionTypeNameText(className)) {
                advance();
                consume(TokenType::LESS,
                        "Type error: expected '<' after collection type.");

                if (className == "Array") {
                    TypeRef elementType = parseTypeExprType();
                    if (!elementType) {
                        addError(
                            m_current.line(),
                            "Type error: expected element type in Array<T>.");
                        return nullptr;
                    }
                    if (elementType->isVoid()) {
                        addError(m_current.line(),
                                 "Type error: 'void' is not valid as an "
                                 "Array element type.");
                        return nullptr;
                    }
                    consume(TokenType::GREATER,
                            "Type error: expected '>' after Array<T>.");
                    return applyOptionalSuffix(
                        TypeInfo::makeArray(elementType));
                }

                if (className == "Set") {
                    TypeRef elementType = parseTypeExprType();
                    if (!elementType) {
                        addError(
                            m_current.line(),
                            "Type error: expected element type in Set<T>.");
                        return nullptr;
                    }
                    if (elementType->isVoid()) {
                        addError(m_current.line(),
                                 "Type error: 'void' is not valid as a Set "
                                 "element type.");
                        return nullptr;
                    }
                    consume(TokenType::GREATER,
                            "Type error: expected '>' after Set<T>.");
                    return applyOptionalSuffix(TypeInfo::makeSet(elementType));
                }

                TypeRef keyType = parseTypeExprType();
                if (!keyType) {
                    addError(m_current.line(),
                             "Type error: expected key type in Dict<K, V>.");
                    return nullptr;
                }
                if (keyType->isVoid()) {
                    addError(m_current.line(),
                             "Type error: 'void' is not valid as a Dict key "
                             "type.");
                    return nullptr;
                }
                consume(TokenType::COMMA,
                        "Type error: expected ',' in Dict<K, V>.");
                TypeRef valueType = parseTypeExprType();
                if (!valueType) {
                    addError(m_current.line(),
                             "Type error: expected value type in Dict<K, V>.");
                    return nullptr;
                }
                if (valueType->isVoid()) {
                    addError(m_current.line(),
                             "Type error: 'void' is not valid as a Dict "
                             "value type.");
                    return nullptr;
                }
                consume(TokenType::GREATER,
                        "Type error: expected '>' after Dict<K, V>.");
                return applyOptionalSuffix(
                    TypeInfo::makeDict(keyType, valueType));
            }

            if (m_classNames.find(className) != m_classNames.end()) {
                advance();
                return applyOptionalSuffix(TypeInfo::makeClass(className));
            }
            auto aliasIt = m_typeAliases.find(className);
            if (aliasIt != m_typeAliases.end()) {
                advance();
                return applyOptionalSuffix(aliasIt->second);
            }
        }

        return nullptr;
    }

    bool isTypedVarDeclarationStart() {
        if (check(TokenType::TYPE_FN)) {
            return looksLikeFunctionTypeDeclarationStart();
        }

        if (isTypeToken(m_current.type())) {
            TokenType lookahead = peekToken().type();
            return lookahead == TokenType::IDENTIFIER ||
                   lookahead == TokenType::QUESTION;
        }

        if (!check(TokenType::IDENTIFIER)) {
            return false;
        }

        Token lookahead = peekToken();
        if (isHandleTypeNameText(tokenText(m_current))) {
            return lookahead.type() == TokenType::LESS;
        }

        if (isCollectionTypeNameToken(m_current)) {
            return lookahead.type() == TokenType::LESS;
        }

        std::string typeName = tokenText(m_current);
        return (m_classNames.find(typeName) != m_classNames.end() ||
                m_typeAliases.find(typeName) != m_typeAliases.end()) &&
               (lookahead.type() == TokenType::IDENTIFIER ||
                lookahead.type() == TokenType::QUESTION);
    }

    ExprInfo parseExpression() { return parseAssignment(); }

    ExprInfo parseAssignment() {
        ExprInfo lhs = parseOr();

        if (hasLineBreakBeforeCurrent() || !isAssignmentOperator(m_current.type())) {
            return lhs;
        }

        TokenType assignmentType = m_current.type();
        size_t line = m_current.line();
        advance();

        if (assignmentType == TokenType::PLUS_PLUS ||
            assignmentType == TokenType::MINUS_MINUS) {
            if (!lhs.isAssignable) {
                return ExprInfo{TypeInfo::makeAny(), false, false, "", line};
            }

            if (lhs.isConstSymbol && !lhs.name.empty()) {
                addError(line, "Type error: cannot assign to const variable '" +
                                   lhs.name + "'.");
                return ExprInfo{lhs.type ? lhs.type : TypeInfo::makeAny(),
                                false, false, lhs.name, line, true};
            }

            TypeRef targetType = lhs.type;
            if ((!targetType || targetType->isAny()) && !lhs.name.empty()) {
                targetType = resolveSymbol(lhs.name);
            }
            if (!targetType) {
                addError(line, "Type error: unknown assignment target '" +
                                   lhs.name + "'.");
                return ExprInfo{TypeInfo::makeAny(), false, false, lhs.name,
                                line};
            }

            if (targetType->isAny()) {
                return ExprInfo{TypeInfo::makeAny(), false, false, lhs.name,
                                line};
            }

            if (!targetType->isNumeric()) {
                addError(
                    line,
                    "Type error: update operator expects numeric operand.");
            }

            return ExprInfo{targetType, false, false, lhs.name, line};
        }

        ExprInfo rhs = parseAssignment();

        if (!lhs.isAssignable) {
            return ExprInfo{TypeInfo::makeAny(), false, false, "", line};
        }

        if (lhs.isConstSymbol && !lhs.name.empty()) {
            addError(line, "Type error: cannot assign to const variable '" +
                               lhs.name + "'.");
            return ExprInfo{lhs.type ? lhs.type : TypeInfo::makeAny(), false,
                            false, lhs.name, line, true};
        }

        TypeRef targetType = lhs.type;
        if ((!targetType || targetType->isAny()) && !lhs.name.empty()) {
            targetType = resolveSymbol(lhs.name);
        }
        if (!targetType) {
            addError(line, "Type error: unknown assignment target '" +
                               lhs.name + "'.");
            return ExprInfo{TypeInfo::makeAny(), false, false, lhs.name, line};
        }

        if (assignmentType == TokenType::EQUAL) {
            if (!isAssignableType(rhs.type, targetType)) {
                addError(line, "Type error: cannot assign '" +
                                   rhs.type->toString() + "' to variable '" +
                                   lhs.name + "' of type '" +
                                   targetType->toString() + "'.");
            }
            return ExprInfo{targetType, false, false, lhs.name, line};
        }

        if (targetType->isAny() || rhs.type->isAny()) {
            return ExprInfo{targetType, false, false, lhs.name, line};
        }

        if (isArithmeticCompoundAssignment(assignmentType)) {
            if (!(targetType->isNumeric() && rhs.type->isNumeric())) {
                addError(
                    line,
                    "Type error: compound assignment requires numeric operands.");
                return ExprInfo{TypeInfo::makeAny(), false, false, lhs.name,
                                line};
            }

            TypeRef promoted = numericPromotion(targetType, rhs.type);
            if (!promoted || !isAssignableType(promoted, targetType)) {
                addError(line,
                         "Type error: result of compound assignment is not "
                         "assignable to '" +
                             targetType->toString() + "'.");
            }

            return ExprInfo{targetType, false, false, lhs.name, line};
        }

        if (assignmentType == TokenType::SHIFT_LEFT_EQUAL ||
            assignmentType == TokenType::SHIFT_RIGHT_EQUAL) {
            if (!(targetType->isInteger() && rhs.type->isInteger())) {
                addError(
                    line,
                    "Type error: shift operators require integer operands.");
                return ExprInfo{TypeInfo::makeAny(), false, false, lhs.name,
                                line};
            }

            return ExprInfo{targetType, false, false, lhs.name, line};
        }

        if (isBitwiseCompoundAssignment(assignmentType)) {
            if (!(targetType->isInteger() && rhs.type->isInteger())) {
                addError(
                    line,
                    "Type error: bitwise operators require integer operands.");
                return ExprInfo{TypeInfo::makeAny(), false, false, lhs.name,
                                line};
            }

            TypeRef resultType = bitwiseIntegerResultType(targetType, rhs.type);
            if (!resultType || !isAssignableType(resultType, targetType)) {
                addError(line,
                         "Type error: result of compound assignment is not "
                         "assignable to '" +
                             targetType->toString() + "'.");
            }
        }

        return ExprInfo{targetType, false, false, lhs.name, line};
    }

    ExprInfo parseOr() {
        ExprInfo expr = parseAnd();
        while (matchSameLine(TokenType::LOGICAL_OR)) {
            ExprInfo rhs = parseAnd();
            if (!(expr.type->kind == TypeKind::BOOL || expr.type->isAny()) ||
                !(rhs.type->kind == TypeKind::BOOL || rhs.type->isAny())) {
                addError(m_previous.line(),
                         "Type error: logical '||' expects bool operands.");
            }
            expr = ExprInfo{TypeInfo::makeBool(), false, false, "",
                            m_previous.line()};
        }
        return expr;
    }

    ExprInfo parseAnd() {
        ExprInfo expr = parseEquality();
        while (matchSameLine(TokenType::LOGICAL_AND)) {
            ExprInfo rhs = parseEquality();
            if (!(expr.type->kind == TypeKind::BOOL || expr.type->isAny()) ||
                !(rhs.type->kind == TypeKind::BOOL || rhs.type->isAny())) {
                addError(m_previous.line(),
                         "Type error: logical '&&' expects bool operands.");
            }
            expr = ExprInfo{TypeInfo::makeBool(), false, false, "",
                            m_previous.line()};
        }
        return expr;
    }

    ExprInfo parseEquality() {
        ExprInfo expr = parseComparison();
        while (!hasLineBreakBeforeCurrent() &&
               isEqualityOperator(m_current.type())) {
            TokenType op = m_current.type();
            size_t line = m_current.line();
            advance();
            ExprInfo rhs = parseComparison();
            if (TypeRef overloaded = lookupOperatorResultType(expr.type, op,
                                                              rhs.type, line)) {
                expr = ExprInfo{overloaded, false, false, "", line};
                continue;
            }
            if (!(isAssignableType(expr.type, rhs.type) ||
                  isAssignableType(rhs.type, expr.type))) {
                addError(
                    line,
                    "Type error: incompatible operands for equality operator.");
            }
            expr = ExprInfo{TypeInfo::makeBool(), false, false, "", line};
        }
        return expr;
    }

    ExprInfo parseComparison() {
        ExprInfo expr = parseBitwiseOr();
        while (!hasLineBreakBeforeCurrent() &&
               isComparisonOperator(m_current.type())) {
            TokenType op = m_current.type();
            size_t line = m_current.line();
            advance();
            ExprInfo rhs = parseBitwiseOr();

            if (TypeRef overloaded = lookupOperatorResultType(expr.type, op,
                                                              rhs.type, line)) {
                expr = ExprInfo{overloaded, false, false, "", line};
                continue;
            }

            bool lhsOk = expr.type->isAny() || expr.type->isNumeric();
            bool rhsOk = rhs.type->isAny() || rhs.type->isNumeric();
            if (!lhsOk || !rhsOk) {
                addError(line,
                         "Type error: comparison operators require numeric "
                         "operands.");
            }
            expr = ExprInfo{TypeInfo::makeBool(), false, false, "", line};
        }
        return expr;
    }

    ExprInfo parseBitwiseOr() {
        ExprInfo expr = parseBitwiseXor();
        while (matchSameLine(TokenType::PIPE)) {
            size_t line = m_previous.line();
            ExprInfo rhs = parseBitwiseXor();
            if (expr.type->isAny() || rhs.type->isAny()) {
                expr = ExprInfo{TypeInfo::makeAny(), false, false, "", line};
                continue;
            }

            TypeRef resultType = bitwiseIntegerResultType(expr.type, rhs.type);
            if (!resultType) {
                addError(line,
                         "Type error: bitwise operators require integer operands.");
                expr = ExprInfo{TypeInfo::makeAny(), false, false, "", line};
                continue;
            }

            expr = ExprInfo{resultType, false, false, "", line};
        }
        return expr;
    }

    ExprInfo parseBitwiseXor() {
        ExprInfo expr = parseBitwiseAnd();
        while (matchSameLine(TokenType::CARET)) {
            size_t line = m_previous.line();
            ExprInfo rhs = parseBitwiseAnd();
            if (expr.type->isAny() || rhs.type->isAny()) {
                expr = ExprInfo{TypeInfo::makeAny(), false, false, "", line};
                continue;
            }

            TypeRef resultType = bitwiseIntegerResultType(expr.type, rhs.type);
            if (!resultType) {
                addError(line,
                         "Type error: bitwise operators require integer operands.");
                expr = ExprInfo{TypeInfo::makeAny(), false, false, "", line};
                continue;
            }

            expr = ExprInfo{resultType, false, false, "", line};
        }
        return expr;
    }

    ExprInfo parseBitwiseAnd() {
        ExprInfo expr = parseShift();
        while (matchSameLine(TokenType::AMPERSAND)) {
            size_t line = m_previous.line();
            ExprInfo rhs = parseShift();
            if (expr.type->isAny() || rhs.type->isAny()) {
                expr = ExprInfo{TypeInfo::makeAny(), false, false, "", line};
                continue;
            }

            TypeRef resultType = bitwiseIntegerResultType(expr.type, rhs.type);
            if (!resultType) {
                addError(line,
                         "Type error: bitwise operators require integer operands.");
                expr = ExprInfo{TypeInfo::makeAny(), false, false, "", line};
                continue;
            }

            expr = ExprInfo{resultType, false, false, "", line};
        }
        return expr;
    }

    ExprInfo parseShift() {
        ExprInfo expr = parseTerm();
        while (!hasLineBreakBeforeCurrent() &&
               (check(TokenType::SHIFT_LEFT_TOKEN) ||
                check(TokenType::SHIFT_RIGHT_TOKEN))) {
            size_t line = m_current.line();
            advance();
            ExprInfo rhs = parseTerm();
            bool lhsOk = expr.type->isInteger() || expr.type->isAny();
            bool rhsOk = rhs.type->isInteger() || rhs.type->isAny();
            if (!lhsOk || !rhsOk) {
                addError(
                    line,
                    "Type error: shift operators require integer operands.");
            }
            expr = ExprInfo{expr.type, false, false, "", line};
        }
        return expr;
    }

    ExprInfo parseTerm() {
        ExprInfo expr = parseFactor();
        while (!hasLineBreakBeforeCurrent() &&
               (check(TokenType::PLUS) || check(TokenType::MINUS))) {
            TokenType op = m_current.type();
            size_t line = m_current.line();
            advance();
            ExprInfo rhs = parseFactor();

            if (TypeRef overloaded = lookupOperatorResultType(expr.type, op,
                                                              rhs.type, line)) {
                expr = ExprInfo{overloaded, false, false, "", line};
                continue;
            }

            if (op == TokenType::PLUS && expr.type->kind == TypeKind::STR &&
                rhs.type->kind == TypeKind::STR) {
                expr = ExprInfo{TypeInfo::makeStr(), false, false, "", line};
                continue;
            }

            if (expr.type->isAny() || rhs.type->isAny()) {
                expr = ExprInfo{TypeInfo::makeAny(), false, false, "", line};
                continue;
            }

            bool lhsOk = expr.type->isNumeric() || expr.type->isAny();
            bool rhsOk = rhs.type->isNumeric() || rhs.type->isAny();
            if (!lhsOk || !rhsOk) {
                addError(line,
                         "Type error: arithmetic operator requires numeric "
                         "operands.");
                expr = ExprInfo{TypeInfo::makeAny(), false, false, "", line};
                continue;
            }

            TypeRef promoted = numericPromotion(expr.type, rhs.type);
            expr = ExprInfo{promoted ? promoted : TypeInfo::makeAny(), false,
                            false, "", line};
        }
        return expr;
    }

    ExprInfo parseFactor() {
        ExprInfo expr = parseUnary();
        while (!hasLineBreakBeforeCurrent() &&
               (check(TokenType::STAR) || check(TokenType::SLASH))) {
            TokenType op = m_current.type();
            size_t line = m_current.line();
            advance();
            ExprInfo rhs = parseUnary();

            if (TypeRef overloaded = lookupOperatorResultType(expr.type, op,
                                                              rhs.type, line)) {
                expr = ExprInfo{overloaded, false, false, "", line};
                continue;
            }
            bool lhsOk = expr.type->isNumeric() || expr.type->isAny();
            bool rhsOk = rhs.type->isNumeric() || rhs.type->isAny();
            if (!lhsOk || !rhsOk) {
                addError(line,
                         "Type error: arithmetic operator requires numeric "
                         "operands.");
                expr = ExprInfo{TypeInfo::makeAny(), false, false, "", line};
                continue;
            }

            TypeRef promoted = numericPromotion(expr.type, rhs.type);
            expr = ExprInfo{promoted ? promoted : TypeInfo::makeAny(), false,
                            false, "", line};
        }
        return expr;
    }

    ExprInfo parseUnary() {
        if (match(TokenType::BANG)) {
            ExprInfo operand = parseUnary();
            if (!(operand.type->kind == TypeKind::BOOL ||
                  operand.type->isAny())) {
                addError(m_previous.line(),
                         "Type error: unary '!' expects a bool operand.");
            }
            return ExprInfo{TypeInfo::makeBool(), false, false, "",
                            m_previous.line()};
        }

        if (match(TokenType::MINUS)) {
            ExprInfo operand = parseUnary();
            if (!(operand.type->isNumeric() || operand.type->isAny())) {
                addError(m_previous.line(),
                         "Type error: unary '-' expects a numeric operand.");
            }
            return ExprInfo{operand.type, false, false, "", m_previous.line()};
        }

        if (match(TokenType::TILDE)) {
            ExprInfo operand = parseUnary();
            if (!(operand.type->isInteger() || operand.type->isAny())) {
                addError(m_previous.line(),
                         "Type error: unary '~' expects an integer operand.");
            }
            return ExprInfo{operand.type, false, false, "", m_previous.line()};
        }

        if (match(TokenType::PLUS_PLUS) || match(TokenType::MINUS_MINUS)) {
            ExprInfo operand = parseUnary();
            if (operand.isConstSymbol && !operand.name.empty()) {
                addError(m_previous.line(),
                         "Type error: cannot assign to const variable '" +
                             operand.name + "'.");
            }
            if (!(operand.type->isNumeric() || operand.type->isAny())) {
                addError(
                    m_previous.line(),
                    "Type error: update operator expects a numeric operand.");
            }
            return ExprInfo{operand.type, false, false, "", m_previous.line()};
        }

        return parseCast();
    }

    ExprInfo parseCast() {
        ExprInfo expr = parseCall();
        while (matchSameLine(TokenType::AS_KW)) {
            size_t line = m_previous.line();
            TypeRef target = parseTypeExprType();
            if (!target) {
                addError(line, "Type error: expected type after 'as'.");
                return ExprInfo{TypeInfo::makeAny(), false, false, "", line};
            }

            if (!isValidExplicitCast(expr.type, target)) {
                addError(line, "Type error: cannot cast '" +
                                   expr.type->toString() + "' to '" +
                                   target->toString() + "'.");
            }
            expr = ExprInfo{target, false, false, "", line};
        }
        return expr;
    }

    ExprInfo parseCall() {
        ExprInfo expr = parsePrimary();

        while (true) {
            if (matchSameLine(TokenType::OPEN_PAREN)) {
                std::vector<ExprInfo> args;
                if (!check(TokenType::CLOSE_PAREN)) {
                    do {
                        args.push_back(parseExpression());
                    } while (match(TokenType::COMMA));
                }
                consume(TokenType::CLOSE_PAREN,
                        "Expected ')' after call arguments.");

                if (expr.type && expr.type->isOptional()) {
                    addError(
                        m_previous.line(),
                        "Type error: cannot call optional value of type '" +
                            expr.type->toString() + "' without a null check.");
                    expr = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                    m_previous.line()};
                    continue;
                }

                if (expr.isClassSymbol) {
                    expr = ExprInfo{TypeInfo::makeClass(expr.name), false,
                                    false, "", m_previous.line()};
                    continue;
                }

                if (!expr.type || expr.type->isAny()) {
                    expr = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                    m_previous.line()};
                    continue;
                }

                if (expr.type->kind != TypeKind::FUNCTION) {
                    addError(
                        m_previous.line(),
                        "Type error: attempted to call a non-function value.");
                    expr = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                    m_previous.line()};
                    continue;
                }

                const auto& params = expr.type->paramTypes;
                if (!params.empty() && params.size() != args.size()) {
                    addError(m_previous.line(),
                             "Type error: function expects " +
                                 std::to_string(params.size()) +
                                 " arguments, got " +
                                 std::to_string(args.size()) + ".");
                }

                size_t compared = std::min(params.size(), args.size());
                for (size_t index = 0; index < compared; ++index) {
                    TypeRef expected =
                        params[index] ? params[index] : TypeInfo::makeAny();
                    if (!isAssignableType(args[index].type, expected)) {
                        addError(args[index].line ? args[index].line
                                                  : m_previous.line(),
                                 "Type error: function argument " +
                                     std::to_string(index + 1) + " expects '" +
                                     expected->toString() + "', got '" +
                                     args[index].type->toString() + "'.");
                    }
                }

                expr = ExprInfo{expr.type->returnType ? expr.type->returnType
                                                      : TypeInfo::makeAny(),
                                false, false, "", m_previous.line()};
                continue;
            }

            if (matchSameLine(TokenType::DOT)) {
                ExprInfo receiver = expr;
                if (expr.type && expr.type->isOptional()) {
                    addError(m_previous.line(),
                             "Type error: cannot access members on optional "
                             "value of type '" +
                                 expr.type->toString() +
                                 "' without a null check.");
                }
                consume(TokenType::IDENTIFIER,
                        "Expected property name after '.'.");
                std::string memberName = tokenText(m_previous);

                if (receiver.type && receiver.type->kind == TypeKind::CLASS) {
                    TypeRef fieldType = lookupClassFieldType(
                        receiver.type->className, memberName);
                    if (fieldType) {
                        expr = ExprInfo{fieldType, true, false, "",
                                        m_previous.line()};
                        continue;
                    }

                    TypeRef methodType = lookupClassMethodType(
                        receiver.type->className, memberName);
                    if (methodType) {
                        expr = ExprInfo{methodType, false, false, "",
                                        m_previous.line()};
                        continue;
                    }

                    addError(m_previous.line(),
                             "Type error: class '" + receiver.type->className +
                                 "' has no member '" + memberName + "'.");
                    expr = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                    m_previous.line()};
                    continue;
                }

                if (receiver.type && (receiver.type->kind == TypeKind::ARRAY ||
                                      receiver.type->kind == TypeKind::DICT ||
                                      receiver.type->kind == TypeKind::SET)) {
                    TypeRef memberType = collectionMemberType(
                        receiver.type, memberName, m_previous.line());
                    expr =
                        ExprInfo{memberType ? memberType : TypeInfo::makeAny(),
                                 false, false, "", m_previous.line()};
                    continue;
                }

                expr = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                m_previous.line()};
                continue;
            }

            if (matchSameLine(TokenType::OPEN_BRACKET)) {
                ExprInfo indexExpr = parseExpression();
                consume(TokenType::CLOSE_BRACKET,
                        "Expected ']' after index expression.");

                if (!expr.type) {
                    expr = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                    m_previous.line()};
                    continue;
                }

                if (expr.type->isAny()) {
                    expr = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                    m_previous.line()};
                    continue;
                }

                if (expr.type->kind == TypeKind::ARRAY) {
                    if (!(indexExpr.type->isInteger() ||
                          indexExpr.type->isAny())) {
                        addError(
                            indexExpr.line ? indexExpr.line : m_previous.line(),
                            "Type error: array index must be an "
                            "integer.");
                    }
                    TypeRef element = expr.type->elementType
                                          ? expr.type->elementType
                                          : TypeInfo::makeAny();
                    expr =
                        ExprInfo{element, true, false, "", m_previous.line()};
                    continue;
                }

                if (expr.type->kind == TypeKind::DICT) {
                    TypeRef keyType = expr.type->keyType ? expr.type->keyType
                                                         : TypeInfo::makeAny();
                    if (!isAssignableType(indexExpr.type, keyType)) {
                        addError(
                            indexExpr.line ? indexExpr.line : m_previous.line(),
                            "Type error: dict key expects '" +
                                keyType->toString() + "', got '" +
                                indexExpr.type->toString() + "'.");
                    }
                    TypeRef valueType = expr.type->valueType
                                            ? expr.type->valueType
                                            : TypeInfo::makeAny();
                    expr =
                        ExprInfo{valueType, true, false, "", m_previous.line()};
                    continue;
                }

                if (expr.type->kind == TypeKind::SET) {
                    TypeRef elementType = expr.type->elementType
                                              ? expr.type->elementType
                                              : TypeInfo::makeAny();
                    if (!isAssignableType(indexExpr.type, elementType)) {
                        addError(
                            indexExpr.line ? indexExpr.line : m_previous.line(),
                            "Type error: set lookup expects '" +
                                elementType->toString() + "', got '" +
                                indexExpr.type->toString() + "'.");
                    }
                    expr = ExprInfo{TypeInfo::makeBool(), false, false, "",
                                    m_previous.line()};
                    continue;
                }

                addError(m_previous.line(),
                         "Type error: indexing is only valid on Array, Dict, "
                         "or Set.");
                expr = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                m_previous.line()};
                continue;
            }

            break;
        }

        return expr;
    }

    ExprInfo parseFunctionLiteralExpr(
        const TypeRef& expectedSignature = nullptr) {
        consume(TokenType::OPEN_PAREN, "Expected '(' after 'fn'.");

        const bool hasExpectedFunctionType =
            expectedSignature && expectedSignature->kind == TypeKind::FUNCTION;
        const auto& expectedParams = hasExpectedFunctionType
                                         ? expectedSignature->paramTypes
                                         : std::vector<TypeRef>{};

        std::vector<std::pair<std::string, TypeRef>> params;
        std::vector<TypeRef> paramTypes;
        bool omittedParameterTypeAnnotation = false;
        Token firstOmittedParameterToken;

        if (!check(TokenType::CLOSE_PAREN)) {
            do {
                std::string paramName;
                size_t paramIndex = paramTypes.size();
                consume(TokenType::IDENTIFIER, "Expected parameter name.");
                paramName = tokenText(m_previous);

                TypeRef paramType = nullptr;
                if (!isTypedTypeAnnotationStart()) {
                    if (hasExpectedFunctionType &&
                        paramIndex < expectedParams.size() &&
                        expectedParams[paramIndex] &&
                        !expectedParams[paramIndex]->isAny()) {
                        paramType = expectedParams[paramIndex];
                    } else {
                        addError(m_previous.line(),
                                 "Type error: parameter '" + paramName +
                                     "' must have a type annotation.");
                        paramType = TypeInfo::makeAny();
                    }
                    if (!omittedParameterTypeAnnotation) {
                        omittedParameterTypeAnnotation = true;
                        firstOmittedParameterToken = m_previous;
                    }
                } else {
                    paramType = parseTypeExprType();
                    if (!paramType) {
                        addError(m_current.line(),
                                 "Type error: expected parameter type annotation.");
                        paramType = TypeInfo::makeAny();
                    }
                }

                if (paramType && paramType->isVoid()) {
                    addError(m_previous.line(),
                             "Type error: parameter '" + paramName +
                                 "' cannot have type 'void'.");
                }

                params.emplace_back(
                    paramName, paramType ? paramType : TypeInfo::makeAny());
                paramTypes.push_back(paramType ? paramType
                                               : TypeInfo::makeAny());
            } while (match(TokenType::COMMA));
        }

        if (hasExpectedFunctionType &&
            paramTypes.size() != expectedParams.size()) {
            addError(m_current.line(),
                     "Type error: closure parameter count mismatch: expected " +
                         std::to_string(expectedParams.size()) + ", got " +
                         std::to_string(paramTypes.size()) + ".");
        }

        consume(TokenType::CLOSE_PAREN, "Expected ')' after parameters.");

        if (check(TokenType::FAT_ARROW)) {
            if (hasLineBreakBeforeCurrent()) {
                addError(m_current.line(),
                         lineLeadingContinuationMessage(m_current.type()));
            }
            if (omittedParameterTypeAnnotation) {
                addError(firstOmittedParameterToken.line(),
                         "Type error: expression-bodied lambdas require "
                         "explicit parameter types.");
            }

            advance();
            if (check(TokenType::OPEN_CURLY)) {
                addError(m_current.line(),
                         "Type error: expression-bodied lambdas do not support "
                         "block bodies; use 'fn(...) { ... }'.");

                advance();
                m_functionContexts.push_back(FunctionCtx{TypeInfo::makeAny()});
                beginScope();
                for (const auto& param : params) {
                    defineSymbol(param.first,
                                 param.second ? param.second
                                              : TypeInfo::makeAny());
                }

                while (!check(TokenType::CLOSE_CURLY) &&
                       !check(TokenType::END_OF_FILE)) {
                    declaration();
                }

                consume(TokenType::CLOSE_CURLY,
                        "Expected '}' after lambda block body.");
                endScope();
                m_functionContexts.pop_back();

                return ExprInfo{
                    TypeInfo::makeFunction(paramTypes, TypeInfo::makeAny()),
                    false, false, "", m_previous.line()};
            }

            m_functionContexts.push_back(FunctionCtx{TypeInfo::makeAny()});
            beginScope();
            for (const auto& param : params) {
                defineSymbol(param.first,
                             param.second ? param.second : TypeInfo::makeAny());
            }

            ExprInfo body = parseExpression();
            recoverLineLeadingContinuation({TokenType::COMMA,
                                            TokenType::CLOSE_PAREN,
                                            TokenType::CLOSE_BRACKET,
                                            TokenType::CLOSE_CURLY});
            rejectUnexpectedTrailingToken({TokenType::COMMA,
                                           TokenType::CLOSE_PAREN,
                                           TokenType::CLOSE_BRACKET,
                                           TokenType::CLOSE_CURLY});

            endScope();
            m_functionContexts.pop_back();

            return ExprInfo{TypeInfo::makeFunction(paramTypes, body.type), false,
                            false, "", body.line};
        }

        TypeRef returnType = TypeInfo::makeAny();
        bool hasExplicitReturnType = false;
        if (isTypedTypeAnnotationStart()) {
            hasExplicitReturnType = true;
            TypeRef parsedReturnType = parseTypeExprType();
            if (!parsedReturnType) {
                addError(m_previous.line(),
                         "Type error: expected return type after parameter list.");
            } else {
                returnType = parsedReturnType;
            }
        } else if (hasExpectedFunctionType && expectedSignature->returnType &&
                   !expectedSignature->returnType->isAny()) {
            returnType = expectedSignature->returnType;
        }

        if (!hasExplicitReturnType && (!returnType || returnType->isAny())) {
            addError(m_current.line(),
                     "Type error: function '<closure>' must declare a return "
                     "type.");
        }

        consume(TokenType::OPEN_CURLY, "Expected '{' before function body.");

        m_functionContexts.push_back(FunctionCtx{returnType});
        beginScope();
        for (const auto& param : params) {
            defineSymbol(param.first,
                         param.second ? param.second : TypeInfo::makeAny());
        }

        while (!check(TokenType::CLOSE_CURLY) &&
               !check(TokenType::END_OF_FILE)) {
            declaration();
        }

        consume(TokenType::CLOSE_CURLY, "Expected '}' after function body.");
        endScope();
        m_functionContexts.pop_back();

        return ExprInfo{TypeInfo::makeFunction(paramTypes, returnType), false,
                        false, "", m_previous.line()};
    }

    ExprInfo parsePrimary() {
        if (match(TokenType::NUMBER)) {
            return ExprInfo{inferNumberLiteralType(m_previous), false, false,
                            "", m_previous.line()};
        }

        if (match(TokenType::STRING)) {
            return ExprInfo{TypeInfo::makeStr(), false, false, "",
                            m_previous.line()};
        }

        if (match(TokenType::TRUE) || match(TokenType::FALSE)) {
            return ExprInfo{TypeInfo::makeBool(), false, false, "",
                            m_previous.line()};
        }

        if (match(TokenType::_NULL) || match(TokenType::TYPE_NULL_KW)) {
            return ExprInfo{TypeInfo::makeNull(), false, false, "",
                            m_previous.line()};
        }

        if (match(TokenType::OPEN_PAREN)) {
            ExprInfo expr = parseExpression();
            consume(TokenType::CLOSE_PAREN, "Expected ')' after expression.");
            return ExprInfo{expr.type, false, false, "", m_previous.line()};
        }

        if (match(TokenType::TYPE_FN)) {
            return parseFunctionLiteralExpr();
        }

        if (match(TokenType::AT)) {
            if (!check(TokenType::IDENTIFIER)) {
                addError(m_current.line(),
                         "Type error: expected directive name after '@'.");
                return ExprInfo{TypeInfo::makeAny(), false, false, "",
                                m_current.line()};
            }
            advance();
            if (tokenText(m_previous) != "import") {
                addError(m_previous.line(),
                         "Type error: unknown '@" + tokenText(m_previous) +
                             "' directive.");
            }
            consume(TokenType::OPEN_PAREN, "Expected '(' after '@import'.");
            consume(TokenType::STRING, "Expected string literal import path.");
            consume(TokenType::CLOSE_PAREN, "Expected ')' after import path.");
            return ExprInfo{TypeInfo::makeAny(), false, false, "",
                            m_previous.line()};
        }

        if (match(TokenType::THIS)) {
            if (m_classContexts.empty()) {
                return ExprInfo{TypeInfo::makeAny(), false, false, "",
                                m_previous.line()};
            }

            return ExprInfo{
                TypeInfo::makeClass(m_classContexts.back().className), false,
                false, "", m_previous.line()};
        }

        if (match(TokenType::SUPER)) {
            if (m_classContexts.empty()) {
                return ExprInfo{TypeInfo::makeAny(), false, false, "",
                                m_previous.line()};
            }

            std::string className = m_classContexts.back().className;
            auto superIt = m_superclassOf.find(className);
            if (superIt == m_superclassOf.end()) {
                return ExprInfo{TypeInfo::makeAny(), false, false, "",
                                m_previous.line()};
            }

            return ExprInfo{TypeInfo::makeClass(superIt->second), false, false,
                            "", m_previous.line()};
        }

        if (match(TokenType::OPEN_BRACKET)) {
            TypeRef elementType = nullptr;
            if (!check(TokenType::CLOSE_BRACKET)) {
                do {
                    ExprInfo item = parseExpression();
                    if (!elementType) {
                        elementType = item.type;
                        continue;
                    }

                    TypeRef merged = mergeInferredTypes(elementType, item.type);
                    if (merged && merged->isAny() && !elementType->isAny() &&
                        !item.type->isAny() &&
                        !isAssignableType(item.type, elementType) &&
                        !isAssignableType(elementType, item.type)) {
                        addError(item.line ? item.line : m_previous.line(),
                                 "Type error: array literal elements must "
                                 "have a consistent type.");
                    }
                    elementType = merged;
                } while (match(TokenType::COMMA));
            }
            consume(TokenType::CLOSE_BRACKET,
                    "Expected ']' after array literal.");
            return ExprInfo{
                TypeInfo::makeArray(elementType ? elementType
                                                : TypeInfo::makeAny()),
                false, false, "", m_previous.line()};
        }

        if (match(TokenType::OPEN_CURLY)) {
            TypeRef keyType = nullptr;
            TypeRef valueType = nullptr;
            if (!check(TokenType::CLOSE_CURLY)) {
                do {
                    ExprInfo keyExpr = parseExpression();
                    if (!keyType) {
                        keyType = keyExpr.type;
                    } else {
                        TypeRef mergedKey =
                            mergeInferredTypes(keyType, keyExpr.type);
                        if (mergedKey && mergedKey->isAny() &&
                            !keyType->isAny() && !keyExpr.type->isAny() &&
                            !isAssignableType(keyExpr.type, keyType) &&
                            !isAssignableType(keyType, keyExpr.type)) {
                            addError(
                                keyExpr.line ? keyExpr.line : m_previous.line(),
                                "Type error: dict literal keys must have "
                                "a consistent type.");
                        }
                        keyType = mergedKey;
                    }
                    consume(TokenType::COLON,
                            "Expected ':' between dictionary key and value.");
                    ExprInfo valueExpr = parseExpression();
                    if (!valueType) {
                        valueType = valueExpr.type;
                    } else {
                        TypeRef mergedValue =
                            mergeInferredTypes(valueType, valueExpr.type);
                        if (mergedValue && mergedValue->isAny() &&
                            !valueType->isAny() && !valueExpr.type->isAny() &&
                            !isAssignableType(valueExpr.type, valueType) &&
                            !isAssignableType(valueType, valueExpr.type)) {
                            addError(valueExpr.line ? valueExpr.line
                                                    : m_previous.line(),
                                     "Type error: dict literal values must "
                                     "have a consistent type.");
                        }
                        valueType = mergedValue;
                    }
                } while (match(TokenType::COMMA));
            }
            consume(TokenType::CLOSE_CURLY,
                    "Expected '}' after dictionary literal.");
            return ExprInfo{
                TypeInfo::makeDict(keyType ? keyType : TypeInfo::makeAny(),
                                   valueType ? valueType : TypeInfo::makeAny()),
                false, false, "", m_previous.line()};
        }

        if (check(TokenType::IDENTIFIER) || check(TokenType::TYPE) ||
            isTypeToken(m_current.type())) {
            Token token = m_current;
            advance();
            std::string name(token.start(), token.length());
            const SymbolInfo* symbol = resolveSymbolInfo(name);
            TypeRef type = symbol ? symbol->type : resolveSymbol(name);

            if (!type && m_classNames.find(name) != m_classNames.end()) {
                return ExprInfo{TypeInfo::makeClass(name), false, true, name,
                                token.line()};
            }

            const bool isClass = m_classNames.find(name) != m_classNames.end();
            const bool assignable = !isClass;
            return ExprInfo{type ? type : TypeInfo::makeAny(), assignable,
                            isClass, name, token.line(),
                            symbol ? symbol->isConst : false};
        }

        addError(m_current.line(), "Type error: expected expression.");
        if (!check(TokenType::END_OF_FILE)) {
            advance();
        }
        return ExprInfo{TypeInfo::makeAny(), false, false, "",
                        m_previous.line()};
    }

    void parseExpressionStatement() {
        parseExpression();
        recoverLineLeadingContinuation();
        rejectUnexpectedTrailingToken();
        rejectStraySemicolon();
    }

    void parseReturnStatement() {
        size_t line = m_previous.line();
        if (m_functionContexts.empty()) {
            addError(line, "Type error: cannot return from top-level code.");
        }

        if (check(TokenType::SEMI_COLON) || check(TokenType::CLOSE_CURLY) ||
            check(TokenType::END_OF_FILE)) {
            if (check(TokenType::SEMI_COLON)) {
                rejectStraySemicolon();
            }
            if (!m_functionContexts.empty()) {
                TypeRef expected = m_functionContexts.back().returnType;
                if (expected && !expected->isVoid()) {
                    addError(line,
                             "Type error: function expects return type '" +
                                 expected->toString() + "'.");
                }
            }
            return;
        }

        ExprInfo value = parseExpression();
        recoverLineLeadingContinuation();
        rejectUnexpectedTrailingToken();
        rejectStraySemicolon();

        if (!m_functionContexts.empty()) {
            TypeRef expected = m_functionContexts.back().returnType;
            if (expected && !isAssignableType(value.type, expected)) {
                addError(line, "Type error: cannot return '" +
                                   value.type->toString() +
                                   "' from function returning '" +
                                   expected->toString() + "'.");
            }
        }
    }

    void parseIfStatement() {
        consume(TokenType::OPEN_PAREN, "Expected '(' after 'if'.");
        ExprInfo cond = parseExpression();
        recoverLineLeadingContinuation({TokenType::CLOSE_PAREN});
        if (!(cond.type->kind == TypeKind::BOOL || cond.type->isAny())) {
            addError(cond.line ? cond.line : m_previous.line(),
                     "Type error: if condition must be bool.");
        }
        consume(TokenType::CLOSE_PAREN, "Expected ')' after condition.");
        statement();
        if (match(TokenType::ELSE)) {
            statement();
        }
    }

    void parseWhileStatement() {
        consume(TokenType::OPEN_PAREN, "Expected '(' after 'while'.");
        ExprInfo cond = parseExpression();
        recoverLineLeadingContinuation({TokenType::CLOSE_PAREN});
        if (!(cond.type->kind == TypeKind::BOOL || cond.type->isAny())) {
            addError(cond.line ? cond.line : m_previous.line(),
                     "Type error: while condition must be bool.");
        }
        consume(TokenType::CLOSE_PAREN, "Expected ')' after condition.");
        statement();
    }

    void parseForStatement() {
        beginScope();
        consume(TokenType::OPEN_PAREN, "Expected '(' after 'for'.");

        if (match(TokenType::SEMI_COLON)) {
        } else if (check(TokenType::VAR) || check(TokenType::CONST)) {
            bool isConst = match(TokenType::CONST);
            if (!isConst) {
                consume(TokenType::VAR, "Expected 'var' or 'const'.");
            }
            consume(TokenType::IDENTIFIER, "Expected loop variable name.");
            std::string variableName = tokenText(m_previous);
            size_t variableLine = m_previous.line();
            TypeRef declaredType = parseTypeExprType();
            if (!declaredType) {
                addError(m_current.line(),
                         "Type error: expected type after loop variable name.");
                declaredType = TypeInfo::makeAny();
            }
            if (declaredType->isVoid()) {
                addError(m_current.line(),
                         "Type error: variables cannot have type 'void'.");
            }

            if (match(TokenType::COLON)) {
                ExprInfo iterable = parseExpression();
                recoverLineLeadingContinuation({TokenType::CLOSE_PAREN});
                consume(TokenType::CLOSE_PAREN,
                        "Expected ')' after foreach iterable expression.");

                TypeRef inferredLoopType = TypeInfo::makeAny();
                if (iterable.type && iterable.type->kind == TypeKind::ARRAY) {
                    inferredLoopType = iterable.type->elementType
                                           ? iterable.type->elementType
                                           : TypeInfo::makeAny();
                } else if (iterable.type &&
                           iterable.type->kind == TypeKind::DICT) {
                    inferredLoopType = iterable.type->keyType
                                           ? iterable.type->keyType
                                           : TypeInfo::makeAny();
                } else if (iterable.type &&
                           iterable.type->kind == TypeKind::SET) {
                    inferredLoopType = iterable.type->elementType
                                           ? iterable.type->elementType
                                           : TypeInfo::makeAny();
                } else if (iterable.type && iterable.type->isAny()) {
                    inferredLoopType = TypeInfo::makeAny();
                } else {
                    addError(iterable.line ? iterable.line : m_previous.line(),
                             "Type error: foreach expects Array<T>, Dict<K, "
                             "V>, or Set<T>.");
                }

                if (!isAssignableType(inferredLoopType, declaredType)) {
                    addError(variableLine,
                             "Type error: cannot assign '" +
                                 inferredLoopType->toString() +
                                 "' to variable '" + variableName +
                                 "' of type '" + declaredType->toString() +
                                 "'.");
                }

                defineSymbol(variableName, declaredType);
                recordDeclarationType(variableName, declaredType,
                                      variableLine, isConst);
                statement();
                endScope();
                return;
            }

            consume(TokenType::EQUAL,
                    "Expected '=' in loop variable declaration "
                    "(initializer is required).");
            ExprInfo initializer = parseExpression();
            recoverLineLeadingContinuation({TokenType::SEMI_COLON});
            if (!isAssignableType(initializer.type, declaredType)) {
                addError(variableLine, "Type error: cannot assign '" +
                                           initializer.type->toString() +
                                           "' to variable '" + variableName +
                                           "' of type '" +
                                           declaredType->toString() + "'.");
            }
            consume(TokenType::SEMI_COLON,
                    "Expected ';' after loop initializer.");
            defineSymbol(variableName, declaredType, isConst);
            recordDeclarationType(variableName, declaredType, variableLine,
                                  isConst);
        } else {
            parseExpression();
            recoverLineLeadingContinuation({TokenType::SEMI_COLON});
            consume(TokenType::SEMI_COLON,
                    "Expected ';' after loop initializer.");
        }

        if (!check(TokenType::SEMI_COLON)) {
            ExprInfo cond = parseExpression();
            recoverLineLeadingContinuation({TokenType::SEMI_COLON});
            if (!(cond.type->kind == TypeKind::BOOL || cond.type->isAny())) {
                addError(cond.line ? cond.line : m_previous.line(),
                         "Type error: for condition must be bool.");
            }
        }
        consume(TokenType::SEMI_COLON, "Expected ';' after loop condition.");

        if (!check(TokenType::CLOSE_PAREN)) {
            parseExpression();
            recoverLineLeadingContinuation({TokenType::CLOSE_PAREN});
        }
        consume(TokenType::CLOSE_PAREN, "Expected ')' after for clauses.");

        statement();
        endScope();
    }

    void parseBlock() {
        beginScope();
        while (!check(TokenType::CLOSE_CURLY) &&
               !check(TokenType::END_OF_FILE)) {
            declaration();
        }
        consume(TokenType::CLOSE_CURLY, "Expected '}' after block.");
        endScope();
    }

    void parseTypedVarDeclaration() {
        if (!check(TokenType::CONST) && !check(TokenType::VAR)) {
            addError(m_current.line(), "Type error: expected 'var' or 'const'.");
            return;
        }

        bool isConst = match(TokenType::CONST);
        if (!isConst) {
            consume(TokenType::VAR, "Expected 'var' or 'const'.");
        }

        auto parseImportExpr = [&]() -> ExprInfo {
            consume(TokenType::AT, "Expected '@' before import.");
            if (!check(TokenType::IDENTIFIER)) {
                addError(m_current.line(),
                         "Type error: expected directive after '@'.");
                return ExprInfo{TypeInfo::makeAny(), false, false, "",
                                m_current.line()};
            }
            advance();
            if (tokenText(m_previous) != "import") {
                addError(m_previous.line(),
                         "Type error: unknown '@" + tokenText(m_previous) +
                             "' directive.");
            }
            consume(TokenType::OPEN_PAREN, "Expected '(' after '@import'.");
            consume(TokenType::STRING, "Expected string literal import path.");
            consume(TokenType::CLOSE_PAREN, "Expected ')' after import path.");
            return ExprInfo{TypeInfo::makeAny(), false, false, "",
                            m_previous.line()};
        };

        if (match(TokenType::OPEN_CURLY)) {
            std::vector<std::pair<std::string, size_t>> bindings;
            if (!check(TokenType::CLOSE_CURLY)) {
                do {
                    consume(TokenType::IDENTIFIER,
                            "Expected export name in destructured import.");
                    std::string localName = tokenText(m_previous);
                    size_t line = m_previous.line();
                    if (match(TokenType::AS_KW)) {
                        consume(TokenType::IDENTIFIER,
                                "Expected local alias after 'as'.");
                        localName = tokenText(m_previous);
                        line = m_previous.line();
                    }
                    bindings.emplace_back(localName, line);
                } while (match(TokenType::COMMA));
            }
            consume(TokenType::CLOSE_CURLY, "Expected '}' after binding list.");
            consume(TokenType::EQUAL, "Expected '=' after destructured binding.");
            parseImportExpr();
            rejectStraySemicolon();
            for (const auto& binding : bindings) {
                defineSymbol(binding.first, TypeInfo::makeAny(), true);
                recordDeclarationType(binding.first, TypeInfo::makeAny(),
                                      binding.second, true);
            }
            return;
        }

        consume(TokenType::IDENTIFIER, "Expected variable name after mutability.");
        Token nameToken = m_previous;
        std::string name = tokenText(nameToken);

        TypeRef declaredType = TypeInfo::makeAny();
        bool omittedType = false;
        if (check(TokenType::EQUAL)) {
            omittedType = true;
            if (peekToken().type() != TokenType::AT ||
                tokenAt(2).type() != TokenType::IDENTIFIER ||
                tokenText(tokenAt(2)) != "import") {
                addError(nameToken.line(),
                         "Type error: variables require an explicit type unless initialized from '@import(...)'.");
            }
        } else {
            declaredType = parseTypeExprType();
            if (!declaredType) {
                addError(m_current.line(),
                         "Type error: expected type after variable name.");
                return;
            }
            if (declaredType->isVoid()) {
                addError(m_current.line(),
                         "Type error: variables cannot have type 'void'.");
            }
        }

        consume(TokenType::EQUAL,
                "Expected '=' in variable declaration (initializer is required).");
        ExprInfo initializer;
        if (!omittedType && declaredType && declaredType->kind == TypeKind::FUNCTION &&
            check(TokenType::TYPE_FN)) {
            advance();
            initializer = parseFunctionLiteralExpr(declaredType);
        } else {
            initializer = check(TokenType::AT) ? parseImportExpr()
                                               : parseExpression();
            recoverLineLeadingContinuation();
            rejectUnexpectedTrailingToken();
        }

        if (!omittedType && !isAssignableType(initializer.type, declaredType)) {
            addError(nameToken.line(), "Type error: cannot assign '" +
                                           initializer.type->toString() +
                                           "' to variable '" + name +
                                           "' of type '" +
                                           declaredType->toString() + "'.");
        }

        rejectStraySemicolon();
        TypeRef finalType = omittedType ? initializer.type : declaredType;
        defineSymbol(name, finalType, isConst);
        recordDeclarationType(name, finalType, nameToken.line(), isConst);
    }

    void parseFunctionCommon(const std::string& functionName,
                             const TypeRef& signature, bool isMethod) {
        consume(TokenType::OPEN_PAREN, "Expected '(' after function name.");

        std::vector<std::pair<std::string, TypeRef>> params;
        if (!check(TokenType::CLOSE_PAREN)) {
            do {
                std::string paramName;
                consume(TokenType::IDENTIFIER, "Expected parameter name.");
                paramName = tokenText(m_previous);

                TypeRef paramType = nullptr;
                if (!isTypedTypeAnnotationStart()) {
                    addError(m_previous.line(),
                             "Type error: parameter '" + paramName +
                                 "' must have a type annotation.");
                    paramType = TypeInfo::makeAny();
                } else {
                    paramType = parseTypeExprType();
                    if (!paramType) {
                        addError(m_current.line(),
                                 "Type error: expected parameter type annotation.");
                        paramType = TypeInfo::makeAny();
                    }
                }

                if (paramType && paramType->isVoid()) {
                    addError(m_previous.line(),
                             "Type error: parameter '" + paramName +
                                 "' cannot have type 'void'.");
                }

                params.emplace_back(
                    paramName, paramType ? paramType : TypeInfo::makeAny());
            } while (match(TokenType::COMMA));
        }

        consume(TokenType::CLOSE_PAREN, "Expected ')' after parameters.");

        TypeRef returnType = TypeInfo::makeAny();
        bool hasExplicitReturnType = false;
        if (isTypedTypeAnnotationStart()) {
            hasExplicitReturnType = true;
            TypeRef parsedReturnType = parseTypeExprType();
            if (!parsedReturnType) {
                addError(m_previous.line(),
                         "Type error: expected return type after parameter list.");
            } else {
                returnType = parsedReturnType;
            }
        }

        const bool hasSignatureReturnType =
            signature && signature->kind == TypeKind::FUNCTION &&
            signature->returnType && !signature->returnType->isAny();
        const bool isInitializer = isMethod && functionName == "init";

        if (!hasExplicitReturnType && hasSignatureReturnType) {
            returnType = signature->returnType;
        }

        if (!hasExplicitReturnType && !hasSignatureReturnType &&
            !isInitializer) {
            addError(m_current.line(),
                     "Type error: function '" + functionName +
                         "' must declare a return type.");
        }

        if (isMethod && !m_classContexts.empty()) {
            std::vector<TypeRef> paramTypes;
            paramTypes.reserve(params.size());
            for (const auto& param : params) {
                paramTypes.push_back(param.second ? param.second
                                                 : TypeInfo::makeAny());
            }
            m_classMethodSignatures[m_classContexts.back().className]
                                   [functionName] =
                TypeInfo::makeFunction(paramTypes, returnType);
        }

        consume(TokenType::OPEN_CURLY, "Expected '{' before function body.");

        m_functionContexts.push_back(FunctionCtx{returnType});
        beginScope();
        for (const auto& param : params) {
            defineSymbol(param.first,
                         param.second ? param.second : TypeInfo::makeAny());
        }

        while (!check(TokenType::CLOSE_CURLY) &&
               !check(TokenType::END_OF_FILE)) {
            declaration();
        }

        consume(TokenType::CLOSE_CURLY, "Expected '}' after function body.");
        endScope();
        m_functionContexts.pop_back();
    }

    void parseFunctionDeclaration() {
        consume(TokenType::IDENTIFIER, "Expected function name.");
        size_t functionLine = m_previous.line();
        std::string name = tokenText(m_previous);

        TypeRef functionType = TypeInfo::makeAny();
        auto it = m_functionSignatures.find(name);
        if (it != m_functionSignatures.end()) {
            functionType = it->second;
        }
        defineSymbol(name, functionType);
        recordDeclarationType(name, functionType, functionLine);

        parseFunctionCommon(name, functionType, false);
    }

    void parseTypeDeclaration() {
        consume(TokenType::IDENTIFIER, "Expected type name.");
        size_t typeLine = m_previous.line();
        std::string typeName = tokenText(m_previous);

        if (!check(TokenType::STRUCT) && !check(TokenType::LESS) &&
            !check(TokenType::OPEN_CURLY)) {
            TypeRef aliasedType = parseTypeExprType();
            if (!aliasedType) {
                addError(m_current.line(),
                         "Type error: expected aliased type or 'struct'.");
                return;
            }
            m_typeAliases[typeName] = aliasedType;
            rejectStraySemicolon();
            return;
        }

        if (match(TokenType::STRUCT)) {
        }

        defineSymbol(typeName, TypeInfo::makeClass(typeName));
        recordDeclarationType(typeName, TypeInfo::makeClass(typeName),
                              typeLine);
        m_classContexts.push_back(ClassCtx{typeName});

        if (match(TokenType::LESS)) {
            consume(TokenType::IDENTIFIER, "Expected superclass name.");
            std::string superclassName = tokenText(m_previous);
            if (superclassName == typeName) {
                addError(m_previous.line(),
                         "Type error: a type cannot inherit from itself.");
            }
            if (m_classNames.find(superclassName) == m_classNames.end()) {
                addError(m_previous.line(), "Type error: unknown superclass '" +
                                                superclassName + "'.");
            }
            m_superclassOf[typeName] = superclassName;
        }

        consume(TokenType::OPEN_CURLY, "Expected '{' before struct body.");
        while (!check(TokenType::CLOSE_CURLY) &&
               !check(TokenType::END_OF_FILE)) {
            if (check(TokenType::SEMI_COLON)) {
                rejectStraySemicolon();
                continue;
            }

            if (isTypeToken(m_current.type()) &&
                m_current.type() != TokenType::TYPE_FN) {
                addError(m_current.line(),
                         "Type error: expected struct field name or method.");
                advance();
                continue;
            }

            std::vector<int> annotatedOperators;

            while (match(TokenType::AT)) {
                consume(TokenType::IDENTIFIER,
                        "Expected annotation name after '@'.");
                if (tokenText(m_previous) != "operator") {
                    addError(m_previous.line(),
                             "Type error: unknown annotation '@" +
                                 tokenText(m_previous) + "'.");
                }
                consume(TokenType::OPEN_PAREN,
                        "Expected '(' after annotation name.");
                consume(TokenType::STRING,
                        "Expected string literal annotation value.");
                std::string literal = tokenText(m_previous);
                consume(TokenType::CLOSE_PAREN,
                        "Expected ')' after annotation value.");
                if (literal.size() >= 2) {
                    int op = parseOperatorAnnotationToken(
                        std::string_view(literal.data() + 1, literal.size() - 2));
                    if (op == -1) {
                        addError(m_previous.line(),
                                 "Type error: unsupported operator annotation.");
                    } else {
                        annotatedOperators.push_back(op);
                    }
                }
            }

            if (match(TokenType::TYPE_FN)) {
                consume(TokenType::IDENTIFIER, "Expected method name.");
                std::string memberName = tokenText(m_previous);
                TypeRef placeholder =
                    TypeInfo::makeFunction({}, TypeInfo::makeAny());
                m_classMethodSignatures[typeName][memberName] = placeholder;
                parseFunctionCommon(memberName, placeholder, true);
                auto methodIt =
                    m_functionSignatures.find(memberName);  // unused fallback
                (void)methodIt;
                for (int op : annotatedOperators) {
                    m_classOperatorMethods[typeName][op] = memberName;
                }
                continue;
            }

            if (!annotatedOperators.empty()) {
                addError(m_current.line(),
                         "Type error: @operator can only annotate a method.");
            }

            consume(TokenType::IDENTIFIER, "Expected struct field name.");
            std::string memberName = tokenText(m_previous);
            TypeRef memberType = parseTypeExprType();
            if (!memberType) {
                addError(m_current.line(),
                         "Type error: expected field type after member name.");
                continue;
            }
            if (memberType->isVoid()) {
                addError(m_previous.line(),
                         "Type error: struct field '" + memberName +
                             "' cannot have type 'void'.");
            }
            m_classFieldTypes[typeName][memberName] =
                memberType ? memberType : TypeInfo::makeAny();
            rejectStraySemicolon();
        }

        consume(TokenType::CLOSE_CURLY, "Expected '}' after struct body.");
        m_classContexts.pop_back();
    }

    void statement() {
        if (check(TokenType::SEMI_COLON)) {
            rejectStraySemicolon();
            return;
        }

        if (match(TokenType::PRINT)) {
            consume(TokenType::OPEN_PAREN, "Expected '(' after 'print'.");
            parseExpression();
            recoverLineLeadingContinuation({TokenType::CLOSE_PAREN});
            consume(TokenType::CLOSE_PAREN,
                    "Expected ')' after print argument.");
            rejectStraySemicolon();
            return;
        }

        if (match(TokenType::IF)) {
            parseIfStatement();
            return;
        }

        if (match(TokenType::WHILE)) {
            parseWhileStatement();
            return;
        }

        if (match(TokenType::FOR)) {
            parseForStatement();
            return;
        }

        if (match(TokenType::_RETURN)) {
            parseReturnStatement();
            return;
        }

        if (match(TokenType::OPEN_CURLY)) {
            parseBlock();
            return;
        }

        parseExpressionStatement();
    }

    void declaration() {
        if (check(TokenType::SEMI_COLON)) {
            rejectStraySemicolon();
            return;
        }

        if (match(TokenType::TYPE)) {
            parseTypeDeclaration();
            return;
        }

        if (check(TokenType::TYPE_FN) &&
            peekToken().type() == TokenType::IDENTIFIER) {
            advance();
            parseFunctionDeclaration();
            return;
        }

        if (check(TokenType::CONST) || check(TokenType::VAR)) {
            parseTypedVarDeclaration();
            return;
        }

        statement();
    }

   public:
    CheckerImpl(
        std::string_view source,
        const std::unordered_set<std::string>& classNames,
        const std::unordered_map<std::string, TypeRef>& typeAliases,
        const std::unordered_map<std::string, TypeRef>& functionSignatures,
        std::vector<TypeError>& out)
        : m_scanner(source),
          m_classNames(classNames),
          m_typeAliases(typeAliases),
          m_functionSignatures(functionSignatures),
          m_errors(out) {
        m_scopes.emplace_back();

        for (const auto& entry : functionSignatures) {
            m_scopes.front()[entry.first] =
                SymbolInfo{entry.second, false};
        }

        m_current = nextToken();
    }

    void run() {
        while (!check(TokenType::END_OF_FILE)) {
            declaration();
        }
    }

    TypeCheckerMetadata metadata() const {
        TypeCheckerMetadata out;
        out.classFieldTypes = m_classFieldTypes;
        out.classMethodSignatures = m_classMethodSignatures;
        out.superclassOf = m_superclassOf;
        out.declarationTypes = m_declarationTypes;
        for (const auto& symbolName : m_declaredGlobalSymbols) {
            auto it = m_scopes.front().find(symbolName);
            if (it != m_scopes.front().end()) {
                out.topLevelSymbolTypes[symbolName] = it->second.type;
            }
        }
        return out;
    }
};

}  // namespace

bool TypeChecker::collectSymbols(
    std::string_view source, std::unordered_set<std::string>& outClassNames,
    std::unordered_map<std::string, TypeRef>& outFunctionSignatures,
    std::unordered_map<std::string, TypeRef>* outTypeAliases) {
    outClassNames.clear();
    if (outTypeAliases != nullptr) {
        outTypeAliases->clear();
    }

    {
        Scanner scanner(source);
        while (true) {
            Token token = scanner.nextToken();
            if (token.type() == TokenType::END_OF_FILE) {
                break;
            }

            if (token.type() != TokenType::TYPE) {
                continue;
            }

            Token name = scanner.nextToken();
            while (name.type() == TokenType::ERROR) {
                name = scanner.nextToken();
            }

            if (name.type() != TokenType::IDENTIFIER) {
                continue;
            }

            std::string typeName(name.start(), name.length());
            Token next = scanner.nextToken();
            while (next.type() == TokenType::ERROR) {
                next = scanner.nextToken();
            }

            if (next.type() == TokenType::STRUCT || next.type() == TokenType::LESS ||
                next.type() == TokenType::OPEN_CURLY) {
                outClassNames.emplace(typeName);
            }
        }
    }

    Scanner scanner(source);
    bool hasBufferedToken = false;
    Token bufferedToken;

    auto nextToken = [&]() -> Token {
        if (hasBufferedToken) {
            hasBufferedToken = false;
            return bufferedToken;
        }
        return scanner.nextToken();
    };

    auto peekToken = [&]() -> Token {
        if (!hasBufferedToken) {
            bufferedToken = scanner.nextToken();
            hasBufferedToken = true;
        }
        return bufferedToken;
    };

    auto tokenToType = [&](const Token& token) -> TypeRef {
        switch (token.type()) {
            case TokenType::TYPE_I8:
                return TypeInfo::makeI8();
            case TokenType::TYPE_I16:
                return TypeInfo::makeI16();
            case TokenType::TYPE_I32:
                return TypeInfo::makeI32();
            case TokenType::TYPE_I64:
                return TypeInfo::makeI64();
            case TokenType::TYPE_U8:
                return TypeInfo::makeU8();
            case TokenType::TYPE_U16:
                return TypeInfo::makeU16();
            case TokenType::TYPE_U32:
                return TypeInfo::makeU32();
            case TokenType::TYPE_U64:
                return TypeInfo::makeU64();
            case TokenType::TYPE_USIZE:
                return TypeInfo::makeUSize();
            case TokenType::TYPE_F32:
                return TypeInfo::makeF32();
            case TokenType::TYPE_F64:
                return TypeInfo::makeF64();
            case TokenType::TYPE_BOOL:
                return TypeInfo::makeBool();
            case TokenType::TYPE_STR:
                return TypeInfo::makeStr();
            case TokenType::TYPE_FN:
                return nullptr;
            case TokenType::TYPE_VOID:
                return TypeInfo::makeVoid();
            case TokenType::TYPE_NULL_KW:
                return TypeInfo::makeNull();
            case TokenType::IDENTIFIER: {
                std::string className(token.start(), token.length());
                if (outTypeAliases != nullptr) {
                    auto aliasIt = outTypeAliases->find(className);
                    if (aliasIt != outTypeAliases->end()) {
                        return aliasIt->second;
                    }
                }
                if (outClassNames.find(className) != outClassNames.end()) {
                    return TypeInfo::makeClass(className);
                }
                return nullptr;
            }
            default:
                return nullptr;
        }
    };

    std::function<TypeRef(const Token&)> parseTypeFromToken;
    parseTypeFromToken = [&](const Token& token) -> TypeRef {
        auto applyOptionalSuffix = [&](TypeRef baseType) -> TypeRef {
            if (!baseType) {
                return nullptr;
            }

            while (peekToken().type() == TokenType::QUESTION) {
                nextToken();
                baseType = TypeInfo::makeOptional(baseType);
            }

            return baseType;
        };

        if (token.type() == TokenType::TYPE_FN) {
            Token openParen = nextToken();
            if (openParen.type() != TokenType::OPEN_PAREN) {
                return nullptr;
            }

            std::vector<TypeRef> params;
            Token cursor = nextToken();
            if (cursor.type() != TokenType::CLOSE_PAREN) {
                while (true) {
                    TypeRef parameterType = parseTypeFromToken(cursor);
                    if (!parameterType || parameterType->isVoid()) {
                        return nullptr;
                    }
                    params.push_back(parameterType);

                    Token delimiter = nextToken();
                    if (delimiter.type() == TokenType::COMMA) {
                        cursor = nextToken();
                        continue;
                    }
                    if (delimiter.type() != TokenType::CLOSE_PAREN) {
                        return nullptr;
                    }
                    break;
                }
            }

            Token arrow = nextToken();
            TypeRef returnType = parseTypeFromToken(arrow);
            if (!returnType) {
                return nullptr;
            }

            return applyOptionalSuffix(
                TypeInfo::makeFunction(params, returnType));
        }

        if (isTypeToken(token.type())) {
            return applyOptionalSuffix(tokenToType(token));
        }

        if (token.type() != TokenType::IDENTIFIER) {
            return nullptr;
        }

        std::string name(token.start(), token.length());
        if (isHandleTypeNameText(name)) {
            Token lessToken = nextToken();
            if (lessToken.type() != TokenType::LESS) {
                return nullptr;
            }

            Token namespaceToken = nextToken();
            if (namespaceToken.type() != TokenType::IDENTIFIER) {
                return nullptr;
            }
            std::string packageNamespace(namespaceToken.start(),
                                         namespaceToken.length());
            Token firstColon = nextToken();
            if (firstColon.type() != TokenType::COLON ||
                !isValidPackageIdPart(packageNamespace)) {
                return nullptr;
            }

            Token packageToken = nextToken();
            if (packageToken.type() != TokenType::IDENTIFIER) {
                return nullptr;
            }
            std::string packageName(packageToken.start(), packageToken.length());
            Token secondColon = nextToken();
            if (secondColon.type() != TokenType::COLON ||
                !isValidPackageIdPart(packageName)) {
                return nullptr;
            }

            Token typeToken = nextToken();
            if (typeToken.type() != TokenType::IDENTIFIER) {
                return nullptr;
            }
            std::string typeName(typeToken.start(), typeToken.length());
            Token greaterToken = nextToken();
            if (greaterToken.type() != TokenType::GREATER ||
                !isValidHandleTypeName(typeName)) {
                return nullptr;
            }

            return applyOptionalSuffix(TypeInfo::makeNativeHandle(
                makePackageId(packageNamespace, packageName), typeName));
        }

        if (isCollectionTypeNameText(name)) {
            Token lessToken = nextToken();
            if (lessToken.type() != TokenType::LESS) {
                return nullptr;
            }

            if (name == "Array") {
                TypeRef elementType = parseTypeFromToken(nextToken());
                Token greaterToken = nextToken();
                if (!elementType || greaterToken.type() != TokenType::GREATER) {
                    return nullptr;
                }
                return applyOptionalSuffix(TypeInfo::makeArray(elementType));
            }

            if (name == "Set") {
                TypeRef elementType = parseTypeFromToken(nextToken());
                Token greaterToken = nextToken();
                if (!elementType || greaterToken.type() != TokenType::GREATER) {
                    return nullptr;
                }
                return applyOptionalSuffix(TypeInfo::makeSet(elementType));
            }

            TypeRef keyType = parseTypeFromToken(nextToken());
            if (!keyType) {
                return nullptr;
            }
            Token commaToken = nextToken();
            if (commaToken.type() != TokenType::COMMA) {
                return nullptr;
            }
            TypeRef valueType = parseTypeFromToken(nextToken());
            if (!valueType) {
                return nullptr;
            }
            Token greaterToken = nextToken();
            if (greaterToken.type() != TokenType::GREATER) {
                return nullptr;
            }
            return applyOptionalSuffix(TypeInfo::makeDict(keyType, valueType));
        }

        if (outClassNames.find(name) != outClassNames.end()) {
            return applyOptionalSuffix(TypeInfo::makeClass(name));
        }

        if (outTypeAliases != nullptr) {
            auto aliasIt = outTypeAliases->find(name);
            if (aliasIt != outTypeAliases->end()) {
                return applyOptionalSuffix(aliasIt->second);
            }
        }

        return nullptr;
    };

    int classDepth = 0;

    while (true) {
        Token token = nextToken();
        if (token.type() == TokenType::END_OF_FILE) {
            return true;
        }

        if (token.type() == TokenType::OPEN_CURLY) {
            classDepth++;
            continue;
        }
        if (token.type() == TokenType::CLOSE_CURLY) {
            if (classDepth > 0) {
                classDepth--;
            }
            continue;
        }

        if (classDepth == 0 && token.type() == TokenType::TYPE) {
            Token typeName = nextToken();
            if (typeName.type() != TokenType::IDENTIFIER) {
                continue;
            }

            Token shape = nextToken();
            if (shape.type() == TokenType::STRUCT || shape.type() == TokenType::LESS ||
                shape.type() == TokenType::OPEN_CURLY) {
                if (shape.type() == TokenType::OPEN_CURLY) {
                    classDepth++;
                }
                continue;
            }

            TypeRef aliasedType = parseTypeFromToken(shape);
            if (aliasedType && outTypeAliases != nullptr) {
                (*outTypeAliases)[std::string(typeName.start(), typeName.length())] =
                    aliasedType;
            }
            continue;
        }

        if (classDepth != 0 || token.type() != TokenType::TYPE_FN) {
            continue;
        }

        if (peekToken().type() != TokenType::IDENTIFIER) {
            continue;
        }

        Token functionName = nextToken();
        if (functionName.type() != TokenType::IDENTIFIER) {
            continue;
        }

        if (peekToken().type() != TokenType::OPEN_PAREN) {
            continue;
        }

        Token openParen = nextToken();
        if (openParen.type() != TokenType::OPEN_PAREN) {
            continue;
        }

        std::vector<TypeRef> params;
        Token current = nextToken();
        if (current.type() != TokenType::CLOSE_PAREN) {
            while (true) {
                TypeRef parameterType = nullptr;

                Token nameToken = current;
                if (nameToken.type() != TokenType::IDENTIFIER) {
                    break;
                }

                Token typeToken = nextToken();
                if (isTypeToken(typeToken.type()) ||
                    typeToken.type() == TokenType::IDENTIFIER ||
                    typeToken.type() == TokenType::TYPE_FN) {
                    parameterType = parseTypeFromToken(typeToken);
                    if (!parameterType) {
                        parameterType = TypeInfo::makeAny();
                    }
                } else {
                    parameterType = TypeInfo::makeAny();
                    break;
                }

                params.push_back(parameterType);

                Token delimiter = nextToken();
                if (delimiter.type() == TokenType::COMMA) {
                    current = nextToken();
                    continue;
                }

                if (delimiter.type() != TokenType::CLOSE_PAREN) {
                    break;
                }

                current = delimiter;
                break;
            }
        }

        TypeRef returnType = TypeInfo::makeAny();
        Token maybeReturn = nextToken();
        if (maybeReturn.type() != TokenType::OPEN_CURLY &&
            maybeReturn.type() != TokenType::END_OF_FILE) {
            TypeRef typedReturn = parseTypeFromToken(maybeReturn);
            if (typedReturn) {
                returnType = typedReturn;
            }
        }

        std::string functionNameText(functionName.start(),
                                     functionName.length());
        outFunctionSignatures[functionNameText] =
            TypeInfo::makeFunction(params, returnType);
    }
}

bool TypeChecker::check(
    std::string_view source, const std::unordered_set<std::string>& classNames,
    const std::unordered_map<std::string, TypeRef>& typeAliases,
    const std::unordered_map<std::string, TypeRef>& functionSignatures,
    std::vector<TypeError>& out, TypeCheckerMetadata* outMetadata) {
    CheckerImpl checker(source, classNames, typeAliases, functionSignatures,
                        out);
    checker.run();
    if (outMetadata) {
        *outMetadata = checker.metadata();
    }
    return out.empty();
}
