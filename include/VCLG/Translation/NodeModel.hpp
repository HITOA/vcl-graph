#pragma once

#include <VCL/Core/Attribute.hpp>
#include <VCL/Core/Diagnostic.hpp>
#include <VCL/AST/Decl.hpp>
#include <VCL/AST/DeclTemplate.hpp>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>

#include <optional>


namespace VCLG {

    /** The attributes vcl-graph gives a meaning to in node sources (registered by DefinitionRegistry). */
    struct NodeAttributes {
        VCL::AttributeDefinition* nodeProcess = nullptr;
        VCL::AttributeDefinition* nodeReset = nullptr;
        VCL::AttributeDefinition* input = nullptr;
        VCL::AttributeDefinition* output = nullptr;
        VCL::AttributeDefinition* parameter = nullptr;
        VCL::AttributeDefinition* autoParameter = nullptr;
        VCL::AttributeDefinition* alwaysWritten = nullptr;
    };

    /**
     * What each module-level declaration of a node is (`state-as-data.md` §4.1, step 3.1): ports,
     * state, compile-time values, entry points and helpers. The one classifier: DefinitionRegistry
     * uses it on the definition's parse, the translation on each variant's.
     *
     * A variable is, by its attribute: an input, an output, a parameter or an AutoParameter; without
     * one, a constant when it's `const`, a host variable when it's VCL `in`/`out`, and state
     * otherwise. The Sema intrinsics declared in the translation unit aren't the node's.
     */
    class NodeModel {
    public:
        enum class VarKind { Input, Output, Parameter, AutoParameter, Constant, Host, State };

        /** Classifies `tu`; reports (a node error naming `nodeName`) and returns nullopt if it isn't a node. */
        static std::optional<NodeModel> Build(VCL::TranslationUnitDecl* tu, const NodeAttributes& attributes,
            VCL::DiagnosticReporter& reporter, llvm::StringRef nodeName);

        inline VCL::TranslationUnitDecl* GetTranslationUnit() const { return tu; }
        inline const NodeAttributes& GetAttributes() const { return attributes; }

        /** Inputs then outputs, each in declaration order: the port order of the node. */
        inline llvm::ArrayRef<VCL::VarDecl*> GetPorts() const { return ports; }
        inline uint32_t GetInputCount() const { return inputCount; }
        inline llvm::ArrayRef<VCL::VarDecl*> GetInputs() const { return llvm::ArrayRef<VCL::VarDecl*>{ ports }.take_front(inputCount); }
        inline llvm::ArrayRef<VCL::VarDecl*> GetOutputs() const { return llvm::ArrayRef<VCL::VarDecl*>{ ports }.drop_front(inputCount); }
        /** Mutable module-level variables that aren't ports, in declaration order: the fields of `State`. */
        inline llvm::ArrayRef<VCL::VarDecl*> GetState() const { return state; }
        inline llvm::ArrayRef<VCL::VarDecl*> GetParameters() const { return parameters; }
        /** `[AutoParameter]` variables and type aliases. */
        inline llvm::ArrayRef<VCL::NamedDecl*> GetAutoParameters() const { return autoParameters; }

        inline VCL::FunctionDecl* GetProcess() const { return process; }
        inline VCL::FunctionDecl* GetReset() const { return reset; }
        /** The node's functions with a body (entry points and helpers), in declaration order. */
        inline llvm::ArrayRef<VCL::FunctionDecl*> GetFunctions() const { return functions; }
        /** Function templates declared by the node (not the intrinsics). */
        inline llvm::ArrayRef<VCL::TemplateDecl*> GetFunctionTemplates() const { return functionTemplates; }

        std::optional<VarKind> GetVarKind(const VCL::Decl* decl) const;
        /** Index of `decl` in GetPorts(), or nullopt if it isn't a port. */
        std::optional<uint32_t> GetPortIndex(const VCL::Decl* decl) const;
        inline bool IsPort(const VCL::Decl* decl) const { return GetPortIndex(decl).has_value(); }
        inline bool IsInput(const VCL::Decl* decl) const { return GetVarKind(decl) == VarKind::Input; }
        inline bool IsOutput(const VCL::Decl* decl) const { return GetVarKind(decl) == VarKind::Output; }
        inline bool IsState(const VCL::Decl* decl) const { return GetVarKind(decl) == VarKind::State; }
        inline bool IsEntryPoint(const VCL::FunctionDecl* decl) const { return decl != nullptr && (decl == process || decl == reset); }
        bool IsAlwaysWritten(const VCL::VarDecl* port) const;

    private:
        NodeModel(VCL::TranslationUnitDecl* tu, const NodeAttributes& attributes) : tu{ tu }, attributes{ attributes } {}

        VCL::TranslationUnitDecl* tu;
        NodeAttributes attributes;
        llvm::SmallVector<VCL::VarDecl*, 8> ports{};
        uint32_t inputCount = 0;
        llvm::SmallVector<VCL::VarDecl*, 8> state{};
        llvm::SmallVector<VCL::VarDecl*, 4> parameters{};
        llvm::SmallVector<VCL::NamedDecl*, 4> autoParameters{};
        VCL::FunctionDecl* process = nullptr;
        VCL::FunctionDecl* reset = nullptr;
        llvm::SmallVector<VCL::FunctionDecl*, 8> functions{};
        llvm::SmallVector<VCL::TemplateDecl*, 2> functionTemplates{};
        llvm::DenseMap<const VCL::Decl*, VarKind> varKinds{};
    };

}
