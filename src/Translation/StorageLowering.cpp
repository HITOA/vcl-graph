#include "Lowering.hpp"

#include <VCL/AST/Expr.hpp>
#include <VCL/AST/Stmt.hpp>
#include <VCL/Sema/TreeTransform.hpp>

#include <llvm/ADT/DenseSet.h>
#include <llvm/ADT/SmallVector.h>


namespace {

    using VCLG::NodeModel;

    // Rebuilds the node into a new translation unit where no mutable module-level variable is
    // left (`state-as-data.md` §4.4):
    // - the state variables become the fields of a `State` record;
    // - every node function (helpers, entry points, the specializations of the node's function
    //   templates) takes `inout State self` first, and a reference to a state variable `x`
    //   becomes `self.x`;
    // - entry points then take every port, in port order: inputs `const T` (by value or by
    //   const reference, as VCL passes them), outputs `out T`; a reference to a port becomes a
    //   reference to its parameter;
    // - calls between node functions pass `self` (and, between entry points, the ports);
    // - constants stay module-level; `__Init` sets every state field and output to its initial value.
    // Entry points are exported (external, mangled with the node's prefix) and their reference
    // parameters get the flags of the entry-point ABI (§4.6).
    class StorageLowering : public VCL::TreeTransform {
    public:
        explicit StorageLowering(VCLG::TranslationContext& context)
            : TreeTransform{ context.sema }, context{ context }, model{ context.model } {}

        bool Run() {
            VCL::TranslationUnitDecl* source = context.translationUnit;
            VCL::TranslationUnitDecl* result = VCL::TranslationUnitDecl::Create(context.ast);
            auto guard = sema.EnterTranslationUnit(result);

            for (VCL::FunctionDecl* function : model.GetFunctions())
                nodeFunctions.insert(function);
            for (VCL::TemplateDecl* templateDecl : model.GetFunctionTemplates())
                for (VCL::FunctionDecl* specialization : Specializations(templateDecl))
                    nodeFunctions.insert(specialization);

            if (!BuildState())
                return false;

            for (auto it = source->Begin(); it != source->End(); ++it) {
                VCL::Decl* decl = it.Get();
                switch (decl->GetDeclClass()) {
                    case VCL::Decl::VarDeclClass: {
                        // Constants (and a node's own host variables) stay module-level.
                        std::optional<NodeModel::VarKind> kind = model.GetVarKind(decl);
                        if (kind == NodeModel::VarKind::State || kind == NodeModel::VarKind::Input || kind == NodeModel::VarKind::Output)
                            break;
                        if (!TransformDecl(decl))
                            return false;
                        break;
                    }
                    case VCL::Decl::FunctionDeclClass:
                        if (nodeFunctions.contains((VCL::FunctionDecl*)decl) && !Lower((VCL::FunctionDecl*)decl))
                            return false;
                        break;
                    case VCL::Decl::TemplateDeclClass:
                        // Every specialization the node uses is already instantiated: each is rebuilt
                        // as an ordinary function, and the template itself is dropped.
                        for (VCL::FunctionDecl* specialization : Specializations((VCL::TemplateDecl*)decl))
                            if (nodeFunctions.contains(specialization) && !Lower(specialization))
                                return false;
                        break;
                    default:
                        // Records, type aliases, directives: types and metadata, used where they are.
                        break;
                }
            }

            if (!BuildInit())
                return false;
            context.translationUnit = result;
            return true;
        }

        VCL::Expr* TransformDeclRefExpr(VCL::DeclRefExpr* expr) override {
            VCL::ValueDecl* decl = expr->GetValueDecl();
            if (model.IsState(decl))
                return StateField(decl, expr->GetSourceRange());
            if (model.IsPort(decl) && GetTransformedDecl(decl) == nullptr)
                return InternalError(expr->GetSourceRange()); // rule 1, checked when the definition was loaded
            if (decl->GetDeclClass() == VCL::Decl::VarDeclClass && !model.GetVarKind(decl).has_value()) {
                VCL::VarDecl* var = (VCL::VarDecl*)decl;
                if (var->HasInAttribute() || var->HasOutAttribute())
                    context.hostSymbols.insert(var->GetIdentifierInfo()->GetName().str());
            }
            return TreeTransform::TransformDeclRefExpr(expr);
        }

        VCL::Expr* TransformCallExpr(VCL::CallExpr* expr) override {
            VCL::FunctionDecl* callee = expr->GetFunctionDecl();
            if (!nodeFunctions.contains(callee))
                return TreeTransform::TransformCallExpr(expr); // libraries can't see node state
            auto* copy = (VCL::FunctionDecl*)GetTransformedDecl(callee);
            if (copy == nullptr)
                return InternalError(expr->GetSourceRange()); // callees are declared first

            VCL::SourceRange range = expr->GetSourceRange();
            llvm::SmallVector<VCL::Expr*, 8> args{ sema.ActOnDeclRefExpr(self, range) };
            if (model.IsEntryPoint(callee)) {
                // An entry point called by another gets the same ports.
                if (!model.IsEntryPoint(currentFunction)) {
                    context.reporter.Error(VCL::Diagnostic::NodeDefinitionError, "entry point '"
                            + callee->GetIdentifierInfo()->GetName().str() + "' can only be called from an entry point")
                        .AddHint(VCL::DiagnosticHint{ range })
                        .SetCompilerInfo(__FILE__, __func__, __LINE__)
                        .Report();
                    return nullptr;
                }
                for (VCL::ParamDecl* port : ports)
                    args.push_back(sema.ActOnDeclRefExpr(port, range));
            }
            for (VCL::Expr* arg : expr->GetArgs()) {
                VCL::Expr* newArg = TransformExpr(arg);
                if (newArg == nullptr)
                    return nullptr;
                args.push_back(newArg);
            }
            return sema.ActOnResolvedCallExpr(copy, copy->GetIdentifierInfo(), args, nullptr, range);
        }

    private:
        static llvm::SmallVector<VCL::FunctionDecl*, 4> Specializations(VCL::TemplateDecl* templateDecl) {
            llvm::SmallVector<VCL::FunctionDecl*, 4> specializations{};
            for (auto it = templateDecl->Begin(); it != templateDecl->End(); ++it) {
                if (it->GetDeclClass() != VCL::Decl::TemplateSpecializationDeclClass)
                    continue;
                VCL::NamedDecl* specialized = ((VCL::TemplateSpecializationDecl*)it.Get())->GetNamedDecl();
                if (specialized->GetDeclClass() != VCL::Decl::FunctionDeclClass)
                    continue;
                auto* function = (VCL::FunctionDecl*)specialized;
                if (!function->HasFunctionFlag(VCL::FunctionDecl::IsIntrinsic) && function->GetBody() != nullptr)
                    specializations.push_back(function);
            }
            return specializations;
        }

        VCL::Expr* InternalError(VCL::SourceRange range) {
            context.reporter.Error(VCL::Diagnostic::InternalError)
                .AddHint(VCL::DiagnosticHint{ range })
                .SetCompilerInfo(__FILE__, __func__, __LINE__)
                .Report();
            return nullptr;
        }

        VCL::QualType StateType() const {
            return VCL::QualType{ context.state->GetType() };
        }

        VCL::Expr* StateField(VCL::ValueDecl* variable, VCL::SourceRange range) {
            VCL::Expr* base = sema.ActOnDeclRefExpr(self, range);
            return sema.ActOnFieldAccessExpr(base, variable->GetIdentifierInfo(), range);
        }

        // `State`: one field per state variable, in declaration order.
        bool BuildState() {
            VCL::SourceRange range = model.GetState().empty() ? model.GetProcess()->GetSourceRange() : model.GetState()[0]->GetSourceRange();
            context.state = sema.ActOnRecordDecl(context.identifiers.Get("State"), range);
            if (context.state == nullptr)
                return false;
            auto guard = sema.PushScope(context.state, false);
            for (VCL::VarDecl* variable : model.GetState()) {
                VCL::QualType type = TransformType(variable->GetValueType());
                if (!type.GetAsOpaquePtr() || !sema.ActOnFieldDecl(type, variable->GetIdentifierInfo(), variable->GetSourceRange()))
                    return false;
            }
            return true;
        }

        // The flags of the entry-point ABI (§4.6), on its reference parameters. Why they hold:
        // - `self` is only its node's `State`, and no port is a field of it;
        // - slots don't overlap, and a node's outputs never overlap its inputs;
        // - inputs are never written during the call, so two inputs may share memory.
        void SetEntryPointFlags(VCL::ParamDecl* selfParam, llvm::ArrayRef<VCL::ParamDecl*> inputs, llvm::ArrayRef<VCL::ParamDecl*> outputs) {
            using F = VCL::ParamDecl;
            constexpr uint32_t common = F::NoAlias | F::NoCapture | F::Aligned | F::Dereferenceable;
            selfParam->SetCodeGenFlag((F::CodeGenFlags)common);
            for (VCL::ParamDecl* input : inputs)
                input->SetCodeGenFlag((F::CodeGenFlags)(common | F::ReadOnly));
            for (VCL::ParamDecl* output : outputs)
                output->SetCodeGenFlag((F::CodeGenFlags)common);
        }

        VCL::ParamDecl* DeclareSelf(VCL::SourceRange range) {
            VCL::Decl::VarAttrBitfield inout{ 1, 1 };
            return sema.ActOnParamDecl(inout, StateType(), context.identifiers.Get("self"), range);
        }

        VCL::ParamDecl* DeclarePort(VCL::VarDecl* port) {
            VCL::Decl::VarAttrBitfield attr{ 0, model.IsInput(port) ? 0u : 1u };
            return sema.ActOnParamDecl(attr, port->GetValueType(), port->GetIdentifierInfo(), port->GetSourceRange());
        }

        bool Lower(VCL::FunctionDecl* decl) {
            bool isEntryPoint = model.IsEntryPoint(decl);
            VCL::FunctionDecl* function = VCL::FunctionDecl::Create(context.ast, decl->GetIdentifierInfo());
            VCL::QualType returnType = TransformType(decl->GetType()->GetReturnType());
            if (!returnType.GetAsOpaquePtr())
                return false;

            VCL::ParamDecl* selfParam = nullptr;
            llvm::SmallVector<VCL::ParamDecl*, 8> portParams{};
            {
                auto guard = sema.PushScope(function, false);
                selfParam = DeclareSelf(decl->GetSourceRange());
                if (selfParam == nullptr)
                    return false;
                if (isEntryPoint) {
                    for (VCL::VarDecl* port : model.GetPorts()) {
                        VCL::ParamDecl* param = DeclarePort(port);
                        if (param == nullptr)
                            return false;
                        portParams.push_back(param);
                    }
                    llvm::ArrayRef<VCL::ParamDecl*> all{ portParams };
                    SetEntryPointFlags(selfParam, all.take_front(model.GetInputCount()), all.drop_front(model.GetInputCount()));
                } else if (!TransformFunctionParams(decl, function)) {
                    return false;
                }
                // Declared while its own scope is current, so it isn't added to the output unit's
                // scope (only to its context, below): the specializations of one template share a
                // name, and nothing looks names up there.
                if (!sema.ActOnFunctionDecl(function, returnType, nullptr, decl->GetSourceRange()))
                    return false;
            }
            if (!sema.AddDeclToContext(function))
                return InternalError(decl->GetSourceRange()), false;
            TransformAttributes(decl, function);
            function->SetExported(isEntryPoint);
            MapDecl(decl, function);

            // The body, with `self` and (in an entry point) the ports.
            self = selfParam;
            ports = portParams;
            currentFunction = decl;
            for (uint32_t i = 0; i < portParams.size(); ++i)
                MapDecl(model.GetPorts()[i], portParams[i]);
            VCL::Stmt* body = nullptr;
            {
                auto guard = sema.PushScope(function, false);
                body = TransformFunctionBody(decl);
            }
            for (VCL::VarDecl* port : model.GetPorts())
                transformedDecls.erase(port);
            ports.clear();
            if (body == nullptr)
                return false;
            function->SetBody(body);

            if (decl == model.GetProcess())
                context.process = function;
            else if (decl == model.GetReset())
                context.reset = function;
            return true;
        }

        // `__Init(inout State self, out <outputs>)`: every state field and output gets its
        // initializer, or zero (a reset puts back what a fresh block would hold).
        bool BuildInit() {
            VCL::SourceRange range = model.GetProcess()->GetSourceRange();
            VCL::FunctionDecl* init = VCL::FunctionDecl::Create(context.ast, context.identifiers.Get("__Init"));
            VCL::QualType voidType{ context.ast.GetTypeCache().GetOrCreateBuiltinType(VCL::BuiltinType::Void) };

            VCL::ParamDecl* selfParam = nullptr;
            llvm::SmallVector<VCL::ParamDecl*, 4> outputParams{};
            {
                auto guard = sema.PushScope(init, false);
                selfParam = DeclareSelf(range);
                if (selfParam == nullptr)
                    return false;
                for (VCL::VarDecl* output : model.GetOutputs()) {
                    VCL::ParamDecl* param = DeclarePort(output);
                    if (param == nullptr)
                        return false;
                    outputParams.push_back(param);
                }
                SetEntryPointFlags(selfParam, {}, outputParams);
                if (!sema.ActOnFunctionDecl(init, voidType, nullptr, range))
                    return false;
            }
            if (!sema.AddDeclToContext(init))
                return InternalError(range), false;
            init->SetExported(true);

            self = selfParam;
            currentFunction = nullptr;
            auto guard = sema.PushScope(init, false);
            llvm::SmallVector<VCL::Stmt*, 16> stmts{};
            auto assign = [&](VCL::Expr* target, VCL::VarDecl* variable, VCL::QualType type) -> bool {
                if (target == nullptr)
                    return false;
                VCL::SourceRange variableRange = variable->GetSourceRange();
                VCL::Expr* value = variable->GetInitializer() != nullptr
                    ? TransformExpr(variable->GetInitializer())
                    : VCL::NullExpr::Create(context.ast, type, variableRange);
                if (value == nullptr)
                    return false;
                VCL::Expr* assignment = sema.ActOnBinaryExpr(target, value, VCL::BinaryOperator::Assignment);
                if (assignment == nullptr)
                    return false;
                stmts.push_back(assignment);
                return true;
            };
            for (VCL::VarDecl* variable : model.GetState())
                if (!assign(StateField(variable, variable->GetSourceRange()), variable, TransformType(variable->GetValueType())))
                    return false;
            for (uint32_t i = 0; i < outputParams.size(); ++i) {
                VCL::VarDecl* output = model.GetOutputs()[i];
                VCL::QualType type = output->GetValueType();
                if (!assign(sema.ActOnDeclRefExpr(outputParams[i], output->GetSourceRange()), output, type))
                    return false;
            }
            init->SetBody(sema.ActOnCompoundStmt(stmts, range));
            context.init = init;
            return true;
        }

        VCLG::TranslationContext& context;
        const NodeModel& model;
        llvm::DenseSet<VCL::FunctionDecl*> nodeFunctions{};
        // The function being rebuilt: its `self`, its port parameters (entry points), the source function.
        VCL::ParamDecl* self = nullptr;
        llvm::SmallVector<VCL::ParamDecl*, 8> ports{};
        VCL::FunctionDecl* currentFunction = nullptr;
    };

}

bool VCLG::RunStorageLowering(TranslationContext& context) {
    StorageLowering lowering{ context };
    return lowering.Run();
}
