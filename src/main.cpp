#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "VirtualMachine.hpp"

struct CliOptions {
    bool trace = false;
    bool showReturn = false;
    bool disassemble = false;
    bool strict = false;
    std::string sourceFile;
    std::vector<std::string> packagePaths;
};

static void printUsage(const char* executable) {
    std::cout
        << "Usage: " << executable
        << " [--trace] [--show-return] [--disassemble] [--strict]"
        << " [--package-path <dir>|--package-path=<dir>] [source file]"
        << std::endl;
}

static bool parseArgs(int argc, char** argv, CliOptions& options) {
    std::vector<std::string> positional;

    for (int index = 1; index < argc; ++index) {
        std::string arg = argv[index];
        if (arg == "--trace") {
            options.trace = true;
        } else if (arg == "--show-return") {
            options.showReturn = true;
        } else if (arg == "--disassemble") {
            options.disassemble = true;
        } else if (arg == "--strict") {
            options.strict = true;
        } else if (arg == "--package-path") {
            if (index + 1 >= argc) {
                std::cerr << "Missing value for --package-path." << std::endl;
                printUsage(argv[0]);
                return false;
            }
            options.packagePaths.push_back(argv[++index]);
        } else if (arg.rfind("--package-path=", 0) == 0) {
            options.packagePaths.push_back(arg.substr(15));
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return false;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return false;
        } else {
            positional.push_back(arg);
        }
    }

    if (positional.size() > 1) {
        std::cerr << "Expected at most one source file." << std::endl;
        printUsage(argv[0]);
        return false;
    }

    if (!positional.empty()) {
        options.sourceFile = positional[0];
    }

    return true;
}

static int runFile(const CliOptions& options) {
    std::ifstream file(options.sourceFile);
    if (!file) {
        std::cerr << "Error: Could not open source file " << options.sourceFile
                  << std::endl;
        return 1;
    }

    auto source =
        std::make_unique<std::string>((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
    file.close();

    std::string absolutePath;
    try {
        absolutePath =
            std::filesystem::weakly_canonical(options.sourceFile).string();
    } catch (const std::exception&) {
        absolutePath = options.sourceFile;
    }

    VirtualMachine vm;
    vm.setPackageSearchPaths(options.packagePaths);
    Status status =
        vm.interpret(*source, options.showReturn, options.trace,
                     options.disassemble, absolutePath, options.strict);

    if (status == Status::COMPILATION_ERROR) {
        std::cerr << "Compilation error in source file: " << options.sourceFile
                  << std::endl;
        return 1;
    }
    if (status == Status::RUNTIME_ERROR) {
        std::cerr << "Runtime error in source file: " << options.sourceFile
                  << std::endl;
        return 1;
    }

    return 0;
}

static int runRepl(const CliOptions& options) {
    VirtualMachine vm;
    vm.setPackageSearchPaths(options.packagePaths);
    std::string line;

    while (true) {
        std::cout << ">> " << std::flush;
        if (!std::getline(std::cin, line)) {
            std::cout << std::endl;
            break;
        }

        if (line == "exit" || line == "quit") {
            break;
        }

        if (line.empty()) {
            continue;
        }

        Status status = vm.interpret(line, options.showReturn, options.trace,
                                     options.disassemble, "", options.strict);
        if (status == Status::COMPILATION_ERROR) {
            std::cerr << "Compilation error." << std::endl;
            continue;
        }
        if (status == Status::RUNTIME_ERROR) {
            std::cerr << "Runtime error." << std::endl;
        }
    }

    return 0;
}

int main(int argc, char** argv) {
    CliOptions options;
    try {
        std::filesystem::path executablePath = std::filesystem::weakly_canonical(argv[0]);
        options.packagePaths.push_back(
            (executablePath.parent_path() / "packages").string());
    } catch (const std::exception&) {
    }
    if (!parseArgs(argc, argv, options)) {
        return 1;
    }

    if (!options.sourceFile.empty()) {
        return runFile(options);
    }

    return runRepl(options);
}
