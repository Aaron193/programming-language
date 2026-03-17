#include "AstSymbolCollector.hpp"

#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "Ast.hpp"
#include "AstParser.hpp"
#include "NativePackage.hpp"
#include "SyntaxRules.hpp"

namespace {

TypeRef tokenToType(const Token& token,
                    const std::unordered_set<std::string>& classNames,
                    const std::unordered_map<std::string, TypeRef>& typeAliases) {
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
            std::string name(token.start(), token.length());
            auto aliasIt = typeAliases.find(name);
            if (aliasIt != typeAliases.end()) {
                return aliasIt->second;
            }
            if (classNames.find(name) != classNames.end()) {
                return TypeInfo::makeClass(name);
            }
            return nullptr;
        }
        default:
            return nullptr;
    }
}

TypeRef resolveTypeExpr(const AstTypeExpr& typeExpr,
                        const std::unordered_set<std::string>& classNames,
                        const std::unordered_map<std::string, TypeRef>& typeAliases) {
    switch (typeExpr.kind) {
        case AstTypeKind::NAMED:
            return tokenToType(typeExpr.token, classNames, typeAliases);
        case AstTypeKind::FUNCTION: {
            std::vector<TypeRef> params;
            params.reserve(typeExpr.paramTypes.size());
            for (const auto& paramType : typeExpr.paramTypes) {
                TypeRef resolved = resolveTypeExpr(*paramType, classNames, typeAliases);
                if (!resolved) {
                    return nullptr;
                }
                params.push_back(resolved);
            }
            if (!typeExpr.returnType) {
                return nullptr;
            }
            TypeRef returnType =
                resolveTypeExpr(*typeExpr.returnType, classNames, typeAliases);
            if (!returnType) {
                return nullptr;
            }
            return TypeInfo::makeFunction(std::move(params), returnType);
        }
        case AstTypeKind::ARRAY:
            if (!typeExpr.elementType) {
                return nullptr;
            }
            if (TypeRef elementType = resolveTypeExpr(*typeExpr.elementType,
                                                      classNames, typeAliases)) {
                return TypeInfo::makeArray(elementType);
            }
            return nullptr;
        case AstTypeKind::DICT:
            if (!typeExpr.keyType || !typeExpr.valueType) {
                return nullptr;
            }
            {
                TypeRef keyType =
                    resolveTypeExpr(*typeExpr.keyType, classNames, typeAliases);
                TypeRef valueType = resolveTypeExpr(*typeExpr.valueType,
                                                   classNames, typeAliases);
                if (!keyType || !valueType) {
                    return nullptr;
                }
                return TypeInfo::makeDict(keyType, valueType);
            }
        case AstTypeKind::SET:
            if (!typeExpr.elementType) {
                return nullptr;
            }
            if (TypeRef elementType = resolveTypeExpr(*typeExpr.elementType,
                                                      classNames, typeAliases)) {
                return TypeInfo::makeSet(elementType);
            }
            return nullptr;
        case AstTypeKind::OPTIONAL:
            if (!typeExpr.innerType) {
                return nullptr;
            }
            if (TypeRef innerType = resolveTypeExpr(*typeExpr.innerType,
                                                    classNames, typeAliases)) {
                return TypeInfo::makeOptional(innerType);
            }
            return nullptr;
        case AstTypeKind::NATIVE_HANDLE:
            if (!isValidPackageIdPart(typeExpr.packageNamespace) ||
                !isValidPackageIdPart(typeExpr.packageName) ||
                !isValidHandleTypeName(typeExpr.nativeHandleTypeName)) {
                return nullptr;
            }
            return TypeInfo::makeNativeHandle(
                makePackageId(typeExpr.packageNamespace, typeExpr.packageName),
                typeExpr.nativeHandleTypeName);
    }

    return nullptr;
}

}  // namespace

bool collectSymbolsFromAst(
    std::string_view source, std::unordered_set<std::string>& outClassNames,
    std::unordered_map<std::string, TypeRef>& outFunctionSignatures,
    std::unordered_map<std::string, TypeRef>* outTypeAliases) {
    AstModule module;
    AstParser parser(source);
    if (!parser.parseModule(module)) {
        return false;
    }

    outClassNames.clear();
    outFunctionSignatures.clear();
    if (outTypeAliases != nullptr) {
        outTypeAliases->clear();
    }

    for (const AstDeclaration& declaration : module.declarations) {
        if (const auto* classDecl = std::get_if<AstClassDecl>(&declaration)) {
            outClassNames.emplace(std::string(classDecl->name.start(),
                                              classDecl->name.length()));
        }
    }

    if (outTypeAliases != nullptr) {
        for (const AstDeclaration& declaration : module.declarations) {
            const auto* aliasDecl = std::get_if<AstTypeAliasDecl>(&declaration);
            if (aliasDecl == nullptr || !aliasDecl->aliasedType) {
                continue;
            }

            TypeRef resolved =
                resolveTypeExpr(*aliasDecl->aliasedType, outClassNames, *outTypeAliases);
            if (resolved) {
                (*outTypeAliases)[std::string(aliasDecl->name.start(),
                                              aliasDecl->name.length())] =
                    resolved;
            }
        }
    }

    const std::unordered_map<std::string, TypeRef> emptyAliases;
    const auto& aliases =
        outTypeAliases != nullptr ? *outTypeAliases : emptyAliases;

    for (const AstDeclaration& declaration : module.declarations) {
        const auto* functionDecl = std::get_if<AstFunctionDecl>(&declaration);
        if (functionDecl == nullptr) {
            continue;
        }

        std::vector<TypeRef> params;
        params.reserve(functionDecl->params.size());
        for (const auto& param : functionDecl->params) {
            TypeRef resolved =
                param.type ? resolveTypeExpr(*param.type, outClassNames, aliases)
                           : nullptr;
            params.push_back(resolved ? resolved : TypeInfo::makeAny());
        }

        TypeRef returnType =
            functionDecl->returnType
                ? resolveTypeExpr(*functionDecl->returnType, outClassNames, aliases)
                : TypeInfo::makeAny();
        if (!returnType) {
            returnType = TypeInfo::makeAny();
        }

        outFunctionSignatures[std::string(functionDecl->name.start(),
                                          functionDecl->name.length())] =
            TypeInfo::makeFunction(std::move(params), returnType);
    }

    return true;
}
