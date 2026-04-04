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
    std::unordered_map<std::string, const AstImportedModuleInterface*>
        m_topLevelImportedModules;

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
        if (!typeExpr.qualifier.empty()) {
            return false;
        }

        const std::string name = tokenText(typeExpr.token);
        return m_classNames.find(name) == m_classNames.end() &&
               m_typeAliases.find(name) == m_typeAliases.end();
    }

    const AstImportedModuleInterface* importedModuleForBinding(
        const AstBindingRef& binding) const {
        if (binding.kind != AstBindingKind::ImportedModule ||
            binding.importExprNodeId == 0) {
            return nullptr;
        }

        const auto it = m_result.importedModules.find(binding.importExprNodeId);
        if (it == m_result.importedModules.end()) {
            return nullptr;
        }

        return &it->second;
    }

    const AstImportedModuleInterface* importedModuleForQualifier(
        std::string_view qualifier) const {
        if (const AstBindingRef* binding = resolveBinding(std::string(qualifier))) {
            if (const AstImportedModuleInterface* imported =
                    importedModuleForBinding(*binding)) {
                return imported;
            }
        }

        const auto topLevelIt = m_topLevelImportedModules.find(
            std::string(qualifier));
        return topLevelIt == m_topLevelImportedModules.end()
                   ? nullptr
                   : topLevelIt->second;
    }

    TypeRef resolveTypeExpr(const AstTypeExpr& typeExpr) const {
        switch (typeExpr.kind) {
            case AstTypeKind::NAMED:
                if (!typeExpr.qualifier.empty()) {
                    const AstImportedModuleInterface* importedModule =
                        importedModuleForQualifier(typeExpr.qualifier);
                    if (importedModule == nullptr) {
                        return nullptr;
                    }
                    const auto exportIt = importedModule->typeExports.find(
                        tokenText(typeExpr.token));
                    return exportIt == importedModule->typeExports.end()
                               ? nullptr
                               : exportIt->second.type;
                }
                return frontendResolveTypeExpr(
                    typeExpr, FrontendTypeContext{m_classNames, m_typeAliases});
            case AstTypeKind::FUNCTION: {
                std::vector<TypeRef> params;
                params.reserve(typeExpr.paramTypes.size());
                for (const auto& paramType : typeExpr.paramTypes) {
                    if (!paramType) {
                        return nullptr;
                    }
                    TypeRef resolved = resolveTypeExpr(*paramType);
                    if (!resolved) {
                        return nullptr;
                    }
                    params.push_back(resolved);
                }
                if (!typeExpr.returnType) {
                    return nullptr;
                }
                TypeRef returnType = resolveTypeExpr(*typeExpr.returnType);
                if (!returnType) {
                    return nullptr;
                }
                return TypeInfo::makeFunction(std::move(params), returnType);
            }
            case AstTypeKind::ARRAY:
                if (!typeExpr.elementType) {
                    return nullptr;
                }
                if (TypeRef elementType = resolveTypeExpr(*typeExpr.elementType)) {
                    return TypeInfo::makeArray(elementType);
                }
                return nullptr;
            case AstTypeKind::DICT:
                if (!typeExpr.keyType || !typeExpr.valueType) {
                    return nullptr;
                }
                {
                    TypeRef keyType = resolveTypeExpr(*typeExpr.keyType);
                    TypeRef valueType = resolveTypeExpr(*typeExpr.valueType);
                    if (!keyType || !valueType) {
                        return nullptr;
                    }
                    return TypeInfo::makeDict(keyType, valueType);
                }
            case AstTypeKind::SET:
                if (!typeExpr.elementType) {
                    return nullptr;
                }
                if (TypeRef elementType = resolveTypeExpr(*typeExpr.elementType)) {
                    return TypeInfo::makeSet(elementType);
                }
                return nullptr;
            case AstTypeKind::OPTIONAL:
                if (!typeExpr.innerType) {
                    return nullptr;
                }
                if (TypeRef innerType = resolveTypeExpr(*typeExpr.innerType)) {
                    return TypeInfo::makeOptional(innerType);
                }
                return nullptr;
            case AstTypeKind::NATIVE_HANDLE:
                return frontendResolveTypeExpr(
                    typeExpr, FrontendTypeContext{m_classNames, m_typeAliases});
        }

        return nullptr;
    }

    void bindTypeExpr(const AstTypeExpr& typeExpr) {
        if (!typeExpr.qualifier.empty()) {
            if (const AstBindingRef* binding =
                    resolveBinding(typeExpr.qualifier)) {
                m_result.references[typeExpr.node.id] = *binding;
            }
        }

        for (const auto& paramType : typeExpr.paramTypes) {
            if (paramType) {
                bindTypeExpr(*paramType);
            }
        }
        if (typeExpr.returnType) {
            bindTypeExpr(*typeExpr.returnType);
        }
        if (typeExpr.elementType) {
            bindTypeExpr(*typeExpr.elementType);
        }
        if (typeExpr.keyType) {
            bindTypeExpr(*typeExpr.keyType);
        }
        if (typeExpr.valueType) {
            bindTypeExpr(*typeExpr.valueType);
        }
        if (typeExpr.innerType) {
            bindTypeExpr(*typeExpr.innerType);
        }
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
        } else {
            out.returnType = TypeInfo::makeVoid();
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

    void predeclareBuiltinFunctions() {
        for (const auto& [name, type] : m_functionSignatures) {
            if (!type || type->kind != TypeKind::FUNCTION) {
                continue;
            }
            defineBinding(name, AstBindingRef{AstBindingKind::Function, 0, name,
                                              false, ""});
        }
    }

    void collectTopLevelImportedModules(const AstModule& module) {
        m_topLevelImportedModules.clear();

        for (const auto& item : module.items) {
            if (!item) {
                continue;
            }

            const auto* stmtPtr = std::get_if<AstStmtPtr>(&item->value);
            if (!stmtPtr || !*stmtPtr) {
                continue;
            }

            const auto* varDecl = std::get_if<AstVarDeclStmt>(&(*stmtPtr)->value);
            if (!varDecl || !varDecl->initializer ||
                !std::holds_alternative<AstImportExpr>(varDecl->initializer->value)) {
                continue;
            }

            const auto importIt =
                m_result.importedModules.find(varDecl->initializer->node.id);
            if (importIt == m_result.importedModules.end()) {
                continue;
            }

            m_topLevelImportedModules[tokenText(varDecl->name)] = &importIt->second;
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
                    if (value.targetType) {
                        bindTypeExpr(*value.targetType);
                    }
                    bindExpr(*value.expression);
                } else if constexpr (std::is_same_v<T, AstFunctionExpr>) {
                    for (const auto& param : value.params) {
                        if (param.type) {
                            bindTypeExpr(*param.type);
                        }
                    }
                    if (value.returnType) {
                        bindTypeExpr(*value.returnType);
                    }
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
        if (stmt.declaredType) {
            bindTypeExpr(*stmt.declaredType);
        }
        if (stmt.initializer) {
            bindExpr(*stmt.initializer);
        }

        AstBindingRef binding{AstBindingKind::Variable, stmtNode.id,
                              tokenText(stmt.name), stmt.isConst, ""};
        if (stmt.initializer &&
            std::holds_alternative<AstImportExpr>(stmt.initializer->value)) {
            binding.kind = AstBindingKind::ImportedModule;
            binding.importExprNodeId = stmt.initializer->node.id;
        }

        defineBinding(tokenText(stmt.name), binding);
    }

    void mergeImportedClassMetadata(const AstImportedModuleInterface& importedModule,
                                    const std::string& className) {
        auto fieldIt = importedModule.metadata.classFieldTypes.find(className);
        if (fieldIt != importedModule.metadata.classFieldTypes.end()) {
            m_result.metadata.classFieldTypes[className] = fieldIt->second;
        }

        auto methodIt =
            importedModule.metadata.classMethodSignatures.find(className);
        if (methodIt != importedModule.metadata.classMethodSignatures.end()) {
            m_result.metadata.classMethodSignatures[className] = methodIt->second;
        }

        auto operatorIt = importedModule.classOperatorMethods.find(className);
        if (operatorIt != importedModule.classOperatorMethods.end()) {
            m_result.classOperatorMethods[className] = operatorIt->second;
        }

        auto superIt = importedModule.metadata.superclassOf.find(className);
        if (superIt != importedModule.metadata.superclassOf.end()) {
            m_result.metadata.superclassOf[className] = superIt->second;
            mergeImportedClassMetadata(importedModule, superIt->second);
        }
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
            AstBindingKind bindingKind = AstBindingKind::Variable;
            std::string className;
            if (importedModule) {
                auto exportIt = importedModule->valueExports.find(exportedName);
                if (exportIt == importedModule->valueExports.end()) {
                    addError(binding.exportedName,
                             "Type error: imported module '" +
                                 importedModule->importTarget.displayName +
                                 "' has no export '" + exportedName + "'.");
                } else if (exportIt->second.type &&
                           exportIt->second.type->kind == TypeKind::CLASS) {
                    bindingKind = AstBindingKind::Class;
                    className = exportIt->second.type->className;
                    mergeImportedClassMetadata(*importedModule, className);
                }
            }

            defineBinding(localName, AstBindingRef{bindingKind, binding.node.id,
                                                   localName, true, className});
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
                    for (const auto& binding : value.bindings) {
                        if (binding.expectedType) {
                            bindTypeExpr(*binding.expectedType);
                        }
                    }
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
                    if (value.declaredType) {
                        bindTypeExpr(*value.declaredType);
                    }
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
        for (const auto& param : functionDecl.params) {
            if (param.type) {
                bindTypeExpr(*param.type);
            }
        }
        if (functionDecl.returnType) {
            bindTypeExpr(*functionDecl.returnType);
        }
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
        for (const auto& field : classDecl.fields) {
            if (field.type) {
                bindTypeExpr(*field.type);
            }
        }
        for (const auto& method : classDecl.methods) {
            for (const auto& param : method.params) {
                if (param.type) {
                    bindTypeExpr(*param.type);
                }
            }
            if (method.returnType) {
                bindTypeExpr(*method.returnType);
            }
        }
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
                    if (value.aliasedType) {
                        bindTypeExpr(*value.aliasedType);
                    }
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
        predeclareBuiltinFunctions();
        predeclareTopLevel(module);
        collectTopLevelImportedModules(module);
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
