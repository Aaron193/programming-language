#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "DependencySpec.hpp"

struct PackageManifest {
    std::string kind = "native";
    std::string importName;
    std::string packageNamespace;
    std::string packageName;
    std::string version;
    uint32_t abiVersion = 0;
    std::string author;
    std::string description;
    std::string entry;
    std::string library;
    std::vector<DependencySpec> dependencies;
    std::vector<DependencySpec> devDependencies;
};

bool loadPackageManifest(const std::string& packageDir,
                         PackageManifest& outManifest,
                         std::string& outError);

bool validatePackageDirectory(const std::string& packageDir,
                              const std::string& repoRoot,
                              std::string& outError);
