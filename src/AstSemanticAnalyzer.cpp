#include "AstSemanticAnalyzer.hpp"

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

#include "NativePackage.hpp"
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

class AstSemanticAnalyzerImpl {
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
        TypeRef returnType = TypeInfo::makeAny();
    };

    struct ClassCtx {
        std::string className;
    };

    struct ResolvedCallableSignature {
        std::vector<std::pair<std::string, TypeRef>> params;
        std::vector<TypeRef> paramTypes;
        TypeRef returnType = TypeInfo::makeAny();
        bool omittedParameterTypeAnnotation = false;
        size_t firstOmittedParameterLine = 0;
    };

    const std::unordered_set<std::string>& m_classNames;
    std::unordered_map<std::string, TypeRef> m_typeAliases;
    const std::unordered_map<std::string, TypeRef>& m_functionSignatures;
    std::vector<TypeError>& m_errors;
    AstSemanticModel* m_model = nullptr;

    std::vector<std::unordered_map<std::string, SymbolInfo>> m_scopes;
    std::unordered_set<std::string> m_declaredGlobalSymbols;
    TypeCheckerMetadata m_metadata;
    std::unordered_map<std::string, std::unordered_map<int, std::string>>
        m_classOperatorMethods;
    std::vector<FunctionCtx> m_functionContexts;
    std::vector<ClassCtx> m_classContexts;

    std::string tokenText(const Token& token) const {
        return std::string(token.start(), token.length());
    }

    void addError(size_t line, const std::string& message) {
        m_errors.push_back(TypeError{line, message});
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

    void recordDeclarationType(const AstNodeInfo& node, const std::string& name,
                               const TypeRef& type, size_t line,
                               bool isConst = false) {
        TypeCheckerDeclarationType declaration;
        declaration.line = line;
        declaration.functionDepth = m_functionContexts.size();
        declaration.scopeDepth = m_scopes.empty() ? 0 : (m_scopes.size() - 1);
        declaration.name = name;
        declaration.type = type ? type : TypeInfo::makeAny();
        declaration.isConst = isConst;
        m_metadata.declarationTypes.push_back(std::move(declaration));
        recordNodeType(node, type);
        recordNodeConstness(node, isConst);
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

            auto it = m_metadata.superclassOf.find(current);
            if (it == m_metadata.superclassOf.end() || it->second.empty()) {
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

        if (floatLiteral && unsignedLiteral) {
            addError(token.line(), "Type error: invalid numeric literal '" +
                                       literal + "'.");
            return TypeInfo::makeAny();
        }

        try {
            if (hasDecimal) {
                (void)std::stod(core);
                return inferredType;
            }

            if (unsignedLiteral || inferredType->isUnsigned()) {
                unsigned long long value = std::stoull(core);
                const auto checkRange = [&](unsigned long long maxValue) {
                    return value <= maxValue;
                };

                switch (inferredType->kind) {
                    case TypeKind::U8:
                        if (!checkRange(std::numeric_limits<uint8_t>::max())) {
                            addError(token.line(),
                                     "Type error: integer literal '" + literal +
                                         "' is out of range for type '" +
                                         inferredType->toString() + "'.");
                            return TypeInfo::makeAny();
                        }
                        break;
                    case TypeKind::U16:
                        if (!checkRange(std::numeric_limits<uint16_t>::max())) {
                            addError(token.line(),
                                     "Type error: integer literal '" + literal +
                                         "' is out of range for type '" +
                                         inferredType->toString() + "'.");
                            return TypeInfo::makeAny();
                        }
                        break;
                    case TypeKind::U32:
                        if (!checkRange(std::numeric_limits<uint32_t>::max())) {
                            addError(token.line(),
                                     "Type error: integer literal '" + literal +
                                         "' is out of range for type '" +
                                         inferredType->toString() + "'.");
                            return TypeInfo::makeAny();
                        }
                        break;
                    default:
                        break;
                }

                return inferredType;
            }

            long long value = std::stoll(core);
            const auto checkRange = [&](long long minValue, long long maxValue) {
                return value >= minValue && value <= maxValue;
            };

            switch (inferredType->kind) {
                case TypeKind::I8:
                    if (!checkRange(std::numeric_limits<int8_t>::min(),
                                    std::numeric_limits<int8_t>::max())) {
                        addError(token.line(),
                                 "Type error: integer literal '" + literal +
                                     "' is out of range for type '" +
                                     inferredType->toString() + "'.");
                        return TypeInfo::makeAny();
                    }
                    break;
                case TypeKind::I16:
                    if (!checkRange(std::numeric_limits<int16_t>::min(),
                                    std::numeric_limits<int16_t>::max())) {
                        addError(token.line(),
                                 "Type error: integer literal '" + literal +
                                     "' is out of range for type '" +
                                     inferredType->toString() + "'.");
                        return TypeInfo::makeAny();
                    }
                    break;
                case TypeKind::I32:
                    if (!checkRange(std::numeric_limits<int32_t>::min(),
                                    std::numeric_limits<int32_t>::max())) {
                        addError(token.line(),
                                 "Type error: integer literal '" + literal +
                                     "' is out of range for type '" +
                                     inferredType->toString() + "'.");
                        return TypeInfo::makeAny();
                    }
                    break;
                default:
                    break;
            }

            return inferredType;
        } catch (...) {
            addError(token.line(),
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
            case TokenType::TYPE_VOID:
                return TypeInfo::makeVoid();
            case TokenType::TYPE_NULL_KW:
                return TypeInfo::makeNull();
            case TokenType::IDENTIFIER: {
                const std::string name = tokenText(token);
                auto aliasIt = m_typeAliases.find(name);
                if (aliasIt != m_typeAliases.end()) {
                    return aliasIt->second;
                }
                if (m_classNames.find(name) != m_classNames.end()) {
                    return TypeInfo::makeClass(name);
                }
                return nullptr;
            }
            default:
                return nullptr;
        }
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
                        addError(typeExpr.node.line,
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
                        addError(typeExpr.node.line,
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
                        addError(typeExpr.node.line,
                                 "Type error: 'void' is not valid as a Dict "
                                 "key type.");
                        return nullptr;
                    }
                    if (valueType->isVoid()) {
                        addError(typeExpr.node.line,
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
                        addError(typeExpr.node.line,
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
                    addError(typeExpr.node.line,
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

            auto classIt = m_metadata.classFieldTypes.find(current);
            if (classIt != m_metadata.classFieldTypes.end()) {
                auto fieldIt = classIt->second.find(fieldName);
                if (fieldIt != classIt->second.end()) {
                    return fieldIt->second;
                }
            }

            auto superIt = m_metadata.superclassOf.find(current);
            if (superIt == m_metadata.superclassOf.end()) {
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

            auto classIt = m_metadata.classMethodSignatures.find(current);
            if (classIt != m_metadata.classMethodSignatures.end()) {
                auto methodIt = classIt->second.find(methodName);
                if (methodIt != classIt->second.end()) {
                    return methodIt->second;
                }
            }

            auto superIt = m_metadata.superclassOf.find(current);
            if (superIt == m_metadata.superclassOf.end()) {
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

    TypeRef requireTypeExpr(const AstTypeExpr* typeExpr, size_t line,
                            const std::string& message) {
        if (!typeExpr) {
            addError(line, message);
            return nullptr;
        }

        TypeRef resolved = resolveTypeExpr(*typeExpr);
        if (!resolved) {
            addError(line, message);
        }
        return resolved;
    }

    ResolvedCallableSignature resolveCallableSignature(
        const std::string& functionName, const std::vector<AstParameter>& params,
        const AstTypeExpr* returnTypeExpr, const TypeRef& expectedSignature,
        bool isMethod, bool isClosure, bool requireDeclaredReturnType,
        size_t fallbackLine) {
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
                    addError(param.name.line(),
                             "Type error: parameter '" + paramName +
                                 "' must have a type annotation.");
                    paramType = TypeInfo::makeAny();
                }

                if (!out.omittedParameterTypeAnnotation) {
                    out.omittedParameterTypeAnnotation = true;
                    out.firstOmittedParameterLine = param.name.line();
                }
            } else {
                paramType = resolveTypeExpr(*param.type);
                if (!paramType) {
                    addError(param.type->node.line,
                             "Type error: expected parameter type annotation.");
                    paramType = TypeInfo::makeAny();
                }
            }

            if (paramType && paramType->isVoid()) {
                addError(param.name.line(),
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
            addError(fallbackLine,
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
                    addError(fallbackLine,
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
                addError(fallbackLine,
                         "Type error: function '" + functionName +
                             "' must declare a return type.");
            }
        }

        if (!out.returnType) {
            out.returnType = TypeInfo::makeAny();
        }

        return out;
    }

    void predeclareClassMetadata(const AstModule& module) {
        for (const auto& item : module.items) {
            if (!item) {
                continue;
            }

            const auto* classDecl = std::get_if<AstClassDecl>(&item->value);
            if (!classDecl) {
                continue;
            }

            const std::string className = tokenText(classDecl->name);
            if (classDecl->superclass.has_value()) {
                const std::string superclassName =
                    tokenText(*classDecl->superclass);
                if (superclassName == className) {
                    addError(classDecl->superclass->line(),
                             "Type error: a type cannot inherit from itself.");
                }
                if (m_classNames.find(superclassName) == m_classNames.end()) {
                    addError(classDecl->superclass->line(),
                             "Type error: unknown superclass '" +
                                 superclassName + "'.");
                }
                m_metadata.superclassOf[className] = superclassName;
            }

            for (const auto& field : classDecl->fields) {
                TypeRef fieldType = requireTypeExpr(
                    field.type.get(), field.node.line,
                    "Type error: expected field type after member name.");
                if (!fieldType) {
                    continue;
                }
                if (fieldType->isVoid()) {
                    addError(field.name.line(),
                             "Type error: struct field '" +
                                 tokenText(field.name) +
                                 "' cannot have type 'void'.");
                }
                m_metadata.classFieldTypes[className][tokenText(field.name)] =
                    fieldType;
                recordNodeType(field.node, fieldType);
            }

            for (const auto& method : classDecl->methods) {
                TypeRef placeholder =
                    TypeInfo::makeFunction({}, TypeInfo::makeAny());
                ResolvedCallableSignature signature = resolveCallableSignature(
                    tokenText(method.name), method.params, method.returnType.get(),
                    placeholder, true, false, true,
                    method.body ? method.body->node.line : method.node.line);
                TypeRef methodType =
                    TypeInfo::makeFunction(signature.paramTypes, signature.returnType);
                m_metadata.classMethodSignatures[className][tokenText(method.name)] =
                    methodType;
                recordNodeType(method.node, methodType);
                for (int op : method.annotatedOperators) {
                    m_classOperatorMethods[className][op] =
                        tokenText(method.name);
                }
            }
        }
    }

    bool isImportExpr(const AstExpr& expr) const {
        return std::holds_alternative<AstImportExpr>(expr.value);
    }

    ExprInfo analyzeExpr(const AstExpr& expr,
                         const TypeRef& expectedFunctionLiteralType = nullptr) {
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
                    const SymbolInfo* symbol = resolveSymbolInfo(name);
                    TypeRef type = symbol ? symbol->type : resolveSymbol(name);
                    const bool isClass =
                        m_classNames.find(name) != m_classNames.end();
                    if (!type && isClass) {
                        type = TypeInfo::makeClass(name);
                    }
                    result = ExprInfo{type ? type : TypeInfo::makeAny(), !isClass,
                                      isClass, name, value.name.line(),
                                      symbol ? symbol->isConst : false};
                } else if constexpr (std::is_same_v<T, AstGroupingExpr>) {
                    ExprInfo inner = analyzeExpr(*value.expression);
                    result = ExprInfo{inner.type, false, false, "", expr.node.line};
                } else if constexpr (std::is_same_v<T, AstUnaryExpr>) {
                    ExprInfo operand = analyzeExpr(*value.operand);
                    if (value.op.type() == TokenType::BANG) {
                        if (!(operand.type->kind == TypeKind::BOOL ||
                              operand.type->isAny())) {
                            addError(value.op.line(),
                                     "Type error: unary '!' expects a bool "
                                     "operand.");
                        }
                        result = ExprInfo{TypeInfo::makeBool(), false, false, "",
                                          value.op.line()};
                    } else if (value.op.type() == TokenType::MINUS) {
                        if (!(operand.type->isNumeric() || operand.type->isAny())) {
                            addError(value.op.line(),
                                     "Type error: unary '-' expects a numeric "
                                     "operand.");
                        }
                        result = ExprInfo{operand.type, false, false, "",
                                          value.op.line()};
                    } else {
                        if (!(operand.type->isInteger() || operand.type->isAny())) {
                            addError(value.op.line(),
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
                            addError(value.op.line(),
                                     "Type error: cannot assign to const "
                                     "variable '" +
                                         operand.name + "'.");
                        }
                        if (!(operand.type->isNumeric() || operand.type->isAny())) {
                            addError(value.op.line(),
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
                                addError(value.op.line(),
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
                                if ((!targetType || targetType->isAny()) &&
                                    !operand.name.empty()) {
                                    targetType = resolveSymbol(operand.name);
                                }
                                if (!targetType) {
                                    addError(value.op.line(),
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
                                        addError(value.op.line(),
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
                    const size_t line = value.op.line();

                    if (op == TokenType::LOGICAL_OR ||
                        op == TokenType::LOGICAL_AND) {
                        if (!(lhs.type->kind == TypeKind::BOOL ||
                              lhs.type->isAny()) ||
                            !(rhs.type->kind == TypeKind::BOOL ||
                              rhs.type->isAny())) {
                            addError(
                                line,
                                std::string("Type error: logical '") +
                                    (op == TokenType::LOGICAL_OR ? "||" : "&&") +
                                    "' expects bool operands.");
                        }
                        result = ExprInfo{TypeInfo::makeBool(), false, false, "",
                                          line};
                    } else if (isEqualityOperator(op)) {
                        if (TypeRef overloaded =
                                lookupOperatorResultType(lhs.type, op, rhs.type,
                                                         line)) {
                            result = ExprInfo{overloaded, false, false, "", line};
                        } else {
                            if (!(isAssignableType(lhs.type, rhs.type) ||
                                  isAssignableType(rhs.type, lhs.type))) {
                                addError(
                                    line,
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
                                                         line)) {
                            result = ExprInfo{overloaded, false, false, "", line};
                        } else {
                            bool lhsOk = lhs.type->isAny() || lhs.type->isNumeric();
                            bool rhsOk = rhs.type->isAny() || rhs.type->isNumeric();
                            if (!lhsOk || !rhsOk) {
                                addError(line,
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
                                addError(line,
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
                            addError(line,
                                     "Type error: shift operators require "
                                     "integer operands.");
                        }
                        result = ExprInfo{lhs.type, false, false, "", line};
                    } else {
                        if (TypeRef overloaded =
                                lookupOperatorResultType(lhs.type, op, rhs.type,
                                                         line)) {
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
                                addError(line,
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
                    ExprInfo rhs = analyzeExpr(*value.value);
                    const TokenType assignmentType = value.op.type();
                    const size_t line = value.op.line();

                    if (!lhs.isAssignable) {
                        result = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                          line};
                    } else if (lhs.isConstSymbol && !lhs.name.empty()) {
                        addError(line,
                                 "Type error: cannot assign to const variable '" +
                                     lhs.name + "'.");
                        result = ExprInfo{lhs.type ? lhs.type
                                                   : TypeInfo::makeAny(),
                                          false, false, lhs.name, line, true};
                    } else {
                        TypeRef targetType = lhs.type;
                        if ((!targetType || targetType->isAny()) &&
                            !lhs.name.empty()) {
                            targetType = resolveSymbol(lhs.name);
                        }
                        if (!targetType) {
                            addError(line,
                                     "Type error: unknown assignment target '" +
                                         lhs.name + "'.");
                            result = ExprInfo{TypeInfo::makeAny(), false, false,
                                              lhs.name, line};
                        } else if (assignmentType == TokenType::EQUAL) {
                            if (!isAssignableType(rhs.type, targetType)) {
                                addError(line,
                                         "Type error: cannot assign '" +
                                             rhs.type->toString() +
                                             "' to variable '" + lhs.name +
                                             "' of type '" +
                                             targetType->toString() + "'.");
                            }
                            result = ExprInfo{targetType, false, false, lhs.name,
                                              line};
                        } else if (targetType->isAny() || rhs.type->isAny()) {
                            result = ExprInfo{targetType, false, false, lhs.name,
                                              line};
                        } else if (isArithmeticCompoundAssignment(
                                       assignmentType)) {
                            if (!(targetType->isNumeric() &&
                                  rhs.type->isNumeric())) {
                                addError(line,
                                         "Type error: compound assignment "
                                         "requires numeric operands.");
                                result = ExprInfo{TypeInfo::makeAny(), false,
                                                  false, lhs.name, line};
                            } else {
                                TypeRef promoted =
                                    numericPromotion(targetType, rhs.type);
                                if (!promoted ||
                                    !isAssignableType(promoted, targetType)) {
                                    addError(line,
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
                                addError(line,
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
                                addError(line,
                                         "Type error: bitwise operators require "
                                         "integer operands.");
                                result = ExprInfo{TypeInfo::makeAny(), false,
                                                  false, lhs.name, line};
                            } else {
                                TypeRef resultType =
                                    bitwiseIntegerResultType(targetType, rhs.type);
                                if (!resultType ||
                                    !isAssignableType(resultType, targetType)) {
                                    addError(line,
                                             "Type error: result of compound "
                                             "assignment is not assignable to '" +
                                                 targetType->toString() + "'.");
                                }
                                result = ExprInfo{targetType, false, false,
                                                  lhs.name, line};
                            }
                        } else {
                            result = ExprInfo{targetType, false, false, lhs.name,
                                              line};
                        }
                    }
                } else if constexpr (std::is_same_v<T, AstCallExpr>) {
                    ExprInfo callee = analyzeExpr(*value.callee);
                    std::vector<ExprInfo> args;
                    args.reserve(value.arguments.size());
                    for (const auto& arg : value.arguments) {
                        args.push_back(analyzeExpr(*arg));
                    }

                    if (callee.type && callee.type->isOptional()) {
                        addError(expr.node.line,
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
                        addError(expr.node.line,
                                 "Type error: attempted to call a non-function "
                                 "value.");
                        result = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                          expr.node.line};
                    } else {
                        const auto& params = callee.type->paramTypes;
                        if (!params.empty() && params.size() != args.size()) {
                            addError(expr.node.line,
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
                                                          : expr.node.line,
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
                        addError(expr.node.line,
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
                                addError(value.member.line(),
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
                            receiver.type, memberName, value.member.line());
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
                            addError(index.line ? index.line : expr.node.line,
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
                            addError(index.line ? index.line : expr.node.line,
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
                            addError(index.line ? index.line : expr.node.line,
                                     "Type error: set lookup expects '" +
                                         elementType->toString() + "', got '" +
                                         index.type->toString() + "'.");
                        }
                        result = ExprInfo{TypeInfo::makeBool(), false, false, "",
                                          expr.node.line};
                    } else {
                        addError(expr.node.line,
                                 "Type error: indexing is only valid on Array, "
                                 "Dict, or Set.");
                        result = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                          expr.node.line};
                    }
                } else if constexpr (std::is_same_v<T, AstCastExpr>) {
                    ExprInfo inner = analyzeExpr(*value.expression);
                    TypeRef target = requireTypeExpr(
                        value.targetType.get(), expr.node.line,
                        "Type error: expected type after 'as'.");
                    if (!target) {
                        result = ExprInfo{TypeInfo::makeAny(), false, false, "",
                                          expr.node.line};
                    } else {
                        if (!isValidExplicitCast(inner.type, target)) {
                            addError(expr.node.line,
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
                        expectedFunctionLiteralType, false, true,
                        !value.expressionBody, expr.node.line);

                    beginScope();
                    m_functionContexts.push_back(FunctionCtx{signature.returnType});
                    for (size_t index = 0; index < signature.params.size();
                         ++index) {
                        defineSymbol(signature.params[index].first,
                                     signature.params[index].second);
                    }

                    if (value.expressionBody) {
                        if (signature.omittedParameterTypeAnnotation &&
                            signature.firstOmittedParameterLine != 0) {
                            addError(signature.firstOmittedParameterLine,
                                     "Type error: expression-bodied lambdas "
                                     "require explicit parameter types.");
                        }

                        ExprInfo body = analyzeExpr(*value.expressionBody);
                        m_functionContexts.pop_back();
                        endScope();
                        result = ExprInfo{
                            TypeInfo::makeFunction(signature.paramTypes, body.type),
                            false, false, "", body.line};
                    } else {
                        if (value.blockBody) {
                            analyzeFunctionBody(*value.blockBody);
                        }
                        m_functionContexts.pop_back();
                        endScope();
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
                        auto superIt = m_metadata.superclassOf.find(className);
                        if (superIt == m_metadata.superclassOf.end()) {
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
                            addError(item.line ? item.line : expr.node.line,
                                     "Type error: array literal elements must "
                                     "have a consistent type.");
                        }
                        elementType = merged;
                    }

                    result = ExprInfo{
                        TypeInfo::makeArray(elementType ? elementType
                                                        : TypeInfo::makeAny()),
                        false, false, "", expr.node.line};
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

                    result = ExprInfo{
                        TypeInfo::makeDict(keyType ? keyType : TypeInfo::makeAny(),
                                           valueType ? valueType
                                                     : TypeInfo::makeAny()),
                        false, false, "", expr.node.line};
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
                addError(stmt.name.line(),
                         "Type error: variables require an explicit type unless "
                         "initialized from '@import(...)'.");
            }
        } else {
            declaredType = requireTypeExpr(
                stmt.declaredType.get(), stmtNode.line,
                "Type error: expected type after variable name.");
            if (!declaredType) {
                declaredType = TypeInfo::makeAny();
            }
            if (declaredType->isVoid()) {
                addError(stmtNode.line,
                         "Type error: variables cannot have type 'void'.");
            }
        }

        TypeRef expectedFunctionType = nullptr;
        if (!omittedType && declaredType && declaredType->kind == TypeKind::FUNCTION &&
            stmt.initializer &&
            std::holds_alternative<AstFunctionExpr>(stmt.initializer->value)) {
            expectedFunctionType = declaredType;
        }

        ExprInfo initializer = stmt.initializer
                                   ? analyzeExpr(*stmt.initializer,
                                                 expectedFunctionType)
                                   : ExprInfo{};

        if (!omittedType && !isAssignableType(initializer.type, declaredType)) {
            addError(stmt.name.line(), "Type error: cannot assign '" +
                                           initializer.type->toString() +
                                           "' to variable '" + name +
                                           "' of type '" +
                                           declaredType->toString() + "'.");
        }

        TypeRef finalType = omittedType ? initializer.type : declaredType;
        defineSymbol(name, finalType, stmt.isConst);
        recordDeclarationType(stmtNode, name, finalType, stmt.name.line(),
                              stmt.isConst);
    }

    void analyzeDestructuredImport(const AstStmt& stmtNode,
                                   const AstDestructuredImportStmt& stmt) {
        if (stmt.initializer) {
            analyzeExpr(*stmt.initializer);
        }

        for (const auto& binding : stmt.bindings) {
            const std::string localName =
                binding.localName.has_value() ? tokenText(*binding.localName)
                                              : tokenText(binding.exportedName);
            const size_t line = binding.localName.has_value()
                                    ? binding.localName->line()
                                    : binding.exportedName.line();
            defineSymbol(localName, TypeInfo::makeAny(), true);
            recordDeclarationType(binding.node, localName, TypeInfo::makeAny(),
                                  line, true);
            recordNodeConstness(binding.node, true);
        }

        recordNodeConstness(stmtNode.node, true);
    }

    void analyzeReturnStmt(const AstReturnStmt& stmt, size_t line) {
        if (m_functionContexts.empty()) {
            addError(line, "Type error: cannot return from top-level code.");
        }

        if (!stmt.value) {
            if (!m_functionContexts.empty()) {
                TypeRef expected = m_functionContexts.back().returnType;
                if (expected && !expected->isVoid()) {
                    addError(line, "Type error: function expects return type '" +
                                       expected->toString() + "'.");
                }
            }
            return;
        }

        ExprInfo value = analyzeExpr(*stmt.value);
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

    void analyzeStmt(const AstStmt& stmt) {
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, AstBlockStmt>) {
                    beginScope();
                    for (const auto& item : value.items) {
                        if (item) {
                            analyzeItem(*item);
                        }
                    }
                    endScope();
                } else if constexpr (std::is_same_v<T, AstExprStmt>) {
                    analyzeExpr(*value.expression);
                } else if constexpr (std::is_same_v<T, AstPrintStmt>) {
                    analyzeExpr(*value.expression);
                } else if constexpr (std::is_same_v<T, AstReturnStmt>) {
                    analyzeReturnStmt(value, stmt.node.line);
                } else if constexpr (std::is_same_v<T, AstIfStmt>) {
                    ExprInfo cond = analyzeExpr(*value.condition);
                    if (!(cond.type->kind == TypeKind::BOOL ||
                          cond.type->isAny())) {
                        addError(cond.line ? cond.line : stmt.node.line,
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
                        addError(cond.line ? cond.line : stmt.node.line,
                                 "Type error: while condition must be bool.");
                    }
                    analyzeStmt(*value.body);
                } else if constexpr (std::is_same_v<T, AstVarDeclStmt>) {
                    analyzeVarDecl(stmt.node, value);
                } else if constexpr (std::is_same_v<T,
                                                    AstDestructuredImportStmt>) {
                    analyzeDestructuredImport(stmt, value);
                } else if constexpr (std::is_same_v<T, AstForStmt>) {
                    beginScope();
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
                            addError(cond.line ? cond.line : stmt.node.line,
                                     "Type error: for condition must be bool.");
                        }
                    }

                    if (value.increment) {
                        analyzeExpr(*value.increment);
                    }

                    analyzeStmt(*value.body);
                    endScope();
                } else if constexpr (std::is_same_v<T, AstForEachStmt>) {
                    beginScope();
                    TypeRef declaredType = requireTypeExpr(
                        value.declaredType.get(), stmt.node.line,
                        "Type error: expected type after loop variable name.");
                    if (!declaredType) {
                        declaredType = TypeInfo::makeAny();
                    }
                    if (declaredType->isVoid()) {
                        addError(stmt.node.line,
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
                        addError(iterable.line ? iterable.line : stmt.node.line,
                                 "Type error: foreach expects Array<T>, Dict<K, "
                                 "V>, or Set<T>.");
                    }

                    if (!isAssignableType(inferredLoopType, declaredType)) {
                        addError(value.name.line(),
                                 "Type error: cannot assign '" +
                                     inferredLoopType->toString() +
                                     "' to variable '" + tokenText(value.name) +
                                     "' of type '" +
                                     declaredType->toString() + "'.");
                    }

                    defineSymbol(tokenText(value.name), declaredType, value.isConst);
                    recordDeclarationType(stmt.node, tokenText(value.name),
                                          declaredType, value.name.line(),
                                          value.isConst);
                    analyzeStmt(*value.body);
                    endScope();
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

        defineSymbol(functionName, functionType);
        recordDeclarationType(functionDecl.node, functionName, functionType,
                              functionDecl.name.line());

        ResolvedCallableSignature signature = resolveCallableSignature(
            functionName, functionDecl.params, functionDecl.returnType.get(),
            functionType, false, false, true,
            functionDecl.body ? functionDecl.body->node.line
                              : functionDecl.node.line);

        beginScope();
        m_functionContexts.push_back(FunctionCtx{signature.returnType});
        for (const auto& param : signature.params) {
            defineSymbol(param.first, param.second);
        }

        if (functionDecl.body) {
            analyzeFunctionBody(*functionDecl.body);
        }

        m_functionContexts.pop_back();
        endScope();
    }

    void analyzeClassDecl(const AstClassDecl& classDecl) {
        const std::string className = tokenText(classDecl.name);
        TypeRef classType = TypeInfo::makeClass(className);
        defineSymbol(className, classType);
        recordDeclarationType(classDecl.node, className, classType,
                              classDecl.name.line());

        m_classContexts.push_back(ClassCtx{className});
        for (const auto& method : classDecl.methods) {
            auto classIt = m_metadata.classMethodSignatures.find(className);
            TypeRef methodType = TypeInfo::makeFunction({}, TypeInfo::makeAny());
            if (classIt != m_metadata.classMethodSignatures.end()) {
                auto methodIt = classIt->second.find(tokenText(method.name));
                if (methodIt != classIt->second.end()) {
                    methodType = methodIt->second;
                }
            }

            beginScope();
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
                defineSymbol(tokenText(method.params[index].name), paramType);
            }

            if (method.body) {
                analyzeFunctionBody(*method.body);
            }

            m_functionContexts.pop_back();
            endScope();
        }
        m_classContexts.pop_back();
    }

    void analyzeTypeAliasDecl(const AstTypeAliasDecl& aliasDecl) {
        TypeRef aliasedType = requireTypeExpr(
            aliasDecl.aliasedType.get(), aliasDecl.node.line,
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
    AstSemanticAnalyzerImpl(
        const std::unordered_set<std::string>& classNames,
        const std::unordered_map<std::string, TypeRef>& typeAliases,
        const std::unordered_map<std::string, TypeRef>& functionSignatures,
        std::vector<TypeError>& errors, AstSemanticModel* model)
        : m_classNames(classNames),
          m_typeAliases(typeAliases),
          m_functionSignatures(functionSignatures),
          m_errors(errors),
          m_model(model) {
        m_scopes.emplace_back();

        for (const auto& entry : functionSignatures) {
            m_scopes.front()[entry.first] = SymbolInfo{entry.second, false};
        }
    }

    void run(const AstModule& module) {
        predeclareClassMetadata(module);

        for (const auto& item : module.items) {
            if (item) {
                analyzeItem(*item);
            }
        }

        for (const auto& symbolName : m_declaredGlobalSymbols) {
            auto it = m_scopes.front().find(symbolName);
            if (it != m_scopes.front().end()) {
                m_metadata.topLevelSymbolTypes[symbolName] = it->second.type;
            }
        }

        if (m_model) {
            m_model->classOperatorMethods = m_classOperatorMethods;
            m_model->metadata = m_metadata;
        }
    }
};

}  // namespace

bool analyzeAstSemantics(
    const AstModule& module,
    const std::unordered_set<std::string>& classNames,
    const std::unordered_map<std::string, TypeRef>& typeAliases,
    const std::unordered_map<std::string, TypeRef>& functionSignatures,
    std::vector<TypeError>& out, AstSemanticModel* outModel) {
    AstSemanticAnalyzerImpl analyzer(classNames, typeAliases, functionSignatures,
                                     out, outModel);
    analyzer.run(module);
    return out.empty();
}
