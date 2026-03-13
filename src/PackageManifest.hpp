#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct PackageManifest {
    std::string packageNamespace;
    std::string packageName;
    std::string version;
    uint32_t abiVersion = 0;
    std::string author;
    std::string description;
    std::vector<std::string> dependencies;
};

bool loadPackageManifest(const std::string& packageDir,
                         PackageManifest& outManifest,
                         std::string& outError);

bool validatePackageDirectory(const std::string& packageDir,
                              const std::string& repoRoot,
                              std::string& outError);
