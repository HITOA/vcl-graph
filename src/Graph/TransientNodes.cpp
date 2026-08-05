#include <VCLG/Graph/TransientNodes.hpp>

#include <VCLG/Graph/GraphInstance.hpp>
#include <VCLG/CodeGen/CodeGenGraph.hpp>

#include <VCL/AST/Decl.hpp>
#include <VCL/Sema/Sema.hpp>
#include <VCL/CodeGen/CodeGenModule.hpp>

#include <unordered_set>


void VCLG::SubgraphOutputNode::Initialize() {
    AddFlag(NodeFlag::IsOutputNode);
    AddFlag(NodeFlag::IsDependent);
    VCL::ASTContext& context = owner.GetGraphContext().GetGlobalASTContext();
    VCL::IdentifierTable& identifierTable = owner.GetGraphContext().GetCompilerContext().GetIdentifierTable();
    VCL::Type* type = context.GetTypeCache().GetOrCreateBuiltinType(VCL::BuiltinType::Float32);
    VCL::TypeAliasDecl* aliasDecl = VCL::TypeAliasDecl::Create(context, identifierTable.Get("Generic"), type, VCL::SourceRange{});
    type = context.GetTypeCache().GetOrCreateTypeAliasType(type, aliasDecl);
    aliasDecl->SetType(type);
    Port* port = owner.InstantiatePort(GetIdentity(), type, "In", Port::PortKind::Input, nullptr, true);
    GetSubstitutionTable().SetTypeSubstitution(aliasDecl, nullptr);
    inPorts.push_back(port);
}

void VCLG::SubgraphOutputNode::Destroy() {
    owner.DestroyPort(inPorts[0]);
}
        
bool VCLG::SubgraphOutputNode::Emit(CodeGenGraph& codegen) {
    Port* inPort = GetInputs()[0];
    Port* outPort = codegen.GetInPortToOutPort(inPort);
    if (!outPort)
        return false;
    codegen.AddOutPortGlobalVar(inPort, codegen.GetOutPortGlobalVar(outPort));
    return true;
}

VCL::Type* VCLG::SubgraphOutputNode::GetType() {
    Port* port = inPorts[0];
    return port->GetSubstitutedType() ? port->GetSubstitutedType() : port->GetType();
}

void VCLG::SubgraphInputNode::Initialize() {
    AddFlag(NodeFlag::IsInputNode);
    VCL::ASTContext& context = owner.GetGraphContext().GetGlobalASTContext();
    Port* port = owner.InstantiatePort(GetIdentity(), type, "Out", Port::PortKind::Output, nullptr, false);
    outPorts.push_back(port);
}

void VCLG::SubgraphInputNode::Destroy() {
    owner.DestroyPort(outPorts[0]);
}

bool VCLG::SubgraphInputNode::Emit(CodeGenGraph& codegen) {
    std::shared_ptr<VCL::CompilerInstance> instance = codegen.GetGraphContext().GetCompilerContext().CreateInstance();
    
    instance->CreateASTContext();
    instance->CreateExportSymbolTable();
    instance->CreateImportModuleTable();
    instance->CreateDefineTable();

    VCL::Sema sema{ 
        instance->GetCompilerContext(),
        instance->GetASTContext(),
        instance->GetCompilerContext().GetDiagnosticReporter(),
        instance->GetCompilerContext().GetIdentifierTable(),
        instance->GetCompilerContext().GetDirectiveRegistry(),
        instance->GetExportSymbolTable(),
        instance->GetImportModuleTable(),
        instance->GetDefineTable() };
    
    VCL::IdentifierInfo* identifier = instance->GetCompilerContext().GetIdentifierTable().Get(GetDisplayName());

    VCL::VarDecl* varDecl = sema.ActOnVarDecl(type, identifier, VCL::VarDecl::VarAttrBitfield{ 0 }, nullptr, VCL::SourceRange{});
    
    VCL::CodeGenModule cgm{
        codegen.GetLLVMModule(), 
        instance->GetASTContext(), 
        instance->GetCompilerContext().GetDiagnosticReporter(),
        instance->GetCompilerContext().GetTarget(),
        instance->GetImportModuleTable(),
        instance->GetCompilerContext().GetAttributeTable(),
        instance->GetCompilerContext().GetIdentifierTable() };
    if (!cgm.EmitGlobalVarDecl(varDecl))
        return false;

    Port* outPort = GetOutputs()[0];
    std::optional<std::string> mangledName = instance->GetMangledSymbolName(identifier->GetName());
    if (!mangledName.has_value()) {
        instance->GetCompilerContext().GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
            .SetCompilerInfo(__FILE__, __func__, __LINE__)
            .Report();
        return false;
    }

    llvm::GlobalVariable* variable = codegen.GetLLVMModule().getGlobalVariable(mangledName.value(), true);
    if (!variable) {
        instance->GetCompilerContext().GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
            .SetCompilerInfo(__FILE__, __func__, __LINE__)
            .Report();
        return false;
    }

    codegen.AddOutPortGlobalVar(outPort, variable);
    return true;
}

void VCLG::SubgraphNode::Initialize() {

}

void VCLG::SubgraphNode::Destroy() {
    for (Port* port : outPorts)
        owner.DestroyPort(port);
    for (Port* port : inPorts)
        owner.DestroyPort(port);
}

bool VCLG::SubgraphNode::Emit(CodeGenGraph& codegen) {
    CodeGenGraph current{ instance->GetGraphContext(), *instance, codegen.GetLLVMModule() };
    if (!current.Emit())
        return false;

    codegen.ImportSubgraph(current);

    for (Node* node : instance->GetNodes()) {
        if (node->GetNodeClass() != Node::TransientNodeClass)
            continue;
        TransientNode* transientNode = (TransientNode*)node;
        if (transientNode->GetHash() == typeid(SubgraphOutputNode).hash_code()) {
            SubgraphOutputNode* outputNode = (SubgraphOutputNode*)transientNode;
            if (nodeToPort.count(transientNode->GetIdentity())) {
                Port* port = owner.GetPortByIdentity(nodeToPort.at(transientNode->GetIdentity()));
                llvm::GlobalVariable* variable = current.GetOutPortGlobalVar(outputNode->GetInputs()[0]);
                codegen.AddOutPortGlobalVar(port, variable);
            }
        } else if (transientNode->GetHash() == typeid(SubgraphInputNode).hash_code()) {
            SubgraphInputNode* inputNode = (SubgraphInputNode*)transientNode;
            if (nodeToPort.count(transientNode->GetIdentity())) {
                Port* port = owner.GetPortByIdentity(nodeToPort.at(transientNode->GetIdentity()));
                llvm::GlobalVariable* variable = current.GetOutPortGlobalVar(inputNode->GetOutputs()[0]);
                Port* connectedPort = codegen.GetInPortToOutPort(port);
                llvm::GlobalVariable* connectedVariable = codegen.GetOutPortGlobalVar(connectedPort);
                variable->setInitializer(connectedVariable->getInitializer());
                variable->replaceAllUsesWith(connectedVariable);
                variable->eraseFromParent();
            }
        }
    }

    return true;
}

void VCLG::SubgraphNode::SetGraph(std::shared_ptr<GraphInstance> instance) {
    this->instance = instance;
    Update();
}

void VCLG::SubgraphNode::Update() {
    displayName = instance->GetName();
    
    outPorts.clear();
    inPorts.clear();

    std::unordered_set<Identity> visitedNode{};

    for (Node* node : instance->GetNodes()) {
        if (node->GetNodeClass() != Node::TransientNodeClass)
            continue;
        TransientNode* transientNode = (TransientNode*)node;
        if (transientNode->GetHash() == typeid(SubgraphOutputNode).hash_code()) {
            SubgraphOutputNode* outputNode = (SubgraphOutputNode*)transientNode;
            if (!nodeToPort.count(transientNode->GetIdentity())) {
                Port* port = owner.InstantiatePort(
                    GetIdentity(), outputNode->GetType(), outputNode->GetDisplayName().str(), Port::PortKind::Output, nullptr, false);
                nodeToPort.insert({ transientNode->GetIdentity(), port->GetIdentity() });
                outPorts.push_back(port);
            } else {
                Port* port = owner.GetPortByIdentity(nodeToPort.at(transientNode->GetIdentity()));
                if (port->GetType() != outputNode->GetType()) {
                    owner.DestroyPort(port);
                    port = owner.InstantiatePort(
                        GetIdentity(), outputNode->GetType(), outputNode->GetDisplayName().str(), Port::PortKind::Output, nullptr, false);
                    nodeToPort[transientNode->GetIdentity()] = port->GetIdentity();
                }
                port->SetDisplayName(outputNode->GetDisplayName().str());
                outPorts.push_back(port);
            }
            visitedNode.insert(transientNode->GetIdentity());
        } else if (transientNode->GetHash() == typeid(SubgraphInputNode).hash_code()) {
            SubgraphInputNode* inputNode = (SubgraphInputNode*)transientNode;
            if (!nodeToPort.count(transientNode->GetIdentity())) {
                Port* port = owner.InstantiatePort(
                    GetIdentity(), inputNode->GetType(), inputNode->GetDisplayName().str(), Port::PortKind::Input, nullptr, false);
                nodeToPort.insert({ transientNode->GetIdentity(), port->GetIdentity() });
                inPorts.push_back(port);
            } else {
                Port* port = owner.GetPortByIdentity(nodeToPort.at(transientNode->GetIdentity()));
                port->SetDisplayName(inputNode->GetDisplayName().str());
                inPorts.push_back(port);
            }
            visitedNode.insert(transientNode->GetIdentity());
        }
    }

    std::vector<Identity> toRemove{};

    for (auto& entry : nodeToPort) {
        if (visitedNode.count(entry.first))
            continue;
        Port* port = owner.GetPortByIdentity(entry.second);
        owner.DestroyPort(port);
        toRemove.push_back(entry.first);
    }

    for (Identity& identity : toRemove)
        nodeToPort.erase(identity);
}
