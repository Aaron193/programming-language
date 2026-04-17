#include "PackageManifest.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_set>

#include "NativePackage.hpp"
#include "PackageRegistry.hpp"

namespace {

#if defined(__APPLE__)
constexpr const char* kPackageLibraryFileName = "package.dylib";
#else
constexpr const char* kPackageLibraryFileName = "package.so";
#endif

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

bool parseUnsignedValue(const std::string& value, uint32_t& out,
                        std::string& outError) {
    if (value.empty()) {
        outError = "Expected integer value.";
        return false;
    }

    for (char ch : value) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            outError = "Expected integer value.";
            return false;
        }
    }

    unsigned long parsed = 0;
    try {
        parsed = std::stoul(value);
    } catch (const std::exception&) {
        outError = "Invalid integer value.";
        return false;
    }

    out = static_cast<uint32_t>(parsed);
    return true;
}

bool parseBoolValue(const std::string& value, bool& out, std::string& outError) {
    if (value == "true") {
        out = true;
        return true;
    }
    if (value == "false") {
        out = false;
        return true;
    }
    outError = "Expected boolean value.";
    return false;
}

bool parseQuotedStringArray(const std::string& value,
                            std::vector<std::string>& outValues,
                            std::string& outError) {
    outValues.clear();
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        outError = "Expected array value.";
        return false;
    }

    std::string body = trim(std::string_view(value).substr(1, value.size() - 2));
    while (!body.empty()) {
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

        std::string parsed;
        if (!parseQuotedString(trim(body.substr(0, valueLength)), parsed, outError)) {
            return false;
        }
        outValues.push_back(parsed);

        if (valueLength >= body.size()) {
            body.clear();
        } else {
            body = trim(std::string_view(body).substr(valueLength + 1));
        }
    }

    return true;
}

std::string normalizeRelativePath(std::string pathText) {
    std::replace(pathText.begin(), pathText.end(), '\\', '/');
    return pathText;
}

bool parseDependencyInlineTable(const std::string& value,
                                DependencySpec& outDependency,
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
        if (key == "path") {
            std::string parsed;
            if (!parseQuotedString(rawValue, parsed, outError)) {
                return false;
            }
            outDependency.path = normalizeRelativePath(parsed);
        } else if (key == "package") {
            std::string parsed;
            if (!parseQuotedString(rawValue, parsed, outError)) {
                return false;
            }
            outDependency.packageId = parsed;
        } else if (key == "version") {
            std::string parsed;
            if (!parseQuotedString(rawValue, parsed, outError)) {
                return false;
            }
            outDependency.version = parsed;
        } else if (key == "git") {
            std::string parsed;
            if (!parseQuotedString(rawValue, parsed, outError)) {
                return false;
            }
            outDependency.git = parsed;
        } else if (key == "registry") {
            std::string parsed;
            if (!parseQuotedString(rawValue, parsed, outError)) {
                return false;
            }
            outDependency.registry = parsed;
        } else if (key == "workspace") {
            if (!parseBoolValue(rawValue, outDependency.workspace, outError)) {
                return false;
            }
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

    if (outDependency.path.empty() && !outDependency.workspace &&
        outDependency.git.empty() && outDependency.registry.empty() &&
        outDependency.packageId.empty()) {
        outError = "Dependency entries must define path, workspace, git, registry, or package metadata.";
        return false;
    }

    return true;
}

bool parseSystemDependencyInlineTable(const std::string& value,
                                      SystemDependencySpec& outDependency,
                                      std::string& outError) {
    if (value.size() < 2 || value.front() != '{' || value.back() != '}') {
        outError = "System dependency entries must be inline tables.";
        return false;
    }

    std::string body = trim(std::string_view(value).substr(1, value.size() - 2));
    while (!body.empty()) {
        const size_t equals = body.find('=');
        if (equals == std::string::npos) {
            outError = "System dependency table must use key = value entries.";
            return false;
        }

        const std::string key = trim(std::string_view(body).substr(0, equals));
        body = trim(std::string_view(body).substr(equals + 1));
        if (body.empty()) {
            outError = "System dependency table entry is missing a value.";
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
        if (key == "version") {
            if (!parseQuotedString(rawValue, outDependency.version, outError)) {
                return false;
            }
        } else if (key == "required") {
            if (!parseBoolValue(rawValue, outDependency.required, outError)) {
                return false;
            }
        } else {
            outError = "Unsupported system dependency field '" + key + "'.";
            return false;
        }

        if (valueLength >= body.size()) {
            body.clear();
        } else {
            body = trim(std::string_view(body).substr(valueLength + 1));
        }
    }

    return true;
}

bool parseLegacyDependencies(const std::string& value,
                             std::vector<DependencySpec>& outDependencies,
                             std::string& outError) {
    std::vector<std::string> ids;
    if (!parseQuotedStringArray(value, ids, outError)) {
        outError = "dependencies must be an array of quoted package IDs.";
        return false;
    }

    outDependencies.clear();
    for (const std::string& dependencyId : ids) {
        DependencySpec dependency;
        dependency.packageId = dependencyId;
        dependency.alias = packageImportNameFromId(dependencyId);
        outDependencies.push_back(std::move(dependency));
    }
    return true;
}

void sortDependencies(std::vector<DependencySpec>& dependencies) {
    std::sort(dependencies.begin(), dependencies.end(),
              [](const DependencySpec& lhs, const DependencySpec& rhs) {
                  return lhs.alias < rhs.alias;
              });
}

void sortSystemDependencies(std::vector<SystemDependencySpec>& systemDependencies) {
    std::sort(systemDependencies.begin(), systemDependencies.end(),
              [](const SystemDependencySpec& lhs,
                 const SystemDependencySpec& rhs) {
                  return lhs.name < rhs.name;
              });
}

bool validateDependencySpecs(const std::vector<DependencySpec>& dependencies,
                             std::string& outError) {
    for (const auto& dependency : dependencies) {
        if (dependency.alias.empty() || !isValidPackageIdPart(dependency.alias)) {
            outError = "Dependency aliases must use lowercase letters, digits, '_', or '-'.";
            return false;
        }

        const std::string& packageId = dependency.packageId;
        if (packageId.empty()) {
            outError = "Package manifests currently require each dependency to declare package = \"namespace:name\".";
            return false;
        }

        size_t colon = packageId.find(':');
        if (colon == std::string::npos ||
            packageId.find(':', colon + 1) != std::string::npos) {
            outError = "Dependency '" + packageId +
                       "' must use a canonical package ID like 'mog:window'.";
            return false;
        }

        std::string_view packageNamespace(packageId.data(), colon);
        std::string_view packageName(packageId.data() + colon + 1,
                                     packageId.size() - colon - 1);
        if (!isValidPackageIdPart(packageNamespace) ||
            !isValidPackageIdPart(packageName)) {
            outError = "Dependency '" + packageId +
                       "' must use lowercase package IDs.";
            return false;
        }
    }

    return true;
}

bool validateSystemDependencySpecs(
    const std::vector<SystemDependencySpec>& systemDependencies,
    std::string& outError) {
    std::unordered_set<std::string> seenNames;
    for (const auto& dependency : systemDependencies) {
        if (dependency.name.empty() || !isValidPackageIdPart(dependency.name)) {
            outError = "System dependency names must use lowercase letters, digits, '_', or '-'.";
            return false;
        }
        if (!seenNames.insert(dependency.name).second) {
            outError = "Duplicate system dependency '" + dependency.name + "'.";
            return false;
        }
    }

    return true;
}

std::string joinPath(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path resolved = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        return path.lexically_normal().string();
    }
    return resolved.string();
}

bool pathEndsWith(const std::filesystem::path& path,
                  const std::vector<std::string>& suffixParts) {
    std::vector<std::string> parts;
    for (const auto& segment : path) {
        parts.push_back(segment.string());
    }
    if (parts.size() < suffixParts.size()) {
        return false;
    }

    size_t start = parts.size() - suffixParts.size();
    for (size_t index = 0; index < suffixParts.size(); ++index) {
        if (parts[start + index] != suffixParts[index]) {
            return false;
        }
    }
    return true;
}

std::string findLibraryPath(const std::filesystem::path& packageDir,
                            const PackageManifest& manifest,
                            const std::filesystem::path& repoRoot) {
    if (!manifest.library.empty()) {
        const std::filesystem::path configuredLibrary = packageDir / manifest.library;
        if (std::filesystem::exists(configuredLibrary)) {
            return joinPath(configuredLibrary);
        }
    }

    const std::filesystem::path directLibrary =
        packageDir / kPackageLibraryFileName;
    if (std::filesystem::exists(directLibrary)) {
        return joinPath(directLibrary);
    }

    const std::filesystem::path builtLibrary =
        repoRoot / "build" / "packages" / manifest.packageNamespace /
        manifest.packageName / kPackageLibraryFileName;
    if (std::filesystem::exists(builtLibrary)) {
        return joinPath(builtLibrary);
    }

    return "";
}

}  // namespace

bool loadPackageManifest(const std::string& packageDir,
                         PackageManifest& outManifest,
                         std::string& outError) {
    outManifest = PackageManifest{};
    outError.clear();

    std::filesystem::path manifestPath =
        std::filesystem::path(packageDir) / "mog.toml";
    if (!std::filesystem::exists(manifestPath)) {
        manifestPath = std::filesystem::path(packageDir) / "package.toml";
    }
    std::ifstream file(manifestPath);
    if (!file) {
        outError = "Could not open manifest '" + manifestPath.string() + "'.";
        return false;
    }

    enum class Section {
        ROOT,
        SYSTEM_DEPENDENCIES,
        DEPENDENCIES,
        DEV_DEPENDENCIES,
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
        if (content == "[dev-dependencies]") {
            section = Section::DEV_DEPENDENCIES;
            continue;
        }
        if (content == "[system-dependencies]") {
            section = Section::SYSTEM_DEPENDENCIES;
            continue;
        }
        if (content.front() == '[' && content.back() == ']') {
            outError = "Unknown manifest section '" + content + "'.";
            return false;
        }

        const size_t equals = content.find('=');
        if (equals == std::string::npos) {
            outError = "Invalid manifest line " + std::to_string(lineNumber) +
                       ": expected key = value.";
            return false;
        }

        const std::string key = trim(std::string_view(content).substr(0, equals));
        const std::string value =
            trim(std::string_view(content).substr(equals + 1));
        std::string parseError;

        if (section == Section::DEPENDENCIES ||
            section == Section::DEV_DEPENDENCIES) {
            DependencySpec dependency;
            dependency.alias = key;
            if (!parseDependencyInlineTable(value, dependency, parseError)) {
                outError = "Invalid dependency '" + key + "': " + parseError;
                return false;
            }
            auto& output = section == Section::DEPENDENCIES ? outManifest.dependencies
                                                            : outManifest.devDependencies;
            output.push_back(std::move(dependency));
            continue;
        }
        if (section == Section::SYSTEM_DEPENDENCIES) {
            SystemDependencySpec dependency;
            dependency.name = key;
            if (!parseSystemDependencyInlineTable(value, dependency, parseError)) {
                outError = "Invalid system dependency '" + key + "': " +
                           parseError;
                return false;
            }
            outManifest.systemDependencies.push_back(std::move(dependency));
            continue;
        }

        if (key == "kind") {
            if (!parseQuotedString(value, outManifest.kind, parseError)) {
                outError = "Invalid manifest kind: " + parseError;
                return false;
            }
        } else if (key == "import_name") {
            if (!parseQuotedString(value, outManifest.importName, parseError)) {
                outError = "Invalid manifest import_name: " + parseError;
                return false;
            }
        } else if (key == "namespace") {
            if (!parseQuotedString(value, outManifest.packageNamespace,
                                   parseError)) {
                outError = "Invalid manifest namespace: " + parseError;
                return false;
            }
        } else if (key == "name") {
            if (!parseQuotedString(value, outManifest.packageName, parseError)) {
                outError = "Invalid manifest name: " + parseError;
                return false;
            }
        } else if (key == "version") {
            if (!parseQuotedString(value, outManifest.version, parseError)) {
                outError = "Invalid manifest version: " + parseError;
                return false;
            }
        } else if (key == "abi_version") {
            if (!parseUnsignedValue(value, outManifest.abiVersion, parseError)) {
                outError = "Invalid manifest abi_version: " + parseError;
                return false;
            }
        } else if (key == "author") {
            if (!parseQuotedString(value, outManifest.author, parseError)) {
                outError = "Invalid manifest author: " + parseError;
                return false;
            }
        } else if (key == "description") {
            if (!parseQuotedString(value, outManifest.description, parseError)) {
                outError = "Invalid manifest description: " + parseError;
                return false;
            }
        } else if (key == "entry") {
            if (!parseQuotedString(value, outManifest.entry, parseError)) {
                outError = "Invalid manifest entry: " + parseError;
                return false;
            }
        } else if (key == "library") {
            if (!parseQuotedString(value, outManifest.library, parseError)) {
                outError = "Invalid manifest library: " + parseError;
                return false;
            }
        } else if (key == "dependencies") {
            if (!parseLegacyDependencies(value, outManifest.dependencies, parseError)) {
                outError = "Invalid manifest dependencies: " + parseError;
                return false;
            }
        } else {
            outError = "Unknown manifest field '" + key + "'.";
            return false;
        }
    }

    if (outManifest.importName.empty()) {
        outManifest.importName = outManifest.packageName;
    }

    if (outManifest.packageNamespace.empty() || outManifest.packageName.empty() ||
        outManifest.version.empty() || outManifest.description.empty()) {
        outError =
            "Manifest must define namespace, name, version, and description.";
        return false;
    }

    if (outManifest.kind.empty()) {
        outManifest.kind = "native";
    }
    if (outManifest.kind != "native" && outManifest.kind != "source") {
        outError = "Manifest kind must be 'native' or 'source'.";
        return false;
    }

    if (!isValidPackageIdPart(outManifest.importName)) {
        outError =
            "Manifest import_name must use lowercase letters, digits, '_', or '-'.";
        return false;
    }

    if (outManifest.kind == "native" && outManifest.abiVersion == 0) {
        outError = "Native package manifest must define abi_version.";
        return false;
    }
    if (outManifest.kind == "source" && outManifest.entry.empty()) {
        outError = "Source package manifest must define entry.";
        return false;
    }
    if (outManifest.kind != "native" && !outManifest.systemDependencies.empty()) {
        outError =
            "Only native package manifests may declare [system-dependencies].";
        return false;
    }

    sortSystemDependencies(outManifest.systemDependencies);
    sortDependencies(outManifest.dependencies);
    sortDependencies(outManifest.devDependencies);

    if (!validateSystemDependencySpecs(outManifest.systemDependencies, outError)) {
        return false;
    }
    if (!validateDependencySpecs(outManifest.dependencies, outError)) {
        return false;
    }
    if (!validateDependencySpecs(outManifest.devDependencies, outError)) {
        return false;
    }

    return true;
}

bool validatePackageDirectory(const std::string& packageDir,
                              const std::string& repoRoot,
                              std::string& outError) {
    outError.clear();

    std::error_code ec;
    const std::filesystem::path dirPath =
        std::filesystem::weakly_canonical(packageDir, ec);
    if (ec || !std::filesystem::exists(dirPath)) {
        outError = "Package directory '" + packageDir + "' does not exist.";
        return false;
    }

    PackageManifest manifest;
    if (!loadPackageManifest(dirPath.string(), manifest, outError)) {
        return false;
    }

    if (!isValidPackageIdPart(manifest.packageNamespace) ||
        !isValidPackageIdPart(manifest.packageName)) {
        outError = "Manifest namespace and name must use lowercase letters, "
                   "digits, '_', or '-'.";
        return false;
    }

    if (manifest.kind != "native") {
        outError = "Package validation currently supports native packages only.";
        return false;
    }

    if (manifest.abiVersion != EXPR_NATIVE_PACKAGE_ABI_VERSION) {
        outError = "Manifest abi_version must be " +
                   std::to_string(EXPR_NATIVE_PACKAGE_ABI_VERSION) +
                   " for namespaced native packages.";
        return false;
    }

    if (!pathEndsWith(dirPath, {manifest.packageNamespace, manifest.packageName}) &&
        !pathEndsWith(dirPath, {"packages", manifest.packageNamespace,
                                manifest.packageName})) {
        outError = "Package directory must end with '" + manifest.packageNamespace +
                   "/" + manifest.packageName + "'.";
        return false;
    }

    const std::filesystem::path repoRootPath(repoRoot);
    if (manifest.packageNamespace == "mog") {
        const bool isOfficialSourcePath =
            pathEndsWith(dirPath, {"packages", "mog", manifest.packageName}) &&
            dirPath.string().rfind(joinPath(repoRootPath / "packages"), 0) == 0;
        const bool isOfficialBuildPath =
            pathEndsWith(dirPath, {"build", "packages", "mog",
                                   manifest.packageName}) &&
            dirPath.string().rfind(joinPath(repoRootPath / "build" / "packages"),
                                   0) == 0;
        const bool isInstalledPath =
            pathEndsWith(dirPath, {".mog", "install", "packages", "mog",
                                   manifest.packageName});
        if (!isOfficialSourcePath && !isOfficialBuildPath && !isInstalledPath) {
            outError =
                "Namespace 'mog' is reserved for runtime-maintained packages.";
            return false;
        }
    }

    const std::string libraryPath = findLibraryPath(dirPath, manifest, repoRootPath);
    if (libraryPath.empty()) {
        outError = "Could not find built package library '" +
                   std::string(kPackageLibraryFileName) + "' for '" +
                   makePackageId(manifest.packageNamespace, manifest.packageName) +
                   "'.";
        return false;
    }

    NativePackageDescriptor descriptor;
    std::string packageError;
    if (!loadNativePackageDescriptor(libraryPath, descriptor, packageError, false,
                                     nullptr)) {
        outError = packageError;
        return false;
    }

    if (descriptor.packageNamespace != manifest.packageNamespace ||
        descriptor.packageName != manifest.packageName) {
        outError = "Manifest declares '" +
                   makePackageId(manifest.packageNamespace, manifest.packageName) +
                   "' but library registers '" + descriptor.packageId + "'.";
        return false;
    }

    const std::filesystem::path apiPath = dirPath / "package.api.mog";
    if (!std::filesystem::exists(apiPath)) {
        outError = "Package directory '" + dirPath.string() +
                   "' is missing package.api.mog.";
        return false;
    }

    PackageApiMetadata apiMetadata;
    const std::string importName =
        manifest.importName.empty() ? manifest.packageName : manifest.importName;
    if (!loadPackageApiMetadata(apiPath.string(), descriptor.packageId,
                                importName, apiMetadata, outError)) {
        return false;
    }
    if (!validateNativePackageApi(apiMetadata, descriptor, outError)) {
        return false;
    }

    return true;
}
