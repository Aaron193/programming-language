#pragma once

#include <string>
#include <string_view>
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
bool isOrdinaryStandardLibraryFunctionName(std::string_view name);

void registerStandardLibraryTypeSignatures(
    std::unordered_map<std::string, TypeRef>& signatures);
void registerOrdinaryStandardLibraryTypeSignatures(
    std::unordered_map<std::string, TypeRef>& signatures);
