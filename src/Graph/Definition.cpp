#include <VCLG/Graph/Definition.hpp>

#include <VCLG/Graph/Directives.hpp>
#include <VCLG/AST/ASTInputConstWriter.hpp>
#include <VCLG/Core/Diagnostics.hpp>

#include <VCL/Core/Diagnostic.hpp>
#include <VCL/Core/Format.hpp>
#include <VCL/Core/Directive.hpp>
#include <VCL/AST/Decl.hpp>
#include <VCL/Sema/DefineTable.hpp>
#include <VCL/Frontend/FrontendActions.hpp>
#include <VCL/Frontend/CompilerInstance.hpp>
#include <VCL/AST/Template.hpp>
#include <VCL/AST/TypePrinter.hpp>

#include <iostream>


VCLG::DefinitionRegistry::DefinitionRegistry(VCL::CompilerContext& cc) : 
        cc{ cc }, definitions{} {
    
    nodeProcessAttributeDefinition = cc.GetAttributeTable().AddDefinition(cc.GetIdentifierTable().Get("NodeProcess"), 0, 0);
    nodeResetAttributeDefinition = cc.GetAttributeTable().AddDefinition(cc.GetIdentifierTable().Get("NodeReset"), 0, 0);
    inputAttributeDefinition = cc.GetAttributeTable().AddDefinition(cc.GetIdentifierTable().Get("Input"), 1, 1);
    outputAttributeDefinition = cc.GetAttributeTable().AddDefinition(cc.GetIdentifierTable().Get("Output"), 1, 1);
    parameterAttributeDefinition = cc.GetAttributeTable().AddDefinition(cc.GetIdentifierTable().Get("Parameter"), 1, 1);
    autoParameterAttributeDefinition = cc.GetAttributeTable().AddDefinition(cc.GetIdentifierTable().Get("AutoParameter"), 0, 0);

    VCL::IdentifierInfo* nodeNameDirectiveIdentifier = cc.GetIdentifierTable().Get("node_name");
    VCL::IdentifierInfo* graphInputDirectiveIdentifier = cc.GetIdentifierTable().Get("set_as_graph_input");
    VCL::IdentifierInfo* graphOutputDirectiveIdentifier = cc.GetIdentifierTable().Get("set_as_graph_output");

    cc.GetDirectiveRegistry().CreateDirectiveHandler<MetadataDirective>(nodeNameDirectiveIdentifier, "NODE_NAME", VCL::ConstantValue::ConstantStringClass);
    cc.GetDirectiveRegistry().CreateDirectiveHandler<MetadataFlagDirective>(graphInputDirectiveIdentifier, "IS_GRAPH_INPUT");
    cc.GetDirectiveRegistry().CreateDirectiveHandler<MetadataFlagDirective>(graphOutputDirectiveIdentifier, "IS_GRAPH_OUTPUT");
}

VCLG::DefinitionRegistry::~DefinitionRegistry() {
    Reset();
}

VCLG::SourceNodeDefinition* VCLG::DefinitionRegistry::GetOrCreateSourceNodeDefinition(VCL::Source* source) {
    if (auto it = definitions.find(source->GetBufferIdentifier()); it != definitions.end())
        return it->second.get();
    return CreateSourceNodeDefinition(source);
}

void VCLG::DefinitionRegistry::Reset() {
    // Also releases the CompilerInstances (and their ASTs) the definitions were parsed with.
    definitions.clear();
}

VCLG::SourceNodeDefinition* VCLG::DefinitionRegistry::CreateSourceNodeDefinition(VCL::Source* source) {
    // Inputs are read-only: a node writing one is rejected when it's loaded.
    ASTInputConstWriter inputConstWriter{ inputAttributeDefinition };
    VCL::ParseSyntaxOnlyAction action{};
    action.SetASTConsumer(&inputConstWriter);

    std::shared_ptr<VCL::CompilerInstance> instance = cc.CreateInstance();
    instance->BeginSource(source);
    bool parsed = instance->ExecuteAction(action);
    instance->EndSource();
    if (!parsed)
        return nullptr;

    VCL::TranslationUnitDecl* tu = instance->GetASTContext().GetTranslationUnitDecl();

    std::vector<SourcePortDefinition> ports{};
    std::vector<SourcePortDefinition> outPorts{};
    std::vector<SourceParameterDefinition> parameters{};
    std::vector<SourceAutoParameterDefinition> autoParameters{};
    bool hasInstanceData = false;
    VCL::FunctionDecl* entrypoint = nullptr;
    VCL::FunctionDecl* reset = nullptr;

    auto fail = [&](const char* message) -> SourceNodeDefinition* {
        cc.GetDiagnosticReporter().Error(VCL::Diagnostic::NodeDefinitionError, std::string{ message } + " (" + source->GetBufferIdentifier().str() + ")")
            .SetCompilerInfo(__FILE__, __func__, __LINE__)
            .Report();
        return nullptr;
    };

    for (auto it = tu->Begin(); it != tu->End(); ++it) {
        switch (it->GetDeclClass()) {
            case VCL::Decl::VarDeclClass: {
                VCL::VarDecl* decl = (VCL::VarDecl*)it.Get();
                if (decl->HasAttribute(inputAttributeDefinition) != nullptr)
                    ports.push_back(CreateSourcePortDefinition(decl, autoParameters));
                else if (decl->HasAttribute(outputAttributeDefinition) != nullptr)
                    outPorts.push_back(CreateSourcePortDefinition(decl, autoParameters));
                else if (decl->HasAttribute(parameterAttributeDefinition) != nullptr) {
                    parameters.push_back(CreateSourceParameterDefinition(decl));
                    hasInstanceData = true;
                } else if (decl->HasAttribute(autoParameterAttributeDefinition) != nullptr) {
                    autoParameters.push_back(CreateSourceAutoParameterDefinition(decl));
                    hasInstanceData = true;
                } else if (!hasInstanceData)
                    hasInstanceData = true;
                break;
            }
            case VCL::Decl::TypeAliasDeclClass: {
                VCL::TypeAliasDecl* decl = (VCL::TypeAliasDecl*)it.Get();
                if (decl->HasAttribute(autoParameterAttributeDefinition) != nullptr) {
                    autoParameters.push_back(CreateSourceAutoParameterDefinition(decl));
                    hasInstanceData = true;
                }
                break;
            }
            case VCL::Decl::FunctionDeclClass: {
                VCL::FunctionDecl* decl = (VCL::FunctionDecl*)it.Get();
                if (decl->HasAttribute(nodeProcessAttributeDefinition) != nullptr) {
                    if (entrypoint != nullptr)
                        return fail("more than one [NodeProcess] function");
                    entrypoint = decl;
                } else if (decl->HasAttribute(nodeResetAttributeDefinition) != nullptr) {
                    if (reset != nullptr)
                        return fail("more than one [NodeReset] function");
                    reset = decl;
                }
                break;
            }
        }
    }

    if (entrypoint == nullptr)
        return fail("no [NodeProcess] function");

    ports.insert(ports.end(), outPorts.begin(), outPorts.end());

    std::string displayName = GetStringDefine(instance, "NODE_NAME");

    std::unique_ptr<SourceNodeDefinition> ownedDefinition = std::make_unique<SourceNodeDefinition>(instance, displayName, entrypoint, reset, 
        hasInstanceData, std::move(ports), std::move(parameters), std::move(autoParameters));
    SourceNodeDefinition* definition = ownedDefinition.get();
    definitions.insert({ source->GetBufferIdentifier(), std::move(ownedDefinition) });

    if (HasFlagDefined(instance, "IS_GRAPH_INPUT"))
        definition->AddFlag(SourceNodeDefinition::DefinitionNodeFlag::IsInputNode);
    if (HasFlagDefined(instance, "IS_GRAPH_OUTPUT"))
        definition->AddFlag(SourceNodeDefinition::DefinitionNodeFlag::IsOutputNode);

    return definition;
}

VCLG::SourcePortDefinition VCLG::DefinitionRegistry::CreateSourcePortDefinition(VCL::VarDecl* varDecl, 
        llvm::ArrayRef<SourceAutoParameterDefinition> autoParameters) {
    std::string name = varDecl->GetIdentifierInfo()->GetName().str();
    std::string displayName = name;
    bool isInput = false;

    if (VCL::AttributeInstance* attribute = varDecl->HasAttribute(inputAttributeDefinition); attribute != nullptr) {
        displayName = GetStringAttribute(attribute, varDecl).value_or(name);
        isInput = true;
    } else if (VCL::AttributeInstance* attribute = varDecl->HasAttribute(outputAttributeDefinition); attribute != nullptr) {
        displayName = GetStringAttribute(attribute, varDecl).value_or(name);
        isInput = false;
    }

    VCL::Type* type = varDecl->GetValueType().GetType();
    bool isDependent = IsPortAutoParameterDependent(type, autoParameters);

    return SourcePortDefinition{ name, displayName, isInput, varDecl, isDependent };
}

VCLG::SourceParameterDefinition VCLG::DefinitionRegistry::CreateSourceParameterDefinition(VCL::VarDecl* varDecl) {
    std::string name = varDecl->GetIdentifierInfo()->GetName().str();
    std::string displayName = name;

    if (VCL::AttributeInstance* attribute = varDecl->HasAttribute(parameterAttributeDefinition); attribute != nullptr) {
        displayName = GetStringAttribute(attribute, varDecl).value_or(name);
    }

    return SourceParameterDefinition{ name, displayName, varDecl };
}

VCLG::SourceAutoParameterDefinition VCLG::DefinitionRegistry::CreateSourceAutoParameterDefinition(VCL::NamedDecl* decl) {
    return SourceAutoParameterDefinition{ decl->GetIdentifierInfo()->GetName().str(), decl };
}

std::optional<std::string> VCLG::DefinitionRegistry::GetStringAttribute(VCL::AttributeInstance* attribute, VCL::Decl* decl) {
    // The parser already enforces the argument count registered in the constructor.
    if (!VCLG_CHECK(cc.GetDiagnosticReporter(), attribute->GetArgsCount() == 1))
        return std::nullopt;
    VCL::ConstantValue* arg = attribute->GetArgs()[0];
    if (arg->GetConstantValueClass() != VCL::ConstantValue::ConstantStringClass) {
        cc.GetDiagnosticReporter().Error(VCL::Diagnostic::NodeDefinitionError,
                "[" + attribute->GetDefinition()->GetIdentifierInfo()->GetName().str() + "] expects a string argument")
            .AddHint(VCL::DiagnosticHint{ decl->GetSourceRange() })
            .Report();
        return std::nullopt;
    }
    return VCL::ParseStringLiteral(((VCL::ConstantString*)arg)->GetString());
}

std::string VCLG::DefinitionRegistry::GetStringDefine(std::shared_ptr<VCL::CompilerInstance> instance, llvm::StringRef name) {
    VCL::IdentifierInfo* identifier = instance->GetCompilerContext().GetIdentifierTable().Get(name);
    VCL::ConstantValue* value = instance->GetDefineTable().Get(identifier);

    if (!value)
        return std::string{};

    if (value->GetConstantValueClass() != VCL::ConstantValue::ConstantStringClass)
        return std::string{};

    return VCL::ParseStringLiteral(((VCL::ConstantString*)value)->GetString());
}

bool VCLG::DefinitionRegistry::HasFlagDefined(std::shared_ptr<VCL::CompilerInstance> instance, llvm::StringRef name) {
    VCL::IdentifierInfo* identifier = instance->GetCompilerContext().GetIdentifierTable().Get(name);
    VCL::ConstantValue* value = instance->GetDefineTable().Get(identifier);

    return value != nullptr;
}

bool VCLG::DefinitionRegistry::IsPortAutoParameterDependent(VCL::Type* portType, llvm::ArrayRef<SourceAutoParameterDefinition> autoParameters) {
    switch (portType->GetTypeClass()) {
        case VCL::Type::TypeAliasTypeClass:
            return IsTypeAliasPresentInAutoParameterList((VCL::TypeAliasType*)portType, autoParameters);
        case VCL::Type::ReferenceTypeClass:
            return IsPortAutoParameterDependent(((VCL::ReferenceType*)portType)->GetType().GetType(), autoParameters);
        case VCL::Type::TemplateSpecializationTypeClass: {
            VCL::TemplateArgumentList* args = ((VCL::TemplateSpecializationType*)portType)->GetTemplateArgumentList();
            bool r = false;
            for (VCL::TemplateArgument arg : args->GetArgs()) {
                switch (arg.GetKind()) {
                    case VCL::TemplateArgument::Type:
                        r |= IsPortAutoParameterDependent(arg.GetType().GetType(), autoParameters);
                        continue;
                    case VCL::TemplateArgument::Expression:
                        r |= IsExpressionDependentInAutoParameterList(arg.GetExpr(), autoParameters);
                        continue;
                    default:
                        continue;
                }
            }
            return r;
        }
        default:
            return false;
    }
}

bool VCLG::DefinitionRegistry::IsTypeAliasPresentInAutoParameterList(VCL::TypeAliasType* type, 
        llvm::ArrayRef<SourceAutoParameterDefinition> autoParameters) {
    for (const SourceAutoParameterDefinition& autoParameter : autoParameters) {
        if (autoParameter.GetDecl()->GetDeclClass() != VCL::Decl::TypeAliasDeclClass)
            continue;
        if (((VCL::TypeAliasDecl*)autoParameter.GetDecl())->GetType() == type)
            return true;
    }
    return false;
}

bool VCLG::DefinitionRegistry::IsExpressionDependentInAutoParameterList(VCL::Expr* expr, 
        llvm::ArrayRef<SourceAutoParameterDefinition> autoParameters) {
    if (expr->GetExprClass() != VCL::Expr::DeclRefExprClass)
        return false;
    VCL::Decl* decl = ((VCL::DeclRefExpr*)expr)->GetValueDecl();
    for (const SourceAutoParameterDefinition& autoParameter : autoParameters) {
        if (autoParameter.GetDecl() == decl)
            return true;
    }
    return false;
}