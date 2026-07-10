#pragma once

#include <VCLG/Core/Allocator.hpp>

#include <VCL/Core/Source.hpp>
#include <VCL/AST/Decl.hpp>
#include <VCL/Frontend/CompilerContext.hpp>

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/Support/TrailingObjects.h>


namespace VCLG {
    
    class SourcePortDefinition {
    public:
        SourcePortDefinition() = delete;
        SourcePortDefinition(const std::string& name, const std::string& displayName, bool isInput, VCL::VarDecl* decl, bool isDependent) :
                name{ name }, displayName{ displayName }, isInput{ isInput }, decl{ decl }, isDependent{ isDependent } {}
        SourcePortDefinition(const SourcePortDefinition& other) = delete;
        SourcePortDefinition(SourcePortDefinition&& other) = delete;
        ~SourcePortDefinition() = default;

        SourcePortDefinition& operator=(const SourcePortDefinition& other) = delete;
        SourcePortDefinition& operator=(SourcePortDefinition&& other) = delete;

        inline const std::string& GetName() const { return name; }
        inline const std::string& GetDisplayName() const { return displayName; }
        inline bool IsInput() const { return isInput; }
        inline VCL::VarDecl* GetDecl() { return decl; }
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
        SourceParameterDefinition() = delete;
        SourceParameterDefinition(const std::string& name, const std::string& displayName, VCL::VarDecl* decl) :
                name{ name }, displayName{ displayName }, decl{ decl } {}
        SourceParameterDefinition(const SourceParameterDefinition& other) = delete;
        SourceParameterDefinition(SourceParameterDefinition&& other) = delete;
        ~SourceParameterDefinition() = default;

        SourceParameterDefinition& operator=(const SourceParameterDefinition& other) = delete;
        SourceParameterDefinition& operator=(SourceParameterDefinition&& other) = delete;

        inline const std::string& GetName() const { return name; }
        inline const std::string& GetDisplayName() const { return displayName; }
        inline VCL::VarDecl* GetDecl() { return decl; }

    private:
        std::string name;
        std::string displayName;
        VCL::VarDecl* decl;
    };

    class SourceAutoParameterDefinition {
    public:
        SourceAutoParameterDefinition() = delete;
        SourceAutoParameterDefinition(const std::string& name, VCL::Decl* decl) :
                name{ name }, decl{ decl } {}
        SourceAutoParameterDefinition(const SourceAutoParameterDefinition& other) = delete;
        SourceAutoParameterDefinition(SourceAutoParameterDefinition&& other) = delete;
        ~SourceAutoParameterDefinition() = default;

        SourceAutoParameterDefinition& operator=(const SourceAutoParameterDefinition& other) = delete;
        SourceAutoParameterDefinition& operator=(SourceAutoParameterDefinition&& other) = delete;

        inline const std::string& GetName() const { return name; }
        inline VCL::Decl* GetDecl() { return decl; }

    private:
        std::string name;
        VCL::Decl* decl;
    };

    class SourceNodeDefinition final : public llvm::TrailingObjects<SourceNodeDefinition, SourcePortDefinition*, SourceParameterDefinition*, SourceAutoParameterDefinition*> {
        friend class TrailingObjects;
    
    public:
        enum class DefinitionNodeFlag : uint32_t {
            None = 0,
            IsInputNode = 1,
            IsOutputNode = 2
        };

    public:
        SourceNodeDefinition() = delete;
        SourceNodeDefinition(std::shared_ptr<VCL::CompilerInstance> instance, const std::string& displayName, VCL::FunctionDecl* entrypoint, 
            VCL::FunctionDecl* reset, bool hasInstanceData, llvm::ArrayRef<SourcePortDefinition*> ports, llvm::ArrayRef<SourceParameterDefinition*> parameters,
            llvm::ArrayRef<SourceAutoParameterDefinition*> autoParameters) 
                : instance{ instance }, displayName{ displayName }, entrypoint{ entrypoint }, reset{ reset }, hasInstanceData{ hasInstanceData }, 
                    portCount{ ports.size() }, parameterCount{ parameters.size() }, autoParameterCount{ autoParameters.size() } {
            std::uninitialized_copy(ports.begin(), ports.end(), getTrailingObjects<SourcePortDefinition*>());
            std::uninitialized_copy(parameters.begin(), parameters.end(), getTrailingObjects<SourceParameterDefinition*>());
            std::uninitialized_copy(autoParameters.begin(), autoParameters.end(), getTrailingObjects<SourceAutoParameterDefinition*>());
        }
        SourceNodeDefinition(const SourceNodeDefinition& other) = delete;
        SourceNodeDefinition(SourceNodeDefinition&& other) = delete;
        ~SourceNodeDefinition() = default;

        SourceNodeDefinition& operator=(const SourceNodeDefinition& other) = delete;
        SourceNodeDefinition& operator=(SourceNodeDefinition&& other) = delete;
        
        inline llvm::StringRef GetDisplayName() const { return displayName; }

        inline VCL::FunctionDecl* GetEntrypoint() const { return entrypoint; }
        inline VCL::FunctionDecl* GetReset() const { return reset; }

        inline llvm::ArrayRef<SourcePortDefinition*> GetPorts() const { 
            return { getTrailingObjects<SourcePortDefinition*>(), portCount }; }
        inline llvm::ArrayRef<SourceParameterDefinition*> GetParameters() const { 
            return { getTrailingObjects<SourceParameterDefinition*>(), parameterCount }; }
        inline llvm::ArrayRef<SourceAutoParameterDefinition*> GetAutoParameters() const { 
            return { getTrailingObjects<SourceAutoParameterDefinition*>(), autoParameterCount }; }

        inline bool HasFlag(DefinitionNodeFlag flag) const { return ((uint32_t)flags & (uint32_t)flag) != 0; }
        inline void AddFlag(DefinitionNodeFlag flag) { this->flags = (DefinitionNodeFlag)((uint32_t)flags | (uint32_t)flag); }
        inline DefinitionNodeFlag GetFlag() const { return flags; }
    
    private:
        size_t numTrailingObjects(OverloadToken<SourcePortDefinition*>) const {
            return portCount;
        }        

        size_t numTrailingObjects(OverloadToken<SourceParameterDefinition*>) const {
            return parameterCount;
        }

        size_t numTrailingObjects(OverloadToken<SourceAutoParameterDefinition*>) const {
            return autoParameterCount;
        }

    private:
        std::shared_ptr<VCL::CompilerInstance> instance;
        std::string displayName;
        DefinitionNodeFlag flags;
        VCL::FunctionDecl* entrypoint;
        VCL::FunctionDecl* reset;
        bool hasInstanceData;
        size_t portCount;
        size_t parameterCount;
        size_t autoParameterCount;
    };

    class DefinitionRegistry : public llvm::RefCountedBase<DefinitionRegistry> {
    public:
        DefinitionRegistry() = delete;
        DefinitionRegistry(VCL::CompilerContext& cc, std::unique_ptr<Allocator> allocator = std::make_unique<TLSFAllocator>());
        DefinitionRegistry(const DefinitionRegistry& other) = delete;
        DefinitionRegistry(DefinitionRegistry&& other) = delete;
        ~DefinitionRegistry();

        DefinitionRegistry& operator=(const DefinitionRegistry& other) = delete;
        DefinitionRegistry& operator=(DefinitionRegistry&& other) = delete;

        SourceNodeDefinition* GetOrCreateSourceNodeDefinition(VCL::Source* source);

        void Reset();

    private:
        SourceNodeDefinition* CreateSourceNodeDefinition(VCL::Source* source);
        SourcePortDefinition* CreateSourcePortDefinition(VCL::VarDecl* varDecl, llvm::ArrayRef<SourceAutoParameterDefinition*> autoParameters);
        SourceParameterDefinition* CreateSourceParameterDefinition(VCL::VarDecl* varDecl);
        SourceAutoParameterDefinition* CreateSourceAutoParameterDefinition(VCL::NamedDecl* decl);

        std::string GetStringAttribute(VCL::AttributeInstance* attribute);
        std::string GetStringDefine(std::shared_ptr<VCL::CompilerInstance> instance, llvm::StringRef name);

        bool HasFlagDefined(std::shared_ptr<VCL::CompilerInstance> instance, llvm::StringRef name);

        bool IsPortAutoParameterDependent(VCL::Type* portType, llvm::ArrayRef<SourceAutoParameterDefinition*> autoParameters);
        bool IsTypeAliasPresentInAutoParameterList(VCL::TypeAliasType* type, llvm::ArrayRef<SourceAutoParameterDefinition*> autoParameters);
        bool IsExpressionDependentInAutoParameterList(VCL::Expr* expr, llvm::ArrayRef<SourceAutoParameterDefinition*> autoParameters);

    private:
        VCL::CompilerContext& cc;
        std::unique_ptr<Allocator> allocator;
        llvm::StringMap<SourceNodeDefinition*> definitions;

        VCL::AttributeDefinition* nodeProcessAttributeDefinition;
        VCL::AttributeDefinition* nodeResetAttributeDefinition;
        VCL::AttributeDefinition* inputAttributeDefinition;
        VCL::AttributeDefinition* outputAttributeDefinition;
        VCL::AttributeDefinition* parameterAttributeDefinition;
        VCL::AttributeDefinition* autoParameterAttributeDefinition;
    };

}