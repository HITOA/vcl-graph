#include <VCLG/Translation/NodeRules.hpp>
#include <VCLG/Core/Diagnostics.hpp>

#include <VCL/AST/Expr.hpp>
#include <VCL/AST/Stmt.hpp>

#include <functional>
#include <string>


namespace {

    // Every reference to a module-level variable in a function body, with whether it's written: the
    // target of an assignment or an increment, or an argument to an `out`/`inout` parameter.
    class VariableAccessWalker {
    public:
        using Callback = std::function<void(VCL::DeclRefExpr* expr, bool written)>;

        explicit VariableAccessWalker(Callback callback) : callback{ std::move(callback) } {}

        void WalkStmt(VCL::Stmt* stmt) {
            if (stmt == nullptr)
                return;
            switch (stmt->GetStmtClass()) {
                case VCL::Stmt::ValueStmtClass: WalkExpr((VCL::Expr*)stmt, false); break;
                case VCL::Stmt::DeclStmtClass: {
                    VCL::Decl* decl = ((VCL::DeclStmt*)stmt)->GetDecl();
                    if (decl->GetDeclClass() == VCL::Decl::VarDeclClass && ((VCL::VarDecl*)decl)->GetInitializer())
                        WalkExpr(((VCL::VarDecl*)decl)->GetInitializer(), false);
                    break;
                }
                case VCL::Stmt::CompoundStmtClass:
                    for (VCL::Stmt* s : ((VCL::CompoundStmt*)stmt)->GetStmts())
                        WalkStmt(s);
                    break;
                case VCL::Stmt::ReturnStmtClass: WalkExpr(((VCL::ReturnStmt*)stmt)->GetExpr(), false); break;
                case VCL::Stmt::IfStmtClass: {
                    VCL::IfStmt* s = (VCL::IfStmt*)stmt;
                    WalkExpr(s->GetCondition(), false);
                    WalkStmt(s->GetThenStmt());
                    WalkStmt(s->GetElseStmt());
                    break;
                }
                case VCL::Stmt::WhileStmtClass: {
                    VCL::WhileStmt* s = (VCL::WhileStmt*)stmt;
                    WalkExpr(s->GetCondition(), false);
                    WalkStmt(s->GetThenStmt());
                    break;
                }
                case VCL::Stmt::ForStmtClass: {
                    VCL::ForStmt* s = (VCL::ForStmt*)stmt;
                    WalkStmt(s->GetStartStmt());
                    WalkExpr(s->GetCondition(), false);
                    WalkExpr(s->GetLoopExpr(), false);
                    WalkStmt(s->GetThenStmt());
                    break;
                }
                default:
                    break;
            }
        }

        void WalkExpr(VCL::Expr* expr, bool written) {
            if (expr == nullptr)
                return;
            switch (expr->GetExprClass()) {
                case VCL::Expr::DeclRefExprClass:
                    callback((VCL::DeclRefExpr*)expr, written);
                    break;
                case VCL::Expr::LoadExprClass: WalkExpr(((VCL::LoadExpr*)expr)->GetExpr(), false); break;
                case VCL::Expr::CastExprClass: WalkExpr(((VCL::CastExpr*)expr)->GetExpr(), false); break;
                case VCL::Expr::SplatExprClass: WalkExpr(((VCL::SplatExpr*)expr)->GetExpr(), false); break;
                case VCL::Expr::BinaryExprClass: {
                    VCL::BinaryExpr* e = (VCL::BinaryExpr*)expr;
                    bool assignment = e->GetOperatorKind() >= VCL::BinaryOperator::Assignment;
                    WalkExpr(e->GetLHS(), assignment);
                    WalkExpr(e->GetRHS(), false);
                    break;
                }
                case VCL::Expr::UnaryExprClass: {
                    VCL::UnaryExpr* e = (VCL::UnaryExpr*)expr;
                    VCL::UnaryOperator op = e->GetOperator();
                    bool increment = op == VCL::UnaryOperator::PrefixIncrement || op == VCL::UnaryOperator::PrefixDecrement
                        || op == VCL::UnaryOperator::PostfixIncrement || op == VCL::UnaryOperator::PostfixDecrement;
                    WalkExpr(e->GetExpr(), increment);
                    break;
                }
                case VCL::Expr::CallExprClass: {
                    VCL::CallExpr* e = (VCL::CallExpr*)expr;
                    llvm::ArrayRef<VCL::QualType> params = e->GetFunctionDecl()->GetType()->GetParamsType();
                    for (size_t i = 0; i < e->GetArgs().size(); ++i) {
                        VCL::QualType param = i < params.size() ? params[i] : VCL::QualType{};
                        bool byWritableReference = param.GetType() != nullptr
                            && param.GetType()->GetTypeClass() == VCL::Type::ReferenceTypeClass
                            && !param.HasQualifier(VCL::Qualifier::Const);
                        WalkExpr(e->GetArgs()[i], byWritableReference);
                    }
                    break;
                }
                case VCL::Expr::DependentCallExprClass:
                    for (VCL::Expr* arg : ((VCL::DependentCallExpr*)expr)->GetArgs())
                        WalkExpr(arg, false);
                    break;
                case VCL::Expr::FieldAccessExprClass: WalkExpr(((VCL::FieldAccessExpr*)expr)->GetExpr(), written); break;
                case VCL::Expr::DependentFieldAccessExprClass: WalkExpr(((VCL::DependentFieldAccessExpr*)expr)->GetExpr(), written); break;
                case VCL::Expr::SubscriptExprClass: {
                    VCL::SubscriptExpr* e = (VCL::SubscriptExpr*)expr;
                    WalkExpr(e->GetExpr(), written);
                    WalkExpr(e->GetIndex(), false);
                    break;
                }
                case VCL::Expr::AggregateExprClass:
                    for (VCL::Expr* element : ((VCL::AggregateExpr*)expr)->GetElements())
                        WalkExpr(element, false);
                    break;
                default:
                    break;
            }
        }

    private:
        Callback callback;
    };

    std::string Name(const VCL::NamedDecl* decl) {
        return decl->GetIdentifierInfo()->GetName().str();
    }

}

bool VCLG::CheckNodeRules(const NodeModel& model, VCL::DiagnosticReporter& reporter) {
    bool ok = true;
    const NodeAttributes& attributes = model.GetAttributes();

    auto error = [&](const std::string& message, VCL::SourceRange range) {
        ReportNodeError(reporter, message)
            .AddHint(VCL::DiagnosticHint{ range })
            .SetCompilerInfo(__FILE__, __func__, __LINE__)
            .Report();
        ok = false;
    };
    auto warning = [&](const std::string& message, VCL::SourceRange range) {
        reporter.Warn(VCL::Diagnostic::CustomDiagnostic, message)
            .AddHint(VCL::DiagnosticHint{ range })
            .SetCompilerInfo(__FILE__, __func__, __LINE__)
            .Report();
    };

    // Rules 1 and 2, in every function body the node wrote (a function template's body included).
    auto checkFunction = [&](VCL::FunctionDecl* function) {
        bool isEntryPoint = model.IsEntryPoint(function);
        bool isReset = function == model.GetReset();
        VariableAccessWalker walker{ [&](VCL::DeclRefExpr* expr, bool written) {
            VCL::ValueDecl* decl = expr->GetValueDecl();
            if (!model.IsPort(decl))
                return;
            if (!isEntryPoint) {
                error("port '" + Name(decl) + "' can only be accessed in an entry point ([NodeProcess], [NodeReset]); pass it to '"
                    + Name(function) + "' as an argument", expr->GetSourceRange());
            } else if (isReset && model.IsInput(decl)) {
                error("input '" + Name(decl) + "' can't be read in [NodeReset]: nothing has produced it yet", expr->GetSourceRange());
            } else if (isReset && written && model.IsAlwaysWritten((VCL::VarDecl*)decl)) {
                warning("output '" + Name(decl) + "' is [AlwaysWritten]: what [NodeReset] writes to it is never observed",
                    expr->GetSourceRange());
            }
        } };
        walker.WalkStmt(function->GetBody());
    };
    for (VCL::FunctionDecl* function : model.GetFunctions())
        checkFunction(function);
    for (VCL::TemplateDecl* templateDecl : model.GetFunctionTemplates()) {
        VCL::FunctionDecl* function = (VCL::FunctionDecl*)templateDecl->GetTemplatedNamedDecl();
        if (function->GetBody() == nullptr)
            continue;
        checkFunction(function);
        // Explicit specializations (`special<...>`) have their own body; instantiations are copies
        // of the template's (same source range), already checked above.
        for (auto it = templateDecl->Begin(); it != templateDecl->End(); ++it) {
            if (it->GetDeclClass() != VCL::Decl::TemplateSpecializationDeclClass)
                continue;
            VCL::NamedDecl* specialized = ((VCL::TemplateSpecializationDecl*)it.Get())->GetNamedDecl();
            if (specialized->GetDeclClass() != VCL::Decl::FunctionDeclClass)
                continue;
            VCL::Stmt* body = ((VCL::FunctionDecl*)specialized)->GetBody();
            if (body != nullptr && body->GetSourceRange().start.GetPtr() != function->GetBody()->GetSourceRange().start.GetPtr())
                checkFunction((VCL::FunctionDecl*)specialized);
        }
    }

    // [AlwaysWritten]: on an output only; its initializer is never observed.
    VCL::TranslationUnitDecl* tu = model.GetTranslationUnit();
    for (auto it = tu->Begin(); it != tu->End(); ++it) {
        VCL::Decl* decl = it.Get();
        if (decl->HasAttribute(attributes.alwaysWritten) == nullptr)
            continue;
        if (!model.IsOutput(decl)) {
            error("[AlwaysWritten] only applies to an [Output]", decl->GetSourceRange());
            continue;
        }
        if (((VCL::VarDecl*)decl)->GetInitializer() != nullptr)
            warning("output '" + Name((VCL::VarDecl*)decl) + "' is [AlwaysWritten]: its initializer is never observed",
                decl->GetSourceRange());
    }

    // [Expose] (§3.4, §3.6): once per variable, with the accesses `Read` and `Write`, on node storage
    // the UI can reach at run time; a node's outputs are its own to write.
    for (auto it = tu->Begin(); it != tu->End(); ++it) {
        VCL::Decl* decl = it.Get();
        uint32_t count = 0;
        for (VCL::AttributeInstance* attribute = decl->GetAttribute(); attribute != nullptr; attribute = attribute->GetNextAttribute()) {
            if (attribute->GetDefinition() != attributes.expose)
                continue;
            if (++count == 2)
                error("[Expose] is given twice; one attribute states the whole access ([Expose(Read, Write)])", attribute->GetSourceRange());
            bool seen[2] = { false, false };
            for (VCL::ConstantValue* arg : attribute->GetArgs()) {
                llvm::StringRef access{};
                if (arg->GetConstantValueClass() == VCL::ConstantValue::ConstantIdentifierClass)
                    access = ((VCL::ConstantIdentifier*)arg)->GetIdentifierInfo()->GetName();
                int index = access == "Read" ? 0 : access == "Write" ? 1 : -1;
                if (index < 0) {
                    error("unknown access in [Expose]: expected Read or Write", attribute->GetSourceRange());
                } else if (seen[index]) {
                    error("access '" + access.str() + "' is given twice in [Expose]", attribute->GetSourceRange());
                }
                if (index >= 0)
                    seen[index] = true;
            }
        }
        if (count == 0)
            continue;
        if (decl->GetDeclClass() != VCL::Decl::VarDeclClass) {
            error("[Expose] only applies to a variable", decl->GetSourceRange());
            continue;
        }
        std::string name = Name((VCL::VarDecl*)decl);
        switch (*model.GetVarKind(decl)) {
            case NodeModel::VarKind::Input:
            case NodeModel::VarKind::State:
                break;
            case NodeModel::VarKind::Output:
                if (model.GetExposure(decl) == Exposure::Write)
                    error("output '" + name + "' can't be [Expose(Write)]: the node owns its outputs; [Expose] lets the UI read it",
                        decl->GetSourceRange());
                break;
            case NodeModel::VarKind::Host:
                error("[Expose] can't apply to host variable '" + name + "': it isn't the node's", decl->GetSourceRange());
                break;
            default:
                error("[Expose] can't apply to '" + name + "': a compile-time value has no run-time storage", decl->GetSourceRange());
                break;
        }
    }

    return ok;
}
