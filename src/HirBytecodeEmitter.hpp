#pragma once

#include <string>
#include <vector>

#include "Hir.hpp"
#include "Compiler.hpp"
#include "SyntaxRules.hpp"

class HirBytecodeEmitter {
   public:
    HirBytecodeEmitter(Compiler& compiler, const HirModule& module,
                       size_t terminalLine);

    bool emitModule();

   private:
    struct LoopControlContext {
        std::optional<Token> label;
        int continueTarget = 0;
        int scopeDepth = 0;
        std::vector<int> breakJumps;
    };

    Compiler& m_compiler;
    const HirModule& m_module;
    size_t m_terminalLine = 1;
    std::vector<LoopControlContext> m_loopContexts;

    std::string tokenText(const Token& token) const;
    size_t safeLine(size_t line) const;
    size_t lastModuleLine() const;
    void errorAtSpan(const SourceSpan& span, const std::string& message);
    void errorAtNode(const HirNodeInfo& node, const std::string& message);
    void errorAtToken(const Token& token, const std::string& message);
    void errorAtLine(size_t line, const std::string& message);
    TypeRef nodeType(const HirNodeInfo& node) const;
    void emitByte(uint8_t byte, size_t line);
    void emitBytes(uint8_t byte1, uint8_t byte2, size_t line);
    void emitConstant(Value value, size_t line);
    int emitJump(uint8_t instruction, size_t line);
    void patchJump(int offset);
    void emitLoop(int loopStart, size_t line);
    void emitReturn(size_t line);
    void emitCoerceToType(const TypeRef& sourceType, const TypeRef& targetType,
                          size_t line);
    void emitCheckInstanceType(const TypeRef& targetType, size_t line);
    bool emitCompoundBinary(TokenType assignmentType, const TypeRef& leftType,
                            const TypeRef& rightType, size_t line);
    void beginScope();
    void endScope(size_t line);
    void emitScopeExitToDepth(int targetDepth, size_t line);
    void defineVariable(uint8_t global, size_t line);
    void emitVariableRead(const Token& name, size_t line);
    TypeRef lookupClassFieldType(const TypeRef& objectType,
                                 const std::string& propertyName) const;
    TypeRef lookupClassMethodType(const TypeRef& objectType,
                                  const std::string& methodName) const;
    int lookupClassFieldSlot(const TypeRef& objectType,
                             const std::string& fieldName) const;
    void emitExportName(const Token& nameToken, size_t line);
    const HirExpr* exprPtr(const std::optional<HirExprId>& id) const;
    const HirStmt* stmtPtr(const std::optional<HirStmtId>& id) const;
    void emitArguments(const std::vector<HirExprId>& arguments, size_t line,
                       uint8_t& argCount);
    void emitClosureObject(const Compiler::CompiledFunction& compiled,
                           size_t line);
    Compiler::CompiledFunction compileFunction(
        const std::string& name, const std::vector<HirParameter>& params,
        const HirNodeInfo& functionNode, const HirStmt* blockBody,
        const HirExpr* expressionBody, bool isMethod);
    void emitFunctionBody(const HirStmt& body);
    void emitFunctionDecl(const HirFunctionDecl& functionDecl);
    void emitClassDecl(const HirClassDecl& classDecl);
    void emitVarDecl(const HirVarDeclStmt& stmt, size_t stmtLine,
                     bool allowExport = true);
    void emitDestructuredImport(const HirDestructuredImportStmt& stmt,
                                size_t line);
    void emitItem(const HirItem& item);
    LoopControlContext* resolveLoopContext(const std::optional<Token>& label);
    void emitStmt(const HirStmt& stmt);
    void emitAssignmentToVariable(const HirBindingExpr& target,
                                  const Token& op, const HirExpr* valueExpr,
                                  size_t line);
    void emitAssignmentToMember(const HirMemberExpr& target, const Token& op,
                                const HirExpr* valueExpr, size_t line);
    void emitAssignmentToIndex(const HirIndexExpr& target, const Token& op,
                               const HirExpr* valueExpr, size_t line);
    void emitExpr(const HirExpr& expr);
};
