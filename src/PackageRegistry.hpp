#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "TypeInfo.hpp"

struct PackageRegistryEntry {
    std::string importName;
    std::string packageId;
    std::string packageNamespace;
    std::string packageName;
    std::string packageDir;
    std::string kind;
    std::string entryPath;
    std::string libraryPath;
    std::string apiPath;
    std::string description;
};

struct PackageApiMetadata {
    std::unordered_map<std::string, TypeRef> exportTypes;
    std::unordered_map<std::string, std::string> exportDocs;
};

std::string packageImportNameFromId(std::string_view packageId);

bool findProjectRootForPackages(const std::string& importerPath,
                                std::string& outProjectRoot);

bool loadProjectPackageRegistry(const std::string& projectRoot,
                                std::vector<PackageRegistryEntry>& outEntries,
                                std::string& outError);

bool resolvePackageRegistryEntry(
    const std::string& importerPath, std::string_view rawSpecifier,
    const std::vector<std::string>& packageSearchPaths,
    PackageRegistryEntry& outEntry, std::string& outError);

bool resolveHandlePackageId(const std::string& importerPath,
                            std::string_view rawSpecifier,
                            const std::vector<std::string>& packageSearchPaths,
                            std::string& outPackageId,
                            std::string& outPackageNamespace,
                            std::string& outPackageName,
                            std::string& outError);

bool loadPackageApiMetadata(const std::string& apiPath,
                            PackageApiMetadata& outMetadata,
                            std::string& outError);
