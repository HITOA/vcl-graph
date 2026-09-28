#pragma once

#include <VCL/Core/Source.hpp>
#include <VCL/AST/Decl.hpp>
#include <VCL/Frontend/CompilerContext.hpp>

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/IntrusiveRefCntPtr.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>


namespace VCLG {
    
    class SourcePortDefinition {
    public:
        SourcePortDefinition(const std::string& name, const std::string& displayName, bool isInput, VCL::VarDecl* decl, bool isDependent) :
                name{ name }, displayName{ displayName }, isInput{ isInput }, decl{ decl }, isDependent{ isDependent } {}

        inline const std::string& GetName() const { return name; }
        inline const std::string& GetDisplayName() const { return displayName; }
        inline bool IsInput() const { return isInput; }
        inline VCL::VarDecl* GetDecl() const { return decl; }
        inline bool IsDependent() const { return isDependent; }

    private:
        std::string name;
        std::string displayName;
        bool isInput;
        VCL::VarDecl* decl;
        bool isDependent;
    };

    class SourceParameterDefinition {
    public:
        SourceParameterDefinition(const std::string& name, const std::string& displayName, VCL::VarDecl* decl) :
                name{ name }, displayName{ displayName }, decl{ decl } {}

        inline const std::string& GetName() const { return name; }
        inline const std::string& GetDisplayName() const { return displayName; }
        inline VCL::VarDecl* GetDecl() const { return decl; }

    private:
        std::string name;
        std::string displayName;
        VCL::VarDecl* decl;
    };

    class SourceAutoParameterDefinition {
    public:
        SourceAutoParameterDefinition(const std::string& name, VCL::Decl* decl) :
                name{ name }, decl{ decl } {}

        inline const std::string& GetName() const { return name; }
        inline VCL::Decl* GetDecl() const { return decl; }

    private:
        std::string name;
        VCL::Decl* decl;
    };

    /**
     * What a node source declares: its ports (inputs first, then outputs), parameters and
     * AutoParameters. Holds the CompilerInstance it was parsed with, which owns the decls.
     */
    class SourceNodeDefinition final {
    public:
        enum class DefinitionNodeFlag : uint32_t {
            None = 0,
            IsInputNode = 1,
            IsOutputNode = 2
        };

    public:
        SourceNodeDefinition() = delete;
        SourceNodeDefinition(std::shared_ptr<VCL::CompilerInstance> instance, const std::string& displayName, VCL::FunctionDecl* entrypoint, 
            VCL::FunctionDecl* reset, bool hasInstanceData, std::vector<SourcePortDefinition> ports, std::vector<SourceParameterDefinition> parameters,
            std::vector<SourceAutoParameterDefinition> autoParameters) 
                : instance{ instance }, displayName{ displayName }, flags{ DefinitionNodeFlag::None }, entrypoint{ entrypoint }, reset{ reset },
                    hasInstanceData{ hasInstanceData }, ports{ std::move(ports) }, parameters{ std::move(parameters) },
                    autoParameters{ std::move(autoParameters) } {}
        SourceNodeDefinition(const SourceNodeDefinition& other) = delete;
        SourceNodeDefinition(SourceNodeDefinition&& other) = delete;
        ~SourceNodeDefinition() = default;

        SourceNodeDefinition& operator=(const SourceNodeDefinition& other) = delete;
        SourceNodeDefinition& operator=(SourceNodeDefinition&& other) = delete;
        
        inline llvm::StringRef GetDisplayName() const { return displayName; }

        inline VCL::FunctionDecl* GetEntrypoint() const { return entrypoint; }
        inline VCL::FunctionDecl* GetReset() const { return reset; }

        inline llvm::ArrayRef<SourcePortDefinition> GetPorts() const { return ports; }
        inline llvm::ArrayRef<SourceParameterDefinition> GetParameters() const { return parameters; }
        inline llvm::ArrayRef<SourceAutoParameterDefinition> GetAutoParameters() const { return autoParameters; }

        inline bool HasFlag(DefinitionNodeFlag flag) const { return ((uint32_t)flags & (uint32_t)flag) != 0; }
        inline void AddFlag(DefinitionNodeFlag flag) { this->flags = (DefinitionNodeFlag)((uint32_t)flags | (uint32_t)flag); }
        inline DefinitionNodeFlag GetFlag() const { return flags; }
    
    private:
        std::shared_ptr<VCL::CompilerInstance> instance;
        std::string displayName;
        DefinitionNodeFlag flags;
        VCL::FunctionDecl* entrypoint;
        VCL::FunctionDecl* reset;
        bool hasInstanceData;
        std::vector<SourcePortDefinition> ports;
        std::vector<SourceParameterDefinition> parameters;
        std::vector<SourceAutoParameterDefinition> autoParameters;
    };

    class DefinitionRegistry : public llvm::RefCountedBase<DefinitionRegistry> {
    public:
        DefinitionRegistry() = delete;
        DefinitionRegistry(VCL::CompilerContext& cc);
        DefinitionRegistry(const DefinitionRegistry& other) = delete;
        DefinitionRegistry(DefinitionRegistry&& other) = delete;
        ~DefinitionRegistry();

        DefinitionRegistry& operator=(const DefinitionRegistry& other) = delete;
        DefinitionRegistry& operator=(DefinitionRegistry&& other) = delete;

        SourceNodeDefinition* GetOrCreateSourceNodeDefinition(VCL::Source* source);

        void Reset();

    private:
        SourceNodeDefinition* CreateSourceNodeDefinition(VCL::Source* source);
        SourcePortDefinition CreateSourcePortDefinition(VCL::VarDecl* varDecl, llvm::ArrayRef<SourceAutoParameterDefinition> autoParameters);
        SourceParameterDefinition CreateSourceParameterDefinition(VCL::VarDecl* varDecl);
        SourceAutoParameterDefinition CreateSourceAutoParameterDefinition(VCL::NamedDecl* decl);

        /** The attribute's string argument; reports an error on `decl` and returns nullopt if it isn't a string. */
        std::optional<std::string> GetStringAttribute(VCL::AttributeInstance* attribute, VCL::Decl* decl);
        std::string GetStringDefine(std::shared_ptr<VCL::CompilerInstance> instance, llvm::StringRef name);

        bool HasFlagDefined(std::shared_ptr<VCL::CompilerInstance> instance, llvm::StringRef name);

        bool IsPortAutoParameterDependent(VCL::Type* portType, llvm::ArrayRef<SourceAutoParameterDefinition> autoParameters);
        bool IsTypeAliasPresentInAutoParameterList(VCL::TypeAliasType* type, llvm::ArrayRef<SourceAutoParameterDefinition> autoParameters);
        bool IsExpressionDependentInAutoParameterList(VCL::Expr* expr, llvm::ArrayRef<SourceAutoParameterDefinition> autoParameters);

    private:
        VCL::CompilerContext& cc;
        llvm::StringMap<std::unique_ptr<SourceNodeDefinition>> definitions;

        VCL::AttributeDefinition* nodeProcessAttributeDefinition;
        VCL::AttributeDefinition* nodeResetAttributeDefinition;
        VCL::AttributeDefinition* inputAttributeDefinition;
        VCL::AttributeDefinition* outputAttributeDefinition;
        VCL::AttributeDefinition* parameterAttributeDefinition;
        VCL::AttributeDefinition* autoParameterAttributeDefinition;
    };

}