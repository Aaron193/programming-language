#include "Compiler.hpp"

void Compiler::compile(std::string_view source) {
    this->scanner = std::make_unique<Scanner>(source);
}
