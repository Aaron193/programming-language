#pragma once

#include <string>

struct DependencySpec {
    std::string alias;
    std::string packageId;
    std::string path;
    std::string version;
    std::string git;
    std::string registry;
    bool workspace = false;
};
