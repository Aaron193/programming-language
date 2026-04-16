#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "ModuleResolver.hpp"
#include "PackageManager.hpp"
#include "PackageManifest.hpp"
#include "PackageRegistry.hpp"
#include "VirtualMachine.hpp"

struct RuntimeOptions {
    bool trace = false;
    bool showReturn = false;
    bool disassemble = false;
    bool frontendTimings = false;
    bool frontendTimingsJson = false;
    InstallOptions installOptions;
    std::string sourceFile;
    std::vector<std::string> packagePaths;
};

static void printUsage(const char* executable) {
    std::cout
        << "Usage: " << executable << " <command> [options]\n"
        << "Commands:\n"
        << "  init [name]            Create a project mog.toml in the current directory\n"
        << "  add <package>          Add a package dependency and install it\n"
        << "  install [flags]        Install dependencies using mog.lock when it is current\n"
        << "  update [flags]         Re-resolve dependencies and rewrite install metadata\n"
        << "  publish [options] [dir] Publish a package to a configured file registry\n"
        << "  run [flags] <file>     Install dependencies if needed, then run a program\n"
        << "  validate-package <dir> Validate a package directory\n"
        << "Flags for install/update/run:\n"
        << "  --locked --offline\n"
        << "Additional flags for run:\n"
        << "  --trace --show-return --disassemble --frontend-timings --frontend-timings-json\n"
        << "  --package-path <dir> | --package-path=<dir>\n"
        << "Legacy mode is still supported: " << executable << " [flags] <file>\n";
}

static bool parseRuntimeArgs(int argc, char** argv, int startIndex,
                             RuntimeOptions& options, std::string& outError) {
    outError.clear();
    std::vector<std::string> positional;

    for (int index = startIndex; index < argc; ++index) {
        std::string arg = argv[index];
        if (arg == "--trace") {
            options.trace = true;
        } else if (arg == "--show-return") {
            options.showReturn = true;
        } else if (arg == "--disassemble") {
            options.disassemble = true;
        } else if (arg == "--frontend-timings") {
            options.frontendTimings = true;
        } else if (arg == "--frontend-timings-json") {
            options.frontendTimingsJson = true;
        } else if (arg == "--locked") {
            options.installOptions.locked = true;
        } else if (arg == "--offline") {
            options.installOptions.offline = true;
        } else if (arg == "--package-path") {
            if (index + 1 >= argc) {
                outError = "Missing value for --package-path.";
                return false;
            }
            options.packagePaths.push_back(argv[++index]);
        } else if (arg.rfind("--package-path=", 0) == 0) {
            options.packagePaths.push_back(arg.substr(15));
        } else if (arg == "--help" || arg == "-h") {
            outError = "help";
            return false;
        } else if (!arg.empty() && arg[0] == '-') {
            outError = "Unknown option: " + arg;
            return false;
        } else {
            positional.push_back(arg);
        }
    }

    if (positional.size() > 1) {
        outError = "Expected at most one source file.";
        return false;
    }

    if (!positional.empty()) {
        options.sourceFile = positional[0];
    }

    return true;
}

static bool parseInstallArgs(int argc, char** argv, int startIndex,
                             InstallOptions& options, std::string& outError) {
    outError.clear();
    options = InstallOptions{};
    for (int index = startIndex; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--locked") {
            options.locked = true;
        } else if (arg == "--offline") {
            options.offline = true;
        } else if (arg == "--help" || arg == "-h") {
            outError = "help";
            return false;
        } else {
            outError = "Unknown option: " + arg;
            return false;
        }
    }

    return true;
}

static bool parsePublishArgs(int argc, char** argv, int startIndex,
                             std::string& outRegistryAlias,
                             std::string& outPackageDir,
                             std::string& outError) {
    outError.clear();
    outRegistryAlias.clear();
    outPackageDir.clear();

    for (int index = startIndex; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--registry") {
            if (index + 1 >= argc) {
                outError = "Missing value for --registry.";
                return false;
            }
            outRegistryAlias = argv[++index];
        } else if (arg.rfind("--registry=", 0) == 0) {
            outRegistryAlias = arg.substr(11);
        } else if (arg == "--help" || arg == "-h") {
            outError = "help";
            return false;
        } else if (!arg.empty() && arg[0] == '-') {
            outError = "Unknown option: " + arg;
            return false;
        } else if (outPackageDir.empty()) {
            outPackageDir = arg;
        } else {
            outError = "publish accepts at most one package directory.";
            return false;
        }
    }

    return true;
}

static std::string currentProjectRoot() {
    try {
        return std::filesystem::current_path().string();
    } catch (const std::exception&) {
        return ".";
    }
}

static std::string currentManagedProjectRoot() {
    std::string projectRoot;
    if (findProjectRootForPackages(currentProjectRoot(), projectRoot)) {
        return projectRoot;
    }
    return currentProjectRoot();
}

static std::string inferValidationRootForPackage(const std::string& packageDir) {
    PackageManifest manifest;
    std::string error;
    if (!loadPackageManifest(packageDir, manifest, error)) {
        return currentProjectRoot();
    }

    std::filesystem::path current;
    try {
        current = std::filesystem::weakly_canonical(packageDir);
    } catch (const std::exception&) {
        current = std::filesystem::path(packageDir);
    }

    while (!current.empty()) {
        const std::filesystem::path candidateLibrary =
            current / "build" / "packages" / manifest.packageNamespace /
            manifest.packageName /
#if defined(__APPLE__)
            "package.dylib";
#else
            "package.so";
#endif
        if (std::filesystem::exists(candidateLibrary)) {
            return current.string();
        }
        if (current == current.root_path()) {
            break;
        }
        current = current.parent_path();
    }

    return currentProjectRoot();
}

static int runValidatePackageDir(const std::string& packageDir) {
    std::string error;
    if (!validatePackageDirectory(packageDir,
                                  inferValidationRootForPackage(packageDir),
                                  error)) {
        std::cerr << "Package validation failed: " << error << std::endl;
        return 1;
    }

    std::cout << "Package validation passed: " << packageDir << std::endl;
    return 0;
}

static int runFile(const RuntimeOptions& options) {
    if (options.sourceFile.empty()) {
        std::cerr << "run requires a source file." << std::endl;
        return 1;
    }

    if (!hasSourceModuleExtension(options.sourceFile)) {
        std::cerr << "Error: Source files must use the " << kSourceModuleExtension
                  << " extension: " << options.sourceFile << std::endl;
        return 1;
    }

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

    std::string projectRoot;
    if (findProjectRootForPackages(absolutePath, projectRoot)) {
        std::string installError;
        if (!ensureProjectPackagesInstalled(projectRoot, options.installOptions,
                                           installError)) {
            std::cerr << "Package install failed: " << installError << std::endl;
            return 1;
        }
    }

    VirtualMachine vm;
    vm.setPackageSearchPaths(options.packagePaths);
    Status status =
        vm.interpret(*source, options.showReturn, options.trace,
                     options.disassemble, absolutePath,
                     options.frontendTimings,
                     options.frontendTimingsJson);

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

static int runRepl(const RuntimeOptions& options) {
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
                                     options.disassemble, "",
                                     options.frontendTimings,
                                     options.frontendTimingsJson);
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

static int runInstallCommand(const std::string& projectRoot,
                             const InstallOptions& options) {
    std::vector<PackageRegistryEntry> entries;
    std::string error;
    if (!installProjectPackages(projectRoot, entries, options, error)) {
        std::cerr << "Install failed: " << error << std::endl;
        return 1;
    }

    std::cout << "Installed " << entries.size() << " package";
    if (entries.size() != 1) {
        std::cout << "s";
    }
    std::cout << " into " << (std::filesystem::path(projectRoot) / ".mog/install")
              << std::endl;
    return 0;
}

static int runInitCommand(int argc, char** argv) {
    std::string projectRoot = currentProjectRoot();
    std::filesystem::path manifestPath = std::filesystem::path(projectRoot) / "mog.toml";
    if (std::filesystem::exists(manifestPath)) {
        std::cerr << "mog.toml already exists in " << projectRoot << std::endl;
        return 1;
    }

    const std::string projectName =
        argc >= 3 ? argv[2] : std::filesystem::path(projectRoot).filename().string();
    std::string error;
    if (!initializeProjectManifest(projectRoot, projectName, error)) {
        std::cerr << "Init failed: " << error << std::endl;
        return 1;
    }

    std::cout << "Created " << manifestPath << std::endl;
    return 0;
}

static int runAddCommand(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "add requires a package specifier." << std::endl;
        return 1;
    }

    const std::string projectRoot = currentManagedProjectRoot();
    DependencySpec dependency;
    std::string error;
    if (!discoverDependencySpec(projectRoot, argv[2], dependency, error)) {
        std::cerr << "Add failed: " << error << std::endl;
        return 1;
    }

    if (!addProjectDependency(projectRoot, dependency, error)) {
        std::cerr << "Add failed: " << error << std::endl;
        return 1;
    }

    std::vector<PackageRegistryEntry> installedEntries;
    if (!installProjectPackages(projectRoot, installedEntries, InstallOptions{},
                                error)) {
        std::cerr << "Add failed during install: " << error << std::endl;
        return 1;
    }

    if (!dependency.path.empty()) {
        std::cout << "Added dependency '" << dependency.alias << "' from "
                  << dependency.path << std::endl;
    } else {
        std::cout << "Added dependency '" << dependency.alias << "' as "
                  << dependency.packageId;
        if (!dependency.version.empty()) {
            std::cout << "@" << dependency.version;
        }
        std::cout << std::endl;
    }
    return 0;
}

static int runPublishCommand(int argc, char** argv) {
    std::string registryAlias;
    std::string packageDir;
    std::string parseError;
    if (!parsePublishArgs(argc, argv, 2, registryAlias, packageDir, parseError)) {
        if (parseError == "help") {
            printUsage(argv[0]);
            return 0;
        }
        std::cerr << parseError << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    if (packageDir.empty()) {
        packageDir = currentProjectRoot();
    }

    std::string error;
    if (!publishProjectPackage(currentManagedProjectRoot(), packageDir, registryAlias,
                               error)) {
        std::cerr << "Publish failed: " << error << std::endl;
        return 1;
    }

    std::cout << "Published package from " << packageDir << std::endl;
    return 0;
}

int main(int argc, char** argv) {
    RuntimeOptions runtimeOptions;
    try {
        std::filesystem::path executablePath = std::filesystem::weakly_canonical(argv[0]);
        runtimeOptions.packagePaths.push_back(
            (executablePath.parent_path() / "packages").string());
    } catch (const std::exception&) {
    }

    if (argc <= 1) {
        return runRepl(runtimeOptions);
    }

    const std::string command = argv[1];
    if (command == "--validate-package") {
        if (argc < 3) {
            std::cerr << "Missing value for --validate-package." << std::endl;
            return 1;
        }
        return runValidatePackageDir(argv[2]);
    }
    if (command.rfind("--validate-package=", 0) == 0) {
        return runValidatePackageDir(command.substr(19));
    }
    if (command == "--help" || command == "-h") {
        printUsage(argv[0]);
        return 0;
    }
    if (command == "init") {
        return runInitCommand(argc, argv);
    }
    if (command == "add") {
        return runAddCommand(argc, argv);
    }
    if (command == "install" || command == "update") {
        InstallOptions installOptions;
        std::string parseError;
        if (!parseInstallArgs(argc, argv, 2, installOptions, parseError)) {
            if (parseError == "help") {
                printUsage(argv[0]);
                return 0;
            }
            std::cerr << parseError << std::endl;
            printUsage(argv[0]);
            return 1;
        }
        installOptions.update = command == "update";
        return runInstallCommand(currentManagedProjectRoot(), installOptions);
    }
    if (command == "publish") {
        return runPublishCommand(argc, argv);
    }
    if (command == "validate-package") {
        if (argc < 3) {
            std::cerr << "validate-package requires a directory." << std::endl;
            return 1;
        }
        return runValidatePackageDir(argv[2]);
    }
    if (command == "run") {
        std::string parseError;
        if (!parseRuntimeArgs(argc, argv, 2, runtimeOptions, parseError)) {
            if (parseError == "help") {
                printUsage(argv[0]);
                return 0;
            }
            std::cerr << parseError << std::endl;
            printUsage(argv[0]);
            return 1;
        }
        return runtimeOptions.sourceFile.empty() ? runRepl(runtimeOptions)
                                                 : runFile(runtimeOptions);
    }

    std::string parseError;
    if (!parseRuntimeArgs(argc, argv, 1, runtimeOptions, parseError)) {
        if (parseError == "help") {
            printUsage(argv[0]);
            return 0;
        }
        std::cerr << parseError << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    return runtimeOptions.sourceFile.empty() ? runRepl(runtimeOptions)
                                             : runFile(runtimeOptions);
}
