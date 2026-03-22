#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "Chunk.hpp"
#include "Compiler.hpp"
#include "GC.hpp"

namespace {

bool readFile(const std::filesystem::path& path, std::string& outSource) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }

    outSource.assign(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
    return true;
}

std::string canonicalPath(const std::string& path) {
    try {
        return std::filesystem::weakly_canonical(path).string();
    } catch (const std::exception&) {
        return path;
    }
}

void printUsage(const char* argv0) {
    std::cerr << "Usage: " << argv0
              << " [--json] [--strict] [--package-path DIR] source.mog [more.mog]"
              << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
    bool json = false;
    bool strict = false;
    std::vector<std::string> packagePaths;
    std::vector<std::string> files;

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--json") {
            json = true;
        } else if (arg == "--strict") {
            strict = true;
        } else if (arg == "--package-path") {
            if (index + 1 >= argc) {
                printUsage(argv[0]);
                return 1;
            }
            packagePaths.push_back(argv[++index]);
        } else if (!arg.empty() && arg[0] == '-') {
            printUsage(argv[0]);
            return 1;
        } else {
            files.push_back(arg);
        }
    }

    if (files.empty()) {
        printUsage(argv[0]);
        return 1;
    }

    GC gc;
    Compiler compiler;
    compiler.setGC(&gc);
    compiler.setPackageSearchPaths(packagePaths);
    compiler.setStrictMode(strict);

    for (size_t fileIndex = 0; fileIndex < files.size(); ++fileIndex) {
        std::string source;
        const std::string path = canonicalPath(files[fileIndex]);
        if (!readFile(path, source)) {
            std::cerr << "Failed to read benchmark source: " << path << std::endl;
            return 1;
        }

        Chunk chunk;
        if (!compiler.compile(source, chunk, path)) {
            return 1;
        }

        const auto& timings = compiler.lastFrontendTimings();
        if (json) {
            std::cout << "{"
                      << "\"file\":\"" << path << "\","
                      << "\"parseMicros\":" << timings.parseMicros << ","
                      << "\"symbolCollectionMicros\":"
                      << timings.symbolCollectionMicros << ","
                      << "\"importResolutionMicros\":"
                      << timings.importResolutionMicros << ","
                      << "\"initialBindMicros\":" << timings.initialBindMicros
                      << ","
                      << "\"initialTypecheckMicros\":"
                      << timings.initialTypecheckMicros << ","
                      << "\"hirLowerMicros\":" << timings.hirLowerMicros << ","
                      << "\"hirOptimizeMicros\":" << timings.hirOptimizeMicros
                      << ","
                      << "\"moduleCacheHits\":" << timings.moduleCacheHits << ","
                      << "\"moduleCacheMisses\":" << timings.moduleCacheMisses
                      << ","
                      << "\"moduleCacheRebuilds\":"
                      << timings.moduleCacheRebuilds << ","
                      << "\"diagnosticCount\":" << timings.diagnosticCount << ","
                      << "\"internedIdentifierCount\":"
                      << timings.internedIdentifierCount << ","
                      << "\"totalMicros\":" << timings.totalMicros << "}"
                      << std::endl;
        } else {
            std::cout << timings.totalMicros << std::endl;
        }
    }

    return 0;
}
