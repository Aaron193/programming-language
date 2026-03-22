#include "AstBinder.hpp"

#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "FrontendTypeUtils.hpp"
#include "SyntaxRules.hpp"
#include "Token.hpp"

namespace {

class AstBinderImpl {
   private:
    struct ResolvedCallableSignature {
        std::vector<TypeRef> paramTypes;
        TypeRef returnType = TypeInfo::makeAny();
    };

    const std::unordered_set<std::string>& m_classNames;
    const std::unordered_map<std::string, TypeRef>& m_typeAliases;
    const std::unordered_map<std::string, TypeRef>& m_functionSignatures;
    std::vector<TypeError>& m_errors;
    AstBindResult& m_result;
    std::vector<std::unordered_map<std::string, AstBindingRef>> m_scopes;
    std::vector<std::string> m_classContexts;

    std::string tokenText(const Token& token) const { return tokenLexeme(token); }

    void addError(const SourceSpan& span, const std::string& message) {
        m_errors.push_back(TypeError{span, message});
    }

    void addError(const AstNodeInfo& node, const std::string& message) {
        addError(node.span, message);
    }

    void addError(const Token& token, const std::string& message) {
        addError(token.span(), message);
    }

    void beginScope() { m_scopes.emplace_back(); }

    void endScope() {
        if (m_scopes.size() > 1) {
            m_scopes.pop_back();
        }
    }

    void defineBinding(const std::string& name, const AstBindingRef& binding) {
        m_scopes.back()[name] = binding;
    }

    const AstBindingRef* resolveBinding(const std::string& name) const {
        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                return &found->second;
            }
        }

        return nullptr;
    }

    bool isOmittedNamedType(const AstTypeExpr& typeExpr) const {
        if (typeExpr.kind != AstTypeKind::NAMED ||
            typeExpr.token.type() != TokenType::IDENTIFIER) {
            return false;
        }

        const std::string name = tokenText(typeExpr.token);
        return m_classNames.find(name) == m_classNames.end() &&
               m_typeAliases.find(name) == m_typeAliases.end();
    }

    TypeRef resolveTypeExpr(const AstTypeExpr& typeExpr) const {
        return frontendResolveTypeExpr(
            typeExpr, FrontendTypeContext{m_classNames, m_typeAliases});
    }

    TypeRef requireTypeExpr(const AstTypeExpr* typeExpr, const SourceSpan& span,
                            const std::string& message) {
        if (!typeExpr) {
            addError(span, message);
            return nullptr;
        }

        TypeRef resolved = resolveTypeExpr(*typeExpr);
        if (!resolved) {
            addError(span, message);
        }
        return resolved;
    }

    ResolvedCallableSignature resolveMethodSignature(
        const std::string& functionName, const std::vector<AstParameter>& params,
        const AstTypeExpr* returnTypeExpr, const SourceSpan& fallbackSpan) {
        ResolvedCallableSignature out;

        for (const auto& param : params) {
            const std::string paramName = tokenText(param.name);
            TypeRef paramType = nullptr;
            if (!param.type || isOmittedNamedType(*param.type)) {
                addError(param.name.span(),
                         "Type error: parameter '" + paramName +
                             "' must have a type annotation.");
                paramType = TypeInfo::makeAny();
            } else {
                paramType = resolveTypeExpr(*param.type);
                if (!paramType) {
                    addError(param.type->node.span,
                             "Type error: expected parameter type annotation.");
                    paramType = TypeInfo::makeAny();
                }
            }

            if (paramType && paramType->isVoid()) {
                addError(param.name.span(),
                         "Type error: parameter '" + paramName +
                             "' cannot have type 'void'.");
            }

            out.paramTypes.push_back(paramType ? paramType : TypeInfo::makeAny());
        }

        const bool treatReturnAsOmitted =
            returnTypeExpr == nullptr ||
            (returnTypeExpr != nullptr && isOmittedNamedType(*returnTypeExpr));
        if (!treatReturnAsOmitted) {
            TypeRef resolved = resolveTypeExpr(*returnTypeExpr);
            if (!resolved) {
                addError(returnTypeExpr->node,
                         "Type error: expected return type after parameter list.");
            } else {
                out.returnType = resolved;
            }
        }

        return out;
    }

    void predeclareTopLevel(const AstModule& module) {
        for (const auto& item : module.items) {
            if (!item) {
                continue;
            }

            if (const auto* functionDecl =
                    std::get_if<AstFunctionDecl>(&item->value)) {
                defineBinding(tokenText(functionDecl->name),
                              AstBindingRef{AstBindingKind::Function,
                                            functionDecl->node.id,
                                            tokenText(functionDecl->name), false,
                                            ""});
            } else if (const auto* classDecl =
                           std::get_if<AstClassDecl>(&item->value)) {
                defineBinding(tokenText(classDecl->name),
                              AstBindingRef{AstBindingKind::Class,
                                            classDecl->node.id,
                                            tokenText(classDecl->name), false,
                                            tokenText(classDecl->name)});
            }
        }
    }

    void predeclareClassMetadata(const AstModule& module) {
        for (const auto& item : module.items) {
            if (!item) {
                continue;
            }

            const auto* classDecl = std::get_if<AstClassDecl>(&item->value);
            if (!classDecl) {
                continue;
            }

            const std::string className = tokenText(classDecl->name);
            if (classDecl->superclass.has_value()) {
                const std::string superclassName =
                    tokenText(*classDecl->superclass);
                if (superclassName == className) {
                    addError(*classDecl->superclass,
                             "Type error: a type cannot inherit from itself.");
                }
                if (m_classNames.find(superclassName) == m_classNames.end()) {
                    addError(*classDecl->superclass,
                             "Type error: unknown superclass '" +
                                 superclassName + "'.");
                }
                m_result.metadata.superclassOf[className] = superclassName;
            }

            for (const auto& field : classDecl->fields) {
                TypeRef fieldType = requireTypeExpr(
                    field.type.get(), field.name.span(),
                    "Type error: expected field type after member name.");
                if (!fieldType) {
                    continue;
                }
                if (fieldType->isVoid()) {
                    addError(field.name,
                             "Type error: struct field '" +
                                 tokenText(field.name) +
                                 "' cannot have type 'void'.");
                }
                m_result.metadata.classFieldTypes[className][tokenText(field.name)] =
                    fieldType;
            }

            for (const auto& method : classDecl->methods) {
                ResolvedCallableSignature signature = resolveMethodSignature(
                    tokenText(method.name), method.params, method.returnType.get(),
                    method.name.span());
                m_result.metadata.classMethodSignatures[className]
                                                   [tokenText(method.name)] =
                    TypeInfo::makeFunction(signature.paramTypes,
                                           signature.returnType);
                for (int op : method.annotatedOperators) {
                    m_result.classOperatorMethods[className][op] =
                        tokenText(method.name);
                }
            }
        }
    }

    void bindFunctionBody(const AstStmt& body) {
        if (const auto* block = std::get_if<AstBlockStmt>(&body.value)) {
            for (const auto& item : block->items) {
                if (item) {
                    bindItem(*item);
                }
            }
            return;
        }

        bindStmt(body);
    }

    void bindExpr(const AstExpr& expr) {
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, AstLiteralExpr>) {
                    return;
                } else if constexpr (std::is_same_v<T, AstIdentifierExpr>) {
                    if (const AstBindingRef* binding =
                            resolveBinding(tokenText(value.name))) {
                        m_result.references[expr.node.id] = *binding;
                    }
                } else if constexpr (std::is_same_v<T, AstGroupingExpr>) {
                    bindExpr(*value.expression);
                } else if constexpr (std::is_same_v<T, AstUnaryExpr>) {
                    bindExpr(*value.operand);
                } else if constexpr (std::is_same_v<T, AstUpdateExpr>) {
                    bindExpr(*value.operand);
                } else if constexpr (std::is_same_v<T, AstBinaryExpr>) {
                    bindExpr(*value.left);
                    bindExpr(*value.right);
                } else if constexpr (std::is_same_v<T, AstAssignmentExpr>) {
                    bindExpr(*value.target);
                    bindExpr(*value.value);
                } else if constexpr (std::is_same_v<T, AstCallExpr>) {
                    bindExpr(*value.callee);
                    for (const auto& arg : value.arguments) {
                        bindExpr(*arg);
                    }
                } else if constexpr (std::is_same_v<T, AstMemberExpr>) {
                    bindExpr(*value.object);
                } else if constexpr (std::is_same_v<T, AstIndexExpr>) {
                    bindExpr(*value.object);
                    bindExpr(*value.index);
                } else if constexpr (std::is_same_v<T, AstCastExpr>) {
                    bindExpr(*value.expression);
                } else if constexpr (std::is_same_v<T, AstFunctionExpr>) {
                    beginScope();
                    for (const auto& param : value.params) {
                        defineBinding(tokenText(param.name),
                                      AstBindingRef{AstBindingKind::Variable,
                                                    param.node.id,
                                                    tokenText(param.name),
                                                    false, ""});
                    }
                    if (value.expressionBody) {
                        bindExpr(*value.expressionBody);
                    }
                    if (value.blockBody) {
                        bindFunctionBody(*value.blockBody);
                    }
                    endScope();
                } else if constexpr (std::is_same_v<T, AstImportExpr>) {
                    return;
                } else if constexpr (std::is_same_v<T, AstThisExpr>) {
                    if (!m_classContexts.empty()) {
                        m_result.references[expr.node.id] =
                            AstBindingRef{AstBindingKind::ThisValue, 0, "this",
                                          false, m_classContexts.back()};
                    }
                } else if constexpr (std::is_same_v<T, AstSuperExpr>) {
                    if (!m_classContexts.empty()) {
                        m_result.references[expr.node.id] =
                            AstBindingRef{AstBindingKind::SuperValue, 0, "super",
                                          false, m_classContexts.back()};
                    }
                } else if constexpr (std::is_same_v<T, AstArrayLiteralExpr>) {
                    for (const auto& element : value.elements) {
                        bindExpr(*element);
                    }
                } else if constexpr (std::is_same_v<T, AstDictLiteralExpr>) {
                    for (const auto& entry : value.entries) {
                        bindExpr(*entry.key);
                        bindExpr(*entry.value);
                    }
                }
            },
            expr.value);
    }

    void bindVarDecl(const AstNodeInfo& stmtNode, const AstVarDeclStmt& stmt) {
        if (stmt.initializer) {
            bindExpr(*stmt.initializer);
        }

        defineBinding(tokenText(stmt.name),
                      AstBindingRef{AstBindingKind::Variable, stmtNode.id,
                                    tokenText(stmt.name), stmt.isConst, ""});
    }

    void bindDestructuredImport(const AstDestructuredImportStmt& stmt) {
        if (stmt.initializer) {
            bindExpr(*stmt.initializer);
        }

        const AstImportedModuleInterface* importedModule =
            stmt.initializer
                ? [&]() -> const AstImportedModuleInterface* {
                      auto it = m_result.importedModules.find(
                          stmt.initializer->node.id);
                      if (it == m_result.importedModules.end()) {
                          return nullptr;
                      }
                      return &it->second;
                  }()
                : nullptr;

        for (const auto& binding : stmt.bindings) {
            const std::string exportedName = tokenText(binding.exportedName);
            const std::string localName =
                binding.localName.has_value() ? tokenText(*binding.localName)
                                              : exportedName;
            if (importedModule) {
                auto exportIt = importedModule->exportTypes.find(exportedName);
                if (exportIt == importedModule->exportTypes.end()) {
                    addError(binding.exportedName,
                             "Type error: imported module '" +
                                 importedModule->importTarget.displayName +
                                 "' has no export '" + exportedName + "'.");
                }
            }

            defineBinding(localName,
                          AstBindingRef{AstBindingKind::Variable, binding.node.id,
                                        localName, true, ""});
        }
    }

    void bindStmt(const AstStmt& stmt) {
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, AstBlockStmt>) {
                    beginScope();
                    for (const auto& item : value.items) {
                        if (item) {
                            bindItem(*item);
                        }
                    }
                    endScope();
                } else if constexpr (std::is_same_v<T, AstExprStmt>) {
                    bindExpr(*value.expression);
                } else if constexpr (std::is_same_v<T, AstPrintStmt>) {
                    bindExpr(*value.expression);
                } else if constexpr (std::is_same_v<T, AstReturnStmt>) {
                    if (value.value) {
                        bindExpr(*value.value);
                    }
                } else if constexpr (std::is_same_v<T, AstIfStmt>) {
                    bindExpr(*value.condition);
                    bindStmt(*value.thenBranch);
                    if (value.elseBranch) {
                        bindStmt(*value.elseBranch);
                    }
                } else if constexpr (std::is_same_v<T, AstWhileStmt>) {
                    bindExpr(*value.condition);
                    bindStmt(*value.body);
                } else if constexpr (std::is_same_v<T, AstVarDeclStmt>) {
                    bindVarDecl(stmt.node, value);
                } else if constexpr (std::is_same_v<T,
                                                    AstDestructuredImportStmt>) {
                    bindDestructuredImport(value);
                } else if constexpr (std::is_same_v<T, AstForStmt>) {
                    beginScope();
                    if (const auto* initDecl =
                            std::get_if<std::unique_ptr<AstVarDeclStmt>>(
                                &value.initializer)) {
                        if (*initDecl) {
                            bindVarDecl((*initDecl)->node, **initDecl);
                        }
                    } else if (const auto* initExpr =
                                   std::get_if<AstExprPtr>(&value.initializer)) {
                        if (*initExpr) {
                            bindExpr(**initExpr);
                        }
                    }

                    if (value.condition) {
                        bindExpr(*value.condition);
                    }
                    if (value.increment) {
                        bindExpr(*value.increment);
                    }
                    bindStmt(*value.body);
                    endScope();
                } else if constexpr (std::is_same_v<T, AstForEachStmt>) {
                    beginScope();
                    bindExpr(*value.iterable);
                    defineBinding(tokenText(value.name),
                                  AstBindingRef{AstBindingKind::Variable,
                                                stmt.node.id,
                                                tokenText(value.name),
                                                value.isConst, ""});
                    bindStmt(*value.body);
                    endScope();
                }
            },
            stmt.value);
    }

    void bindFunctionDecl(const AstFunctionDecl& functionDecl) {
        defineBinding(tokenText(functionDecl.name),
                      AstBindingRef{AstBindingKind::Function,
                                    functionDecl.node.id,
                                    tokenText(functionDecl.name), false, ""});

        beginScope();
        for (const auto& param : functionDecl.params) {
            defineBinding(tokenText(param.name),
                          AstBindingRef{AstBindingKind::Variable, param.node.id,
                                        tokenText(param.name), false, ""});
        }
        if (functionDecl.body) {
            bindFunctionBody(*functionDecl.body);
        }
        endScope();
    }

    void bindClassDecl(const AstClassDecl& classDecl) {
        const std::string className = tokenText(classDecl.name);
        defineBinding(className,
                      AstBindingRef{AstBindingKind::Class, classDecl.node.id,
                                    className, false, className});

        m_classContexts.push_back(className);
        for (const auto& method : classDecl.methods) {
            beginScope();
            for (const auto& param : method.params) {
                defineBinding(tokenText(param.name),
                              AstBindingRef{AstBindingKind::Variable,
                                            param.node.id,
                                            tokenText(param.name), false, ""});
            }
            if (method.body) {
                bindFunctionBody(*method.body);
            }
            endScope();
        }
        m_classContexts.pop_back();
    }

    void bindItem(const AstItem& item) {
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, AstTypeAliasDecl>) {
                    return;
                } else if constexpr (std::is_same_v<T, AstClassDecl>) {
                    bindClassDecl(value);
                } else if constexpr (std::is_same_v<T, AstFunctionDecl>) {
                    bindFunctionDecl(value);
                } else if constexpr (std::is_same_v<T, AstStmtPtr>) {
                    if (value) {
                        bindStmt(*value);
                    }
                }
            },
            item.value);
    }

   public:
    AstBinderImpl(
        const std::unordered_set<std::string>& classNames,
        const std::unordered_map<std::string, TypeRef>& typeAliases,
        const std::unordered_map<std::string, TypeRef>& functionSignatures,
        const std::unordered_map<AstNodeId, AstImportedModuleInterface>&
            importedModules,
        std::vector<TypeError>& errors, AstBindResult& result)
        : m_classNames(classNames),
          m_typeAliases(typeAliases),
          m_functionSignatures(functionSignatures),
          m_errors(errors),
          m_result(result) {
        m_scopes.emplace_back();
        m_result.importedModules = importedModules;
    }

    void run(const AstModule& module) {
        predeclareTopLevel(module);
        predeclareClassMetadata(module);

        for (const auto& item : module.items) {
            if (item) {
                bindItem(*item);
            }
        }
    }
};

}  // namespace

bool bindAst(
    const AstModule& module,
    const std::unordered_set<std::string>& classNames,
    const std::unordered_map<std::string, TypeRef>& typeAliases,
    const std::unordered_map<std::string, TypeRef>& functionSignatures,
    const std::unordered_map<AstNodeId, AstImportedModuleInterface>&
        importedModules,
    std::vector<TypeError>& out,
    AstBindResult* outBindings) {
    AstBindResult bindings;
    AstBinderImpl binder(classNames, typeAliases, functionSignatures,
                         importedModules, out, bindings);
    binder.run(module);
    if (outBindings) {
        *outBindings = std::move(bindings);
    }
    return out.empty();
}
