#pragma once

#include <string>
#include <vector>

#include "AstFrontend.hpp"
#include "Compiler.hpp"
#include "SyntaxRules.hpp"

class AstBytecodeEmitter {
   public:
    AstBytecodeEmitter(Compiler& compiler, const AstFrontendResult& frontend);

    bool emitModule();

   private:
    Compiler& m_compiler;
    const AstFrontendResult& m_frontend;

    std::string tokenText(const Token& token) const;
    TypeRef resolveTypeExpr(const AstTypeExpr* typeExpr) const;
    size_t safeLine(size_t line) const;
    size_t lastModuleLine() const;
    void errorAtSpan(const SourceSpan& span, const std::string& message);
    void errorAtNode(const AstNodeInfo& node, const std::string& message);
    void errorAtToken(const Token& token, const std::string& message);
    void errorAtLine(size_t line, const std::string& message);
    TypeRef nodeType(AstNodeId id) const;
    TypeRef nodeType(const AstNodeInfo& node) const;
    bool nodeConst(AstNodeId id) const;
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
    void defineVariable(uint8_t global, size_t line);
    void emitVariableRead(const Token& name, size_t line);
    TypeRef lookupClassFieldType(const TypeRef& objectType,
                                 const std::string& propertyName) const;
    TypeRef lookupClassMethodType(const TypeRef& objectType,
                                  const std::string& methodName) const;
    int lookupClassFieldSlot(const TypeRef& objectType,
                             const std::string& fieldName) const;
    const AstImportedModuleInterface* importedModuleForNode(
        AstNodeId nodeId) const;
    void emitExportName(const Token& nameToken, size_t line);
    void emitArguments(const std::vector<AstExprPtr>& arguments, size_t line,
                       uint8_t& argCount);
    void emitClosureObject(const Compiler::CompiledFunction& compiled,
                           size_t line);
    Compiler::CompiledFunction compileFunction(
        const std::string& name, const std::vector<AstParameter>& params,
        const AstNodeInfo& functionNode, const AstStmt* blockBody,
        const AstExpr* expressionBody, bool isMethod);
    void emitFunctionBody(const AstStmt& body);
    void emitFunctionDecl(const AstFunctionDecl& functionDecl);
    void emitClassDecl(const AstClassDecl& classDecl);
    void emitVarDecl(const AstVarDeclStmt& stmt, size_t stmtLine,
                     bool allowExport = true);
    void emitDestructuredImport(const AstDestructuredImportStmt& stmt,
                                size_t line);
    void emitItem(const AstItem& item);
    void emitStmt(const AstStmt& stmt);
    void emitAssignmentToVariable(const AstIdentifierExpr& target,
                                  const Token& op, const AstExpr* valueExpr,
                                  size_t line);
    void emitAssignmentToMember(const AstMemberExpr& target, const Token& op,
                                const AstExpr* valueExpr, size_t line);
    void emitAssignmentToIndex(const AstIndexExpr& target, const Token& op,
                               const AstExpr* valueExpr, size_t line);
    void emitExpr(const AstExpr& expr);
};
