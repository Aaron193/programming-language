#pragma once

#include <string>
#include <string_view>

inline constexpr std::string_view kSourceModuleExtension = ".mog";

bool hasSourceModuleExtension(const std::string& pathText);

std::string resolveImportPath(const std::string& importerPath,
                              const std::string& rawImportPath);
