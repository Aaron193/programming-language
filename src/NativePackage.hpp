#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Chunk.hpp"
#include "NativePackageAPI.hpp"

enum class ImportTargetKind {
    SOURCE_MODULE,
    NATIVE_PACKAGE,
};

struct ImportTarget {
    ImportTargetKind kind = ImportTargetKind::SOURCE_MODULE;
    std::string rawSpecifier;
    std::string canonicalId;
    std::string resolvedPath;
    std::string displayName;
    std::string packageNamespace;
    std::string packageName;
    bool isLegacyBarePackage = false;
};

struct NativePackageConstantDescriptor {
    std::string name;
    TypeRef type;
    ExprPackageValue value{};
    std::string stringValueStorage;
};

struct NativePackageFunctionDescriptor {
    std::string name;
    TypeRef type;
    int arity = 0;
    const ExprPackageFunctionExport* callback = nullptr;
};

struct NativePackageDescriptor {
    std::string packageNamespace;
    std::string packageName;
    std::string packageId;
    std::string libraryPath;
    bool isLegacyAbi = false;
    std::unordered_map<std::string, TypeRef> exportTypes;
    std::vector<NativePackageFunctionDescriptor> functions;
    std::vector<NativePackageConstantDescriptor> constants;
};

struct NativePackageBinding {
    std::string packageId;
    std::string packageNamespace;
    std::string packageName;
    const ExprPackageFunctionExport* function = nullptr;
};

std::vector<std::string> normalizePackageSearchPaths(
    const std::vector<std::string>& packageSearchPaths,
    const std::string& importerPath);

bool resolveImportTarget(const std::string& importerPath,
                         const std::string& rawImportPath,
                         const std::vector<std::string>& packageSearchPaths,
                         ImportTarget& outTarget,
                         std::string& outError);

bool isNativeImportTargetId(const std::string& canonicalId);
std::string nativeImportLibraryPath(const std::string& canonicalId);

bool loadNativePackageDescriptor(const std::string& libraryPath,
                                 NativePackageDescriptor& outDescriptor,
                                 std::string& outError,
                                 bool keepLibraryLoaded,
                                 void** outLibraryHandle);
