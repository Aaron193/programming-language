#include "StdLib.hpp"

namespace {

std::vector<std::pair<std::string, TypeRef>> arrayCollectionMembers(
    const TypeRef& receiverType) {
    const TypeRef element = receiverType && receiverType->elementType
                                ? receiverType->elementType
                                : TypeInfo::makeAny();
    return {
        {"clear", TypeInfo::makeFunction({}, TypeInfo::makeI64())},
        {"first", TypeInfo::makeFunction({}, element)},
        {"has", TypeInfo::makeFunction({element}, TypeInfo::makeBool())},
        {"insert", TypeInfo::makeFunction({TypeInfo::makeI64(), element}, element)},
        {"isEmpty", TypeInfo::makeFunction({}, TypeInfo::makeBool())},
        {"last", TypeInfo::makeFunction({}, element)},
        {"pop", TypeInfo::makeFunction({}, element)},
        {"push", TypeInfo::makeFunction({element}, TypeInfo::makeI64())},
        {"remove", TypeInfo::makeFunction({TypeInfo::makeI64()}, element)},
        {"size", TypeInfo::makeFunction({}, TypeInfo::makeI64())},
    };
}

std::vector<std::pair<std::string, TypeRef>> dictCollectionMembers(
    const TypeRef& receiverType) {
    const TypeRef key = receiverType && receiverType->keyType
                            ? receiverType->keyType
                            : TypeInfo::makeAny();
    const TypeRef value = receiverType && receiverType->valueType
                              ? receiverType->valueType
                              : TypeInfo::makeAny();
    return {
        {"clear", TypeInfo::makeFunction({}, TypeInfo::makeI64())},
        {"get", TypeInfo::makeFunction({key}, value)},
        {"getOr", TypeInfo::makeFunction({key, value}, value)},
        {"has", TypeInfo::makeFunction({key}, TypeInfo::makeBool())},
        {"isEmpty", TypeInfo::makeFunction({}, TypeInfo::makeBool())},
        {"keys", TypeInfo::makeFunction({}, TypeInfo::makeArray(key))},
        {"remove", TypeInfo::makeFunction({key}, value)},
        {"set", TypeInfo::makeFunction({key, value}, value)},
        {"size", TypeInfo::makeFunction({}, TypeInfo::makeI64())},
        {"values", TypeInfo::makeFunction({}, TypeInfo::makeArray(value))},
    };
}

std::vector<std::pair<std::string, TypeRef>> setCollectionMembers(
    const TypeRef& receiverType) {
    const TypeRef element = receiverType && receiverType->elementType
                                ? receiverType->elementType
                                : TypeInfo::makeAny();
    const TypeRef setType = TypeInfo::makeSet(element);
    return {
        {"add", TypeInfo::makeFunction({element}, TypeInfo::makeBool())},
        {"clear", TypeInfo::makeFunction({}, TypeInfo::makeI64())},
        {"difference", TypeInfo::makeFunction({setType}, setType)},
        {"has", TypeInfo::makeFunction({element}, TypeInfo::makeBool())},
        {"intersect", TypeInfo::makeFunction({setType}, setType)},
        {"isEmpty", TypeInfo::makeFunction({}, TypeInfo::makeBool())},
        {"remove", TypeInfo::makeFunction({element}, TypeInfo::makeBool())},
        {"size", TypeInfo::makeFunction({}, TypeInfo::makeI64())},
        {"toArray", TypeInfo::makeFunction({}, TypeInfo::makeArray(element))},
        {"union", TypeInfo::makeFunction({setType}, setType)},
    };
}

const std::vector<NativeDescriptor>& makeDescriptors() {
    static const std::vector<NativeDescriptor> descriptors = {
        {"clock", {}, TypeInfo::makeF64(), 0},
        {"sqrt", {TypeInfo::makeF64()}, TypeInfo::makeF64(), 1},
        {"len", {TypeInfo::makeAny()}, TypeInfo::makeI64(), 1},
        {"error", {TypeInfo::makeStr()}, TypeInfo::makeVoid(), 1},
        {"num", {TypeInfo::makeAny()}, TypeInfo::makeF64(), 1},
        {"type", {TypeInfo::makeAny()}, TypeInfo::makeStr(), 1},
        {"str", {TypeInfo::makeAny()}, TypeInfo::makeStr(), 1},
        {"toString", {TypeInfo::makeAny()}, TypeInfo::makeStr(), 1},
        {"parseInt", {TypeInfo::makeStr()}, TypeInfo::makeI64(), 1},
        {"parseUInt", {TypeInfo::makeStr()}, TypeInfo::makeU64(), 1},
        {"parseFloat", {TypeInfo::makeStr()}, TypeInfo::makeF64(), 1},
        {"abs", {TypeInfo::makeF64()}, TypeInfo::makeF64(), 1},
        {"floor", {TypeInfo::makeF64()}, TypeInfo::makeF64(), 1},
        {"ceil", {TypeInfo::makeF64()}, TypeInfo::makeF64(), 1},
        {"pow",
         {TypeInfo::makeF64(), TypeInfo::makeF64()},
         TypeInfo::makeF64(),
         2},
        {"Array", {}, TypeInfo::makeArray(TypeInfo::makeAny()), -1},
        {"Dict",
         {},
         TypeInfo::makeDict(TypeInfo::makeAny(), TypeInfo::makeAny()),
         0},
        {"Set", {}, TypeInfo::makeSet(TypeInfo::makeAny()), -1},
    };

    return descriptors;
}

}  // namespace

const std::vector<NativeDescriptor>& standardLibraryNatives() {
    return makeDescriptors();
}

bool isOrdinaryStandardLibraryFunctionName(std::string_view name) {
    return name != "type" && name != "str" && name != "Array" &&
           name != "Dict" && name != "Set";
}

void registerStandardLibraryTypeSignatures(
    std::unordered_map<std::string, TypeRef>& signatures) {
    for (const auto& descriptor : standardLibraryNatives()) {
        signatures[descriptor.name] = TypeInfo::makeFunction(
            descriptor.paramTypes, descriptor.returnType);
    }
}

void registerOrdinaryStandardLibraryTypeSignatures(
    std::unordered_map<std::string, TypeRef>& signatures) {
    for (const auto& descriptor : standardLibraryNatives()) {
        if (!isOrdinaryStandardLibraryFunctionName(descriptor.name)) {
            continue;
        }
        signatures[descriptor.name] = TypeInfo::makeFunction(
            descriptor.paramTypes, descriptor.returnType);
    }
}

std::optional<TypeRef> builtinCollectionMemberType(
    const TypeRef& receiverType, std::string_view memberName) {
    const auto members = builtinCollectionMembers(receiverType);
    for (const auto& [name, type] : members) {
        if (name == memberName) {
            return type;
        }
    }
    return std::nullopt;
}

std::vector<std::pair<std::string, TypeRef>> builtinCollectionMembers(
    const TypeRef& receiverType) {
    if (!receiverType) {
        return {};
    }

    switch (receiverType->kind) {
        case TypeKind::ARRAY:
            return arrayCollectionMembers(receiverType);
        case TypeKind::DICT:
            return dictCollectionMembers(receiverType);
        case TypeKind::SET:
            return setCollectionMembers(receiverType);
        default:
            return {};
    }
}
