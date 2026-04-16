#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "SourceLocation.hpp"
#include "TypeInfo.hpp"

struct NativePackageDescriptor;

struct PackageRegistryEntry {
    std::string importName;
    std::string packageId;
    std::string packageNamespace;
    std::string packageName;
    std::string version;
    std::string packageDir;
    std::string kind;
    std::string entryPath;
    std::string libraryPath;
    std::string apiPath;
    std::string description;
    std::vector<std::string> dependencyIds;
    std::vector<std::string> dependencyGroups;
    std::string sourceType;
    std::string sourcePath;
    std::string registry;
    std::string artifactPath;
    std::string artifactDigest;
    std::string selectedTarget;
    bool buildFromSource = false;
    std::string manifestDigest;
    std::string apiDigest;
};

struct PackageApiExport {
    TypeRef type;
    std::string doc;
    std::string kind;
    std::vector<std::string> parameterLabels;
    std::string returnTypeLabel;
    SourceSpan range = makePointSpan(1, 1);
    SourceSpan selectionRange = makePointSpan(1, 1);
};

struct PackageApiOpaqueType {
    TypeRef type;
    std::string doc;
    std::string runtimeHandleTypeName;
    SourceSpan range = makePointSpan(1, 1);
    SourceSpan selectionRange = makePointSpan(1, 1);
};

struct PackageApiMetadata {
    std::string packageName;
    std::unordered_map<std::string, PackageApiExport> valueExports;
    std::unordered_map<std::string, PackageApiOpaqueType> typeExports;
};

std::string packageImportNameFromId(std::string_view packageId);

bool findProjectRootForPackages(const std::string& importerPath,
                                std::string& outProjectRoot);

bool loadProjectPackageRegistry(const std::string& projectRoot,
                                std::vector<PackageRegistryEntry>& outEntries,
                                std::string& outError);

bool loadProjectLockfile(const std::string& projectRoot,
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
                            const std::string& packageId,
                            const std::string& importName,
                            PackageApiMetadata& outMetadata,
                            std::string& outError);

bool parsePackageApiMetadata(std::string_view source,
                             const std::string& apiPath,
                             const std::string& packageId,
                             const std::string& importName,
                             PackageApiMetadata& outMetadata,
                             std::string& outError);

bool validateNativePackageApi(const PackageApiMetadata& api,
                              const NativePackageDescriptor& descriptor,
                              std::string& outError);
