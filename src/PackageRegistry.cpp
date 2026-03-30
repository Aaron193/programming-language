#include "PackageRegistry.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <string_view>
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
constexpr const char* kProjectLockFileName = "mog.lock";
constexpr const char* kPackageApiFileName = "package.api.toml";

enum class TomlSectionKind {
    NONE,
    PACKAGE,
    EXPORT,
};

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

std::string canonicalOrEmpty(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path resolved =
        std::filesystem::weakly_canonical(path, ec);
    if (ec || !std::filesystem::exists(resolved, ec) || ec) {
        return "";
    }
    return resolved.string();
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

bool appendRegistryEntry(std::vector<PackageRegistryEntry>& outEntries,
                         PackageRegistryEntry&& entry, std::string& outError) {
    if (entry.importName.empty()) {
        outError = "Package registry entry is missing 'name'.";
        return false;
    }

    for (const auto& existing : outEntries) {
        if (existing.importName == entry.importName) {
            outError = "Duplicate package registry entry for '" +
                       entry.importName + "'.";
            return false;
        }
    }

    outEntries.push_back(std::move(entry));
    return true;
}

bool resolveEntryPaths(const std::string& projectRoot,
                       PackageRegistryEntry& entry,
                       std::string& outError) {
    if (entry.packageDir.empty()) {
        outError = "Package registry entry '" + entry.importName +
                   "' is missing 'package_dir'.";
        return false;
    }

    const std::filesystem::path packageDir =
        std::filesystem::path(projectRoot) / entry.packageDir;
    entry.packageDir = canonicalOrLexical(packageDir);

    if (entry.apiPath.empty()) {
        entry.apiPath = (packageDir / kPackageApiFileName).string();
    } else {
        entry.apiPath =
            canonicalOrLexical(std::filesystem::path(projectRoot) / entry.apiPath);
    }

    if (entry.kind == "source") {
        if (entry.entryPath.empty()) {
            entry.entryPath = (packageDir / "src" / "main.mog").string();
        } else {
            entry.entryPath = canonicalOrLexical(std::filesystem::path(projectRoot) /
                                                 entry.entryPath);
        }
    } else {
        if (entry.libraryPath.empty()) {
            entry.libraryPath =
                (std::filesystem::path(projectRoot) / "build" / "packages" /
                 entry.packageNamespace / entry.packageName /
                 kPackageLibraryFileName)
                    .string();
        } else {
            entry.libraryPath =
                canonicalOrLexical(std::filesystem::path(projectRoot) /
                                   entry.libraryPath);
        }
    }

    return true;
}

bool loadLockfileEntries(const std::filesystem::path& lockfilePath,
                         std::vector<PackageRegistryEntry>& outEntries,
                         std::string& outError) {
    outEntries.clear();
    outError.clear();

    std::ifstream file(lockfilePath);
    if (!file) {
        outError = "Could not open lockfile '" + lockfilePath.string() + "'.";
        return false;
    }

    TomlSectionKind section = TomlSectionKind::NONE;
    PackageRegistryEntry current;
    bool hasCurrent = false;
    std::string line;
    size_t lineNumber = 0;

    auto flushCurrent = [&]() -> bool {
        if (!hasCurrent) {
            return true;
        }
        if (current.packageId.empty() &&
            !current.packageNamespace.empty() &&
            !current.packageName.empty()) {
            current.packageId =
                makePackageId(current.packageNamespace, current.packageName);
        }
        if (!appendRegistryEntry(outEntries, std::move(current), outError)) {
            return false;
        }
        current = PackageRegistryEntry{};
        hasCurrent = false;
        return true;
    };

    while (std::getline(file, line)) {
        ++lineNumber;
        const std::string content = stripComment(line);
        if (content.empty()) {
            continue;
        }

        if (content == "[[package]]") {
            if (!flushCurrent()) {
                outError = "Invalid lockfile line " + std::to_string(lineNumber) +
                           ": " + outError;
                return false;
            }
            section = TomlSectionKind::PACKAGE;
            hasCurrent = true;
            continue;
        }

        const size_t equals = content.find('=');
        if (equals == std::string::npos) {
            outError = "Invalid lockfile line " + std::to_string(lineNumber) +
                       ": expected key = value.";
            return false;
        }

        if (section != TomlSectionKind::PACKAGE || !hasCurrent) {
            outError = "Invalid lockfile line " + std::to_string(lineNumber) +
                       ": entries must appear inside [[package]].";
            return false;
        }

        const std::string key = trim(std::string_view(content).substr(0, equals));
        const std::string value =
            trim(std::string_view(content).substr(equals + 1));
        std::string parsed;
        if (!parseQuotedString(value, parsed, outError)) {
            outError = "Invalid lockfile line " + std::to_string(lineNumber) +
                       ": " + outError;
            return false;
        }

        if (key == "name") {
            current.importName = parsed;
        } else if (key == "package_id") {
            current.packageId = parsed;
        } else if (key == "namespace") {
            current.packageNamespace = parsed;
        } else if (key == "package_name") {
            current.packageName = parsed;
        } else if (key == "kind") {
            current.kind = parsed;
        } else if (key == "package_dir") {
            current.packageDir = parsed;
        } else if (key == "entry") {
            current.entryPath = parsed;
        } else if (key == "library") {
            current.libraryPath = parsed;
        } else if (key == "api") {
            current.apiPath = parsed;
        } else if (key == "description") {
            current.description = parsed;
        }
    }

    return flushCurrent();
}

bool fileExists(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
}

bool loadPackageManifestEntry(const std::filesystem::path& packageDir,
                              PackageRegistryEntry& outEntry,
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
    outEntry.packageId =
        makePackageId(outEntry.packageNamespace, outEntry.packageName);
    outEntry.packageDir = canonicalOrLexical(packageDir);
    outEntry.kind = manifest.kind.empty() ? "native" : manifest.kind;
    outEntry.description = manifest.description;
    if (!manifest.entry.empty()) {
        outEntry.entryPath = canonicalOrLexical(packageDir / manifest.entry);
    }
    if (!manifest.library.empty()) {
        outEntry.libraryPath = canonicalOrLexical(packageDir / manifest.library);
    }

    const std::filesystem::path apiPath = packageDir / kPackageApiFileName;
    if (fileExists(apiPath.string())) {
        outEntry.apiPath = canonicalOrLexical(apiPath);
    }

    return true;
}

bool scanPackageRootForEntry(const std::filesystem::path& root,
                             std::string_view rawSpecifier,
                             PackageRegistryEntry& outEntry) {
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || ec) {
        return false;
    }

    for (const auto& namespaceEntry : std::filesystem::directory_iterator(root, ec)) {
        if (ec || !namespaceEntry.is_directory()) {
            continue;
        }

        const std::filesystem::path packageDir =
            namespaceEntry.path() / std::string(rawSpecifier);
        std::string loadError;
        if (!loadPackageManifestEntry(packageDir, outEntry, loadError)) {
            continue;
        }
        return true;
    }

    return false;
}

}  // namespace

std::string packageImportNameFromId(std::string_view packageId) {
    const size_t colon = packageId.rfind(':');
    if (colon == std::string::npos) {
        return std::string(packageId);
    }
    return std::string(packageId.substr(colon + 1));
}

bool findProjectRootForPackages(const std::string& importerPath,
                                std::string& outProjectRoot) {
    outProjectRoot.clear();

    std::vector<std::filesystem::path> starts;
    if (!importerPath.empty()) {
        starts.push_back(std::filesystem::path(importerPath).parent_path());
    }
    starts.push_back(std::filesystem::current_path());

    std::unordered_set<std::string> visited;
    for (std::filesystem::path current : starts) {
        std::error_code ec;
        current = std::filesystem::weakly_canonical(current, ec);
        if (ec) {
            current = current.lexically_normal();
        }

        while (!current.empty()) {
            const std::string normalized = current.string();
            if (!visited.insert(normalized).second) {
                break;
            }

            const std::filesystem::path manifestPath =
                current / kProjectManifestFileName;
            if (fileExists(manifestPath.string())) {
                outProjectRoot = normalized;
                return true;
            }

            if (current == current.root_path()) {
                break;
            }
            current = current.parent_path();
        }
    }

    return false;
}

bool loadProjectPackageRegistry(const std::string& projectRoot,
                                std::vector<PackageRegistryEntry>& outEntries,
                                std::string& outError) {
    outEntries.clear();
    outError.clear();

    const std::filesystem::path lockfilePath =
        std::filesystem::path(projectRoot) / kProjectLockFileName;
    if (!loadLockfileEntries(lockfilePath, outEntries, outError)) {
        return false;
    }

    for (auto& entry : outEntries) {
        if (!resolveEntryPaths(projectRoot, entry, outError)) {
            return false;
        }
    }

    return true;
}

bool resolvePackageRegistryEntry(
    const std::string& importerPath, std::string_view rawSpecifier,
    const std::vector<std::string>& packageSearchPaths,
    PackageRegistryEntry& outEntry, std::string& outError) {
    outEntry = PackageRegistryEntry{};
    outError.clear();

    if (rawSpecifier.find(':') != std::string_view::npos) {
        outError = "Package imports must use bare names like 'window', not '" +
                   std::string(rawSpecifier) + "'.";
        return false;
    }

    std::string projectRoot;
    if (findProjectRootForPackages(importerPath, projectRoot)) {
        std::vector<PackageRegistryEntry> entries;
        std::string registryError;
        if (!loadProjectPackageRegistry(projectRoot, entries, registryError)) {
            outError = registryError;
            return false;
        }

        for (const auto& entry : entries) {
            if (entry.importName == rawSpecifier) {
                outEntry = entry;
                return true;
            }
        }
    }

    for (const auto& root : normalizePackageSearchPaths(packageSearchPaths, importerPath)) {
        if (scanPackageRootForEntry(root, rawSpecifier, outEntry)) {
            return true;
        }
    }

    outError = "Cannot find package '" + std::string(rawSpecifier) + "'.";
    return false;
}

bool resolveHandlePackageId(const std::string& importerPath,
                            std::string_view rawSpecifier,
                            const std::vector<std::string>& packageSearchPaths,
                            std::string& outPackageId,
                            std::string& outPackageNamespace,
                            std::string& outPackageName,
                            std::string& outError) {
    outPackageId.clear();
    outPackageNamespace.clear();
    outPackageName.clear();
    outError.clear();

    if (rawSpecifier.find(':') != std::string_view::npos) {
        outError =
            "Handle types must use bare package names like "
            "handle<window:WindowHandle>.";
        return false;
    }

    if (!isValidPackageIdPart(rawSpecifier)) {
        outError = "Handle type must use a lowercase package import name.";
        return false;
    }

    PackageRegistryEntry entry;
    if (!resolvePackageRegistryEntry(importerPath, rawSpecifier, packageSearchPaths,
                                     entry, outError)) {
        return false;
    }

    outPackageId = entry.packageId;
    outPackageNamespace = entry.packageNamespace;
    outPackageName = entry.packageName;
    return true;
}

bool loadPackageApiMetadata(const std::string& apiPath,
                            PackageApiMetadata& outMetadata,
                            std::string& outError) {
    outMetadata = PackageApiMetadata{};
    outError.clear();

    std::ifstream file(apiPath);
    if (!file) {
        outError = "Could not open package API file '" + apiPath + "'.";
        return false;
    }

    TomlSectionKind section = TomlSectionKind::NONE;
    std::string currentName;
    std::string currentTypeText;
    std::string currentDoc;
    std::string line;
    size_t lineNumber = 0;

    auto flushCurrent = [&]() -> bool {
        if (section != TomlSectionKind::EXPORT || currentName.empty()) {
            currentName.clear();
            currentTypeText.clear();
            currentDoc.clear();
            return true;
        }

        std::string typeError;
        TypeRef type = parsePackageType(currentTypeText, typeError);
        if (!type) {
            outError = "Package API export '" + currentName +
                       "' has invalid type: " + typeError;
            return false;
        }
        outMetadata.exportTypes[currentName] = type;
        if (!currentDoc.empty()) {
            outMetadata.exportDocs[currentName] = currentDoc;
        }
        currentName.clear();
        currentTypeText.clear();
        currentDoc.clear();
        return true;
    };

    while (std::getline(file, line)) {
        ++lineNumber;
        const std::string content = stripComment(line);
        if (content.empty()) {
            continue;
        }

        if (content == "[[export]]") {
            if (!flushCurrent()) {
                return false;
            }
            section = TomlSectionKind::EXPORT;
            continue;
        }

        const size_t equals = content.find('=');
        if (equals == std::string::npos) {
            outError = "Invalid package API line " + std::to_string(lineNumber) +
                       ": expected key = value.";
            return false;
        }

        if (section != TomlSectionKind::EXPORT) {
            outError = "Invalid package API line " + std::to_string(lineNumber) +
                       ": entries must appear inside [[export]].";
            return false;
        }

        const std::string key = trim(std::string_view(content).substr(0, equals));
        const std::string value =
            trim(std::string_view(content).substr(equals + 1));
        std::string parsed;
        if (!parseQuotedString(value, parsed, outError)) {
            outError = "Invalid package API line " + std::to_string(lineNumber) +
                       ": " + outError;
            return false;
        }

        if (key == "name") {
            currentName = parsed;
        } else if (key == "type") {
            currentTypeText = parsed;
        } else if (key == "doc") {
            currentDoc = parsed;
        }
    }

    return flushCurrent();
}
