#include "StdLib.hpp"

namespace {

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
        {"Set", {}, TypeInfo::makeSet(TypeInfo::makeAny()), -1},
    };

    return descriptors;
}

}  // namespace

const std::vector<NativeDescriptor>& standardLibraryNatives() {
    return makeDescriptors();
}

void registerStandardLibraryTypeSignatures(
    std::unordered_map<std::string, TypeRef>& signatures) {
    for (const auto& descriptor : standardLibraryNatives()) {
        signatures[descriptor.name] = TypeInfo::makeFunction(
            descriptor.paramTypes, descriptor.returnType);
    }
}
