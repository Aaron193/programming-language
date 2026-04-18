#pragma once

#include <string>
#include <vector>

#include "DependencySpec.hpp"
#include "PackageRegistry.hpp"

struct ProjectRegistryConfig {
    std::string alias;
    std::string index;
};

struct ProjectNativeToolchainConfig {
    std::string target;
    std::string cmakeToolchain;
};

struct ProjectManifestData {
    std::string kind = "project";
    std::string name;
    std::string version = "0.1.0";
    std::string description;
    std::vector<std::string> workspaceMembers;
    std::vector<ProjectRegistryConfig> registries;
    std::vector<ProjectNativeToolchainConfig> nativeToolchains;
    std::vector<DependencySpec> dependencies;
    std::vector<DependencySpec> devDependencies;
};

struct InstallOptions {
    bool locked = false;
    bool offline = false;
    bool preferPrebuilt = true;
    bool noNativeBuild = false;
    bool includeDevDependencies = true;
    bool update = false;
    std::string target;
    std::string cmakeToolchainFile;
};

bool loadProjectManifestData(const std::string& projectRoot,
                             ProjectManifestData& outManifest,
                             std::string& outError);

bool writeProjectManifestData(const std::string& projectRoot,
                              const ProjectManifestData& manifest,
                              std::string& outError);

bool initializeProjectManifest(const std::string& projectRoot,
                               const std::string& projectName,
                               std::string& outError);

bool discoverDependencySpec(const std::string& projectRoot,
                            const std::string& rawSpecifier,
                            DependencySpec& outDependency,
                            std::string& outError);

bool addProjectDependency(const std::string& projectRoot,
                          const DependencySpec& dependency,
                          std::string& outError);

bool publishProjectPackage(const std::string& projectRoot,
                           const std::string& packageDir,
                           const std::string& registryAlias,
                           std::string& outError);

bool installProjectPackages(const std::string& projectRoot,
                            std::vector<PackageRegistryEntry>& outEntries,
                            const InstallOptions& options,
                            std::string& outError);

bool ensureProjectPackagesInstalled(const std::string& projectRoot,
                                    const InstallOptions& options,
                                    std::string& outError);
