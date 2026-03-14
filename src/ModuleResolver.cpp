#include "ModuleResolver.hpp"

#include <filesystem>

bool hasSourceModuleExtension(const std::string& pathText) {
    if (pathText.empty()) {
        return false;
    }

    return std::filesystem::path(pathText).extension().string() ==
           kSourceModuleExtension;
}

std::string resolveImportPath(const std::string& importerPath,
                              const std::string& rawImportPath) {
    if (rawImportPath.empty() || !hasSourceModuleExtension(rawImportPath)) {
        return "";
    }

    std::error_code ec;
    std::filesystem::path importPath(rawImportPath);
    std::filesystem::path candidate;

    if (importPath.is_absolute()) {
        candidate = importPath;
    } else {
        if (importerPath.empty()) {
            return "";
        }

        std::filesystem::path importer(importerPath);
        candidate = importer.parent_path() / importPath;
    }

    std::filesystem::path resolved =
        std::filesystem::weakly_canonical(candidate, ec);
    if (ec) {
        return "";
    }

    if (!std::filesystem::exists(resolved, ec) || ec) {
        return "";
    }

    std::string resolvedPath = resolved.string();
    if (!hasSourceModuleExtension(resolvedPath)) {
        return "";
    }

    return resolvedPath;
}
