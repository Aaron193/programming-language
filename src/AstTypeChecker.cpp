#include "AstTypeChecker.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "FrontendTypeUtils.hpp"
#include "NativePackage.hpp"
#include "NumericLiteral.hpp"
#include "SyntaxRules.hpp"

namespace {

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

class AstTypeCheckerImpl {
   private:
    struct ExprInfo {
        TypeRef type = TypeInfo::makeAny();
        bool isAssignable = false;
        bool isClassSymbol = false;
        std::string name;
        size_t line = 0;
        bool isConstSymbol = false;
        bool hasBinding = false;
        AstNodeId declarationNodeId = 0;
    };

    struct SymbolInfo {
        TypeRef type = TypeInfo::makeAny();
        bool isConst = false;
    };

    struct FunctionCtx {
        TypeRef returnType = TypeInfo::makeAny();
    };

    struct LoopCtx {
        std::optional<Token> label;
    };

    struct ClassCtx {
        std::string className;
    };

    struct ResolvedCallableSignature {
        std::vector<std::pair<std::string, TypeRef>> params;
        std::vector<TypeRef> paramTypes;
        TypeRef returnType = TypeInfo::makeAny();
        bool omittedParameterTypeAnnotation = false;
        SourceSpan firstOmittedParameterSpan = makePointSpan(1, 1);
    };

    const std::unordered_set<std::string>& m_classNames;
    std::unordered_map<std::string, TypeRef> m_typeAliases;
    const std::unordered_map<std::string, TypeRef>& m_functionSignatures;
    const AstBindResult& m_bindings;
    std::vector<TypeError>& m_errors;
    AstSemanticModel* m_model = nullptr;

    std::unordered_map<AstNodeId, SymbolInfo> m_declaredValueTypes;
    std::vector<FunctionCtx> m_functionContexts;
    std::vector<LoopCtx> m_loopContexts;
    std::vector<ClassCtx> m_classContexts;

    std::string tokenText(const Token& token) const {
        return tokenLexeme(token);
    }

    bool labelsEqual(const Token& lhs, const Token& rhs) const {
        return tokenText(lhs) == tokenText(rhs);
    }

    void addError(const SourceSpan& span, const std::string& message) {
        m_errors.push_back(TypeError{span, message});
    }

    void addError(const AstNodeInfo& node, const std::string& message) {
        addError(node.span, message);
    }

    void addError(const Token& token, const std::string& message) {
        addError(token.span(), message);
    }

    void addError(size_t line, const std::string& message) {
        addError(makePointSpan(line, 1), message);
    }

    void recordNodeType(const AstNodeInfo& node, const TypeRef& type) {
        if (!m_model || node.id == 0) {
            return;
        }
        m_model->nodeTypes[node.id] = type ? type : TypeInfo::makeAny();
    }

    void recordNodeConstness(const AstNodeInfo& node, bool isConst) {
        if (!m_model || node.id == 0) {
            return;
        }
        m_model->nodeConstness[node.id] = isConst;
    }

    void recordDeclarationType(const AstNodeInfo& node, const std::string& name,
                               const TypeRef& type, size_t line,
                               bool isConst = false) {
        (void)name;
        (void)line;
        recordNodeType(node, type);
        recordNodeConstness(node, isConst);
    }

    void declareValue(AstNodeId nodeId, const TypeRef& type, bool isConst) {
        if (nodeId == 0) {
            return;
        }

        m_declaredValueTypes[nodeId] =
            SymbolInfo{type ? type : TypeInfo::makeAny(), isConst};
    }

    const SymbolInfo* declaredValue(AstNodeId nodeId) const {
        auto it = m_declaredValueTypes.find(nodeId);
        if (it == m_declaredValueTypes.end()) {
            return nullptr;
        }

        return &it->second;
    }

    const AstBindingRef* lookupBinding(AstNodeId nodeId) const {
        auto it = m_bindings.references.find(nodeId);
        if (it == m_bindings.references.end()) {
            return nullptr;
        }

        return &it->second;
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

            auto it = m_bindings.metadata.superclassOf.find(current);
            if (it == m_bindings.metadata.superclassOf.end() ||
                it->second.empty()) {
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

    TypeRef inferNumberLiteralType(const Token& token) {
        const std::string literal = tokenText(token);
        const NumericLiteralInfo parsed = parseNumericLiteralInfo(literal);
        if (!parsed.valid) {
            addError(token.span(),
                     "Type error: invalid numeric literal '" + literal + "'.");
            return TypeInfo::makeAny();
        }

        try {
            if (parsed.isFloat) {
                (void)std::stod(parsed.core);
                return parsed.type;
            }

            if (parsed.isUnsigned || parsed.type->isUnsigned()) {
                unsigned long long value = std::stoull(parsed.core);
                const auto checkRange = [&](unsigned long long maxValue) {
                    return value <= maxValue;
                };

                switch (parsed.type->kind) {
                    case TypeKind::U8:
                        if (!checkRange(std::numeric_limits<uint8_t>::max())) {
                            addError(token.span(),
                                     "Type error: integer literal '" + literal +
                                         "' is out of range for type '" +
                                         parsed.type->toString() + "'.");
                            return TypeInfo::makeAny();
                        }
                        break;
                    case TypeKind::U16:
                        if (!checkRange(std::numeric_limits<uint16_t>::max())) {
                            addError(token.span(),
                                     "Type error: integer literal '" + literal +
                                         "' is out of range for type '" +
                                         parsed.type->toString() + "'.");
                            return TypeInfo::makeAny();
                        }
                        break;
                    case TypeKind::U32:
                        if (!checkRange(std::numeric_limits<uint32_t>::max())) {
                            addError(token.span(),
                                     "Type error: integer literal '" + literal +
                                         "' is out of range for type '" +
                                         parsed.type->toString() + "'.");
                            return TypeInfo::makeAny();
                        }
                        break;
                    default:
                        break;
                }

                return parsed.type;
            }

            long long value = std::stoll(parsed.core);
            const auto checkRange = [&](long long minValue, long long maxValue) {
                return value >= minValue && value <= maxValue;
            };

            switch (parsed.type->kind) {
                case TypeKind::I8:
                    if (!checkRange(std::numeric_limits<int8_t>::min(),
                                    std::numeric_limits<int8_t>::max())) {
                        addError(token.span(),
                                 "Type error: integer literal '" + literal +
                                     "' is out of range for type '" +
                                     parsed.type->toString() + "'.");
                        return TypeInfo::makeAny();
                    }
                    break;
                case TypeKind::I16:
                    if (!checkRange(std::numeric_limits<int16_t>::min(),
                                    std::numeric_limits<int16_t>::max())) {
                        addError(token.span(),
                                 "Type error: integer literal '" + literal +
                                     "' is out of range for type '" +
                                     parsed.type->toString() + "'.");
                        return TypeInfo::makeAny();
                    }
                    break;
                case TypeKind::I32:
                    if (!checkRange(std::numeric_limits<int32_t>::min(),
                                    std::numeric_limits<int32_t>::max())) {
                        addError(token.span(),
                                 "Type error: integer literal '" + literal +
                                     "' is out of range for type '" +
                                     parsed.type->toString() + "'.");
                        return TypeInfo::makeAny();
                    }
                    break;
                default:
                    break;
            }

            return parsed.type;
        } catch (...) {
            addError(token.span(),
                     "Type error: invalid numeric literal '" + literal + "'.");
            return TypeInfo::makeAny();
        }
    }

    bool isOmittedNamedType(const AstTypeExpr& typeExpr) const {
        if (typeExpr.kind != AstTypeKind::NAMED ||
            typeExpr.token.type() != TokenType::IDENTIFIER) {
            return false;
        }

        const std::string name = tokenText(typeExpr.token);
        return m_classNames.find(name) == m_classNames.end() &&
               m_typeAliases.find(name) == m_typeAliases.end();
    }

    TypeRef tokenToType(const Token& token) const {
        return frontendTokenToType(token,
                                   FrontendTypeContext{m_classNames,
                                                       m_typeAliases});
    }

    TypeRef resolveTypeExpr(const AstTypeExpr& typeExpr) {
        switch (typeExpr.kind) {
            case AstTypeKind::NAMED:
                return tokenToType(typeExpr.token);
            case AstTypeKind::FUNCTION: {
                std::vector<TypeRef> params;
                params.reserve(typeExpr.paramTypes.size());
                for (const auto& paramType : typeExpr.paramTypes) {
                    TypeRef resolved = resolveTypeExpr(*paramType);
                    if (!resolved) {
                        return nullptr;
                    }
                    if (resolved->isVoid()) {
                        addError(typeExpr.node.span,
                                 "Type error: function type parameter cannot "
                                 "be 'void'.");
                        return nullptr;
                    }
                    params.push_back(resolved);
                }
                if (!typeExpr.returnType) {
                    return nullptr;
                }
                TypeRef returnType = resolveTypeExpr(*typeExpr.returnType);
                if (!returnType) {
                    return nullptr;
                }
                return TypeInfo::makeFunction(std::move(params), returnType);
            }
            case AstTypeKind::ARRAY:
                if (!typeExpr.elementType) {
                    return nullptr;
                }
                if (TypeRef elementType = resolveTypeExpr(*typeExpr.elementType)) {
                    if (elementType->isVoid()) {
                        addError(typeExpr.node.span,
                                 "Type error: 'void' is not valid as an Array "
                                 "element type.");
                        return nullptr;
                    }
                    return TypeInfo::makeArray(elementType);
                }
                return nullptr;
            case AstTypeKind::DICT:
                if (!typeExpr.keyType || !typeExpr.valueType) {
                    return nullptr;
                }
                {
                    TypeRef keyType = resolveTypeExpr(*typeExpr.keyType);
                    TypeRef valueType = resolveTypeExpr(*typeExpr.valueType);
                    if (!keyType || !valueType) {
                        return nullptr;
                    }
                    if (keyType->isVoid()) {
                        addError(typeExpr.node.span,
                                 "Type error: 'void' is not valid as a Dict "
                                 "key type.");
                        return nullptr;
                    }
                    if (valueType->isVoid()) {
                        addError(typeExpr.node.span,
                                 "Type error: 'void' is not valid as a Dict "
                                 "value type.");
                        return nullptr;
                    }
                    return TypeInfo::makeDict(keyType, valueType);
                }
            case AstTypeKind::SET:
                if (!typeExpr.elementType) {
                    return nullptr;
                }
                if (TypeRef elementType = resolveTypeExpr(*typeExpr.elementType)) {
                    if (elementType->isVoid()) {
                        addError(typeExpr.node.span,
                                 "Type error: 'void' is not valid as a Set "
                                 "element type.");
                        return nullptr;
                    }
                    return TypeInfo::makeSet(elementType);
                }
                return nullptr;
            case AstTypeKind::OPTIONAL:
                if (!typeExpr.innerType) {
                    return nullptr;
                }
                if (TypeRef innerType = resolveTypeExpr(*typeExpr.innerType)) {
                    return TypeInfo::makeOptional(innerType);
                }
                return nullptr;
            case AstTypeKind::NATIVE_HANDLE:
                if (!isValidPackageIdPart(typeExpr.packageNamespace) ||
                    !isValidPackageIdPart(typeExpr.packageName) ||
                    !isValidHandleTypeName(typeExpr.nativeHandleTypeName)) {
                    addError(typeExpr.node.span,
                             "Type error: handle type must use "
                             "handle<namespace:name:Type> with lowercase "
                             "package IDs and an alphanumeric type name.");
                    return nullptr;
                }
                return TypeInfo::makeNativeHandle(
                    makePackageId(typeExpr.packageNamespace, typeExpr.packageName),
                    typeExpr.nativeHandleTypeName);
        }

        return nullptr;
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

            auto classIt = m_bindings.metadata.classFieldTypes.find(current);
            if (classIt != m_bindings.metadata.classFieldTypes.end()) {
                auto fieldIt = classIt->second.find(fieldName);
                if (fieldIt != classIt->second.end()) {
                    return fieldIt->second;
                }
            }

            auto superIt = m_bindings.metadata.superclassOf.find(current);
            if (superIt == m_bindings.metadata.superclassOf.end()) {
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

            auto classIt = m_bindings.metadata.classMethodSignatures.find(current);
            if (classIt != m_bindings.metadata.classMethodSignatures.end()) {
                auto methodIt = classIt->second.find(methodName);
                if (methodIt != classIt->second.end()) {
                    return methodIt->second;
                }
            }

            auto superIt = m_bindings.metadata.superclassOf.find(current);
            if (superIt == m_bindings.metadata.superclassOf.end()) {
                break;
            }
            current = superIt->second;
        }

        return nullptr;
    }

    TypeRef lookupOperatorResultType(const TypeRef& receiverType, TokenType op,
                                     const TypeRef& rhs,
                                     const SourceSpan& span) {
        if (!receiverType || receiverType->kind != TypeKind::CLASS) {
            return nullptr;
        }

        auto classIt =
            m_bindings.classOperatorMethods.find(receiverType->className);
        if (classIt == m_bindings.classOperatorMethods.end()) {
            return nullptr;
        }

        auto opIt = classIt->second.find(op);
        if (opIt == classIt->second.end()) {
            return nullptr;
        }

        TypeRef methodType =
            lookupClassMethodType(receiverType->className, opIt->second);
        if (!methodType || methodType->kind != TypeKind::FUNCTION) {
            addError(span, "Type error: operator method '" + opIt->second +
                               "' is not callable.");
            return TypeInfo::makeAny();
        }

        if (methodType->paramTypes.size() != 1) {
            addError(span, "Type error: operator method '" + opIt->second +
                               "' must take exactly one argument.");
            return methodType->returnType ? methodType->returnType
                                          : TypeInfo::makeAny();
        }

        TypeRef expected = methodType->paramTypes[0]
                               ? methodType->paramTypes[0]
                               : TypeInfo::makeAny();
        if (!isAssignableType(rhs, expected)) {
            addError(span, "Type error: operator '" + opIt->second +
                               "' expects '" + expected->toString() +
                               "', got '" + rhs->toString() + "'.");
        }

        return methodType->returnType ? methodType->returnType
                                      : TypeInfo::makeAny();
    }

    TypeRef collectionMemberType(const TypeRef& receiverType,
                                 const std::string& memberName,
                                 const SourceSpan& span) {
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

            addError(span,
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

            addError(span,
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

            addError(span,
                     "Type error: set has no member '" + memberName + "'.");
            return TypeInfo::makeAny();
        }

        return TypeInfo::makeAny();
    }

    TypeRef requireTypeExpr(const AstTypeExpr* typeExpr, const SourceSpan& span,
                            const std::string& message) {
        if (!typeExpr) {
            addError(span, message);
            return nullptr;
        }

        TypeRef resolved = resolveTypeExpr(*typeExpr);
        if (!resolved) {
            addError(span, message);
        }
        return resolved;
    }

    ResolvedCallableSignature resolveCallableSignature(
        const std::string& functionName, const std::vector<AstParameter>& params,
        const AstTypeExpr* returnTypeExpr, const TypeRef& expectedSignature,
        bool isMethod, bool isClosure, bool requireDeclaredReturnType,
        const SourceSpan& fallbackSpan) {
        ResolvedCallableSignature out;
        const bool hasExpectedFunctionType =
            expectedSignature && expectedSignature->kind == TypeKind::FUNCTION;
        const std::vector<TypeRef> emptyExpectedParams;
        const auto& expectedParams =
            hasExpectedFunctionType ? expectedSignature->paramTypes
                                    : emptyExpectedParams;

        for (size_t index = 0; index < params.size(); ++index) {
            const AstParameter& param = params[index];
            const std::string paramName = tokenText(param.name);

            TypeRef paramType = nullptr;
            const bool omittedType = !param.type || isOmittedNamedType(*param.type);
            if (omittedType) {
                if (isClosure && hasExpectedFunctionType &&
                    index < expectedParams.size() && expectedParams[index] &&
                    !expectedParams[index]->isAny()) {
                    paramType = expectedParams[index];
                } else {
                    addError(param.name.span(),
                             "Type error: parameter '" + paramName +
                                 "' must have a type annotation.");
                    paramType = TypeInfo::makeAny();
                }

                if (!out.omittedParameterTypeAnnotation) {
                    out.omittedParameterTypeAnnotation = true;
                    out.firstOmittedParameterSpan = param.name.span();
                }
            } else {
                paramType = resolveTypeExpr(*param.type);
                if (!paramType) {
                    addError(param.type->node.span,
                             "Type error: expected parameter type annotation.");
                    paramType = TypeInfo::makeAny();
                }
            }

            if (paramType && paramType->isVoid()) {
                addError(param.name.span(),
                         "Type error: parameter '" + paramName +
                             "' cannot have type 'void'.");
            }

            out.params.emplace_back(
                paramName, paramType ? paramType : TypeInfo::makeAny());
            out.paramTypes.push_back(paramType ? paramType : TypeInfo::makeAny());
            recordNodeType(param.node,
                           paramType ? paramType : TypeInfo::makeAny());
        }

        if (isClosure && hasExpectedFunctionType &&
            out.paramTypes.size() != expectedParams.size()) {
            addError(fallbackSpan,
                     "Type error: closure parameter count mismatch: expected " +
                         std::to_string(expectedParams.size()) + ", got " +
                         std::to_string(out.paramTypes.size()) + ".");
        }

        if (requireDeclaredReturnType) {
            const bool treatReturnAsOmitted =
                returnTypeExpr == nullptr || isOmittedNamedType(*returnTypeExpr);
            bool hasExplicitReturnType = false;
            if (!treatReturnAsOmitted) {
                hasExplicitReturnType = true;
                TypeRef parsedReturnType = resolveTypeExpr(*returnTypeExpr);
                if (!parsedReturnType) {
                    addError(returnTypeExpr->node,
                             "Type error: expected return type after parameter "
                             "list.");
                } else {
                    out.returnType = parsedReturnType;
                }
            } else if (hasExpectedFunctionType && expectedSignature->returnType &&
                       !expectedSignature->returnType->isAny()) {
                out.returnType = expectedSignature->returnType;
            }

            const bool hasSignatureReturnType =
                hasExpectedFunctionType && expectedSignature->returnType &&
                !expectedSignature->returnType->isAny();
            const bool isInitializer = isMethod && functionName == "init";

            if (!hasExplicitReturnType && !hasSignatureReturnType &&
                !isInitializer) {
                addError(fallbackSpan,
                         "Type error: function '" + functionName +
                             "' must declare a return type.");
            }
        }

        if (!out.returnType) {
            out.returnType = TypeInfo::makeAny();
        }

        return out;
    }

    const AstImportedModuleInterface* lookupImportedModule(
        AstNodeId nodeId) const {
        auto it = m_bindings.importedModules.find(nodeId);
        if (it == m_bindings.importedModules.end()) {
            return nullptr;
        }

        return &it->second;
    }

    bool isImportExpr(const AstExpr& expr) const {
        return std::holds_alternative<AstImportExpr>(expr.value);
    }

    ExprInfo analyzeExpr(const AstExpr& expr,
                         const TypeRef& expectedType = nullptr) {
        ExprInfo result;

        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, AstLiteralExpr>) {
                    switch (value.token.type()) {
                        case TokenType::NUMBER:
                            result = ExprInfo{inferNumberLiteralType(value.token),
                                              false, false, "",
                                              value.token.line()};
                            break;
                        case TokenType::STRING:
                            result = ExprInfo{TypeInfo::makeStr(), false, false, "",
                                              value.token.line()};
                            break;
                        case TokenType::TRUE:
                        case TokenType::FALSE:
                            result = ExprInfo{TypeInfo::makeBool(), false, false,
                                              "", value.token.line()};
                            break;
                        case TokenType::_NULL:
                        case TokenType::TYPE_NULL_KW:
                            result = ExprInfo{TypeInfo::makeNull(), false, false,
                                              "", value.token.line()};
                            break;
                        default:
                            result = ExprInfo{TypeInfo::makeAny(), false, false,
                                              "", value.token.line()};
                            break;
                    }
                } else if constexpr (std::is_same_v<T, AstIdentifierExpr>) {
                    const std::string name = tokenText(value.name);
                    const AstBindingRef* binding = lookupBinding(expr.node.id);
                    if (!binding) {
                        const bool isClass =
                            m_classNames.find(name) != m_classNames.end();
                        TypeRef type =
                            isClass ? TypeInfo::makeClass(name) : TypeInfo::makeAny();
                        result = ExprInfo{type, !isClass, isClass, name,
                                          value.name.line(), false, false, 0};
                    } else if (binding->kind == AstBindingKind::Function &&
                               binding->declarationNodeId == 0) {
                        auto signatureIt = m_functionSignatures.find(name);
                        TypeRef type = signatureIt != m_functionSignatures.end()
                                           ? signatureIt->second
                                           : TypeInfo::makeAny();
                        result = ExprInfo{type, false, false, name,
                                          value.name.line(), false, true, 0};
                    } else if (binding->kind == AstBindingKind::Class) {
                        const std::string className =
                            binding->className.empty() ? binding->name
                                                       : binding->className;
                        result = ExprInfo{TypeInfo::makeClass(className), false,
                                          true, binding->name, value.name.line(),
                                          false, true,
                                          binding->declarationNodeId};
                    } else if (binding->kind == AstBindingKind::ThisValue) {
                        result = ExprInfo{TypeInfo::makeClass(binding->className),
                                          false, false, "", value.name.line(),
                                          false, true,
                                          binding->declarationNodeId};
                    } else if (binding->kind == AstBindingKind::SuperValue) {
                        auto superIt = m_bindings.metadata.superclassOf.find(
                            binding->className);
                        TypeRef type =
                            (superIt == m_bindings.metadata.superclassOf.end())
                                ? TypeInfo::makeAny()
                                : TypeInfo::makeClass(superIt->second);
                        result = ExprInfo{type, false, false, "",
                                          value.name.line(), false, true,
                                          binding->declarationNodeId};
                    } else {
                        const SymbolInfo* symbol =
                            declaredValue(binding->declarationNodeId);
                        TypeRef type = symbol ? symbol->type : TypeInfo::makeAny();
                        const bool isConst = symbol ? symbol->isConst
                                                    : binding->isConst;
                        result = ExprInfo{type, true, false, name,
                                          value.name.line(), isConst, true,
                                          binding->declarationNodeId};
                    }
                } else if constexpr (std::is_same_v<T, AstGroupingExpr>) {
                    ExprInfo inner = analyzeExpr(*value.expression);
                    result = ExprInfo{inner.type, false, false, "", expr.node.line};
                } else if constexpr (std::is_same_v<T, AstUnaryExpr>) {
                    ExprInfo operand = analyzeExpr(*value.operand);
                    if (value.op.type() == TokenType::BANG) {
                        if (!(operand.type->kind == TypeKind::BOOL ||
                              operand.type->isAny())) {
                            addError(value.op,
                                     "Type error: unary '!' expects a bool "
                                     "operand.");
                        }
                        result = ExprInfo{TypeInfo::makeBool(), false, false, "",
                                          value.op.line()};
                    } else if (value.op.type() == TokenType::MINUS) {
                        if (!(operand.type->isNumeric() || operand.type->isAny())) {
                            addError(value.op,
                                     "Type error: unary '-' expects a numeric "
                                     "operand.");
                        }
                        result = ExprInfo{operand.type, false, false, "",
                                          value.op.line()};
                    } else {
                        if (!(operand.type->isInteger() || operand.type->isAny())) {
                            addError(value.op,
                                     "Type error: unary '~' expects an integer "
                                     "operand.");
                        }
                        result = ExprInfo{operand.type, false, false, "",
                                          value.op.line()};
                    }
                } else if constexpr (std::is_same_v<T, AstUpdateExpr>) {
                    ExprInfo operand = analyzeExpr(*value.operand);
                    if (value.isPrefix) {
                        if (operand.isConstSymbol && !operand.name.empty()) {
                            addError(value.op,
                                     "Type error: cannot assign to const "
                                     "variable '" +
                                         operand.name + "'.");
                        }
                        if (!(operand.type->isNumeric() || operand.type->isAny())) {
                            addError(value.op,
                                     "Type error: update operator expects a "
                                     "numeric operand.");
                        }
                        result = ExprInfo{operand.type, false, false, "",
                                          value.op.line()};
                    } else {
                        if (!operand.isAssignable) {
                            result = ExprInfo{TypeInfo::makeAny(), false, false,
                                              "", value.op.line()};
                        } else {
                            if (operand.isConstSymbol && !operand.name.empty()) {
                                addError(value.op,
                                         "Type error: cannot assign to const "
                                         "variable '" +
                                             operand.name + "'.");
                                result =
                                    ExprInfo{operand.type ? operand.type
                                                          : TypeInfo::makeAny(),
                                             false, false, operand.name,
                                             value.op.line(), true};
                            } else {
                                TypeRef targetType = operand.type;
                                if (!operand.hasBinding && !operand.name.empty()) {
                                    targetType = nullptr;
                                }
                                if (!targetType && !operand.name.empty()) {
                                    addError(value.op,
                                             "Type error: unknown assignment "
                                             "target '" +
                                                 operand.name + "'.");
                                    result = ExprInfo{TypeInfo::makeAny(), false,
                                                      false, operand.name,
                                                      value.op.line()};
                                } else if (targetType->isAny()) {
                                    result = ExprInfo{TypeInfo::makeAny(), false,
                                                      false, operand.name,
                                                      value.op.line()};
                                } else {
                                    if (!targetType->isNumeric()) {
                                        addError(value.op,
                                                 "Type error: update operator "
                                                 "expects numeric operand.");
                                    }
                                    result = ExprInfo{targetType, false, false,
                                                      operand.name,
                                                      value.op.line()};
                                }
                            }
                        }
                    }
                } else if constexpr (std::is_same_v<T, AstBinaryExpr>) {
                    ExprInfo lhs = analyzeExpr(*value.left);
                    ExprInfo rhs = analyzeExpr(*value.right);
                    const TokenType op = value.op.type();
                    const SourceSpan opSpan = value.op.span();
                    const size_t line = value.op.line();

                    if (op == TokenType::LOGICAL_OR ||
                        op == TokenType::LOGICAL_AND) {
                        if (!(lhs.type->kind == TypeKind::BOOL ||
                              lhs.type->isAny()) ||
                            !(rhs.type->kind == TypeKind::BOOL ||
                              rhs.type->isAny())) {
                            addError(
                                opSpan,
                                std::string("Type error: logical '") +
                                    (op == TokenType::LOGICAL_OR ? "||" : "&&") +
                                    "' expects bool operands.");
                        }
                        result = ExprInfo{TypeInfo::makeBool(), false, false, "",
                                          line};
                    } else if (isEqualityOperator(op)) {
                        if (TypeRef overloaded =
                                lookupOperatorResultType(lhs.type, op, rhs.type,
                                                         opSpan)) {
                            result = ExprInfo{overloaded, false, false, "", line};
                        } else {
                            if (!(isAssignableType(lhs.type, rhs.type) ||
                                  isAssignableType(rhs.type, lhs.type))) {
                                addError(
                                    opSpan,
                                    "Type error: incompatible operands for "
                                    "equality operator.");
                            }
                            result =
                                ExprInfo{TypeInfo::makeBool(), false, false, "",
                                         line};
                        }
                    } else if (isComparisonOperator(op)) {
                        if (TypeRef overloaded =
                                lookupOperatorResultType(lhs.type, op, rhs.type,
                                                         opSpan)) {
                            result = ExprInfo{overloaded, false, false, "", line};
                        } else {
                            bool lhsOk = lhs.type->isAny() || lhs.type->isNumeric();
                            bool rhsOk = rhs.type->isAny() || rhs.type->isNumeric();
                            if (!lhsOk || !rhsOk) {
                                addError(opSpan,
                                         "Type error: comparison operators "
                                         "require numeric operands.");
                            }
                            result =
                                ExprInfo{TypeInfo::makeBool(), false, false, "",
                                         line};
                        }
                    } else if (op == TokenType::PIPE || op == TokenType::CARET ||
                               op == TokenType::AMPERSAND) {
                        if (lhs.type->isAny() || rhs.type->isAny()) {
                            result =
                                ExprInfo{TypeInfo::makeAny(), false, false, "",
                                         line};
                        } else {
                            TypeRef resultType =
                                bitwiseIntegerResultType(lhs.type, rhs.type);
                            if (!resultType) {
                                addError(opSpan,
                                         "Type error: bitwise operators "
                                         "require integer operands.");
                                result = ExprInfo{TypeInfo::makeAny(), false,
                                                  false, "", line};
                            } else {
                                result = ExprInfo{resultType, false, false, "",
                                                  line};
                            }
                        }
                    } else if (op == TokenType::SHIFT_LEFT_TOKEN ||
                               op == TokenType::SHIFT_RIGHT_TOKEN) {
                       bool lhsOk = lhs.type->isInteger() || lhs.type->isAny();
                        bool rhsOk = rhs.type->isInteger() || rhs.type->isAny();
                        if (!lhsOk || !rhsOk) {
                            addError(opSpan,
                                     "Type error: shift operators require "
                                     "integer operands.");
                        }
                        result = ExprInfo{lhs.type, false, false, "", line};
                    } else {
                        if (TypeRef overloaded =
                                lookupOperatorResultType(lhs.type, op, rhs.type,
                                                         opSpan)) {
                            result = ExprInfo{overloaded, false, false, "", line};
                        } else if (op == TokenType::PLUS &&
                                   lhs.type->kind == TypeKind::STR &&
                                   rhs.type->kind == TypeKind::STR) {
                            result = ExprInfo{TypeInfo::makeStr(), false, false,
                                              "", line};
                        } else if (lhs.type->isAny() || rhs.type->isAny()) {
                            result =
                                ExprInfo{TypeInfo::makeAny(), false, false, "",
                                         line};
                        } else {
                            bool lhsOk = lhs.type->isNumeric();
                            bool rhsOk = rhs.type->isNumeric();
                            if (!lhsOk || !rhsOk) {
                                addError(opSpan,
                                         "Type error: arithmetic operator "
                                         "requires numeric operands.");
                                result = ExprInfo{TypeInfo::makeAny(), false,
                                                  false, "", line};
                            } else {
                                TypeRef promoted =
                                    numericPromotion(lhs.type, rhs.type);
                                result =
                                    ExprInfo{promoted ? promoted
                                                      : TypeInfo::makeAny(),
                                             false, false, "", line};
                            }
                        }
                    }
                } else if constexpr (std::is_same_v<T, AstAssignmentExpr>) {
                    ExprInfo lhs = analyzeExpr(*value.target);
                    const TokenType assignmentType = value.op.type();
                    const SourceSpan opSpan = value.op.span();
                    const size_t line = value.op.line();

                    if (!lhs.isAssignable) {
                        result = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                          line};
                    } else if (lhs.isConstSymbol && !lhs.name.empty()) {
                        addError(opSpan,
                                 "Type error: cannot assign to const variable '" +
                                     lhs.name + "'.");
                        result = ExprInfo{lhs.type ? lhs.type
                                                   : TypeInfo::makeAny(),
                                          false, false, lhs.name, line, true};
                    } else {
                        TypeRef targetType = lhs.type;
                        if (!lhs.hasBinding && !lhs.name.empty()) {
                            targetType = nullptr;
                        }
                        if (!targetType && !lhs.name.empty()) {
                            addError(opSpan,
                                     "Type error: unknown assignment target '" +
                                         lhs.name + "'.");
                            result = ExprInfo{TypeInfo::makeAny(), false, false,
                                              lhs.name, line};
                        } else if (assignmentType == TokenType::EQUAL) {
                            ExprInfo rhs = analyzeExpr(*value.value, targetType);
                            if (!isAssignableType(rhs.type, targetType)) {
                                addError(opSpan,
                                         "Type error: cannot assign '" +
                                             rhs.type->toString() +
                                             "' to variable '" + lhs.name +
                                             "' of type '" +
                                             targetType->toString() + "'.");
                            }
                            result = ExprInfo{targetType, false, false, lhs.name,
                                              line};
                        } else {
                            ExprInfo rhs = analyzeExpr(*value.value);
                            if (targetType->isAny() || rhs.type->isAny()) {
                            result = ExprInfo{targetType, false, false, lhs.name,
                                              line};
                            } else if (isArithmeticCompoundAssignment(
                                           assignmentType)) {
                                if (!(targetType->isNumeric() &&
                                      rhs.type->isNumeric())) {
                                    addError(opSpan,
                                             "Type error: compound assignment "
                                             "requires numeric operands.");
                                    result = ExprInfo{TypeInfo::makeAny(), false,
                                                      false, lhs.name, line};
                                } else {
                                    TypeRef promoted =
                                        numericPromotion(targetType, rhs.type);
                                    if (!promoted ||
                                        !isAssignableType(promoted, targetType)) {
                                        addError(opSpan,
                                                 "Type error: result of compound "
                                                 "assignment is not assignable to '" +
                                                     targetType->toString() + "'.");
                                    }
                                    result = ExprInfo{targetType, false, false,
                                                      lhs.name, line};
                                }
                            } else if (assignmentType == TokenType::SHIFT_LEFT_EQUAL ||
                                       assignmentType ==
                                           TokenType::SHIFT_RIGHT_EQUAL) {
                                if (!(targetType->isInteger() &&
                                      rhs.type->isInteger())) {
                                    addError(opSpan,
                                             "Type error: shift operators require "
                                             "integer operands.");
                                    result = ExprInfo{TypeInfo::makeAny(), false,
                                                      false, lhs.name, line};
                                } else {
                                    result = ExprInfo{targetType, false, false,
                                                      lhs.name, line};
                                }
                            } else if (isBitwiseCompoundAssignment(assignmentType)) {
                                if (!(targetType->isInteger() &&
                                      rhs.type->isInteger())) {
                                    addError(opSpan,
                                             "Type error: bitwise operators require "
                                             "integer operands.");
                                    result = ExprInfo{TypeInfo::makeAny(), false,
                                                      false, lhs.name, line};
                                } else {
                                    TypeRef resultType = bitwiseIntegerResultType(
                                        targetType, rhs.type);
                                    if (!resultType ||
                                        !isAssignableType(resultType, targetType)) {
                                        addError(opSpan,
                                                 "Type error: result of compound "
                                                 "assignment is not assignable to '" +
                                                     targetType->toString() + "'.");
                                    }
                                    result = ExprInfo{targetType, false, false,
                                                      lhs.name, line};
                                }
                            } else {
                                result = ExprInfo{targetType, false, false,
                                                  lhs.name, line};
                            }
                        }
                    }
                } else if constexpr (std::is_same_v<T, AstCallExpr>) {
                    ExprInfo callee = analyzeExpr(*value.callee);
                    std::vector<ExprInfo> args;
                    args.reserve(value.arguments.size());
                    for (size_t index = 0; index < value.arguments.size();
                         ++index) {
                        TypeRef expectedArgType = nullptr;
                        if (callee.type && callee.type->kind == TypeKind::FUNCTION &&
                            index < callee.type->paramTypes.size()) {
                            expectedArgType = callee.type->paramTypes[index];
                        }
                        args.push_back(
                            analyzeExpr(*value.arguments[index], expectedArgType));
                    }

                    if (callee.type && callee.type->isOptional()) {
                        addError(value.callee->node,
                                 "Type error: cannot call optional value of "
                                 "type '" +
                                     callee.type->toString() +
                                     "' without a null check.");
                        result = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                          expr.node.line};
                    } else if (callee.isClassSymbol) {
                        result = ExprInfo{TypeInfo::makeClass(callee.name), false,
                                          false, "", expr.node.line};
                    } else if (!callee.type || callee.type->isAny()) {
                        result = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                          expr.node.line};
                    } else if (callee.type->kind != TypeKind::FUNCTION) {
                        addError(value.callee->node,
                                 "Type error: attempted to call a non-function "
                                 "value.");
                        result = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                          expr.node.line};
                    } else {
                        const auto& params = callee.type->paramTypes;
                        if (!params.empty() && params.size() != args.size()) {
                            addError(expr.node,
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
                                addError(value.arguments[index]->node,
                                         "Type error: function argument " +
                                             std::to_string(index + 1) +
                                             " expects '" +
                                             expected->toString() + "', got '" +
                                             args[index].type->toString() + "'.");
                            }
                        }

                        result = ExprInfo{
                            callee.type->returnType ? callee.type->returnType
                                                    : TypeInfo::makeAny(),
                            false, false, "", expr.node.line};
                    }
                } else if constexpr (std::is_same_v<T, AstMemberExpr>) {
                    ExprInfo receiver = analyzeExpr(*value.object);
                    if (receiver.type && receiver.type->isOptional()) {
                        addError(value.object->node,
                                 "Type error: cannot access members on optional "
                                 "value of type '" +
                                     receiver.type->toString() +
                                     "' without a null check.");
                    }

                    const std::string memberName = tokenText(value.member);
                    if (receiver.type &&
                        receiver.type->kind == TypeKind::CLASS) {
                        TypeRef fieldType =
                            lookupClassFieldType(receiver.type->className,
                                                 memberName);
                        if (fieldType) {
                            result = ExprInfo{fieldType, true, false, "",
                                              value.member.line()};
                        } else {
                            TypeRef methodType =
                                lookupClassMethodType(receiver.type->className,
                                                      memberName);
                            if (methodType) {
                                result = ExprInfo{methodType, false, false, "",
                                                  value.member.line()};
                            } else {
                                addError(value.member,
                                         "Type error: class '" +
                                             receiver.type->className +
                                             "' has no member '" + memberName +
                                             "'.");
                                result = ExprInfo{TypeInfo::makeAny(), false,
                                                  false, "",
                                                  value.member.line()};
                            }
                        }
                    } else if (receiver.type &&
                               (receiver.type->kind == TypeKind::ARRAY ||
                                receiver.type->kind == TypeKind::DICT ||
                                receiver.type->kind == TypeKind::SET)) {
                        TypeRef memberType = collectionMemberType(
                            receiver.type, memberName, value.member.span());
                        result = ExprInfo{memberType ? memberType
                                                     : TypeInfo::makeAny(),
                                          false, false, "",
                                          value.member.line()};
                    } else {
                        result = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                          value.member.line()};
                    }
                } else if constexpr (std::is_same_v<T, AstIndexExpr>) {
                    ExprInfo collection = analyzeExpr(*value.object);
                    ExprInfo index = analyzeExpr(*value.index);

                    if (!collection.type || collection.type->isAny()) {
                        result = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                          expr.node.line};
                    } else if (collection.type->kind == TypeKind::ARRAY) {
                        if (!(index.type->isInteger() || index.type->isAny())) {
                            addError(value.index->node,
                                     "Type error: array index must be an "
                                     "integer.");
                        }
                        TypeRef element = collection.type->elementType
                                              ? collection.type->elementType
                                              : TypeInfo::makeAny();
                        result = ExprInfo{element, true, false, "",
                                          expr.node.line};
                    } else if (collection.type->kind == TypeKind::DICT) {
                        TypeRef keyType = collection.type->keyType
                                              ? collection.type->keyType
                                              : TypeInfo::makeAny();
                        if (!isAssignableType(index.type, keyType)) {
                            addError(value.index->node,
                                     "Type error: dict key expects '" +
                                         keyType->toString() + "', got '" +
                                         index.type->toString() + "'.");
                        }
                        TypeRef valueType = collection.type->valueType
                                                ? collection.type->valueType
                                                : TypeInfo::makeAny();
                        result = ExprInfo{valueType, true, false, "",
                                          expr.node.line};
                    } else if (collection.type->kind == TypeKind::SET) {
                        TypeRef elementType = collection.type->elementType
                                                  ? collection.type->elementType
                                                  : TypeInfo::makeAny();
                        if (!isAssignableType(index.type, elementType)) {
                            addError(value.index->node,
                                     "Type error: set lookup expects '" +
                                         elementType->toString() + "', got '" +
                                         index.type->toString() + "'.");
                        }
                        result = ExprInfo{TypeInfo::makeBool(), false, false, "",
                                          expr.node.line};
                    } else {
                        addError(expr.node,
                                 "Type error: indexing is only valid on Array, "
                                 "Dict, or Set.");
                        result = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                          expr.node.line};
                    }
                } else if constexpr (std::is_same_v<T, AstCastExpr>) {
                    ExprInfo inner = analyzeExpr(*value.expression);
                    TypeRef target = requireTypeExpr(
                        value.targetType.get(), expr.node.span,
                        "Type error: expected type after 'as'.");
                    if (!target) {
                        result = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                          expr.node.line};
                    } else {
                        if (!isValidExplicitCast(inner.type, target)) {
                            addError(expr.node,
                                     "Type error: cannot cast '" +
                                         inner.type->toString() + "' to '" +
                                         target->toString() + "'.");
                        }
                        result = ExprInfo{target, false, false, "",
                                          expr.node.line};
                    }
                } else if constexpr (std::is_same_v<T, AstFunctionExpr>) {
                    ResolvedCallableSignature signature = resolveCallableSignature(
                        "<closure>", value.params, value.returnType.get(),
                        expectedType, false, true,
                        !value.expressionBody, expr.node.span);

                    m_functionContexts.push_back(FunctionCtx{signature.returnType});
                    for (size_t index = 0; index < signature.params.size();
                         ++index) {
                        declareValue(value.params[index].node.id,
                                     signature.params[index].second, false);
                    }

                    if (value.expressionBody) {
                        if (signature.omittedParameterTypeAnnotation) {
                            addError(signature.firstOmittedParameterSpan,
                                     "Type error: expression-bodied lambdas "
                                     "require explicit parameter types.");
                        }

                        ExprInfo body = analyzeExpr(*value.expressionBody);
                        m_functionContexts.pop_back();
                        result = ExprInfo{
                            TypeInfo::makeFunction(signature.paramTypes, body.type),
                            false, false, "", body.line};
                    } else {
                        if (value.usesFatArrow && value.blockBody) {
                            addError(value.blockBody->node,
                                     "Type error: expression-bodied lambdas do "
                                     "not support block bodies; use "
                                     "'fn(...) { ... }'.");
                        }
                        if (value.blockBody) {
                            analyzeFunctionBody(*value.blockBody);
                        }
                        m_functionContexts.pop_back();
                        result = ExprInfo{TypeInfo::makeFunction(
                                              signature.paramTypes,
                                              signature.returnType),
                                          false, false, "", expr.node.line};
                    }
                } else if constexpr (std::is_same_v<T, AstImportExpr>) {
                    result = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                      value.path.line()};
                } else if constexpr (std::is_same_v<T, AstThisExpr>) {
                    if (m_classContexts.empty()) {
                        result = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                          value.token.line()};
                    } else {
                        result = ExprInfo{TypeInfo::makeClass(
                                              m_classContexts.back().className),
                                          false, false, "", value.token.line()};
                    }
                } else if constexpr (std::is_same_v<T, AstSuperExpr>) {
                    if (m_classContexts.empty()) {
                        result = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                          value.token.line()};
                    } else {
                        const std::string className =
                            m_classContexts.back().className;
                        auto superIt =
                            m_bindings.metadata.superclassOf.find(className);
                        if (superIt == m_bindings.metadata.superclassOf.end()) {
                            result = ExprInfo{TypeInfo::makeAny(), false, false,
                                              "", value.token.line()};
                        } else {
                            result = ExprInfo{
                                TypeInfo::makeClass(superIt->second), false,
                                false, "", value.token.line()};
                        }
                    }
                } else if constexpr (std::is_same_v<T, AstArrayLiteralExpr>) {
                    TypeRef elementType = nullptr;
                    for (const auto& element : value.elements) {
                        ExprInfo item = analyzeExpr(*element);
                        if (!elementType) {
                            elementType = item.type;
                            continue;
                        }

                        TypeRef merged =
                            mergeInferredTypes(elementType, item.type);
                        if (merged && merged->isAny() && !elementType->isAny() &&
                            !item.type->isAny() &&
                            !isAssignableType(item.type, elementType) &&
                            !isAssignableType(elementType, item.type)) {
                            addError(element->node,
                                     "Type error: array literal elements must "
                                     "have a consistent type.");
                        }
                        elementType = merged;
                    }

                    TypeRef arrayType = TypeInfo::makeArray(
                        elementType ? elementType : TypeInfo::makeAny());
                    if (value.elements.empty() && expectedType &&
                        expectedType->kind == TypeKind::ARRAY) {
                        arrayType = expectedType;
                    }
                    result =
                        ExprInfo{arrayType, false, false, "", expr.node.line};
                } else if constexpr (std::is_same_v<T, AstDictLiteralExpr>) {
                    TypeRef keyType = nullptr;
                    TypeRef valueType = nullptr;
                    for (const auto& entry : value.entries) {
                        ExprInfo keyExpr = analyzeExpr(*entry.key);
                        if (!keyType) {
                            keyType = keyExpr.type;
                        } else {
                            TypeRef mergedKey =
                                mergeInferredTypes(keyType, keyExpr.type);
                            if (mergedKey && mergedKey->isAny() &&
                                !keyType->isAny() && !keyExpr.type->isAny() &&
                                !isAssignableType(keyExpr.type, keyType) &&
                                !isAssignableType(keyType, keyExpr.type)) {
                                addError(keyExpr.line ? keyExpr.line
                                                      : expr.node.line,
                                         "Type error: dict literal keys must "
                                         "have a consistent type.");
                            }
                            keyType = mergedKey;
                        }

                        ExprInfo valueExpr = analyzeExpr(*entry.value);
                        if (!valueType) {
                            valueType = valueExpr.type;
                        } else {
                            TypeRef mergedValue =
                                mergeInferredTypes(valueType, valueExpr.type);
                            if (mergedValue && mergedValue->isAny() &&
                                !valueType->isAny() &&
                                !valueExpr.type->isAny() &&
                                !isAssignableType(valueExpr.type, valueType) &&
                                !isAssignableType(valueType, valueExpr.type)) {
                                addError(valueExpr.line ? valueExpr.line
                                                        : expr.node.line,
                                         "Type error: dict literal values must "
                                         "have a consistent type.");
                            }
                            valueType = mergedValue;
                        }
                    }

                    TypeRef dictType =
                        TypeInfo::makeDict(keyType ? keyType : TypeInfo::makeAny(),
                                           valueType ? valueType
                                                     : TypeInfo::makeAny());
                    if (value.entries.empty() && expectedType &&
                        expectedType->kind == TypeKind::DICT) {
                        dictType = expectedType;
                    }
                    result =
                        ExprInfo{dictType, false, false, "", expr.node.line};
                }
            },
            expr.value);

        recordNodeType(expr.node, result.type);
        return result;
    }

    void analyzeVarDecl(const AstNodeInfo& stmtNode, const AstVarDeclStmt& stmt) {
        const std::string name = tokenText(stmt.name);
        TypeRef declaredType = TypeInfo::makeAny();
        const bool omittedType = stmt.omittedType;

        if (omittedType) {
            if (!stmt.initializer || !isImportExpr(*stmt.initializer)) {
                addError(stmt.name.span(),
                         "Type error: variables require an explicit type unless "
                         "initialized from '@import(...)'.");
            }
        } else {
            declaredType = requireTypeExpr(
                stmt.declaredType.get(), stmt.name.span(),
                "Type error: expected type after variable name.");
            if (!declaredType) {
                declaredType = TypeInfo::makeAny();
            }
            if (declaredType->isVoid()) {
                addError(stmtNode.span,
                         "Type error: variables cannot have type 'void'.");
            }
        }

        TypeRef expectedInitializerType = omittedType ? nullptr : declaredType;

        ExprInfo initializer = stmt.initializer
                                   ? analyzeExpr(*stmt.initializer,
                                                 expectedInitializerType)
                                   : ExprInfo{};

        if (!omittedType && !isAssignableType(initializer.type, declaredType)) {
            addError(stmt.name.span(), "Type error: cannot assign '" +
                                           initializer.type->toString() +
                                           "' to variable '" + name +
                                           "' of type '" +
                                           declaredType->toString() + "'.");
        }

        TypeRef finalType = omittedType ? initializer.type : declaredType;
        declareValue(stmtNode.id, finalType, stmt.isConst);
        recordDeclarationType(stmtNode, name, finalType, stmt.name.line(),
                              stmt.isConst);
    }

    void analyzeDestructuredImport(const AstStmt& stmtNode,
                                   const AstDestructuredImportStmt& stmt) {
        if (stmt.initializer) {
            analyzeExpr(*stmt.initializer);
        }

        const AstImportedModuleInterface* importedModule =
            stmt.initializer ? lookupImportedModule(stmt.initializer->node.id)
                             : nullptr;

        for (const auto& binding : stmt.bindings) {
            const std::string localName =
                binding.localName.has_value() ? tokenText(*binding.localName)
                                              : tokenText(binding.exportedName);
            const size_t line = binding.localName.has_value()
                                    ? binding.localName->line()
                                    : binding.exportedName.line();
            TypeRef importedType = TypeInfo::makeAny();
            if (!importedModule) {
                importedType = TypeInfo::makeAny();
            } else {
                auto exportIt = importedModule->exportTypes.find(
                    tokenText(binding.exportedName));
                if (exportIt == importedModule->exportTypes.end()) {
                    addError(binding.exportedName,
                             "Type error: imported module '" +
                                 importedModule->importTarget.displayName +
                                 "' has no export '" +
                                 tokenText(binding.exportedName) + "'.");
                } else if (exportIt->second) {
                    importedType = exportIt->second;
                }
            }

            TypeRef finalType = importedType;
            if (binding.expectedType) {
                TypeRef expectedType = requireTypeExpr(
                    binding.expectedType.get(), binding.exportedName.span(),
                    "Type error: expected type after ':' in import binding.");
                if (!expectedType) {
                    expectedType = TypeInfo::makeAny();
                }

                if (!isAssignableType(importedType, expectedType)) {
                    addError(binding.exportedName,
                             "Type error: cannot assign imported value '" +
                                 importedType->toString() + "' to binding '" +
                                 localName + "' of type '" +
                                 expectedType->toString() + "'.");
                }
                finalType = expectedType;
            }

            declareValue(binding.node.id, finalType, true);
            recordDeclarationType(binding.node, localName, finalType, line, true);
            recordNodeConstness(binding.node, true);
        }

        recordNodeConstness(stmtNode.node, true);
    }

    void analyzeReturnStmt(const AstNodeInfo& stmtNode, const AstReturnStmt& stmt) {
        if (m_functionContexts.empty()) {
            addError(stmtNode, "Type error: cannot return from top-level code.");
        }

        if (!stmt.value) {
            if (!m_functionContexts.empty()) {
                TypeRef expected = m_functionContexts.back().returnType;
                if (expected && !expected->isVoid()) {
                    addError(stmtNode,
                             "Type error: function expects return type '" +
                                 expected->toString() + "'.");
                }
            }
            return;
        }

        if (!m_functionContexts.empty()) {
            TypeRef expected = m_functionContexts.back().returnType;
            ExprInfo value = analyzeExpr(*stmt.value, expected);
            if (expected && !isAssignableType(value.type, expected)) {
                addError(stmt.value->node,
                         "Type error: cannot return '" +
                             value.type->toString() +
                             "' from function returning '" +
                             expected->toString() + "'.");
            }
        }
    }

    void beginLoop(const std::optional<Token>& label) {
        if (label) {
            for (auto it = m_loopContexts.rbegin(); it != m_loopContexts.rend();
                 ++it) {
                if (it->label && labelsEqual(*it->label, *label)) {
                    addError(*label, "Type error: duplicate loop label '" +
                                         tokenText(*label) + "'.");
                    break;
                }
            }
        }

        m_loopContexts.push_back(LoopCtx{label});
    }

    void endLoop() {
        if (!m_loopContexts.empty()) {
            m_loopContexts.pop_back();
        }
    }

    void analyzeLoopControlStmt(const AstNodeInfo& stmtNode,
                                const std::optional<Token>& label,
                                bool isContinue) {
        const char* action = isContinue ? "continue" : "break";
        if (m_loopContexts.empty()) {
            addError(stmtNode, std::string("Type error: cannot ") + action +
                                   " outside of a loop.");
            return;
        }

        if (!label) {
            return;
        }

        for (auto it = m_loopContexts.rbegin(); it != m_loopContexts.rend();
             ++it) {
            if (it->label && labelsEqual(*it->label, *label)) {
                return;
            }
        }

        addError(*label, std::string("Type error: unknown loop label '") +
                             tokenText(*label) + "' for '" + action + "'.");
    }

    void analyzeStmt(const AstStmt& stmt) {
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, AstBlockStmt>) {
                    for (const auto& item : value.items) {
                        if (item) {
                            analyzeItem(*item);
                        }
                    }
                } else if constexpr (std::is_same_v<T, AstExprStmt>) {
                    analyzeExpr(*value.expression);
                } else if constexpr (std::is_same_v<T, AstPrintStmt>) {
                    analyzeExpr(*value.expression);
                } else if constexpr (std::is_same_v<T, AstReturnStmt>) {
                    analyzeReturnStmt(stmt.node, value);
                } else if constexpr (std::is_same_v<T, AstBreakStmt>) {
                    analyzeLoopControlStmt(stmt.node, value.label, false);
                } else if constexpr (std::is_same_v<T, AstContinueStmt>) {
                    analyzeLoopControlStmt(stmt.node, value.label, true);
                } else if constexpr (std::is_same_v<T, AstIfStmt>) {
                    ExprInfo cond = analyzeExpr(*value.condition);
                    if (!(cond.type->kind == TypeKind::BOOL ||
                          cond.type->isAny())) {
                        addError(value.condition->node,
                                 "Type error: if condition must be bool.");
                    }
                    analyzeStmt(*value.thenBranch);
                    if (value.elseBranch) {
                        analyzeStmt(*value.elseBranch);
                    }
                } else if constexpr (std::is_same_v<T, AstWhileStmt>) {
                    ExprInfo cond = analyzeExpr(*value.condition);
                    if (!(cond.type->kind == TypeKind::BOOL ||
                          cond.type->isAny())) {
                        addError(value.condition->node,
                                 "Type error: while condition must be bool.");
                    }
                    beginLoop(value.label);
                    analyzeStmt(*value.body);
                    endLoop();
                } else if constexpr (std::is_same_v<T, AstVarDeclStmt>) {
                    analyzeVarDecl(stmt.node, value);
                } else if constexpr (std::is_same_v<T,
                                                    AstDestructuredImportStmt>) {
                    analyzeDestructuredImport(stmt, value);
                } else if constexpr (std::is_same_v<T, AstForStmt>) {
                    if (const auto* initDecl =
                            std::get_if<std::unique_ptr<AstVarDeclStmt>>(
                                &value.initializer)) {
                        if (*initDecl) {
                            analyzeVarDecl((*initDecl)->node, **initDecl);
                        }
                    } else if (const auto* initExpr =
                                   std::get_if<AstExprPtr>(&value.initializer)) {
                        if (*initExpr) {
                            analyzeExpr(**initExpr);
                        }
                    }

                    if (value.condition) {
                        ExprInfo cond = analyzeExpr(*value.condition);
                        if (!(cond.type->kind == TypeKind::BOOL ||
                              cond.type->isAny())) {
                            addError(value.condition->node,
                                     "Type error: for condition must be bool.");
                        }
                    }

                    if (value.increment) {
                        analyzeExpr(*value.increment);
                    }

                    beginLoop(value.label);
                    analyzeStmt(*value.body);
                    endLoop();
                } else if constexpr (std::is_same_v<T, AstForEachStmt>) {
                    TypeRef declaredType = requireTypeExpr(
                        value.declaredType.get(), value.name.span(),
                        "Type error: expected type after loop variable name.");
                    if (!declaredType) {
                        declaredType = TypeInfo::makeAny();
                    }
                    if (declaredType->isVoid()) {
                        addError(value.name,
                                 "Type error: variables cannot have type "
                                 "'void'.");
                    }

                    ExprInfo iterable = analyzeExpr(*value.iterable);
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
                    } else if (!(iterable.type && iterable.type->isAny())) {
                        addError(value.iterable->node,
                                 "Type error: foreach expects Array<T>, Dict<K, "
                                 "V>, or Set<T>.");
                    }

                    if (!isAssignableType(inferredLoopType, declaredType)) {
                        addError(value.name,
                                 "Type error: cannot assign '" +
                                     inferredLoopType->toString() +
                                     "' to variable '" + tokenText(value.name) +
                                     "' of type '" +
                                     declaredType->toString() + "'.");
                    }

                    declareValue(stmt.node.id, declaredType, value.isConst);
                    recordDeclarationType(stmt.node, tokenText(value.name),
                                          declaredType, value.name.line(),
                                          value.isConst);
                    beginLoop(value.label);
                    analyzeStmt(*value.body);
                    endLoop();
                }
            },
            stmt.value);
    }

    void analyzeFunctionBody(const AstStmt& body) {
        if (const auto* block = std::get_if<AstBlockStmt>(&body.value)) {
            for (const auto& item : block->items) {
                if (item) {
                    analyzeItem(*item);
                }
            }
            return;
        }

        analyzeStmt(body);
    }

    void analyzeFunctionDecl(const AstFunctionDecl& functionDecl) {
        const std::string functionName = tokenText(functionDecl.name);
        TypeRef functionType = TypeInfo::makeAny();
        auto it = m_functionSignatures.find(functionName);
        if (it != m_functionSignatures.end()) {
            functionType = it->second;
        }

        declareValue(functionDecl.node.id, functionType, false);
        recordDeclarationType(functionDecl.node, functionName, functionType,
                              functionDecl.name.line());

        ResolvedCallableSignature signature = resolveCallableSignature(
            functionName, functionDecl.params, functionDecl.returnType.get(),
            functionType, false, false, true, functionDecl.name.span());

        m_functionContexts.push_back(FunctionCtx{signature.returnType});
        for (size_t index = 0; index < signature.params.size(); ++index) {
            declareValue(functionDecl.params[index].node.id,
                         signature.params[index].second, false);
        }

        if (functionDecl.body) {
            analyzeFunctionBody(*functionDecl.body);
        }

        m_functionContexts.pop_back();
    }

    void analyzeClassDecl(const AstClassDecl& classDecl) {
        const std::string className = tokenText(classDecl.name);
        TypeRef classType = TypeInfo::makeClass(className);
        declareValue(classDecl.node.id, classType, false);
        recordDeclarationType(classDecl.node, className, classType,
                              classDecl.name.line());

        auto fieldIt = m_bindings.metadata.classFieldTypes.find(className);
        if (fieldIt != m_bindings.metadata.classFieldTypes.end()) {
            for (const auto& field : classDecl.fields) {
                auto resolved = fieldIt->second.find(tokenText(field.name));
                if (resolved != fieldIt->second.end()) {
                    recordNodeType(field.node, resolved->second);
                }
            }
        }

        m_classContexts.push_back(ClassCtx{className});
        for (const auto& method : classDecl.methods) {
            auto classIt = m_bindings.metadata.classMethodSignatures.find(className);
            TypeRef methodType = TypeInfo::makeFunction({}, TypeInfo::makeAny());
            if (classIt != m_bindings.metadata.classMethodSignatures.end()) {
                auto methodIt = classIt->second.find(tokenText(method.name));
                if (methodIt != classIt->second.end()) {
                    methodType = methodIt->second;
                }
            }

            recordNodeType(method.node, methodType);

            m_functionContexts.push_back(FunctionCtx{
                methodType && methodType->kind == TypeKind::FUNCTION &&
                        methodType->returnType
                    ? methodType->returnType
                    : TypeInfo::makeAny()});

            const auto& paramTypes =
                methodType && methodType->kind == TypeKind::FUNCTION
                    ? methodType->paramTypes
                    : std::vector<TypeRef>{};
            for (size_t index = 0; index < method.params.size(); ++index) {
                TypeRef paramType =
                    index < paramTypes.size() && paramTypes[index]
                        ? paramTypes[index]
                        : TypeInfo::makeAny();
                declareValue(method.params[index].node.id, paramType, false);
            }

            if (method.body) {
                analyzeFunctionBody(*method.body);
            }

            m_functionContexts.pop_back();
        }
        m_classContexts.pop_back();
    }

    void analyzeTypeAliasDecl(const AstTypeAliasDecl& aliasDecl) {
        TypeRef aliasedType = requireTypeExpr(
            aliasDecl.aliasedType.get(), aliasDecl.name.span(),
            "Type error: expected aliased type or 'struct'.");
        if (aliasedType) {
            m_typeAliases[tokenText(aliasDecl.name)] = aliasedType;
        }
    }

    void analyzeItem(const AstItem& item) {
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, AstTypeAliasDecl>) {
                    analyzeTypeAliasDecl(value);
                } else if constexpr (std::is_same_v<T, AstClassDecl>) {
                    analyzeClassDecl(value);
                } else if constexpr (std::is_same_v<T, AstFunctionDecl>) {
                    analyzeFunctionDecl(value);
                } else if constexpr (std::is_same_v<T, AstStmtPtr>) {
                    if (value) {
                        analyzeStmt(*value);
                    }
                }
            },
            item.value);
    }

   public:
    AstTypeCheckerImpl(
        const std::unordered_set<std::string>& classNames,
        const std::unordered_map<std::string, TypeRef>& typeAliases,
        const std::unordered_map<std::string, TypeRef>& functionSignatures,
        const AstBindResult& bindings,
        std::vector<TypeError>& errors, AstSemanticModel* model)
        : m_classNames(classNames),
          m_typeAliases(typeAliases),
          m_functionSignatures(functionSignatures),
          m_bindings(bindings),
          m_errors(errors),
          m_model(model) {}

    void run(const AstModule& module) {
        for (const auto& item : module.items) {
            if (item) {
                analyzeItem(*item);
            }
        }

        if (m_model) {
            m_model->importedModules = m_bindings.importedModules;
            m_model->classOperatorMethods = m_bindings.classOperatorMethods;
            m_model->metadata = m_bindings.metadata;
        }
    }
};

}  // namespace

bool checkAstTypes(
    const AstModule& module,
    const std::unordered_set<std::string>& classNames,
    const std::unordered_map<std::string, TypeRef>& typeAliases,
    const std::unordered_map<std::string, TypeRef>& functionSignatures,
    const AstBindResult& bindings,
    std::vector<TypeError>& out, AstSemanticModel* outModel) {
    AstTypeCheckerImpl checker(classNames, typeAliases, functionSignatures,
                               bindings, out, outModel);
    checker.run(module);
    return out.empty();
}
