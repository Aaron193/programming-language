#include "Compiler.hpp"

void Compiler::compile(std::string_view source) {
    m_scanner = std::make_unique<Scanner>(source);
}
