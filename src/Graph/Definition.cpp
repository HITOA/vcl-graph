#include <VCLG/Graph/Definition.hpp>

#include <VCL/Core/Diagnostic.hpp>
#include <VCL/Core/Format.hpp>
#include <VCL/AST/Decl.hpp>
#include <VCL/Frontend/FrontendActions.hpp>
#include <VCL/Frontend/CompilerInstance.hpp>

VCLG::DefinitionRegistry::DefinitionRegistry(VCL::CompilerContext& cc, std::unique_ptr<Allocator> allocator) : 
        cc{ cc }, allocator{ std::move(allocator) }, definitions{} {
    
    nodeProcessAttributeDefinition = cc.GetAttributeTable().AddDefinition(cc.GetIdentifierTable().Get("NodeProcess"), 0, 0);
    inputAttributeDefinition = cc.GetAttributeTable().AddDefinition(cc.GetIdentifierTable().Get("Input"), 1, 1);
    outputAttributeDefinition = cc.GetAttributeTable().AddDefinition(cc.GetIdentifierTable().Get("Output"), 1, 1);
}

VCLG::DefinitionRegistry::~DefinitionRegistry() {
    Reset();
}

VCLG::SourceNodeDefinition* VCLG::DefinitionRegistry::GetOrCreateSourceNodeDefinition(VCL::Source* source) {
    if (definitions.count(source->GetBufferIdentifier()))
        return definitions[source->GetBufferIdentifier()];
    return CreateSourceNodeDefinition(source);
}

void VCLG::DefinitionRegistry::Reset() {
    for (llvm::StringRef str : definitions.keys()) {
        SourceNodeDefinition* definition = definitions[str];
        size_t portDefSize = SourceNodeDefinition::totalSizeToAlloc<SourcePortDefinition*>(definition->GetPorts().size());
        for (SourcePortDefinition* port : definition->GetPorts())
            allocator->Deallocate(port, sizeof(SourcePortDefinition));
        allocator->Deallocate(definition, sizeof(SourceNodeDefinition) + portDefSize);
    }
    definitions.clear();
}

VCLG::SourceNodeDefinition* VCLG::DefinitionRegistry::CreateSourceNodeDefinition(VCL::Source* source) {
    VCL::ParseSyntaxOnlyAction action{};

    std::shared_ptr<VCL::CompilerInstance> instance = cc.CreateInstance();
    instance->BeginSource(source);
    if (!instance->ExecuteAction(action))
        return nullptr;
    instance->EndSource();

    VCL::TranslationUnitDecl* tu = instance->GetASTContext().GetTranslationUnitDecl();

    llvm::SmallVector<SourcePortDefinition*> ports{};
    llvm::SmallVector<SourcePortDefinition*> outPorts{};
    bool hasInstanceData = false;

    for (auto it = tu->Begin(); it != tu->End(); ++it) {
        switch (it->GetDeclClass()) {
            case VCL::Decl::VarDeclClass: {
                VCL::VarDecl* decl = (VCL::VarDecl*)it.Get();
                if (decl->HasAttribute(inputAttributeDefinition) != nullptr)
                    ports.push_back(CreateSourcePortDefinition(decl));
                else if (decl->HasAttribute(outputAttributeDefinition) != nullptr)
                    outPorts.push_back(CreateSourcePortDefinition(decl));
                else if (!hasInstanceData)
                    hasInstanceData = true;
                break;
            }
        }
    }

    ports.append(outPorts);

    size_t portDefSize = SourceNodeDefinition::totalSizeToAlloc<SourcePortDefinition*>(ports.size());
    SourceNodeDefinition* definition = (SourceNodeDefinition*)allocator->Allocate(sizeof(SourceNodeDefinition) + portDefSize, 4);
    new (definition) SourceNodeDefinition{ instance, hasInstanceData, ports };
    definitions.insert({ source->GetBufferIdentifier(), definition });
    return definition;
}

VCLG::SourcePortDefinition* VCLG::DefinitionRegistry::CreateSourcePortDefinition(VCL::VarDecl* varDecl) {
    std::string name = varDecl->GetIdentifierInfo()->GetName().str();
    std::string displayName = name;
    bool isInput = false;

    if (VCL::AttributeInstance* attribute = varDecl->HasAttribute(inputAttributeDefinition); attribute != nullptr) {
        displayName = GetStringAttribute(attribute);
        isInput = true;
    } else if (VCL::AttributeInstance* attribute = varDecl->HasAttribute(outputAttributeDefinition); attribute != nullptr) {
        displayName = GetStringAttribute(attribute);
        isInput = false;
    }

    SourcePortDefinition* definition = (SourcePortDefinition*)allocator->Allocate(sizeof(SourcePortDefinition), 4);
    new (definition) SourcePortDefinition{ name, displayName, isInput, varDecl };
    return definition;
}

std::string VCLG::DefinitionRegistry::GetStringAttribute(VCL::AttributeInstance* attribute) {
    if (attribute->GetArgsCount() != 1) {
        cc.GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
            .SetCompilerInfo(__FILE__, __func__, __LINE__)
            .Report();
        return std::string{};
    }
    VCL::ConstantValue* arg = attribute->GetArgs()[0];
    if (arg->GetConstantValueClass() != VCL::ConstantValue::ConstantStringClass) {
        cc.GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
            .SetCompilerInfo(__FILE__, __func__, __LINE__)
            .Report();
        return std::string{};
    }
    return VCL::ParseStringLiteral(((VCL::ConstantString*)arg)->GetString());
}