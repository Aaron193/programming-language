#include "TypeChecker.hpp"

#include <algorithm>
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

    Scanner m_scanner;
    Token m_current;
    Token m_previous;
    bool m_hasBufferedToken = false;
    Token m_bufferedToken;

    const std::unordered_set<std::string>& m_classNames;
    const std::unordered_map<std::string, TypeRef>& m_functionSignatures;
    std::vector<TypeError>& m_errors;

    std::vector<std::unordered_map<std::string, TypeRef>> m_scopes;
    std::unordered_map<std::string, std::string> m_superclassOf;
    std::vector<FunctionCtx> m_functionContexts;

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
        return m_classNames.find(typeName) != m_classNames.end() &&
               lookahead.type() == TokenType::IDENTIFIER;
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

    TypeRef parseTypeExprType() {
        if (isTypeToken(m_current.type())) {
            Token token = m_current;
            advance();
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
                case TokenType::TYPE_NULL_KW:
                    return TypeInfo::makeNull();
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
                    consume(TokenType::GREATER,
                            "Type error: expected '>' after Array<T>.");
                    return TypeInfo::makeArray(elementType);
                }

                if (className == "Set") {
                    TypeRef elementType = parseTypeExprType();
                    if (!elementType) {
                        addError(
                            m_current.line(),
                            "Type error: expected element type in Set<T>.");
                        return nullptr;
                    }
                    consume(TokenType::GREATER,
                            "Type error: expected '>' after Set<T>.");
                    return TypeInfo::makeSet(elementType);
                }

                TypeRef keyType = parseTypeExprType();
                if (!keyType) {
                    addError(m_current.line(),
                             "Type error: expected key type in Dict<K, V>.");
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
                consume(TokenType::GREATER,
                        "Type error: expected '>' after Dict<K, V>.");
                return TypeInfo::makeDict(keyType, valueType);
            }

            if (m_classNames.find(className) != m_classNames.end()) {
                advance();
                return TypeInfo::makeClass(className);
            }
        }

        return nullptr;
    }

    bool isTypedVarDeclarationStart() {
        if (isTypeToken(m_current.type())) {
            return peekToken().type() == TokenType::IDENTIFIER;
        }

        if (!check(TokenType::IDENTIFIER)) {
            return false;
        }

        Token lookahead = peekToken();
        if (isCollectionTypeNameToken(m_current)) {
            return lookahead.type() == TokenType::LESS;
        }

        std::string typeName = tokenText(m_current);
        return m_classNames.find(typeName) != m_classNames.end() &&
               lookahead.type() == TokenType::IDENTIFIER;
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
            if (!lhs.isAssignable || lhs.name.empty()) {
                return ExprInfo{TypeInfo::makeAny(), false, false, "", line};
            }

            TypeRef targetType = resolveSymbol(lhs.name);
            if (!targetType || targetType->isAny()) {
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

        if (!lhs.isAssignable || lhs.name.empty()) {
            return ExprInfo{TypeInfo::makeAny(), false, false, "", line};
        }

        TypeRef targetType = resolveSymbol(lhs.name);
        if (!targetType) {
            targetType = TypeInfo::makeAny();
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
                consume(TokenType::IDENTIFIER,
                        "Expected property name after '.'.");
                expr = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                m_previous.line()};
                continue;
            }

            if (match(TokenType::OPEN_BRACKET)) {
                parseExpression();
                consume(TokenType::CLOSE_BRACKET,
                        "Expected ']' after index expression.");
                expr = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                m_previous.line()};
                continue;
            }

            break;
        }

        return expr;
    }

    ExprInfo parsePrimary() {
        if (match(TokenType::NUMBER)) {
            return ExprInfo{TypeInfo::makeAny(), false, false, "",
                            m_previous.line()};
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

        if (match(TokenType::THIS) || match(TokenType::SUPER)) {
            return ExprInfo{TypeInfo::makeAny(), false, false, "",
                            m_previous.line()};
        }

        if (match(TokenType::OPEN_BRACKET)) {
            TypeRef elementType = nullptr;
            if (!check(TokenType::CLOSE_BRACKET)) {
                do {
                    ExprInfo item = parseExpression();
                    elementType = mergeInferredTypes(elementType, item.type);
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
                    keyType = mergeInferredTypes(keyType, keyExpr.type);
                    consume(TokenType::COLON,
                            "Expected ':' between dictionary key and value.");
                    ExprInfo valueExpr = parseExpression();
                    valueType = mergeInferredTypes(valueType, valueExpr.type);
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
                if (expected && !expected->isAny() && !expected->isVoid()) {
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
            if (expected && !expected->isAny() &&
                !isAssignableType(value.type, expected)) {
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
        } else if (match(TokenType::VAR)) {
            consume(TokenType::IDENTIFIER, "Expected variable name.");
            std::string variableName = tokenText(m_previous);

            if (match(TokenType::COLON)) {
                parseExpression();
                consume(TokenType::CLOSE_PAREN,
                        "Expected ')' after foreach iterable expression.");
                defineSymbol(variableName, TypeInfo::makeAny());
                statement();
                endScope();
                return;
            }

            TypeRef declared = TypeInfo::makeAny();
            if (match(TokenType::EQUAL)) {
                declared = parseExpression().type;
            }
            consume(TokenType::SEMI_COLON,
                    "Expected ';' after loop initializer.");
            defineSymbol(variableName, declared);
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

    void parseVarDeclaration() {
        consume(TokenType::IDENTIFIER, "Expected variable name.");
        std::string name = tokenText(m_previous);

        TypeRef declared = TypeInfo::makeAny();
        if (match(TokenType::EQUAL)) {
            declared = parseExpression().type;
        }

        consume(TokenType::SEMI_COLON,
                "Expected ';' after variable declaration.");
        defineSymbol(name, declared ? declared : TypeInfo::makeAny());
    }

    void parseTypedVarDeclaration() {
        TypeRef declaredType = parseTypeExprType();
        if (!declaredType) {
            addError(
                m_current.line(),
                "Type error: expected type in typed variable declaration.");
            return;
        }

        consume(TokenType::IDENTIFIER, "Expected variable name after type.");
        Token nameToken = m_previous;
        std::string name = tokenText(nameToken);

        consume(TokenType::EQUAL,
                "Expected '=' in typed variable declaration (initializer is "
                "required).");
        ExprInfo initializer = parseExpression();

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
    }

    void parseFunctionCommon(const TypeRef& signature) {
        consume(TokenType::OPEN_PAREN, "Expected '(' after function name.");

        std::vector<std::pair<std::string, TypeRef>> params;
        if (!check(TokenType::CLOSE_PAREN)) {
            do {
                TypeRef paramType = TypeInfo::makeAny();

                if (isTypedTypeAnnotationStart()) {
                    paramType = parseTypeExprType();
                }

                consume(TokenType::IDENTIFIER, "Expected parameter name.");
                params.emplace_back(tokenText(m_previous), paramType);
            } while (match(TokenType::COMMA));
        }

        consume(TokenType::CLOSE_PAREN, "Expected ')' after parameters.");

        TypeRef returnType = TypeInfo::makeAny();
        if (match(TokenType::ARROW)) {
            TypeRef parsedReturnType = parseTypeExprType();
            if (!parsedReturnType) {
                addError(m_previous.line(),
                         "Type error: expected return type after '->'.");
            } else {
                returnType = parsedReturnType;
            }
        } else if (signature && signature->kind == TypeKind::FUNCTION &&
                   signature->returnType) {
            returnType = signature->returnType;
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
        std::string name = tokenText(m_previous);

        TypeRef functionType = TypeInfo::makeAny();
        auto it = m_functionSignatures.find(name);
        if (it != m_functionSignatures.end()) {
            functionType = it->second;
        }
        defineSymbol(name, functionType);

        parseFunctionCommon(functionType);
    }

    void parseClassDeclaration() {
        consume(TokenType::IDENTIFIER, "Expected class name.");
        std::string className = tokenText(m_previous);

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

                if (match(TokenType::SEMI_COLON)) {
                    continue;
                }

                if (check(TokenType::OPEN_PAREN)) {
                    TypeRef sig = TypeInfo::makeFunction(
                        {}, memberType ? memberType : TypeInfo::makeAny());
                    parseFunctionCommon(sig);
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
                parseFunctionCommon(
                    TypeInfo::makeFunction({}, TypeInfo::makeAny()));
                continue;
            }

            advance();
        }

        consume(TokenType::CLOSE_CURLY, "Expected '}' after class body.");
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

        if (match(TokenType::VAR)) {
            parseVarDeclaration();
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

        if (match(TokenType::VAR)) {
            parseVarDeclaration();
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
};

}  // namespace

bool TypeChecker::check(
    std::string_view source, const std::unordered_set<std::string>& classNames,
    const std::unordered_map<std::string, TypeRef>& functionSignatures,
    std::vector<TypeError>& out) {
    CheckerImpl checker(source, classNames, functionSignatures, out);
    checker.run();
    return out.empty();
}
