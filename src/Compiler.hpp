#pragma once
#include <memory>
#include <string_view>

#include "Scanner.hpp"

class Compiler {
   private:
    std::unique_ptr<Scanner> scanner;

   public:
    Compiler() = default;
    ~Compiler() = default;

    void compile(std::string_view source);
};
