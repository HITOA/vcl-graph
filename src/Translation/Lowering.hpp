#pragma once

#include <VCLG/Translation/NodeModel.hpp>

#include <VCL/AST/ASTContext.hpp>
#include <VCL/AST/Decl.hpp>
#include <VCL/Core/Diagnostic.hpp>
#include <VCL/Core/Identifier.hpp>
#include <VCL/Sema/Sema.hpp>

#include <set>
#include <string>


namespace VCLG {

    /**
     * What the lowering steps share: the variant's Sema (alive, to build checked code), its node
     * model, and the current translation unit, which each step replaces with its output.
     */
    struct TranslationContext {
        VCL::Sema& sema;
        VCL::ASTContext& ast;
        VCL::IdentifierTable& identifiers;
        VCL::DiagnosticReporter& reporter;
        const NodeModel& model;
        /** The source's translation unit, then each step's output. */
        VCL::TranslationUnitDecl* translationUnit;

        // Produced by storage lowering.
        VCL::RecordDecl* state = nullptr;
        VCL::FunctionDecl* process = nullptr;
        VCL::FunctionDecl* reset = nullptr;
        VCL::FunctionDecl* init = nullptr;
        /** Per input: the constant holding its declared initializer, or null when it has none. */
        llvm::SmallVector<VCL::VarDecl*, 8> inputDefaults{};
        std::set<std::string> hostSymbols{};
    };

    /** Storage lowering (`state-as-data.md` §4.4). */
    bool RunStorageLowering(TranslationContext& context);

}
