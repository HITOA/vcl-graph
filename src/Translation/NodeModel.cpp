#include <VCLG/Translation/NodeModel.hpp>
#include <VCLG/Core/Diagnostics.hpp>

#include <string>


namespace {

    bool IsNodeFunction(VCL::FunctionDecl* decl) {
        return !decl->HasFunctionFlag(VCL::FunctionDecl::IsIntrinsic);
    }

}

std::optional<VCLG::NodeModel> VCLG::NodeModel::Build(VCL::TranslationUnitDecl* tu, const NodeAttributes& attributes,
        VCL::DiagnosticReporter& reporter, llvm::StringRef nodeName) {
    NodeModel model{ tu, attributes };
    llvm::SmallVector<VCL::VarDecl*, 4> outputs{};

    auto fail = [&](const char* message) -> std::optional<NodeModel> {
        ReportNodeError(reporter, std::string{ message } + " (" + nodeName.str() + ")")
            .SetCompilerInfo(__FILE__, __func__, __LINE__)
            .Report();
        return std::nullopt;
    };

    for (auto it = tu->Begin(); it != tu->End(); ++it) {
        switch (it->GetDeclClass()) {
            case VCL::Decl::VarDeclClass: {
                VCL::VarDecl* decl = (VCL::VarDecl*)it.Get();
                VarKind kind = VarKind::State;
                if (decl->HasAttribute(attributes.input) != nullptr)
                    kind = VarKind::Input;
                else if (decl->HasAttribute(attributes.output) != nullptr)
                    kind = VarKind::Output;
                else if (decl->HasAttribute(attributes.parameter) != nullptr)
                    kind = VarKind::Parameter;
                else if (decl->HasAttribute(attributes.autoParameter) != nullptr)
                    kind = VarKind::AutoParameter;
                else if (decl->HasInAttribute() || decl->HasOutAttribute())
                    kind = VarKind::Host;
                else if (decl->GetValueType().HasQualifier(VCL::Qualifier::Const))
                    kind = VarKind::Constant;
                model.varKinds[decl] = kind;
                switch (kind) {
                    case VarKind::Input: model.ports.push_back(decl); break;
                    case VarKind::Output: outputs.push_back(decl); break;
                    case VarKind::Parameter: model.parameters.push_back(decl); break;
                    case VarKind::AutoParameter: model.autoParameters.push_back(decl); break;
                    case VarKind::State: model.state.push_back(decl); break;
                    default: break;
                }
                break;
            }
            case VCL::Decl::TypeAliasDeclClass: {
                VCL::TypeAliasDecl* decl = (VCL::TypeAliasDecl*)it.Get();
                if (decl->HasAttribute(attributes.autoParameter) != nullptr)
                    model.autoParameters.push_back(decl);
                break;
            }
            case VCL::Decl::FunctionDeclClass: {
                VCL::FunctionDecl* decl = (VCL::FunctionDecl*)it.Get();
                if (!IsNodeFunction(decl))
                    break;
                if (decl->HasAttribute(attributes.nodeProcess) != nullptr) {
                    if (model.process != nullptr)
                        return fail("more than one [NodeProcess] function");
                    model.process = decl;
                } else if (decl->HasAttribute(attributes.nodeReset) != nullptr) {
                    if (model.reset != nullptr)
                        return fail("more than one [NodeReset] function");
                    model.reset = decl;
                }
                if (decl->GetBody() != nullptr)
                    model.functions.push_back(decl);
                break;
            }
            case VCL::Decl::TemplateDeclClass: {
                VCL::TemplateDecl* decl = (VCL::TemplateDecl*)it.Get();
                VCL::NamedDecl* templated = decl->GetTemplatedNamedDecl();
                if (templated != nullptr && templated->GetDeclClass() == VCL::Decl::FunctionDeclClass
                        && IsNodeFunction((VCL::FunctionDecl*)templated))
                    model.functionTemplates.push_back(decl);
                break;
            }
            default:
                break;
        }
    }

    if (model.process == nullptr)
        return fail("no [NodeProcess] function");

    model.inputCount = (uint32_t)model.ports.size();
    model.ports.append(outputs.begin(), outputs.end());
    return model;
}

std::optional<VCLG::NodeModel::VarKind> VCLG::NodeModel::GetVarKind(const VCL::Decl* decl) const {
    auto it = varKinds.find(decl);
    if (it == varKinds.end())
        return std::nullopt;
    return it->second;
}

std::optional<uint32_t> VCLG::NodeModel::GetPortIndex(const VCL::Decl* decl) const {
    for (uint32_t i = 0; i < ports.size(); ++i)
        if (ports[i] == decl)
            return i;
    return std::nullopt;
}

bool VCLG::NodeModel::IsAlwaysWritten(const VCL::VarDecl* port) const {
    return IsOutput(port) && ((VCL::Decl*)port)->HasAttribute(attributes.alwaysWritten) != nullptr;
}

VCLG::Exposure VCLG::NodeModel::GetExposure(const VCL::Decl* decl) const {
    VCL::AttributeInstance* attribute = ((VCL::Decl*)decl)->HasAttribute(attributes.expose);
    if (attribute == nullptr)
        return Exposure::None;
    for (VCL::ConstantValue* arg : attribute->GetArgs())
        if (arg->GetConstantValueClass() == VCL::ConstantValue::ConstantIdentifierClass
                && ((VCL::ConstantIdentifier*)arg)->GetIdentifierInfo()->GetName() == "Write")
            return Exposure::Write;
    return Exposure::Read;
}
