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
        SourcePortDefinition(const std::string& name, const std::string& displayName, bool isInput, VCL::VarDecl* decl) :
                name{ name }, displayName{ displayName }, isInput{ isInput }, decl{ decl } {}
        SourcePortDefinition(const SourcePortDefinition& other) = delete;
        SourcePortDefinition(SourcePortDefinition&& other) = delete;
        ~SourcePortDefinition() = default;

        SourcePortDefinition& operator=(const SourcePortDefinition& other) = delete;
        SourcePortDefinition& operator=(SourcePortDefinition&& other) = delete;

        inline const std::string& GetName() const { return name; }
        inline const std::string& GetDisplayName() const { return displayName; }
        inline bool IsInput() const { return isInput; }
        inline VCL::VarDecl* GetDecl() { return decl; }

    private:
        std::string name;
        std::string displayName;
        bool isInput;
        VCL::VarDecl* decl;
    };

    class SourceNodeDefinition final : public llvm::TrailingObjects<SourceNodeDefinition, SourcePortDefinition*> {
        friend class TrailingObjects;

    public:
        SourceNodeDefinition() = delete;
        SourceNodeDefinition(std::shared_ptr<VCL::CompilerInstance> instance, bool hasInstanceData, llvm::ArrayRef<SourcePortDefinition*> ports) 
                : instance{ instance }, hasInstanceData{ hasInstanceData }, portCount{ ports.size() } {
            std::uninitialized_copy(ports.begin(), ports.end(), getTrailingObjects());
        }
        SourceNodeDefinition(const SourceNodeDefinition& other) = delete;
        SourceNodeDefinition(SourceNodeDefinition&& other) = delete;
        ~SourceNodeDefinition() = default;

        SourceNodeDefinition& operator=(const SourceNodeDefinition& other) = delete;
        SourceNodeDefinition& operator=(SourceNodeDefinition&& other) = delete;

        llvm::ArrayRef<SourcePortDefinition*> GetPorts() const { return { getTrailingObjects(), portCount }; }
        
    private:
        std::shared_ptr<VCL::CompilerInstance> instance;
        bool hasInstanceData;
        size_t portCount;
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
        SourcePortDefinition* CreateSourcePortDefinition(VCL::VarDecl* varDecl);

        std::string GetStringAttribute(VCL::AttributeInstance* attribute);

    private:
        VCL::CompilerContext& cc;
        std::unique_ptr<Allocator> allocator;
        llvm::StringMap<SourceNodeDefinition*> definitions;

        VCL::AttributeDefinition* nodeProcessAttributeDefinition;
        VCL::AttributeDefinition* inputAttributeDefinition;
        VCL::AttributeDefinition* outputAttributeDefinition;
    };

}