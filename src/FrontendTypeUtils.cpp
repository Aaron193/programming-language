#include "FrontendTypeUtils.hpp"

#include "AstBinder.hpp"
#include "NativePackage.hpp"
#include "PackageRegistry.hpp"

TypeRef frontendTokenToType(const Token& token,
                            const FrontendTypeContext& context) {
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
            const std::string name = tokenLexeme(token);
            auto aliasIt = context.typeAliases.find(name);
            if (aliasIt != context.typeAliases.end()) {
                return aliasIt->second;
            }
            if (context.classNames.find(name) != context.classNames.end()) {
                return TypeInfo::makeClass(name);
            }
            return nullptr;
        }
        default:
            return nullptr;
    }
}

TypeRef frontendResolveTypeExpr(const AstTypeExpr& typeExpr,
                                const FrontendTypeContext& context) {
    switch (typeExpr.kind) {
        case AstTypeKind::NAMED:
            if (!typeExpr.qualifier.empty()) {
                if (context.importedModulesByName == nullptr) {
                    return nullptr;
                }

                const auto moduleIt = context.importedModulesByName->find(
                    typeExpr.qualifier);
                if (moduleIt == context.importedModulesByName->end() ||
                    moduleIt->second == nullptr) {
                    return nullptr;
                }

                const auto exportIt = moduleIt->second->typeExports.find(
                    tokenLexeme(typeExpr.token));
                return exportIt == moduleIt->second->typeExports.end()
                           ? nullptr
                           : exportIt->second.type;
            }
            return frontendTokenToType(typeExpr.token, context);
        case AstTypeKind::FUNCTION: {
            std::vector<TypeRef> params;
            params.reserve(typeExpr.paramTypes.size());
            for (const auto& paramType : typeExpr.paramTypes) {
                TypeRef resolved = frontendResolveTypeExpr(*paramType, context);
                if (!resolved) {
                    return nullptr;
                }
                params.push_back(resolved);
            }
            if (!typeExpr.returnType) {
                return nullptr;
            }
            TypeRef returnType =
                frontendResolveTypeExpr(*typeExpr.returnType, context);
            if (!returnType) {
                return nullptr;
            }
            return TypeInfo::makeFunction(std::move(params), returnType);
        }
        case AstTypeKind::ARRAY:
            if (!typeExpr.elementType) {
                return nullptr;
            }
            if (TypeRef elementType =
                    frontendResolveTypeExpr(*typeExpr.elementType, context)) {
                return TypeInfo::makeArray(elementType);
            }
            return nullptr;
        case AstTypeKind::DICT:
            if (!typeExpr.keyType || !typeExpr.valueType) {
                return nullptr;
            }
            {
                TypeRef keyType =
                    frontendResolveTypeExpr(*typeExpr.keyType, context);
                TypeRef valueType =
                    frontendResolveTypeExpr(*typeExpr.valueType, context);
                if (!keyType || !valueType) {
                    return nullptr;
                }
                return TypeInfo::makeDict(keyType, valueType);
            }
        case AstTypeKind::SET:
            if (!typeExpr.elementType) {
                return nullptr;
            }
            if (TypeRef elementType =
                    frontendResolveTypeExpr(*typeExpr.elementType, context)) {
                return TypeInfo::makeSet(elementType);
            }
            return nullptr;
        case AstTypeKind::OPTIONAL:
            if (!typeExpr.innerType) {
                return nullptr;
            }
            if (TypeRef innerType =
                    frontendResolveTypeExpr(*typeExpr.innerType, context)) {
                return TypeInfo::makeOptional(innerType);
            }
            return nullptr;
        case AstTypeKind::NATIVE_HANDLE:
            if (!isValidHandleTypeName(typeExpr.nativeHandleTypeName)) {
                return nullptr;
            }
            std::string packageId;
            std::string packageNamespace;
            std::string packageName;
            std::string error;
            if (!resolveHandlePackageId(context.sourcePath,
                                        typeExpr.packageNamespace,
                                        context.packageSearchPaths,
                                        packageId, packageNamespace,
                                        packageName, error)) {
                return nullptr;
            }
            return TypeInfo::makeNativeHandle(packageId,
                                              typeExpr.nativeHandleTypeName);
    }

    return nullptr;
}

std::string frontendTypeExprText(const AstTypeExpr& typeExpr) {
    switch (typeExpr.kind) {
        case AstTypeKind::NAMED:
            return typeExpr.qualifier.empty()
                       ? tokenLexeme(typeExpr.token)
                       : typeExpr.qualifier + "." + tokenLexeme(typeExpr.token);
        case AstTypeKind::FUNCTION: {
            std::string text = "fn(";
            for (size_t index = 0; index < typeExpr.paramTypes.size(); ++index) {
                if (index != 0) {
                    text += ", ";
                }
                text += frontendTypeExprText(*typeExpr.paramTypes[index]);
            }
            text += ") ";
            text += typeExpr.returnType ? frontendTypeExprText(*typeExpr.returnType)
                                        : std::string("any");
            return text;
        }
        case AstTypeKind::ARRAY:
            return tokenLexeme(typeExpr.token) + "<" +
                   frontendTypeExprText(*typeExpr.elementType) + ">";
        case AstTypeKind::DICT:
            return tokenLexeme(typeExpr.token) + "<" +
                   frontendTypeExprText(*typeExpr.keyType) + ", " +
                   frontendTypeExprText(*typeExpr.valueType) + ">";
        case AstTypeKind::SET:
            return tokenLexeme(typeExpr.token) + "<" +
                   frontendTypeExprText(*typeExpr.elementType) + ">";
        case AstTypeKind::OPTIONAL:
            return frontendTypeExprText(*typeExpr.innerType) + "?";
        case AstTypeKind::NATIVE_HANDLE:
            return tokenLexeme(typeExpr.token) + "<" + typeExpr.packageNamespace +
                   ":" + typeExpr.nativeHandleTypeName + ">";
    }

    return "any";
}
