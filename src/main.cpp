#include <fstream>
#include <iostream>
#include <memory>

#include "VirtualMachine.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <source file>" << std::endl;
        return 1;
    }

    std::string source_file = argv[1];

    std::ifstream file(source_file);
    if (!file) {
        std::cerr << "Error: Could not open source file " << source_file
                  << std::endl;
        return 1;
    }

    // read file as string
    auto source =
        std::make_unique<std::string>((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
    file.close();

    VirtualMachine vm;
    Status status = vm.interpret(*source);

    if (status == Status::COMPILATION_ERROR) {
        std::cerr << "Compilation error in source file: " << source_file
                  << std::endl;
        exit(1);
    }
    if (status == Status::RUNTIME_ERROR) {
        std::cerr << "Runtime error in source file: " << source_file
                  << std::endl;
        exit(1);
    }

    return 0;
}
