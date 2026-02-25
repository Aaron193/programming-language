#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "TypeInfo.hpp"

struct NativeDescriptor {
    std::string name;
    std::vector<TypeRef> paramTypes;
    TypeRef returnType;
    int arity;
};

const std::vector<NativeDescriptor>& standardLibraryNatives();

void registerStandardLibraryTypeSignatures(
    std::unordered_map<std::string, TypeRef>& signatures);
