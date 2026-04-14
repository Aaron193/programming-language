#pragma once

#include <string>
#include <vector>

#include "PackageRegistry.hpp"

struct ProjectDependencySpec {
    std::string alias;
    std::string packageId;
    std::string path;
    std::string version;
    std::string git;
    std::string registry;
    bool workspace = false;
};

struct ProjectManifestData {
    std::string kind = "project";
    std::string name;
    std::string version = "0.1.0";
    std::string description;
    std::vector<std::string> workspaceMembers;
    std::vector<ProjectDependencySpec> dependencies;
    std::vector<ProjectDependencySpec> devDependencies;
};

struct InstallOptions {
    bool locked = false;
    bool offline = false;
    bool includeDevDependencies = true;
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

bool discoverLocalDependencySpec(const std::string& projectRoot,
                                 const std::string& rawSpecifier,
                                 ProjectDependencySpec& outDependency,
                                 std::string& outError);

bool addProjectDependency(const std::string& projectRoot,
                          const ProjectDependencySpec& dependency,
                          std::string& outError);

bool installProjectPackages(const std::string& projectRoot,
                            std::vector<PackageRegistryEntry>& outEntries,
                            const InstallOptions& options,
                            std::string& outError);

bool ensureProjectPackagesInstalled(const std::string& projectRoot,
                                    const InstallOptions& options,
                                    std::string& outError);
