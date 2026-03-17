#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Compiler.hpp"
#include "GC.hpp"

namespace {

bool hasStrictDirective(std::string_view source) {
    return source.rfind("#!strict", 0) == 0;
}

std::string stripStrictDirectiveLine(std::string_view source) {
    if (!hasStrictDirective(source)) {
        return std::string(source);
    }

    size_t newlinePos = source.find('\n');
    if (newlinePos == std::string_view::npos) {
        return std::string();
    }

    return std::string(source.substr(newlinePos + 1));
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        return {};
    }

    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

std::string captureChunkDisassembly(const Chunk& chunk) {
    std::ostringstream output;
    std::streambuf* original = std::cout.rdbuf(output.rdbuf());
    Chunk& mutableChunk = const_cast<Chunk&>(chunk);

    int offset = 0;
    while (offset < chunk.count()) {
        offset = mutableChunk.disassembleInstruction(offset);
    }

    std::cout.rdbuf(original);

    std::istringstream input(output.str());
    std::ostringstream filtered;
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("LINE: ", 0) == 0) {
            continue;
        }
        filtered << line << '\n';
    }

    return filtered.str();
}

void appendChunkTree(std::ostringstream& out, const Chunk& chunk,
                     const std::string& label,
                     std::unordered_set<const Chunk*>& visited) {
    if (!visited.insert(&chunk).second) {
        return;
    }

    out << "== " << label << " ==\n";
    out << captureChunkDisassembly(chunk);

    const auto& constants = chunk.getConstantsRange();
    for (const Value& constant : constants) {
        if (!constant.isFunction()) {
            continue;
        }

        FunctionObject* function = constant.asFunction();
        if (function == nullptr || !function->chunk) {
            continue;
        }

        const std::string functionLabel =
            function->name.empty() ? "<closure>" : function->name;
        appendChunkTree(out, *function->chunk, "fn " + functionLabel, visited);
    }
}

std::string canonicalizeChunkTree(const Chunk& chunk) {
    std::ostringstream out;
    std::unordered_set<const Chunk*> visited;
    appendChunkTree(out, chunk, "<module>", visited);
    return out.str();
}

bool compileWithMode(const std::filesystem::path& path, CompilerEmitterMode mode,
                     GC& gc, const std::vector<std::string>& packagePaths,
                     Chunk& outChunk, std::string& outCanonical) {
    const std::string source = readFile(path);
    if (source.empty() && std::filesystem::file_size(path) != 0) {
        return false;
    }

    Compiler compiler;
    compiler.setGC(&gc);
    compiler.setPackageSearchPaths(packagePaths);
    compiler.setEmitterMode(mode);
    compiler.setStrictMode(hasStrictDirective(source));

    const std::string strippedSource = stripStrictDirectiveLine(source);
    if (!compiler.compile(strippedSource, outChunk, path.string())) {
        return false;
    }

    outCanonical = canonicalizeChunkTree(outChunk);
    return true;
}

std::vector<std::filesystem::path> defaultCorpus(
    const std::filesystem::path& repoRoot) {
    return {
        repoRoot / "tests/sample_var.mog",
        repoRoot / "tests/sample_const.mog",
        repoRoot / "tests/sample_closure_capture.mog",
        repoRoot / "tests/sample_closure_mutation.mog",
        repoRoot / "tests/sample_class.mog",
        repoRoot / "tests/sample_invoke_fusion.mog",
        repoRoot / "tests/sample_for_each.mog",
        repoRoot / "tests/sample_import_basic.mog",
        repoRoot / "tests/sample_import_named.mog",
        repoRoot / "tests/sample_import_class.mog",
        repoRoot / "tests/sample_import_nested.mog",
        repoRoot / "tests/sample_runtime_stacktrace.mog",
        repoRoot / "tests/sample_strict_loop_optimizations.mog",
    };
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path repoRoot = std::filesystem::current_path();

    std::vector<std::filesystem::path> corpus;
    if (argc > 1) {
        for (int index = 1; index < argc; ++index) {
            corpus.emplace_back(argv[index]);
        }
    } else {
        corpus = defaultCorpus(repoRoot);
    }

    std::vector<std::string> packagePaths;
    packagePaths.push_back((repoRoot / "build/packages").string());

    size_t passed = 0;
    for (const auto& file : corpus) {
        std::filesystem::path canonicalPath;
        try {
            canonicalPath = std::filesystem::weakly_canonical(file);
        } catch (const std::exception&) {
            canonicalPath = file;
        }

        if (!std::filesystem::exists(canonicalPath)) {
            std::cerr << "missing corpus file: " << canonicalPath << '\n';
            return 1;
        }

        GC astGc;
        Chunk astChunk;
        std::string astCanonical;
        if (!compileWithMode(canonicalPath, CompilerEmitterMode::ForceAst, astGc,
                             packagePaths, astChunk, astCanonical)) {
            std::cerr << "AST compile failed for " << canonicalPath << '\n';
            return 1;
        }

        GC legacyGc;
        Chunk legacyChunk;
        std::string legacyCanonical;
        if (!compileWithMode(canonicalPath, CompilerEmitterMode::ForceLegacy,
                             legacyGc, packagePaths, legacyChunk,
                             legacyCanonical)) {
            std::cerr << "Legacy compile failed for " << canonicalPath << '\n';
            return 1;
        }

        if (astCanonical != legacyCanonical) {
            std::cerr << "Emitter parity mismatch: " << canonicalPath << "\n\n";
            std::cerr << "--- AST ---\n" << astCanonical;
            std::cerr << "--- Legacy ---\n" << legacyCanonical;
            return 1;
        }

        ++passed;
    }

    std::cout << "[PASS] AST/legacy emitter parity corpus matched for "
              << passed << " file(s)." << std::endl;
    return 0;
}
