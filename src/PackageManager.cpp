#include "PackageManager.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "NativePackage.hpp"
#include "PackageManifest.hpp"

namespace {

#if defined(__APPLE__)
constexpr const char* kPackageLibraryFileName = "package.dylib";
#else
constexpr const char* kPackageLibraryFileName = "package.so";
#endif

constexpr const char* kProjectManifestFileName = "mog.toml";
constexpr const char* kLockfileName = "mog.lock";
constexpr const char* kInstallRegistryFileName = "registry.toml";
constexpr const char* kPackageApiFileName = "package.api.mog";

std::string trim(std::string_view text) {
    size_t start = 0;
    while (start < text.size() &&
           std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }

    size_t end = text.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return std::string(text.substr(start, end - start));
}

std::string stripComment(std::string_view line) {
    bool inString = false;
    for (size_t index = 0; index < line.size(); ++index) {
        if (line[index] == '"' && (index == 0 || line[index - 1] != '\\')) {
            inString = !inString;
            continue;
        }
        if (!inString && line[index] == '#') {
            return trim(line.substr(0, index));
        }
    }
    return trim(line);
}

bool parseQuotedString(const std::string& value, std::string& out,
                       std::string& outError) {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        outError = "Expected quoted string value.";
        return false;
    }

    out.clear();
    for (size_t index = 1; index + 1 < value.size(); ++index) {
        char ch = value[index];
        if (ch == '\\' && index + 1 < value.size() - 1) {
            ++index;
            char escaped = value[index];
            switch (escaped) {
                case '\\':
                case '"':
                    out.push_back(escaped);
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                default:
                    outError = "Unsupported escape sequence in string value.";
                    return false;
            }
            continue;
        }
        out.push_back(ch);
    }

    return true;
}

std::string quoteTomlString(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (char ch : text) {
        switch (ch) {
            case '\\':
            case '"':
                out.push_back('\\');
                out.push_back(ch);
                break;
            case '\n':
                out += "\\n";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(ch);
                break;
        }
    }
    out.push_back('"');
    return out;
}

std::string canonicalOrLexical(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path resolved =
        std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        return path.lexically_normal().string();
    }
    return resolved.string();
}

bool fileExists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
}

std::string defaultProjectName(const std::string& projectRoot) {
    const std::filesystem::path path(projectRoot);
    const std::string stem = path.filename().string();
    return stem.empty() ? "mog-project" : stem;
}

bool parseDependencyInlineTable(const std::string& value,
                                ProjectDependencySpec& outDependency,
                                std::string& outError) {
    if (value.size() < 2 || value.front() != '{' || value.back() != '}') {
        outError = "Dependency entries must be inline tables.";
        return false;
    }

    std::string body = trim(std::string_view(value).substr(1, value.size() - 2));
    while (!body.empty()) {
        const size_t equals = body.find('=');
        if (equals == std::string::npos) {
            outError = "Dependency table must use key = value entries.";
            return false;
        }

        const std::string key = trim(std::string_view(body).substr(0, equals));
        body = trim(std::string_view(body).substr(equals + 1));
        if (body.empty()) {
            outError = "Dependency table entry is missing a value.";
            return false;
        }

        size_t valueLength = 0;
        bool inString = false;
        bool escaped = false;
        for (; valueLength < body.size(); ++valueLength) {
            const char ch = body[valueLength];
            if (inString) {
                if (escaped) {
                    escaped = false;
                } else if (ch == '\\') {
                    escaped = true;
                } else if (ch == '"') {
                    inString = false;
                }
                continue;
            }

            if (ch == '"') {
                inString = true;
                continue;
            }

            if (ch == ',') {
                break;
            }
        }

        const std::string rawValue = trim(body.substr(0, valueLength));
        std::string parsed;
        if (!parseQuotedString(rawValue, parsed, outError)) {
            return false;
        }

        if (key == "path") {
            outDependency.path = parsed;
        } else if (key == "package") {
            outDependency.packageId = parsed;
        } else {
            outError = "Unsupported dependency field '" + key + "'.";
            return false;
        }

        if (valueLength >= body.size()) {
            body.clear();
        } else {
            body = trim(std::string_view(body).substr(valueLength + 1));
        }
    }

    if (outDependency.path.empty()) {
        outError = "Dependency entries must define a path.";
        return false;
    }

    return true;
}

bool loadPackageEntryFromDir(const std::filesystem::path& packageDir,
                             PackageRegistryEntry& outEntry,
                             std::vector<std::string>* outDependencies,
                             std::string& outError) {
    PackageManifest manifest;
    if (!loadPackageManifest(packageDir.string(), manifest, outError)) {
        return false;
    }

    outEntry = PackageRegistryEntry{};
    outEntry.importName = manifest.importName.empty() ? manifest.packageName
                                                      : manifest.importName;
    outEntry.packageNamespace = manifest.packageNamespace;
    outEntry.packageName = manifest.packageName;
    outEntry.packageId = makePackageId(manifest.packageNamespace, manifest.packageName);
    outEntry.packageDir = canonicalOrLexical(packageDir);
    outEntry.kind = manifest.kind.empty() ? "native" : manifest.kind;
    outEntry.description = manifest.description;

    if (outEntry.kind == "source") {
        std::filesystem::path entryPath = manifest.entry.empty()
                                              ? packageDir / "src" / "main.mog"
                                              : packageDir / manifest.entry;
        outEntry.entryPath = canonicalOrLexical(entryPath);
    } else if (!manifest.library.empty()) {
        outEntry.libraryPath = canonicalOrLexical(packageDir / manifest.library);
    } else {
        const std::filesystem::path directLibrary =
            packageDir / kPackageLibraryFileName;
        if (fileExists(directLibrary)) {
            outEntry.libraryPath = canonicalOrLexical(directLibrary);
        }
    }

    const std::filesystem::path apiPath = packageDir / kPackageApiFileName;
    if (fileExists(apiPath)) {
        outEntry.apiPath = canonicalOrLexical(apiPath);
    }

    if (outDependencies != nullptr) {
        *outDependencies = manifest.dependencies;
    }

    return true;
}

std::string normalizePackageIdLikeSpecifier(const std::string& rawSpecifier) {
    if (rawSpecifier.find(':') != std::string::npos) {
        return rawSpecifier;
    }

    const size_t slash = rawSpecifier.find('/');
    if (slash == std::string::npos) {
        return "";
    }

    return rawSpecifier.substr(0, slash) + ":" + rawSpecifier.substr(slash + 1);
}

bool relativePathString(const std::filesystem::path& from,
                        const std::filesystem::path& to,
                        std::string& outPath) {
    std::error_code ec;
    std::filesystem::path relative = std::filesystem::relative(to, from, ec);
    if (ec) {
        outPath = to.lexically_normal().string();
        return false;
    }
    outPath = relative.lexically_normal().string();
    return true;
}

std::filesystem::path installRoot(const std::filesystem::path& projectRoot) {
    return projectRoot / ".mog" / "install";
}

std::filesystem::path installRegistryPath(const std::filesystem::path& projectRoot) {
    return installRoot(projectRoot) / kInstallRegistryFileName;
}

std::string inferWorkspaceRoot(const std::filesystem::path& packageDir,
                               const PackageRegistryEntry& entry,
                               const std::string& fallbackProjectRoot) {
    std::filesystem::path current = packageDir;
    while (!current.empty()) {
        const std::filesystem::path candidateLibrary =
            current / "build" / "packages" / entry.packageNamespace /
            entry.packageName / kPackageLibraryFileName;
        if (fileExists(candidateLibrary)) {
            return current.string();
        }
        if (current == current.root_path()) {
            break;
        }
        current = current.parent_path();
    }

    return fallbackProjectRoot;
}

bool writeRegistryFile(const std::filesystem::path& outputPath,
                       const std::vector<PackageRegistryEntry>& entries,
                       std::string& outError) {
    std::error_code ec;
    std::filesystem::create_directories(outputPath.parent_path(), ec);
    if (ec) {
        outError = "Could not create install directory '" +
                   outputPath.parent_path().string() + "'.";
        return false;
    }

    std::ofstream out(outputPath);
    if (!out) {
        outError = "Could not open '" + outputPath.string() + "' for writing.";
        return false;
    }

    out << "# Generated by mog install. Do not edit by hand.\n";
    for (const auto& entry : entries) {
        out << "\n[[package]]\n";
        out << "name = " << quoteTomlString(entry.importName) << "\n";
        out << "package_id = " << quoteTomlString(entry.packageId) << "\n";
        out << "namespace = " << quoteTomlString(entry.packageNamespace) << "\n";
        out << "package_name = " << quoteTomlString(entry.packageName) << "\n";
        out << "kind = " << quoteTomlString(entry.kind) << "\n";
        out << "package_dir = " << quoteTomlString(entry.packageDir) << "\n";
        if (!entry.entryPath.empty()) {
            out << "entry = " << quoteTomlString(entry.entryPath) << "\n";
        }
        if (!entry.libraryPath.empty()) {
            out << "library = " << quoteTomlString(entry.libraryPath) << "\n";
        }
        if (!entry.apiPath.empty()) {
            out << "api = " << quoteTomlString(entry.apiPath) << "\n";
        }
        if (!entry.description.empty()) {
            out << "description = " << quoteTomlString(entry.description) << "\n";
        }
    }

    return true;
}

struct PackageNode {
    PackageRegistryEntry entry;
    std::filesystem::path packageDir;
    std::vector<std::string> dependencies;
};

bool scanPackageDirectories(const std::filesystem::path& root,
                            std::unordered_map<std::string, PackageNode>& outNodes,
                            std::string& outError) {
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || ec) {
        return true;
    }

    for (std::filesystem::recursive_directory_iterator it(root, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (ec || !it->is_directory()) {
            continue;
        }

        const std::filesystem::path dir = it->path();
        if (!fileExists(dir / kProjectManifestFileName) &&
            !fileExists(dir / "package.toml")) {
            continue;
        }

        PackageNode node;
        if (!loadPackageEntryFromDir(dir, node.entry, &node.dependencies, outError)) {
            continue;
        }
        node.packageDir = std::filesystem::path(node.entry.packageDir);
        outNodes[node.entry.packageId] = std::move(node);
    }

    return true;
}

bool validateInstalledPackage(const PackageNode& node,
                              const std::string& projectRoot,
                              std::string& outError) {
    if (node.entry.kind == "native") {
        const std::string validationRoot =
            inferWorkspaceRoot(node.packageDir, node.entry, projectRoot);
        return validatePackageDirectory(node.packageDir.string(), validationRoot,
                                        outError);
    }

    if (node.entry.entryPath.empty() || !fileExists(node.entry.entryPath)) {
        outError = "Source package '" + node.entry.importName +
                   "' is missing entry module '" + node.entry.entryPath + "'.";
        return false;
    }

    if (!node.entry.apiPath.empty()) {
        PackageApiMetadata apiMetadata;
        if (!loadPackageApiMetadata(node.entry.apiPath, node.entry.packageId,
                                    node.entry.importName, apiMetadata,
                                    outError)) {
            return false;
        }
    }

    return true;
}

bool collectInstalledPackages(
    const std::string& projectRoot, const ProjectManifestData& manifest,
    std::vector<PackageRegistryEntry>& outEntries, std::string& outError) {
    outEntries.clear();
    outError.clear();

    std::unordered_map<std::string, PackageNode> available;
    if (!scanPackageDirectories(std::filesystem::path(projectRoot) / "packages",
                                available, outError)) {
        return false;
    }
    for (auto& [packageId, node] : available) {
        (void)packageId;
        if (node.entry.kind == "native" && node.entry.libraryPath.empty()) {
            const std::string workspaceRoot =
                inferWorkspaceRoot(node.packageDir, node.entry, projectRoot);
            node.entry.libraryPath = canonicalOrLexical(
                std::filesystem::path(workspaceRoot) / "build" / "packages" /
                node.entry.packageNamespace / node.entry.packageName /
                kPackageLibraryFileName);
        }
    }

    std::vector<PackageNode> roots;
    for (const auto& dependency : manifest.dependencies) {
        const std::filesystem::path dependencyPath =
            std::filesystem::path(projectRoot) / dependency.path;
        PackageNode node;
        if (!loadPackageEntryFromDir(dependencyPath, node.entry, &node.dependencies,
                                     outError)) {
            outError = "Could not load dependency '" + dependency.alias + "': " +
                       outError;
            return false;
        }
        node.packageDir = std::filesystem::path(node.entry.packageDir);
        if (node.entry.kind == "native" && node.entry.libraryPath.empty()) {
            const std::string workspaceRoot =
                inferWorkspaceRoot(node.packageDir, node.entry, projectRoot);
            node.entry.libraryPath = canonicalOrLexical(
                std::filesystem::path(workspaceRoot) / "build" / "packages" /
                node.entry.packageNamespace / node.entry.packageName /
                kPackageLibraryFileName);
        }
        if (!dependency.packageId.empty() &&
            dependency.packageId != node.entry.packageId) {
            outError = "Dependency '" + dependency.alias + "' declares package '" +
                       dependency.packageId + "' but path resolves to '" +
                       node.entry.packageId + "'.";
            return false;
        }
        available[node.entry.packageId] = node;
        roots.push_back(std::move(node));
    }

    if (roots.empty()) {
        for (const auto& [packageId, node] : available) {
            (void)packageId;
            roots.push_back(node);
        }
    }

    std::sort(roots.begin(), roots.end(),
              [](const PackageNode& lhs, const PackageNode& rhs) {
                  return lhs.entry.packageId < rhs.entry.packageId;
              });

    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> inProgress;
    std::vector<PackageRegistryEntry> ordered;

    std::function<bool(const PackageNode&)> visit = [&](const PackageNode& node) {
        if (!visited.insert(node.entry.packageId).second) {
            return true;
        }
        if (!inProgress.insert(node.entry.packageId).second) {
            outError = "Package dependency cycle detected at '" +
                       node.entry.packageId + "'.";
            return false;
        }

        for (const std::string& dependencyId : node.dependencies) {
            auto dependencyIt = available.find(dependencyId);
            if (dependencyIt == available.end()) {
                outError = "Package '" + node.entry.packageId +
                           "' depends on '" + dependencyId +
                           "', but no local package provides it.";
                return false;
            }
            if (!visit(dependencyIt->second)) {
                return false;
            }
        }

        inProgress.erase(node.entry.packageId);
        ordered.push_back(node.entry);
        return true;
    };

    for (const auto& root : roots) {
        if (!visit(root)) {
            return false;
        }
    }

    std::sort(ordered.begin(), ordered.end(),
              [](const PackageRegistryEntry& lhs, const PackageRegistryEntry& rhs) {
                  if (lhs.importName != rhs.importName) {
                      return lhs.importName < rhs.importName;
                  }
                  return lhs.packageId < rhs.packageId;
              });

    for (const auto& entry : ordered) {
        const auto nodeIt = available.find(entry.packageId);
        if (nodeIt == available.end()) {
            outError = "Internal package install error for '" + entry.packageId +
                       "'.";
            return false;
        }
        if (!validateInstalledPackage(nodeIt->second, projectRoot, outError)) {
            return false;
        }
    }

    outEntries = std::move(ordered);
    return true;
}

}  // namespace

bool loadProjectManifestData(const std::string& projectRoot,
                             ProjectManifestData& outManifest,
                             std::string& outError) {
    outManifest = ProjectManifestData{};
    outManifest.name = defaultProjectName(projectRoot);
    outManifest.description = outManifest.name + " project";
    outError.clear();

    const std::filesystem::path manifestPath =
        std::filesystem::path(projectRoot) / kProjectManifestFileName;
    std::ifstream file(manifestPath);
    if (!file) {
        outError = "Could not open project manifest '" + manifestPath.string() + "'.";
        return false;
    }

    enum class Section {
        ROOT,
        DEPENDENCIES,
    };

    Section section = Section::ROOT;
    std::string line;
    size_t lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        const std::string content = stripComment(line);
        if (content.empty()) {
            continue;
        }

        if (content == "[dependencies]") {
            section = Section::DEPENDENCIES;
            continue;
        }

        if (content.front() == '[' && content.back() == ']') {
            outError = "Unsupported project manifest section '" + content + "'.";
            return false;
        }

        const size_t equals = content.find('=');
        if (equals == std::string::npos) {
            outError = "Invalid project manifest line " + std::to_string(lineNumber) +
                       ": expected key = value.";
            return false;
        }

        const std::string key = trim(std::string_view(content).substr(0, equals));
        const std::string value =
            trim(std::string_view(content).substr(equals + 1));
        std::string parseError;

        if (section == Section::ROOT) {
            if (key == "kind") {
                if (!parseQuotedString(value, outManifest.kind, parseError)) {
                    outError = parseError;
                    return false;
                }
            } else if (key == "name") {
                if (!parseQuotedString(value, outManifest.name, parseError)) {
                    outError = parseError;
                    return false;
                }
            } else if (key == "version") {
                if (!parseQuotedString(value, outManifest.version, parseError)) {
                    outError = parseError;
                    return false;
                }
            } else if (key == "description") {
                if (!parseQuotedString(value, outManifest.description, parseError)) {
                    outError = parseError;
                    return false;
                }
            } else {
                // Preserve compatibility with older root manifests.
                continue;
            }
            continue;
        }

        ProjectDependencySpec dependency;
        dependency.alias = key;
        if (!parseDependencyInlineTable(value, dependency, parseError)) {
            outError = "Invalid dependency '" + key + "': " + parseError;
            return false;
        }
        outManifest.dependencies.push_back(std::move(dependency));
    }

    if (outManifest.kind.empty()) {
        outManifest.kind = "project";
    }
    if (outManifest.kind != "project") {
        outError = "Root mog.toml must declare kind = \"project\".";
        return false;
    }

    return true;
}

bool writeProjectManifestData(const std::string& projectRoot,
                              const ProjectManifestData& manifest,
                              std::string& outError) {
    outError.clear();

    const std::filesystem::path manifestPath =
        std::filesystem::path(projectRoot) / kProjectManifestFileName;
    std::ofstream out(manifestPath);
    if (!out) {
        outError = "Could not open '" + manifestPath.string() + "' for writing.";
        return false;
    }

    out << "kind = " << quoteTomlString("project") << "\n";
    out << "name = " << quoteTomlString(manifest.name) << "\n";
    out << "version = " << quoteTomlString(manifest.version) << "\n";
    out << "description = " << quoteTomlString(manifest.description) << "\n";

    if (!manifest.dependencies.empty()) {
        out << "\n[dependencies]\n";
        for (const auto& dependency : manifest.dependencies) {
            out << dependency.alias << " = { path = "
                << quoteTomlString(dependency.path);
            if (!dependency.packageId.empty()) {
                out << ", package = " << quoteTomlString(dependency.packageId);
            }
            out << " }\n";
        }
    }

    return true;
}

bool initializeProjectManifest(const std::string& projectRoot,
                               const std::string& projectName,
                               std::string& outError) {
    ProjectManifestData manifest;
    manifest.name = projectName.empty() ? defaultProjectName(projectRoot) : projectName;
    manifest.description = manifest.name + " project";
    return writeProjectManifestData(projectRoot, manifest, outError);
}

bool discoverLocalDependencySpec(const std::string& projectRoot,
                                 const std::string& rawSpecifier,
                                 ProjectDependencySpec& outDependency,
                                 std::string& outError) {
    outDependency = ProjectDependencySpec{};
    outError.clear();

    const std::string packageId = normalizePackageIdLikeSpecifier(rawSpecifier);
    std::unordered_map<std::string, PackageNode> available;
    if (!scanPackageDirectories(std::filesystem::path(projectRoot) / "packages",
                                available, outError)) {
        return false;
    }

    const PackageNode* matched = nullptr;
    for (const auto& [candidateId, node] : available) {
        if (node.entry.importName == rawSpecifier || candidateId == rawSpecifier ||
            (!packageId.empty() && candidateId == packageId)) {
            matched = &node;
            break;
        }
    }

    if (matched == nullptr) {
        outError = "Cannot discover a local package matching '" + rawSpecifier + "'.";
        return false;
    }

    outDependency.alias = matched->entry.importName;
    outDependency.packageId = matched->entry.packageId;
    relativePathString(projectRoot, matched->packageDir, outDependency.path);
    return true;
}

bool addProjectDependency(const std::string& projectRoot,
                          const ProjectDependencySpec& dependency,
                          std::string& outError) {
    ProjectManifestData manifest;
    if (!loadProjectManifestData(projectRoot, manifest, outError)) {
        return false;
    }

    auto existing = std::find_if(
        manifest.dependencies.begin(), manifest.dependencies.end(),
        [&](const ProjectDependencySpec& candidate) {
            return candidate.alias == dependency.alias;
        });
    if (existing != manifest.dependencies.end()) {
        *existing = dependency;
    } else {
        manifest.dependencies.push_back(dependency);
    }

    std::sort(manifest.dependencies.begin(), manifest.dependencies.end(),
              [](const ProjectDependencySpec& lhs,
                 const ProjectDependencySpec& rhs) {
                  return lhs.alias < rhs.alias;
              });

    return writeProjectManifestData(projectRoot, manifest, outError);
}

bool installProjectPackages(const std::string& projectRoot,
                            std::vector<PackageRegistryEntry>& outEntries,
                            std::string& outError) {
    ProjectManifestData manifest;
    if (!loadProjectManifestData(projectRoot, manifest, outError)) {
        return false;
    }

    if (!collectInstalledPackages(projectRoot, manifest, outEntries, outError)) {
        return false;
    }

    const std::filesystem::path projectRootPath(projectRoot);
    const std::filesystem::path registryPath = installRegistryPath(projectRootPath);
    if (!writeRegistryFile(registryPath, outEntries, outError)) {
        return false;
    }

    const std::filesystem::path lockfilePath = projectRootPath / kLockfileName;
    if (!writeRegistryFile(lockfilePath, outEntries, outError)) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : "") /
            ".cache" / "mog" / "packages",
        ec);

    return true;
}

bool ensureProjectPackagesInstalled(const std::string& projectRoot,
                                    std::string& outError) {
    outError.clear();

    ProjectManifestData manifest;
    if (!loadProjectManifestData(projectRoot, manifest, outError)) {
        return false;
    }

    if (manifest.dependencies.empty()) {
        return true;
    }

    const std::filesystem::path registryPath =
        installRegistryPath(std::filesystem::path(projectRoot));
    const std::filesystem::path manifestPath =
        std::filesystem::path(projectRoot) / kProjectManifestFileName;

    std::error_code ec;
    if (std::filesystem::exists(registryPath, ec) && !ec) {
        const auto registryTime = std::filesystem::last_write_time(registryPath, ec);
        if (!ec) {
            const auto manifestTime =
                std::filesystem::last_write_time(manifestPath, ec);
            if (!ec && registryTime >= manifestTime) {
                return true;
            }
        }
    }

    std::vector<PackageRegistryEntry> installedEntries;
    return installProjectPackages(projectRoot, installedEntries, outError);
}
