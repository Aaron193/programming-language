#include "TypeChecker.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Scanner.hpp"

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

bool isCollectionTypeNameText(std::string_view name) {
    return name == "Array" || name == "Dict" || name == "Set";
}

class CheckerImpl {
   private:
    struct ExprInfo {
        TypeRef type = TypeInfo::makeAny();
        bool isAssignable = false;
        bool isClassSymbol = false;
        std::string name;
        size_t line = 0;
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
    bool m_hasBufferedToken = false;
    Token m_bufferedToken;

    const std::unordered_set<std::string>& m_classNames;
    const std::unordered_map<std::string, TypeRef>& m_functionSignatures;
    std::vector<TypeError>& m_errors;

    std::vector<std::unordered_map<std::string, TypeRef>> m_scopes;
    std::unordered_set<std::string> m_declaredGlobalSymbols;
    std::vector<TypeCheckerDeclarationType> m_declarationTypes;
    std::unordered_map<std::string, std::string> m_superclassOf;
    std::unordered_map<std::string, std::unordered_map<std::string, TypeRef>>
        m_classFieldTypes;
    std::unordered_map<std::string, std::unordered_map<std::string, TypeRef>>
        m_classMethodSignatures;
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
        if (isCollectionTypeNameToken(m_current)) {
            return lookahead.type() == TokenType::LESS;
        }

        std::string typeName = tokenText(m_current);
        if (m_classNames.find(typeName) == m_classNames.end()) {
            return false;
        }

        return lookahead.type() == TokenType::IDENTIFIER ||
               lookahead.type() == TokenType::QUESTION;
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

    void addError(size_t line, const std::string& message) {
        m_errors.push_back(TypeError{line, message});
    }

    Token nextToken() {
        if (m_hasBufferedToken) {
            m_hasBufferedToken = false;
            return m_bufferedToken;
        }
        return m_scanner.nextToken();
    }

    Token peekToken() {
        if (!m_hasBufferedToken) {
            m_bufferedToken = m_scanner.nextToken();
            m_hasBufferedToken = true;
        }
        return m_bufferedToken;
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

    void defineSymbol(const std::string& name, const TypeRef& type) {
        m_scopes.back()[name] = type ? type : TypeInfo::makeAny();
        if (m_scopes.size() == 1) {
            m_declaredGlobalSymbols.emplace(name);
        }
    }

    void recordDeclarationType(const std::string& name, const TypeRef& type,
                               size_t line) {
        TypeCheckerDeclarationType declaration;
        declaration.line = line;
        declaration.functionDepth = m_functionContexts.size();
        declaration.scopeDepth = m_scopes.empty() ? 0 : (m_scopes.size() - 1);
        declaration.name = name;
        declaration.type = type ? type : TypeInfo::makeAny();
        m_declarationTypes.push_back(std::move(declaration));
    }

    TypeRef resolveSymbol(const std::string& name) const {
        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                return found->second;
            }
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
            consume(TokenType::ARROW,
                    "Type error: expected '->' after function type "
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
        }

        return nullptr;
    }

    bool isTypedVarDeclarationStart() {
        if (check(TokenType::TYPE_FN)) {
            return peekToken().type() == TokenType::OPEN_PAREN;
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
        if (isCollectionTypeNameToken(m_current)) {
            return lookahead.type() == TokenType::LESS;
        }

        std::string typeName = tokenText(m_current);
        if (m_classNames.find(typeName) == m_classNames.end()) {
            return false;
        }

        return lookahead.type() == TokenType::IDENTIFIER ||
               lookahead.type() == TokenType::QUESTION;
    }

    ExprInfo parseExpression() { return parseAssignment(); }

    ExprInfo parseAssignment() {
        ExprInfo lhs = parseOr();

        if (!isAssignmentOperator(m_current.type())) {
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

        if (!(targetType->isNumeric() && rhs.type->isNumeric())) {
            addError(
                line,
                "Type error: compound assignment requires numeric operands.");
            return ExprInfo{TypeInfo::makeAny(), false, false, lhs.name, line};
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

    ExprInfo parseOr() {
        ExprInfo expr = parseAnd();
        while (match(TokenType::OR)) {
            ExprInfo rhs = parseAnd();
            if (!(expr.type->kind == TypeKind::BOOL || expr.type->isAny()) ||
                !(rhs.type->kind == TypeKind::BOOL || rhs.type->isAny())) {
                addError(m_previous.line(),
                         "Type error: logical 'or' expects bool operands.");
            }
            expr = ExprInfo{TypeInfo::makeBool(), false, false, "",
                            m_previous.line()};
        }
        return expr;
    }

    ExprInfo parseAnd() {
        ExprInfo expr = parseEquality();
        while (match(TokenType::AND)) {
            ExprInfo rhs = parseEquality();
            if (!(expr.type->kind == TypeKind::BOOL || expr.type->isAny()) ||
                !(rhs.type->kind == TypeKind::BOOL || rhs.type->isAny())) {
                addError(m_previous.line(),
                         "Type error: logical 'and' expects bool operands.");
            }
            expr = ExprInfo{TypeInfo::makeBool(), false, false, "",
                            m_previous.line()};
        }
        return expr;
    }

    ExprInfo parseEquality() {
        ExprInfo expr = parseComparison();
        while (isEqualityOperator(m_current.type())) {
            size_t line = m_current.line();
            advance();
            ExprInfo rhs = parseComparison();
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
        ExprInfo expr = parseShift();
        while (isComparisonOperator(m_current.type())) {
            size_t line = m_current.line();
            advance();
            ExprInfo rhs = parseShift();

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

    ExprInfo parseShift() {
        ExprInfo expr = parseTerm();
        while (check(TokenType::SHIFT_LEFT_TOKEN) ||
               check(TokenType::SHIFT_RIGHT_TOKEN)) {
            size_t line = m_current.line();
            advance();
            ExprInfo rhs = parseTerm();
            bool lhsOk = expr.type->isNumeric() || expr.type->isAny();
            bool rhsOk = rhs.type->isNumeric() || rhs.type->isAny();
            if (!lhsOk || !rhsOk) {
                addError(
                    line,
                    "Type error: shift operators require integer operands.");
            }
            expr = ExprInfo{TypeInfo::makeAny(), false, false, "", line};
        }
        return expr;
    }

    ExprInfo parseTerm() {
        ExprInfo expr = parseFactor();
        while (check(TokenType::PLUS) || check(TokenType::MINUS)) {
            TokenType op = m_current.type();
            size_t line = m_current.line();
            advance();
            ExprInfo rhs = parseFactor();

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
        while (check(TokenType::STAR) || check(TokenType::SLASH)) {
            size_t line = m_current.line();
            advance();
            ExprInfo rhs = parseUnary();
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

        if (match(TokenType::PLUS_PLUS) || match(TokenType::MINUS_MINUS)) {
            ExprInfo operand = parseUnary();
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
        while (match(TokenType::AS_KW)) {
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
            if (match(TokenType::OPEN_PAREN)) {
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

            if (match(TokenType::DOT)) {
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

            if (match(TokenType::OPEN_BRACKET)) {
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
        consume(TokenType::OPEN_PAREN, "Expected '(' after 'function'.");

        const bool hasExpectedFunctionType =
            expectedSignature && expectedSignature->kind == TypeKind::FUNCTION;
        const auto& expectedParams = hasExpectedFunctionType
                                         ? expectedSignature->paramTypes
                                         : std::vector<TypeRef>{};

        std::vector<std::pair<std::string, TypeRef>> params;
        std::vector<TypeRef> paramTypes;

        if (!check(TokenType::CLOSE_PAREN)) {
            do {
                TypeRef paramType = nullptr;
                std::string paramName;
                size_t paramIndex = paramTypes.size();

                if (!isTypedTypeAnnotationStart()) {
                    consume(TokenType::IDENTIFIER, "Expected parameter name.");
                    paramName = tokenText(m_previous);

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
                } else {
                    paramType = parseTypeExprType();
                    if (!paramType) {
                        addError(m_current.line(),
                                 "Type error: expected parameter type "
                                 "annotation.");
                        paramType = TypeInfo::makeAny();
                    }

                    consume(TokenType::IDENTIFIER, "Expected parameter name.");
                    paramName = tokenText(m_previous);
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

        TypeRef returnType = TypeInfo::makeAny();
        bool hasExplicitReturnType = false;
        if (match(TokenType::ARROW)) {
            hasExplicitReturnType = true;
            TypeRef parsedReturnType = parseTypeExprType();
            if (!parsedReturnType) {
                addError(m_previous.line(),
                         "Type error: expected return type after '->'.");
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
                     "type with '->'.");
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

        if (match(TokenType::FUNCTION)) {
            return parseFunctionLiteralExpr();
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

        if (check(TokenType::IDENTIFIER) || isTypeToken(m_current.type())) {
            Token token = m_current;
            advance();
            std::string name(token.start(), token.length());
            TypeRef type = resolveSymbol(name);

            if (!type && m_classNames.find(name) != m_classNames.end()) {
                return ExprInfo{TypeInfo::makeClass(name), false, true, name,
                                token.line()};
            }

            const bool isClass = m_classNames.find(name) != m_classNames.end();
            const bool assignable = !isClass;
            return ExprInfo{type ? type : TypeInfo::makeAny(), assignable,
                            isClass, name, token.line()};
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
        if (check(TokenType::SEMI_COLON)) {
            advance();
        }
    }

    void parseReturnStatement() {
        size_t line = m_previous.line();
        if (m_functionContexts.empty()) {
            addError(line, "Type error: cannot return from top-level code.");
        }

        if (match(TokenType::SEMI_COLON)) {
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
        if (check(TokenType::SEMI_COLON)) {
            advance();
        }

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
        } else if (match(TokenType::AUTO)) {
            consume(TokenType::IDENTIFIER, "Expected variable name.");
            std::string variableName = tokenText(m_previous);
            size_t variableLine = m_previous.line();

            if (match(TokenType::COLON)) {
                ExprInfo iterable = parseExpression();
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

                defineSymbol(variableName, inferredLoopType);
                recordDeclarationType(variableName, inferredLoopType,
                                      variableLine);
                statement();
                endScope();
                return;
            }

            TypeRef declared = TypeInfo::makeAny();
            if (match(TokenType::EQUAL)) {
                declared = parseExpression().type;
            } else {
                addError(
                    m_previous.line(),
                    "Type error: 'auto' declaration requires an initializer.");
            }
            consume(TokenType::SEMI_COLON,
                    "Expected ';' after loop initializer.");
            defineSymbol(variableName, declared);
            recordDeclarationType(variableName, declared, variableLine);
        } else {
            parseExpression();
            consume(TokenType::SEMI_COLON,
                    "Expected ';' after loop initializer.");
        }

        if (!check(TokenType::SEMI_COLON)) {
            ExprInfo cond = parseExpression();
            if (!(cond.type->kind == TypeKind::BOOL || cond.type->isAny())) {
                addError(cond.line ? cond.line : m_previous.line(),
                         "Type error: for condition must be bool.");
            }
        }
        consume(TokenType::SEMI_COLON, "Expected ';' after loop condition.");

        if (!check(TokenType::CLOSE_PAREN)) {
            parseExpression();
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

    void parseAutoVarDeclaration() {
        consume(TokenType::IDENTIFIER, "Expected variable name after 'auto'.");
        Token nameToken = m_previous;
        std::string name = tokenText(nameToken);

        if (!match(TokenType::EQUAL)) {
            addError(nameToken.line(),
                     "Type error: 'auto' declaration requires an initializer.");
            consume(TokenType::SEMI_COLON,
                    "Expected ';' after auto variable declaration.");
            defineSymbol(name, TypeInfo::makeAny());
            return;
        }

        ExprInfo initializer = parseExpression();
        if (initializer.type && initializer.type->kind == TypeKind::NULL_TYPE) {
            addError(nameToken.line(),
                     "Type error: cannot infer type for 'auto' from 'null'.");
            defineSymbol(name, TypeInfo::makeAny());
            recordDeclarationType(name, TypeInfo::makeAny(), nameToken.line());
        } else {
            defineSymbol(name, initializer.type ? initializer.type
                                                : TypeInfo::makeAny());
            recordDeclarationType(
                name, initializer.type ? initializer.type : TypeInfo::makeAny(),
                nameToken.line());
        }

        consume(TokenType::SEMI_COLON,
                "Expected ';' after auto variable declaration.");
    }

    void parseTypedVarDeclaration() {
        TypeRef declaredType = parseTypeExprType();
        if (!declaredType) {
            addError(
                m_current.line(),
                "Type error: expected type in typed variable declaration.");
            return;
        }

        if (declaredType->isVoid()) {
            addError(m_current.line(),
                     "Type error: variables cannot have type 'void'.");
        }

        consume(TokenType::IDENTIFIER, "Expected variable name after type.");
        Token nameToken = m_previous;
        std::string name = tokenText(nameToken);

        consume(TokenType::EQUAL,
                "Expected '=' in typed variable declaration (initializer is "
                "required).");
        ExprInfo initializer;
        if (declaredType->kind == TypeKind::FUNCTION &&
            check(TokenType::FUNCTION)) {
            advance();
            initializer = parseFunctionLiteralExpr(declaredType);
        } else {
            initializer = parseExpression();
        }

        if (!isAssignableType(initializer.type, declaredType)) {
            addError(nameToken.line(), "Type error: cannot assign '" +
                                           initializer.type->toString() +
                                           "' to variable '" + name +
                                           "' of type '" +
                                           declaredType->toString() + "'.");
        }

        consume(TokenType::SEMI_COLON,
                "Expected ';' after typed variable declaration.");
        defineSymbol(name, declaredType);
        recordDeclarationType(name, declaredType, nameToken.line());
    }

    void parseFunctionCommon(const std::string& functionName,
                             const TypeRef& signature, bool isMethod) {
        consume(TokenType::OPEN_PAREN, "Expected '(' after function name.");

        std::vector<std::pair<std::string, TypeRef>> params;
        if (!check(TokenType::CLOSE_PAREN)) {
            do {
                TypeRef paramType = nullptr;
                std::string paramName;

                if (!isTypedTypeAnnotationStart()) {
                    consume(TokenType::IDENTIFIER, "Expected parameter name.");
                    paramName = tokenText(m_previous);
                    addError(m_previous.line(),
                             "Type error: parameter '" + paramName +
                                 "' must have a type annotation.");
                    paramType = TypeInfo::makeAny();
                } else {
                    paramType = parseTypeExprType();
                    if (!paramType) {
                        addError(m_current.line(),
                                 "Type error: expected parameter type "
                                 "annotation.");
                        paramType = TypeInfo::makeAny();
                    }

                    consume(TokenType::IDENTIFIER, "Expected parameter name.");
                    paramName = tokenText(m_previous);
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
        if (match(TokenType::ARROW)) {
            hasExplicitReturnType = true;
            TypeRef parsedReturnType = parseTypeExprType();
            if (!parsedReturnType) {
                addError(m_previous.line(),
                         "Type error: expected return type after '->'.");
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
                         "' must declare a return type with '->'.");
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

    void parseClassDeclaration() {
        consume(TokenType::IDENTIFIER, "Expected class name.");
        size_t classLine = m_previous.line();
        std::string className = tokenText(m_previous);
        defineSymbol(className, TypeInfo::makeClass(className));
        recordDeclarationType(className, TypeInfo::makeClass(className),
                              classLine);

        m_classContexts.push_back(ClassCtx{className});

        if (match(TokenType::LESS)) {
            consume(TokenType::IDENTIFIER, "Expected superclass name.");
            std::string superclassName = tokenText(m_previous);

            if (superclassName == className) {
                addError(m_previous.line(),
                         "Type error: a class cannot inherit from itself.");
            }

            if (m_classNames.find(superclassName) == m_classNames.end()) {
                addError(m_previous.line(), "Type error: unknown superclass '" +
                                                superclassName + "'.");
            }

            m_superclassOf[className] = superclassName;
        }

        consume(TokenType::OPEN_CURLY, "Expected '{' before class body.");

        bool seenMethodBody = false;

        while (!check(TokenType::CLOSE_CURLY) &&
               !check(TokenType::END_OF_FILE)) {
            bool typedHead = false;
            TypeRef memberType = nullptr;

            if (isTypedTypeAnnotationStart()) {
                typedHead = true;
                memberType = parseTypeExprType();
            }

            if (typedHead) {
                consume(TokenType::IDENTIFIER, "Expected class member name.");
                std::string memberName = tokenText(m_previous);

                if (match(TokenType::SEMI_COLON)) {
                    if (seenMethodBody) {
                        addError(m_previous.line(),
                                 "Type error: class fields must be declared "
                                 "before method declarations.");
                    }
                    if (memberType && memberType->isVoid()) {
                        addError(m_previous.line(),
                                 "Type error: class field '" + memberName +
                                     "' cannot have type 'void'.");
                    }
                    m_classFieldTypes[className][memberName] =
                        memberType ? memberType : TypeInfo::makeAny();
                    continue;
                }

                if (check(TokenType::OPEN_PAREN)) {
                    seenMethodBody = true;
                    TypeRef sig = TypeInfo::makeFunction(
                        {}, memberType ? memberType : TypeInfo::makeAny());
                    m_classMethodSignatures[className][memberName] = sig;
                    parseFunctionCommon(memberName, sig, true);
                    continue;
                }

                addError(
                    m_current.line(),
                    "Type error: expected ';' for field or '(' for method.");
                continue;
            }

            if (check(TokenType::IDENTIFIER) &&
                peekToken().type() == TokenType::OPEN_PAREN) {
                advance();
                std::string memberName = tokenText(m_previous);
                seenMethodBody = true;
                m_classMethodSignatures[className][memberName] =
                    TypeInfo::makeFunction({}, TypeInfo::makeAny());
                parseFunctionCommon(
                    memberName, TypeInfo::makeFunction({}, TypeInfo::makeAny()),
                    true);
                continue;
            }

            advance();
        }

        consume(TokenType::CLOSE_CURLY, "Expected '}' after class body.");
        m_classContexts.pop_back();
    }

    void parseImportDeclaration() {
        while (!check(TokenType::SEMI_COLON) &&
               !check(TokenType::END_OF_FILE)) {
            advance();
        }
        if (check(TokenType::SEMI_COLON)) {
            advance();
        }
    }

    void parseExportDeclaration() {
        if (match(TokenType::FUNCTION)) {
            parseFunctionDeclaration();
            return;
        }

        if (match(TokenType::AUTO)) {
            parseAutoVarDeclaration();
            return;
        }

        if (match(TokenType::CLASS)) {
            parseClassDeclaration();
            return;
        }

        parseExpressionStatement();
    }

    void statement() {
        if (match(TokenType::PRINT)) {
            parseExpression();
            if (check(TokenType::SEMI_COLON)) {
                advance();
            }
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
        if (match(TokenType::CLASS)) {
            parseClassDeclaration();
            return;
        }

        if (match(TokenType::IMPORT)) {
            parseImportDeclaration();
            return;
        }

        if (match(TokenType::EXPORT)) {
            parseExportDeclaration();
            return;
        }

        if (match(TokenType::FUNCTION)) {
            parseFunctionDeclaration();
            return;
        }

        if (match(TokenType::AUTO)) {
            parseAutoVarDeclaration();
            return;
        }

        if (isTypedVarDeclarationStart()) {
            parseTypedVarDeclaration();
            return;
        }

        statement();
    }

   public:
    CheckerImpl(
        std::string_view source,
        const std::unordered_set<std::string>& classNames,
        const std::unordered_map<std::string, TypeRef>& functionSignatures,
        std::vector<TypeError>& out)
        : m_scanner(source),
          m_classNames(classNames),
          m_functionSignatures(functionSignatures),
          m_errors(out) {
        m_scopes.emplace_back();

        for (const auto& entry : functionSignatures) {
            m_scopes.front()[entry.first] = entry.second;
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
                out.topLevelSymbolTypes[symbolName] = it->second;
            }
        }
        return out;
    }
};

}  // namespace

bool TypeChecker::collectSymbols(
    std::string_view source, std::unordered_set<std::string>& outClassNames,
    std::unordered_map<std::string, TypeRef>& outFunctionSignatures) {
    outClassNames.clear();

    {
        Scanner scanner(source);
        while (true) {
            Token token = scanner.nextToken();
            if (token.type() == TokenType::END_OF_FILE) {
                break;
            }

            if (token.type() != TokenType::CLASS) {
                continue;
            }

            Token name = scanner.nextToken();
            while (name.type() == TokenType::ERROR) {
                name = scanner.nextToken();
            }

            if (name.type() == TokenType::IDENTIFIER) {
                outClassNames.emplace(name.start(), name.length());
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
            if (arrow.type() != TokenType::ARROW) {
                return nullptr;
            }

            TypeRef returnType = parseTypeFromToken(nextToken());
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

        if (classDepth != 0 || token.type() != TokenType::FUNCTION) {
            continue;
        }

        Token functionName = nextToken();
        if (functionName.type() != TokenType::IDENTIFIER) {
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

                if (isTypeToken(current.type()) ||
                    current.type() == TokenType::IDENTIFIER ||
                    current.type() == TokenType::TYPE_FN) {
                    parameterType = parseTypeFromToken(current);
                    if (!parameterType) {
                        parameterType = TypeInfo::makeAny();
                    }

                    Token nameToken = nextToken();
                    if (nameToken.type() != TokenType::IDENTIFIER) {
                        break;
                    }
                } else {
                    Token nameToken = current;
                    if (nameToken.type() != TokenType::IDENTIFIER) {
                        break;
                    }
                    parameterType = TypeInfo::makeAny();
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
        Token maybeArrow = nextToken();
        if (maybeArrow.type() == TokenType::ARROW) {
            Token returnToken = nextToken();
            TypeRef typedReturn = parseTypeFromToken(returnToken);
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
    const std::unordered_map<std::string, TypeRef>& functionSignatures,
    std::vector<TypeError>& out, TypeCheckerMetadata* outMetadata) {
    CheckerImpl checker(source, classNames, functionSignatures, out);
    checker.run();
    if (outMetadata) {
        *outMetadata = checker.metadata();
    }
    return out.empty();
}
