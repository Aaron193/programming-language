#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct ClassObject;

enum class TypeKind : uint8_t {
    I8,
    I16,
    I32,
    I64,
    U8,
    U16,
    U32,
    U64,
    USIZE,
    F32,
    F64,
    BOOL,
    STR,
    NULL_TYPE,
    VOID,
    ANY,
    CLASS,
    FUNCTION,
    ARRAY,
    DICT,
    SET,
    OPTIONAL,
};

struct TypeInfo;
using TypeRef = std::shared_ptr<TypeInfo>;

struct TypeInfo {
    TypeKind kind;

    std::string className;
    ClassObject* classPtr = nullptr;

    std::vector<TypeRef> paramTypes;
    TypeRef returnType;

    TypeRef elementType;
    TypeRef keyType;
    TypeRef valueType;
    TypeRef innerType;

    explicit TypeInfo(TypeKind kind) : kind(kind) {}

    static TypeRef makeI8();
    static TypeRef makeI16();
    static TypeRef makeI32();
    static TypeRef makeI64();
    static TypeRef makeU8();
    static TypeRef makeU16();
    static TypeRef makeU32();
    static TypeRef makeU64();
    static TypeRef makeUSize();
    static TypeRef makeF32();
    static TypeRef makeF64();
    static TypeRef makeBool();
    static TypeRef makeStr();
    static TypeRef makeAny();
    static TypeRef makeVoid();
    static TypeRef makeNull();
    static TypeRef makeClass(const std::string& name);
    static TypeRef makeFunction(std::vector<TypeRef> params, TypeRef ret);
    static TypeRef makeArray(TypeRef element);
    static TypeRef makeDict(TypeRef key, TypeRef value);
    static TypeRef makeSet(TypeRef element);
    static TypeRef makeOptional(TypeRef inner);

    bool isInteger() const;
    bool isFloat() const;
    bool isNumeric() const;
    bool isSigned() const;
    bool isUnsigned() const;
    bool isAny() const { return kind == TypeKind::ANY; }
    bool isVoid() const { return kind == TypeKind::VOID; }
    bool isClass() const { return kind == TypeKind::CLASS; }
    bool isOptional() const { return kind == TypeKind::OPTIONAL; }

    std::string toString() const;
    int bitWidth() const;
};

bool isAssignable(const TypeRef& from, const TypeRef& to);
TypeRef numericPromotion(const TypeRef& lhs, const TypeRef& rhs);
