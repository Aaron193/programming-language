#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AstFrontend.hpp"
#include "Chunk.hpp"
#include "GC.hpp"
#include "NativePackage.hpp"
#include "Token.hpp"
#include "TypeInfo.hpp"

enum class CompilerEmitterMode {
    Auto,
    ForceHir,
};

class Compiler {
   private:
    friend class HirBytecodeEmitter;

    struct Local {
        Token name;
        int depth = 0;
        bool isCaptured = false;
        TypeRef type;
        bool isConst = false;
    };

    struct Upvalue {
        uint8_t index = 0;
        bool isLocal = false;
        TypeRef type;
        bool isConst = false;
    };

    struct ResolvedVariable {
        uint8_t arg = 0;
        uint8_t getOp = 0;
        uint8_t setOp = 0;
        TypeRef type;
        bool isConst = false;
    };

    struct FunctionContext {
        std::vector<Local> locals;
        std::vector<Upvalue> upvalues;
        int scopeDepth = 0;
        bool inFunction = false;
        bool inMethod = false;
        TypeRef returnType;
    };

    struct CompiledFunction {
        FunctionObject* function = nullptr;
        std::vector<Upvalue> upvalues;
        std::vector<TypeRef> parameterTypes;
        TypeRef returnType;
    };

    struct ClassContext {
        bool hasSuperclass = false;
        ClassContext* enclosing = nullptr;
        std::string className;
    };

    Chunk* m_chunk = nullptr;
    ClassContext* m_currentClass = nullptr;
    std::vector<FunctionContext> m_contexts;
    GC* m_gc = nullptr;
    std::unordered_map<std::string, uint8_t> m_globalSlots;
    std::unordered_set<std::string> m_classNames;
    std::unordered_map<std::string, TypeRef> m_typeAliases;
    std::unordered_map<std::string, TypeRef> m_functionSignatures;
    std::unordered_map<std::string, std::unordered_map<std::string, TypeRef>>
        m_classFieldTypes;
    std::unordered_map<std::string, std::unordered_map<std::string, TypeRef>>
        m_classMethodSignatures;
    std::unordered_map<std::string, std::unordered_map<int, std::string>>
        m_classOperatorMethods;
    std::unordered_map<std::string, std::string> m_superclassOf;
    std::vector<TypeRef> m_globalTypes;
    std::vector<bool> m_globalConstness;
    std::vector<TypeRef> m_exprTypeStack;
    std::vector<std::string> m_globalNames;
    std::vector<std::string> m_exportedNames;
    std::vector<std::string> m_packageSearchPaths;
    std::string m_sourcePath;
    AstFrontendResult::Timings m_lastFrontendTimings;
    AstFrontendModuleGraphCache m_frontendModuleGraph;
    CompilerEmitterMode m_emitterMode = CompilerEmitterMode::Auto;
    bool m_hadError = false;
    bool m_panicMode = false;

    Chunk* currentChunk() { return m_chunk; }
    FunctionContext& currentContext() { return m_contexts.back(); }
    const FunctionContext& currentContext() const { return m_contexts.back(); }

    void emitByte(uint8_t byte, size_t line);
    void emitBytes(uint8_t byte1, uint8_t byte2, size_t line);
    Value makeStringValue(const std::string& text);
    uint8_t makeConstant(Value value);
    uint8_t identifierConstant(const Token& name);
    uint8_t globalSlot(const Token& name);
    ResolvedVariable resolveNamedVariable(const Token& name);
    void addLocal(const Token& name,
                  const TypeRef& declaredType = TypeInfo::makeAny(),
                  bool isConst = false);
    int resolveLocal(const Token& name);
    int resolveLocalInContext(const Token& name, int contextIndex);
    int addUpvalue(int contextIndex, uint8_t index, bool isLocal,
                   const TypeRef& type, bool isConst);
    int resolveUpvalue(const Token& name, int contextIndex);
    void markInitialized();
    bool identifiersEqual(const Token& lhs, const Token& rhs) const;
    void errorAt(const Token& token, const std::string& message);
    void errorAtSpan(const SourceSpan& span, const std::string& message);
    void errorAtLine(size_t line, const std::string& message);

    TypeRef tokenToType(const Token& token) const;
    TypeRef lookupClassFieldType(const std::string& className,
                                 const std::string& fieldName) const;
    TypeRef lookupClassMethodType(const std::string& className,
                                  const std::string& methodName) const;
    int lookupClassFieldSlot(const std::string& className,
                             const std::string& fieldName) const;
    uint8_t arithmeticOpcode(TokenType operatorType,
                             const TypeRef& numericType) const;
    void pushCallResultType(const TypeRef& calleeType);
    void pushExprType(const TypeRef& type);
    TypeRef popExprType();
    TypeRef peekExprType() const;

   public:
    Compiler() = default;
    ~Compiler() = default;

    void setGC(GC* gc) { m_gc = gc; }
    void setSourcePath(const std::string& path) { m_sourcePath = path; }
    void setEmitterMode(CompilerEmitterMode mode) { m_emitterMode = mode; }
    void setPackageSearchPaths(std::vector<std::string> packageSearchPaths) {
        m_packageSearchPaths = std::move(packageSearchPaths);
    }
    const std::vector<std::string>& globalNames() const {
        return m_globalNames;
    }
    const std::vector<TypeRef>& globalTypes() const { return m_globalTypes; }
    const std::vector<std::string>& exportedNames() const {
        return m_exportedNames;
    }
    const AstFrontendResult::Timings& lastFrontendTimings() const {
        return m_lastFrontendTimings;
    }
    const std::unordered_map<std::string, TypeRef>& functionSignatures() const {
        return m_functionSignatures;
    }
    const std::unordered_map<std::string,
                             std::unordered_map<std::string, TypeRef>>&
    classFieldTypes() const {
        return m_classFieldTypes;
    }
    const std::unordered_map<std::string,
                             std::unordered_map<std::string, TypeRef>>&
    classMethodSignatures() const {
        return m_classMethodSignatures;
    }

    bool compile(std::string_view source, Chunk& chunk,
                 const std::string& sourcePath = "");
};
