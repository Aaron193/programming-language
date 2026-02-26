#include "TypeInfo.hpp"

#include <sstream>
#include <utility>

#include "Chunk.hpp"

namespace {
TypeRef makePrimitiveSingleton(TypeKind kind) {
    return std::make_shared<TypeInfo>(kind);
}

TypeRef widestSignedInt(int bitWidth) {
    if (bitWidth <= 8) return TypeInfo::makeI8();
    if (bitWidth <= 16) return TypeInfo::makeI16();
    if (bitWidth <= 32) return TypeInfo::makeI32();
    return TypeInfo::makeI64();
}

TypeRef widestUnsignedInt(int bitWidth) {
    if (bitWidth <= 8) return TypeInfo::makeU8();
    if (bitWidth <= 16) return TypeInfo::makeU16();
    if (bitWidth <= 32) return TypeInfo::makeU32();
    return TypeInfo::makeU64();
}

bool isClassSubtype(const ClassObject* derived, const ClassObject* base) {
    if (!derived || !base) {
        return false;
    }

    const ClassObject* current = derived;
    while (current != nullptr) {
        if (current == base) {
            return true;
        }
        current = current->superclass;
    }

    return false;
}
}  // namespace

TypeRef TypeInfo::makeI8() {
    static TypeRef instance = makePrimitiveSingleton(TypeKind::I8);
    return instance;
}

TypeRef TypeInfo::makeI16() {
    static TypeRef instance = makePrimitiveSingleton(TypeKind::I16);
    return instance;
}

TypeRef TypeInfo::makeI32() {
    static TypeRef instance = makePrimitiveSingleton(TypeKind::I32);
    return instance;
}

TypeRef TypeInfo::makeI64() {
    static TypeRef instance = makePrimitiveSingleton(TypeKind::I64);
    return instance;
}

TypeRef TypeInfo::makeU8() {
    static TypeRef instance = makePrimitiveSingleton(TypeKind::U8);
    return instance;
}

TypeRef TypeInfo::makeU16() {
    static TypeRef instance = makePrimitiveSingleton(TypeKind::U16);
    return instance;
}

TypeRef TypeInfo::makeU32() {
    static TypeRef instance = makePrimitiveSingleton(TypeKind::U32);
    return instance;
}

TypeRef TypeInfo::makeU64() {
    static TypeRef instance = makePrimitiveSingleton(TypeKind::U64);
    return instance;
}

TypeRef TypeInfo::makeUSize() {
    static TypeRef instance = makePrimitiveSingleton(TypeKind::USIZE);
    return instance;
}

TypeRef TypeInfo::makeF32() {
    static TypeRef instance = makePrimitiveSingleton(TypeKind::F32);
    return instance;
}

TypeRef TypeInfo::makeF64() {
    static TypeRef instance = makePrimitiveSingleton(TypeKind::F64);
    return instance;
}

TypeRef TypeInfo::makeBool() {
    static TypeRef instance = makePrimitiveSingleton(TypeKind::BOOL);
    return instance;
}

TypeRef TypeInfo::makeStr() {
    static TypeRef instance = makePrimitiveSingleton(TypeKind::STR);
    return instance;
}

TypeRef TypeInfo::makeAny() {
    static TypeRef instance = makePrimitiveSingleton(TypeKind::ANY);
    return instance;
}

TypeRef TypeInfo::makeVoid() {
    static TypeRef instance = makePrimitiveSingleton(TypeKind::VOID);
    return instance;
}

TypeRef TypeInfo::makeNull() {
    static TypeRef instance = makePrimitiveSingleton(TypeKind::NULL_TYPE);
    return instance;
}

TypeRef TypeInfo::makeClass(const std::string& name) {
    auto type = std::make_shared<TypeInfo>(TypeKind::CLASS);
    type->className = name;
    return type;
}

TypeRef TypeInfo::makeFunction(std::vector<TypeRef> params, TypeRef ret) {
    auto type = std::make_shared<TypeInfo>(TypeKind::FUNCTION);
    type->paramTypes = std::move(params);
    type->returnType = std::move(ret);
    return type;
}

TypeRef TypeInfo::makeArray(TypeRef element) {
    auto type = std::make_shared<TypeInfo>(TypeKind::ARRAY);
    type->elementType = std::move(element);
    return type;
}

TypeRef TypeInfo::makeDict(TypeRef key, TypeRef value) {
    auto type = std::make_shared<TypeInfo>(TypeKind::DICT);
    type->keyType = std::move(key);
    type->valueType = std::move(value);
    return type;
}

TypeRef TypeInfo::makeSet(TypeRef element) {
    auto type = std::make_shared<TypeInfo>(TypeKind::SET);
    type->elementType = std::move(element);
    return type;
}

bool TypeInfo::isInteger() const {
    switch (kind) {
        case TypeKind::I8:
        case TypeKind::I16:
        case TypeKind::I32:
        case TypeKind::I64:
        case TypeKind::U8:
        case TypeKind::U16:
        case TypeKind::U32:
        case TypeKind::U64:
        case TypeKind::USIZE:
            return true;
        default:
            return false;
    }
}

bool TypeInfo::isFloat() const {
    return kind == TypeKind::F32 || kind == TypeKind::F64;
}

bool TypeInfo::isNumeric() const { return isInteger() || isFloat(); }

bool TypeInfo::isSigned() const {
    return kind == TypeKind::I8 || kind == TypeKind::I16 ||
           kind == TypeKind::I32 || kind == TypeKind::I64;
}

bool TypeInfo::isUnsigned() const {
    return kind == TypeKind::U8 || kind == TypeKind::U16 ||
           kind == TypeKind::U32 || kind == TypeKind::U64 ||
           kind == TypeKind::USIZE;
}

std::string TypeInfo::toString() const {
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
        case TypeKind::F32:
            return "f32";
        case TypeKind::F64:
            return "f64";
        case TypeKind::BOOL:
            return "bool";
        case TypeKind::STR:
            return "str";
        case TypeKind::NULL_TYPE:
            return "null";
        case TypeKind::VOID:
            return "void";
        case TypeKind::ANY:
            return "any";
        case TypeKind::CLASS:
            return className;
        case TypeKind::FUNCTION: {
            std::ostringstream out;
            out << "function(";
            for (size_t i = 0; i < paramTypes.size(); ++i) {
                if (i != 0) out << ", ";
                out << (paramTypes[i] ? paramTypes[i]->toString() : "any");
            }
            out << ") -> "
                << (returnType ? returnType->toString() : std::string("void"));
            return out.str();
        }
        case TypeKind::ARRAY:
            return "Array<" + (elementType ? elementType->toString() : "any") +
                   ">";
        case TypeKind::DICT:
            return "Dict<" + (keyType ? keyType->toString() : "any") + ", " +
                   (valueType ? valueType->toString() : "any") + ">";
        case TypeKind::SET:
            return "Set<" + (elementType ? elementType->toString() : "any") +
                   ">";
        default:
            return "<unknown>";
    }
}

int TypeInfo::bitWidth() const {
    switch (kind) {
        case TypeKind::I8:
        case TypeKind::U8:
            return 8;
        case TypeKind::I16:
        case TypeKind::U16:
            return 16;
        case TypeKind::I32:
        case TypeKind::U32:
        case TypeKind::F32:
            return 32;
        case TypeKind::I64:
        case TypeKind::U64:
        case TypeKind::USIZE:
        case TypeKind::F64:
            return 64;
        default:
            return 0;
    }
}

bool isAssignable(const TypeRef& from, const TypeRef& to) {
    if (!from || !to) {
        return false;
    }

    if (to->isAny() || from->isAny()) {
        return true;
    }

    if (from->kind == to->kind) {
        if (from->kind == TypeKind::ARRAY) {
            TypeRef fromElem =
                from->elementType ? from->elementType : TypeInfo::makeAny();
            TypeRef toElem =
                to->elementType ? to->elementType : TypeInfo::makeAny();
            return isAssignable(fromElem, toElem);
        }

        if (from->kind == TypeKind::SET) {
            TypeRef fromElem =
                from->elementType ? from->elementType : TypeInfo::makeAny();
            TypeRef toElem =
                to->elementType ? to->elementType : TypeInfo::makeAny();
            return isAssignable(fromElem, toElem);
        }

        if (from->kind == TypeKind::DICT) {
            TypeRef fromKey =
                from->keyType ? from->keyType : TypeInfo::makeAny();
            TypeRef toKey = to->keyType ? to->keyType : TypeInfo::makeAny();
            TypeRef fromValue =
                from->valueType ? from->valueType : TypeInfo::makeAny();
            TypeRef toValue =
                to->valueType ? to->valueType : TypeInfo::makeAny();
            return isAssignable(fromKey, toKey) &&
                   isAssignable(fromValue, toValue);
        }

        if (from->kind != TypeKind::CLASS) {
            return true;
        }

        if (from->classPtr != nullptr && to->classPtr != nullptr) {
            return isClassSubtype(from->classPtr, to->classPtr);
        }

        return from->className == to->className;
    }

    if (from->kind == TypeKind::CLASS && to->kind == TypeKind::CLASS) {
        if (from->classPtr != nullptr && to->classPtr != nullptr) {
            return isClassSubtype(from->classPtr, to->classPtr);
        }
    }

    if (from->kind == TypeKind::NULL_TYPE || to->kind == TypeKind::NULL_TYPE) {
        return false;
    }

    if (from->isNumeric() && to->isNumeric()) {
        if (to->kind == TypeKind::F64) {
            return true;
        }

        if (to->kind == TypeKind::F32) {
            return from->kind == TypeKind::F32;
        }

        if (from->isSigned() && to->isSigned()) {
            return from->bitWidth() <= to->bitWidth();
        }

        if (from->isUnsigned() && to->isUnsigned()) {
            return from->bitWidth() <= to->bitWidth();
        }

        return false;
    }

    return false;
}

TypeRef numericPromotion(const TypeRef& lhs, const TypeRef& rhs) {
    if (!lhs || !rhs || !lhs->isNumeric() || !rhs->isNumeric()) {
        return nullptr;
    }

    if (lhs->isFloat() || rhs->isFloat()) {
        if (lhs->kind == TypeKind::F32 && rhs->kind == TypeKind::F32) {
            return TypeInfo::makeF32();
        }
        return TypeInfo::makeF64();
    }

    if (lhs->isSigned() && rhs->isSigned()) {
        return widestSignedInt(std::max(lhs->bitWidth(), rhs->bitWidth()));
    }

    if (lhs->isUnsigned() && rhs->isUnsigned()) {
        return widestUnsignedInt(std::max(lhs->bitWidth(), rhs->bitWidth()));
    }

    return TypeInfo::makeF64();
}
