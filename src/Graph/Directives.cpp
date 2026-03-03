#include <VCLG/Graph/Directives.hpp>

#include <VCL/Core/Diagnostic.hpp>
#include <VCL/Sema/Sema.hpp>


bool VCLG::MetadataDirective::OnSema(VCL::Sema& sema, VCL::DirectiveDecl* decl) {
    VCL::IdentifierInfo* identifier = sema.GetIdentifierTable().Get(name);
    
    if (decl->GetArgs().size() > 1) {
        sema.GetDiagnosticReporter().Error(VCL::Diagnostic::DirectiveError, "metadata directive only take one argument")
            .AddHint(VCL::DiagnosticHint{ decl->GetSourceRange() })
            .SetCompilerInfo(__FILE__, __func__, __LINE__)
            .Report();
        return false;
    }
    if (decl->GetArgs().size() < 1) {
        sema.GetDiagnosticReporter().Error(VCL::Diagnostic::DirectiveError, "metadata directive is missing its argument")
            .AddHint(VCL::DiagnosticHint{ decl->GetSourceRange() })
            .SetCompilerInfo(__FILE__, __func__, __LINE__)
            .Report();
        return false;
    }
    if (decl->GetArgs()[0]->GetConstantValueClass() != type) {
        sema.GetDiagnosticReporter().Error(VCL::Diagnostic::DirectiveError, "metadata directive wrong type")
            .AddHint(VCL::DiagnosticHint{ decl->GetSourceRange() })
            .SetCompilerInfo(__FILE__, __func__, __LINE__)
            .Report();
        return false;
    }

    sema.GetDefineTable().Add(identifier, decl->GetArgs()[0]);
    return true;
}

bool VCLG::MetadataFlagDirective::OnSema(VCL::Sema& sema, VCL::DirectiveDecl* decl) {
    VCL::IdentifierInfo* identifier = sema.GetIdentifierTable().Get(name);
    
    if (decl->GetArgs().size() > 0) {
        sema.GetDiagnosticReporter().Error(VCL::Diagnostic::DirectiveError, "metadata flag directive takes no argument")
            .AddHint(VCL::DiagnosticHint{ decl->GetSourceRange() })
            .SetCompilerInfo(__FILE__, __func__, __LINE__)
            .Report();
        return false;
    }

    sema.GetDefineTable().Add(identifier, 
        sema.GetASTContext().AllocateNode<VCL::ConstantNull>(
            sema.GetASTContext().GetTypeCache().GetOrCreateBuiltinType(VCL::BuiltinType::Void)));
    return true;
}