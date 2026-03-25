#include "AstToHirLowering.hpp"

#include <type_traits>
#include <utility>
#include <variant>

#include "FrontendTypeUtils.hpp"

namespace {

class AstToHirLowerer {
   public:
    explicit AstToHirLowerer(const AstFrontendResult& frontend)
        : m_frontend(frontend) {}

    bool run(HirModule& outModule) {
        outModule.clear();
        m_outModule = &outModule;
        for (const auto& item : m_frontend.module.items) {
            if (!item) {
                continue;
            }
            outModule.items.push_back(lowerItem(*item));
        }
        return true;
    }

   private:
    const AstFrontendResult& m_frontend;
    HirModule* m_outModule = nullptr;
    HirNodeId m_nextNodeId = 1;

    TypeRef nodeType(AstNodeId nodeId) const {
        auto it = m_frontend.semanticModel.nodeTypes.find(nodeId);
        if (it == m_frontend.semanticModel.nodeTypes.end() || !it->second) {
            return TypeInfo::makeAny();
        }

        return it->second;
    }

    bool nodeConst(AstNodeId nodeId) const {
        auto it = m_frontend.semanticModel.nodeConstness.find(nodeId);
        return it != m_frontend.semanticModel.nodeConstness.end() && it->second;
    }

    AstBindingRef bindingFor(AstNodeId nodeId) const {
        auto it = m_frontend.bindings.references.find(nodeId);
        if (it == m_frontend.bindings.references.end()) {
            return AstBindingRef{};
        }

        return it->second;
    }

    const AstImportedModuleInterface* importedModuleFor(AstNodeId nodeId) const {
        auto it = m_frontend.bindings.importedModules.find(nodeId);
        if (it == m_frontend.bindings.importedModules.end()) {
            return nullptr;
        }

        return &it->second;
    }

    HirNodeInfo makeNodeInfo(const AstNodeInfo& node) {
        HirNodeInfo info;
        info.id = m_nextNodeId++;
        info.astNodeId = node.id;
        info.line = node.line;
        info.span = node.span;
        info.type = nodeType(node.id);
        info.isConst = nodeConst(node.id);
        return info;
    }

    TypeRef resolveDeclaredType(const AstTypeExpr* typeExpr) const {
        if (typeExpr == nullptr) {
            return TypeInfo::makeAny();
        }

        TypeRef resolved = frontendResolveTypeExpr(
            *typeExpr, FrontendTypeContext{m_frontend.classNames,
                                           m_frontend.typeAliases});
        return resolved ? resolved : TypeInfo::makeAny();
    }

    TypeRef functionReturnType(const TypeRef& type) const {
        if (!type || type->kind != TypeKind::FUNCTION || !type->returnType) {
            return TypeInfo::makeAny();
        }

        return type->returnType;
    }

    std::vector<TypeRef> functionParamTypes(const TypeRef& type) const {
        if (!type || type->kind != TypeKind::FUNCTION) {
            return {};
        }

        return type->paramTypes;
    }

    HirParameter lowerParameter(const AstParameter& param) {
        HirParameter lowered;
        lowered.node = makeNodeInfo(param.node);
        lowered.name = param.name;
        lowered.type = nodeType(param.node.id);
        return lowered;
    }

    HirExprId storeExpr(HirExpr expr) {
        return m_outModule->addExpr(std::move(expr));
    }

    HirStmtId storeStmt(HirStmt stmt) {
        return m_outModule->addStmt(std::move(stmt));
    }

    HirItemId storeItem(HirItem item) {
        return m_outModule->addItem(std::move(item));
    }

    HirVarDeclStmt lowerVarDecl(const AstVarDeclStmt& stmt) {
        HirVarDeclStmt lowered;
        lowered.node = makeNodeInfo(stmt.node);
        lowered.isConst = stmt.isConst;
        lowered.name = stmt.name;
        lowered.declaredType = stmt.omittedType
                                   ? TypeInfo::makeAny()
                                   : resolveDeclaredType(stmt.declaredType.get());
        if (stmt.initializer) {
            lowered.initializer = lowerExpr(*stmt.initializer);
        }
        lowered.omittedType = stmt.omittedType;
        return lowered;
    }

    HirExprId lowerExpr(const AstExpr& expr) {
        HirExpr lowered;
        lowered.node = makeNodeInfo(expr.node);

        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, AstLiteralExpr>) {
                    lowered.value = HirLiteralExpr{value.token};
                } else if constexpr (std::is_same_v<T, AstIdentifierExpr>) {
                    lowered.value =
                        HirBindingExpr{value.name, bindingFor(expr.node.id)};
                } else if constexpr (std::is_same_v<T, AstGroupingExpr>) {
                    if (value.expression) {
                        lowered.value =
                            m_outModule->expr(lowerExpr(*value.expression)).value;
                    } else {
                        lowered.value = HirLiteralExpr{};
                    }
                } else if constexpr (std::is_same_v<T, AstUnaryExpr>) {
                    lowered.value =
                        HirUnaryExpr{value.op, lowerExpr(*value.operand)};
                } else if constexpr (std::is_same_v<T, AstUpdateExpr>) {
                    lowered.value = HirUpdateExpr{
                        value.op, lowerExpr(*value.operand), value.isPrefix};
                } else if constexpr (std::is_same_v<T, AstBinaryExpr>) {
                    lowered.value = HirBinaryExpr{
                        lowerExpr(*value.left), value.op, lowerExpr(*value.right)};
                } else if constexpr (std::is_same_v<T, AstAssignmentExpr>) {
                    lowered.value = HirAssignmentExpr{lowerExpr(*value.target),
                                                      value.op,
                                                      lowerExpr(*value.value)};
                } else if constexpr (std::is_same_v<T, AstCallExpr>) {
                    HirCallExpr call;
                    call.callee = lowerExpr(*value.callee);
                    for (const auto& argument : value.arguments) {
                        call.arguments.push_back(lowerExpr(*argument));
                    }
                    lowered.value = std::move(call);
                } else if constexpr (std::is_same_v<T, AstMemberExpr>) {
                    lowered.value =
                        HirMemberExpr{lowerExpr(*value.object), value.member};
                } else if constexpr (std::is_same_v<T, AstIndexExpr>) {
                    lowered.value = HirIndexExpr{lowerExpr(*value.object),
                                                 lowerExpr(*value.index)};
                } else if constexpr (std::is_same_v<T, AstCastExpr>) {
                    lowered.value = HirCastExpr{lowerExpr(*value.expression),
                                                lowered.node.type};
                } else if constexpr (std::is_same_v<T, AstFunctionExpr>) {
                    HirFunctionExpr function;
                    for (const auto& param : value.params) {
                        function.params.push_back(lowerParameter(param));
                    }
                    function.returnType = lowered.node.type
                                              ? functionReturnType(lowered.node.type)
                                              : TypeInfo::makeAny();
                    if (value.blockBody) {
                        function.blockBody = lowerStmt(*value.blockBody);
                    }
                    if (value.expressionBody) {
                        function.expressionBody = lowerExpr(*value.expressionBody);
                    }
                    function.usesFatArrow = value.usesFatArrow;
                    lowered.value = std::move(function);
                } else if constexpr (std::is_same_v<T, AstImportExpr>) {
                    HirImportExpr importExpr;
                    importExpr.path = value.path;
                    if (const auto* imported = importedModuleFor(expr.node.id)) {
                        importExpr.importedModule = *imported;
                    }
                    lowered.value = std::move(importExpr);
                } else if constexpr (std::is_same_v<T, AstThisExpr>) {
                    lowered.value =
                        HirThisExpr{value.token, bindingFor(expr.node.id)};
                } else if constexpr (std::is_same_v<T, AstSuperExpr>) {
                    lowered.value =
                        HirSuperExpr{value.token, bindingFor(expr.node.id)};
                } else if constexpr (std::is_same_v<T, AstArrayLiteralExpr>) {
                    HirArrayLiteralExpr array;
                    for (const auto& element : value.elements) {
                        array.elements.push_back(lowerExpr(*element));
                    }
                    lowered.value = std::move(array);
                } else if constexpr (std::is_same_v<T, AstDictLiteralExpr>) {
                    HirDictLiteralExpr dict;
                    for (const auto& entry : value.entries) {
                        HirDictEntry loweredEntry;
                        loweredEntry.key = lowerExpr(*entry.key);
                        loweredEntry.value = lowerExpr(*entry.value);
                        dict.entries.push_back(std::move(loweredEntry));
                    }
                    lowered.value = std::move(dict);
                }
            },
            expr.value);

        return storeExpr(std::move(lowered));
    }

    HirStmtId lowerStmt(const AstStmt& stmt) {
        HirStmt lowered;
        lowered.node = makeNodeInfo(stmt.node);

        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, AstBlockStmt>) {
                    HirBlockStmt block;
                    for (const auto& item : value.items) {
                        if (item) {
                            block.items.push_back(lowerItem(*item));
                        }
                    }
                    lowered.value = std::move(block);
                } else if constexpr (std::is_same_v<T, AstExprStmt>) {
                    HirExprStmt exprStmt;
                    if (value.expression) {
                        exprStmt.expression = lowerExpr(*value.expression);
                    }
                    lowered.value = std::move(exprStmt);
                } else if constexpr (std::is_same_v<T, AstPrintStmt>) {
                    HirPrintStmt printStmt;
                    if (value.expression) {
                        printStmt.expression = lowerExpr(*value.expression);
                    }
                    lowered.value = std::move(printStmt);
                } else if constexpr (std::is_same_v<T, AstReturnStmt>) {
                    HirReturnStmt returnStmt;
                    if (value.value) {
                        returnStmt.value = lowerExpr(*value.value);
                    }
                    lowered.value = std::move(returnStmt);
                } else if constexpr (std::is_same_v<T, AstBreakStmt>) {
                    HirBreakStmt breakStmt;
                    breakStmt.label = value.label;
                    lowered.value = std::move(breakStmt);
                } else if constexpr (std::is_same_v<T, AstContinueStmt>) {
                    HirContinueStmt continueStmt;
                    continueStmt.label = value.label;
                    lowered.value = std::move(continueStmt);
                } else if constexpr (std::is_same_v<T, AstIfStmt>) {
                    HirIfStmt ifStmt;
                    if (value.condition) {
                        ifStmt.condition = lowerExpr(*value.condition);
                    }
                    if (value.thenBranch) {
                        ifStmt.thenBranch = lowerStmt(*value.thenBranch);
                    }
                    if (value.elseBranch) {
                        ifStmt.elseBranch = lowerStmt(*value.elseBranch);
                    }
                    lowered.value = std::move(ifStmt);
                } else if constexpr (std::is_same_v<T, AstWhileStmt>) {
                    HirWhileStmt whileStmt;
                    whileStmt.label = value.label;
                    if (value.condition) {
                        whileStmt.condition = lowerExpr(*value.condition);
                    }
                    if (value.body) {
                        whileStmt.body = lowerStmt(*value.body);
                    }
                    lowered.value = std::move(whileStmt);
                } else if constexpr (std::is_same_v<T, AstVarDeclStmt>) {
                    lowered.value = lowerVarDecl(value);
                } else if constexpr (std::is_same_v<T, AstDestructuredImportStmt>) {
                    HirDestructuredImportStmt importStmt;
                    importStmt.isConst = value.isConst;
                    for (const auto& binding : value.bindings) {
                        HirImportBinding loweredBinding;
                        loweredBinding.node = makeNodeInfo(binding.node);
                        loweredBinding.exportedName = binding.exportedName;
                        loweredBinding.localName = binding.localName;
                        loweredBinding.expectedType =
                            resolveDeclaredType(binding.expectedType.get());
                        loweredBinding.bindingType = nodeType(binding.node.id);
                        importStmt.bindings.push_back(std::move(loweredBinding));
                    }
                    if (value.initializer) {
                        importStmt.initializer = lowerExpr(*value.initializer);
                    }
                    lowered.value = std::move(importStmt);
                } else if constexpr (std::is_same_v<T, AstForStmt>) {
                    HirForStmt forStmt;
                    forStmt.label = value.label;
                    if (const auto* initDecl =
                            std::get_if<std::unique_ptr<AstVarDeclStmt>>(
                                &value.initializer)) {
                        if (initDecl->get() != nullptr) {
                            HirStmt initStmt;
                            initStmt.node = makeNodeInfo((*initDecl)->node);
                            initStmt.value = lowerVarDecl(**initDecl);
                            forStmt.initializer = storeStmt(std::move(initStmt));
                        }
                    } else if (const auto* initExpr =
                                   std::get_if<AstExprPtr>(&value.initializer)) {
                        if (initExpr->get() != nullptr) {
                            HirStmt initStmt;
                            initStmt.node = makeNodeInfo((*initExpr)->node);
                            HirExprStmt exprStmt;
                            exprStmt.expression = lowerExpr(**initExpr);
                            initStmt.value = std::move(exprStmt);
                            forStmt.initializer = storeStmt(std::move(initStmt));
                        }
                    }
                    if (value.condition) {
                        forStmt.condition = lowerExpr(*value.condition);
                    }
                    if (value.increment) {
                        forStmt.increment = lowerExpr(*value.increment);
                    }
                    if (value.body) {
                        forStmt.body = lowerStmt(*value.body);
                    }
                    lowered.value = std::move(forStmt);
                } else if constexpr (std::is_same_v<T, AstForEachStmt>) {
                    HirForEachStmt forEach;
                    forEach.label = value.label;
                    forEach.isConst = value.isConst;
                    forEach.name = value.name;
                    forEach.declaredType =
                        resolveDeclaredType(value.declaredType.get());
                    if (value.iterable) {
                        forEach.iterable = lowerExpr(*value.iterable);
                    }
                    if (value.body) {
                        forEach.body = lowerStmt(*value.body);
                    }
                    lowered.value = std::move(forEach);
                }
            },
            stmt.value);

        return storeStmt(std::move(lowered));
    }

    HirItemId lowerItem(const AstItem& item) {
        HirItem lowered;
        lowered.node = makeNodeInfo(item.node);

        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, AstTypeAliasDecl>) {
                    HirTypeAliasDecl typeAlias;
                    typeAlias.node = makeNodeInfo(value.node);
                    typeAlias.name = value.name;
                    auto aliasIt =
                        m_frontend.typeAliases.find(tokenLexeme(value.name));
                    if (aliasIt != m_frontend.typeAliases.end()) {
                        typeAlias.aliasedType = aliasIt->second;
                    }
                    lowered.value = std::move(typeAlias);
                } else if constexpr (std::is_same_v<T, AstClassDecl>) {
                    HirClassDecl classDecl;
                    classDecl.node = makeNodeInfo(value.node);
                    classDecl.name = value.name;
                    classDecl.superclass = value.superclass;
                    for (const auto& field : value.fields) {
                        HirFieldDecl loweredField;
                        loweredField.node = makeNodeInfo(field.node);
                        loweredField.name = field.name;
                        loweredField.type = nodeType(field.node.id);
                        classDecl.fields.push_back(std::move(loweredField));
                    }
                    for (const auto& method : value.methods) {
                        HirMethodDecl loweredMethod;
                        loweredMethod.node = makeNodeInfo(method.node);
                        loweredMethod.name = method.name;
                        TypeRef methodType = nodeType(method.node.id);
                        const std::vector<TypeRef> paramTypes =
                            functionParamTypes(methodType);
                        for (size_t i = 0; i < method.params.size(); ++i) {
                            HirParameter param = lowerParameter(method.params[i]);
                            if (i < paramTypes.size() && paramTypes[i]) {
                                param.type = paramTypes[i];
                            }
                            loweredMethod.params.push_back(std::move(param));
                        }
                        loweredMethod.returnType = functionReturnType(methodType);
                        if (method.body) {
                            loweredMethod.body = lowerStmt(*method.body);
                        }
                        loweredMethod.annotatedOperators =
                            method.annotatedOperators;
                        classDecl.methods.push_back(std::move(loweredMethod));
                    }
                    lowered.value = std::move(classDecl);
                } else if constexpr (std::is_same_v<T, AstFunctionDecl>) {
                    HirFunctionDecl functionDecl;
                    functionDecl.node = makeNodeInfo(value.node);
                    functionDecl.name = value.name;
                    TypeRef functionType = nodeType(value.node.id);
                    const std::vector<TypeRef> paramTypes =
                        functionParamTypes(functionType);
                    for (size_t i = 0; i < value.params.size(); ++i) {
                        HirParameter param = lowerParameter(value.params[i]);
                        if (i < paramTypes.size() && paramTypes[i]) {
                            param.type = paramTypes[i];
                        }
                            functionDecl.params.push_back(std::move(param));
                        }
                        functionDecl.returnType = functionReturnType(functionType);
                    if (value.body) {
                        functionDecl.body = lowerStmt(*value.body);
                    }
                    lowered.value = std::move(functionDecl);
                } else if constexpr (std::is_same_v<T, AstStmtPtr>) {
                    if (value) {
                        lowered.value = lowerStmt(*value);
                    }
                }
            },
            item.value);

        return storeItem(std::move(lowered));
    }
};

}  // namespace

bool lowerAstToHir(const AstFrontendResult& frontend, HirModule& outModule) {
    AstToHirLowerer lowerer(frontend);
    return lowerer.run(outModule);
}
