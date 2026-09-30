#pragma once

#include <VCLG/Graph/Elaboration.hpp>
#include <VCLG/Graph/GraphContext.hpp>
#include <VCLG/AST/ASTParameterWriter.hpp>
#include <VCLG/AST/ASTAutoParameterSubstitution.hpp>
#include <VCLG/AST/ASTPortTypeOverrideWriter.hpp>
#include <VCLG/AST/ASTInputConstWriter.hpp>

#include <VCL/AST/ASTConsumer.hpp>
#include <VCL/Core/Source.hpp>
#include <VCL/Frontend/CompilerContext.hpp>
#include <VCL/Frontend/CompilerInstance.hpp>
#include <VCL/Lex/Lexer.hpp>
#include <VCL/Lex/TokenStream.hpp>
#include <VCL/Parse/Parser.hpp>
#include <VCL/Sema/Sema.hpp>

#include <llvm/ADT/SmallVector.h>

#include <memory>
#include <string>


namespace VCLG {

    /**
     * One parse of an elaborated source node, with what makes it this copy (`state-as-data.md` §4.1,
     * step 1): parameter values, AutoParameter substitutions, port type overrides, and the const
     * qualifier on inputs, injected while parsing. Sema stays alive afterwards, for the translation
     * to build code.
     *
     * `node` and `source` must outlive it (the AST writers refer to the node's values).
     */
    class SourceNodeCompilation {
    public:
        SourceNodeCompilation() = delete;
        SourceNodeCompilation(GraphContext& graphContext, VCL::CompilerContext& cc, const ElaboratedGraph::Node& node,
            VCL::Source* source, const std::string& manglingPrefix);
        SourceNodeCompilation(const SourceNodeCompilation& other) = delete;
        SourceNodeCompilation(SourceNodeCompilation&& other) = delete;
        ~SourceNodeCompilation() = default;

        SourceNodeCompilation& operator=(const SourceNodeCompilation& other) = delete;
        SourceNodeCompilation& operator=(SourceNodeCompilation&& other) = delete;

        /** Parses and checks the node; false if an error was reported. */
        bool Parse();

        inline const std::shared_ptr<VCL::CompilerInstance>& GetInstance() const { return instance; }
        inline VCL::Sema& GetSema() { return *sema; }
        inline SourceNodeDefinition* GetDefinition() const { return node.definition; }
        inline const ElaboratedGraph::Node& GetNode() const { return node; }

    private:
        const ElaboratedGraph::Node& node;
        // Declared in construction order; destroyed in reverse.
        std::shared_ptr<VCL::CompilerInstance> instance;
        std::unique_ptr<VCL::Lexer> lexer;
        std::unique_ptr<VCL::TokenStream> stream;
        std::unique_ptr<VCL::Sema> sema;
        std::unique_ptr<VCL::Parser> parser;
        llvm::SmallVector<VCL::Type*, 4> portTypeOverrides{};
        std::unique_ptr<ASTParameterWriter> parameterWriter;
        std::unique_ptr<ASTAutoParameterSubstitution> autoParameterWriter;
        std::unique_ptr<ASTPortTypeOverrideWriter> portWriter;
        std::unique_ptr<ASTInputConstWriter> inputConstWriter;
        VCL::MultiplexerASTConsumer consumer{};
    };

}
