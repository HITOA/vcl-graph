#include <VCLG/Graph/BuiltinNodes.hpp>

#include <VCLG/Graph/GraphInstance.hpp>
#include <VCLG/CodeGen/CodeGenGraph.hpp>
#include <VCLG/Core/Diagnostics.hpp>

#include <VCL/AST/Decl.hpp>
#include <VCL/Sema/Sema.hpp>
#include <VCL/CodeGen/CodeGenModule.hpp>

#include <unordered_set>


// A mistake in the graph the user can fix. The node itself is identified by the NodeDiagnosticScope
// CodeGenGraph opens around it.
static bool ReportGraphError(VCLG::CodeGenGraph& codegen, const std::string& message) {
    codegen.GetGraphContext().GetCompilerContext().GetDiagnosticReporter().Error(VCL::Diagnostic::CustomDiagnostic, message)
        .Report();
    return false;
}

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
        return ReportGraphError(codegen, "subgraph output is not connected");
    llvm::GlobalVariable* variable = codegen.GetOutPortGlobalVar(outPort);
    if (!VCLG_CHECK(codegen.GetGraphContext().GetCompilerContext().GetDiagnosticReporter(), variable != nullptr))
        return false;
    codegen.AddOutPortGlobalVar(inPort, variable);
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
    
    instance->SetManglingPrefix(codegen.GetNodeManglingPrefix(this));
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
    if (!VCLG_CHECK(instance->GetCompilerContext().GetDiagnosticReporter(), mangledName.has_value()))
        return false;

    llvm::GlobalVariable* variable = codegen.GetLLVMModule().getGlobalVariable(mangledName.value(), true);
    if (!VCLG_CHECK(instance->GetCompilerContext().GetDiagnosticReporter(), variable != nullptr))
        return false;

    codegen.AddOutPortGlobalVar(outPort, variable);
    return true;
}

VCL::Type* VCLG::SubgraphInputNode::GetType() {
    return type;
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
    for (Node* node : instance->GetNodes()) {
        if (SubgraphInputNode* inputNode = llvm::dyn_cast<SubgraphInputNode>(node)) {
            if (nodeToPort.count(inputNode->GetIdentity())) {
                Port* port = owner.GetPortByIdentity(nodeToPort.at(inputNode->GetIdentity()));
                if (port->GetOverrideType() != nullptr)
                    inputNode->GetOutputs()[0]->SetOverrideType(port->GetOverrideType());
            }
        }
    }

    // Scoped under this SubgraphNode: the same subgraph used by another SubgraphNode gets its own symbols.
    CodeGenGraph current{ instance->GetGraphContext(), *instance, codegen.GetLLVMModule(), codegen.GetNodeManglingPrefix(this) };
    if (!current.Emit())
        return false;

    codegen.ImportSubgraph(current);

    for (Node* node : instance->GetNodes()) {
        if (SubgraphOutputNode* outputNode = llvm::dyn_cast<SubgraphOutputNode>(node)) {
            if (nodeToPort.count(outputNode->GetIdentity())) {
                Port* port = owner.GetPortByIdentity(nodeToPort.at(outputNode->GetIdentity()));
                llvm::GlobalVariable* variable = current.GetOutPortGlobalVar(outputNode->GetInputs()[0]);
                if (!variable)
                    continue;
                codegen.AddOutPortGlobalVar(port, variable);
            }
        } else if (SubgraphInputNode* inputNode = llvm::dyn_cast<SubgraphInputNode>(node)) {
            if (nodeToPort.count(inputNode->GetIdentity())) {
                Port* port = owner.GetPortByIdentity(nodeToPort.at(inputNode->GetIdentity()));
                llvm::GlobalVariable* variable = current.GetOutPortGlobalVar(inputNode->GetOutputs()[0]);
                if (!variable)
                    continue;
                Port* connectedPort = codegen.GetInPortToOutPort(port);
                if (connectedPort != nullptr) {
                    llvm::GlobalVariable* connectedVariable = codegen.GetOutPortGlobalVar(connectedPort);
                    variable->setInitializer(connectedVariable->getInitializer());
                    variable->replaceAllUsesWith(connectedVariable);
                    variable->eraseFromParent();
                } else if (port->GetInitializerOverride()) {
                    VCL::Type* type = VCL::Type::GetCanonicalType(inputNode->GetOutputs()[0]->GetType());
                    std::shared_ptr<VCL::CompilerInstance> instance = codegen.GetGraphContext().GetCompilerContext().CreateInstance();
                    instance->SetManglingPrefix(codegen.GetNodeManglingPrefix(this) + "/init");
                    instance->CreateASTContext();
                    instance->CreateExportSymbolTable();
                    instance->CreateImportModuleTable();
                    instance->CreateDefineTable();
                    VCL::CodeGenModule cgm{
                        codegen.GetLLVMModule(), 
                        instance->GetASTContext(), 
                        instance->GetCompilerContext().GetDiagnosticReporter(),
                        instance->GetCompilerContext().GetTarget(),
                        instance->GetImportModuleTable(),
                        instance->GetCompilerContext().GetAttributeTable(),
                        instance->GetCompilerContext().GetIdentifierTable() };
                    llvm::Constant* value = cgm.GenerateConstantValue(port->GetInitializerOverride());
                    uint32_t s = codegen.GetGraphContext().GetCompilerContext().GetTarget().GetVectorWidthInElement();
                    if (type->GetTypeClass() == VCL::Type::VectorTypeClass)
                        value = llvm::ConstantDataVector::getSplat(s, value);
                    if (type->GetTypeClass() == VCL::Type::LanesTypeClass) {
                        llvm::ArrayType* arrayType = llvm::ArrayType::get(value->getType(), s);
                        llvm::SmallVector<llvm::Constant*> elems{};
                        elems.assign(s, value);
                        value = llvm::ConstantArray::get(arrayType, elems);
                    }
                    variable->setInitializer(value);
                }
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
        if (SubgraphOutputNode* outputNode = llvm::dyn_cast<SubgraphOutputNode>(node)) {
            if (!nodeToPort.count(outputNode->GetIdentity())) {
                Port* port = owner.InstantiatePort(
                    GetIdentity(), outputNode->GetType(), outputNode->GetDisplayName().str(), Port::PortKind::Output, nullptr, false);
                nodeToPort.insert({ outputNode->GetIdentity(), port->GetIdentity() });
                outPorts.push_back(port);
            } else {
                Port* port = owner.GetPortByIdentity(nodeToPort.at(outputNode->GetIdentity()));
                if (port->GetType() != outputNode->GetType()) {
                    owner.DestroyPort(port);
                    port = owner.InstantiatePort(
                        GetIdentity(), outputNode->GetType(), outputNode->GetDisplayName().str(), Port::PortKind::Output, nullptr, false);
                    nodeToPort[outputNode->GetIdentity()] = port->GetIdentity();
                }
                port->SetDisplayName(outputNode->GetDisplayName().str());
                outPorts.push_back(port);
            }
            visitedNode.insert(outputNode->GetIdentity());
        } else if (SubgraphInputNode* inputNode = llvm::dyn_cast<SubgraphInputNode>(node)) {
            if (!nodeToPort.count(inputNode->GetIdentity())) {
                Port* port = owner.InstantiatePort(
                    GetIdentity(), inputNode->GetType(), inputNode->GetDisplayName().str(), Port::PortKind::Input, nullptr, false);
                nodeToPort.insert({ inputNode->GetIdentity(), port->GetIdentity() });
                inPorts.push_back(port);
            } else {
                Port* port = owner.GetPortByIdentity(nodeToPort.at(inputNode->GetIdentity()));
                port->SetDisplayName(inputNode->GetDisplayName().str());
                inPorts.push_back(port);
            }
            visitedNode.insert(inputNode->GetIdentity());
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

void VCLG::FeedbackInputNode::Initialize() {
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

void VCLG::FeedbackInputNode::Destroy() {
    owner.DestroyPort(inPorts[0]);
}

bool VCLG::FeedbackInputNode::Emit(CodeGenGraph& codegen) {
    Port* inPort = GetInputs()[0];
    // Declared by the FeedbackOutputNode(s) reading this feedback, which the execution order emits first.
    llvm::GlobalVariable* variable = codegen.GetOutPortGlobalVar(inPort);
    if (variable == nullptr)
        return ReportGraphError(codegen, "feedback is never read: no feedback output is linked to it");
    Port* outPort = codegen.GetInPortToOutPort(inPort);
    if (!outPort)
        return ReportGraphError(codegen, "feedback input is not connected");
    llvm::GlobalVariable* connectedVariable = codegen.GetOutPortGlobalVar(outPort);
    if (!VCLG_CHECK(codegen.GetGraphContext().GetCompilerContext().GetDiagnosticReporter(), connectedVariable != nullptr))
        return false;
    variable->setInitializer(connectedVariable->getInitializer());
    variable->replaceAllUsesWith(connectedVariable);
    variable->eraseFromParent();
    return true;
}

VCL::Type* VCLG::FeedbackInputNode::GetType() {
    Port* port = inPorts[0];
    return port->GetSubstitutedType() ? port->GetSubstitutedType() : port->GetType();
}

void VCLG::FeedbackOutputNode::Initialize() {

}

void VCLG::FeedbackOutputNode::Destroy() {
    for (Port* port : outPorts)
        owner.DestroyPort(port);
}

bool VCLG::FeedbackOutputNode::Emit(CodeGenGraph& codegen) {
    if (feedbackIdentity == INVALID_IDENTITY)
        return ReportGraphError(codegen, "feedback output is not linked to a feedback input");
    FeedbackInputNode* feedbackInputNode = (FeedbackInputNode*)owner.GetNodeByIdentity(feedbackIdentity);
    Port* feedbackInPort = feedbackInputNode->GetInputs()[0];
    llvm::GlobalVariable* variable = codegen.GetOutPortGlobalVar(feedbackInPort);
    
    if (variable == nullptr) {
        std::shared_ptr<VCL::CompilerInstance> instance = codegen.GetGraphContext().GetCompilerContext().CreateInstance();
        
        instance->SetManglingPrefix(codegen.GetNodeManglingPrefix(this));
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
        
        VCL::IdentifierInfo* identifier = instance->GetCompilerContext().GetIdentifierTable().Get(feedbackInputNode->GetDisplayName());

        VCL::VarDecl* varDecl = sema.ActOnVarDecl(feedbackInputNode->GetType(), identifier, VCL::VarDecl::VarAttrBitfield{ 0 }, nullptr, VCL::SourceRange{});
        
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

        std::optional<std::string> mangledName = instance->GetMangledSymbolName(identifier->GetName());
        if (!VCLG_CHECK(instance->GetCompilerContext().GetDiagnosticReporter(), mangledName.has_value()))
            return false;

        variable = codegen.GetLLVMModule().getGlobalVariable(mangledName.value(), true);
        if (!VCLG_CHECK(instance->GetCompilerContext().GetDiagnosticReporter(), variable != nullptr))
            return false;
        codegen.AddOutPortGlobalVar(feedbackInPort, variable);
    }

    codegen.AddOutPortGlobalVar(GetOutputs()[0], variable);
    return true;
}

void VCLG::FeedbackOutputNode::Update(Identity feedbackIdentity) {
    if (feedbackIdentity == INVALID_IDENTITY) {
        for (Port* port : outPorts)
            owner.DestroyPort(port);
        outPorts.clear();
        this->feedbackIdentity = feedbackIdentity;
        return;
    }

    VCLG::Node* node = owner.GetNodeByIdentity(feedbackIdentity);
    VCLG::FeedbackInputNode* feedbackNode = llvm::dyn_cast_or_null<VCLG::FeedbackInputNode>(node);
    if (!feedbackNode)
        return;

    displayName = feedbackNode->GetDisplayName();
    if (this->feedbackIdentity == feedbackNode->GetIdentity()) {
        Port* port = GetOutputs()[0];
        if (port->GetType() != feedbackNode->GetType()) {
            outPorts[0] = owner.OverwritePort(
                port, GetIdentity(), feedbackNode->GetType(), "Out", Port::PortKind::Output, nullptr, false);
        }
    } else {
        if (!GetOutputs().empty()) {
            Port* port = GetOutputs()[0];
            outPorts[0] = owner.OverwritePort(
                    port, GetIdentity(), feedbackNode->GetType(), "Out", Port::PortKind::Output, nullptr, false);
        } else {
            Port* port = owner.InstantiatePort(
                GetIdentity(), feedbackNode->GetType(), "Out", Port::PortKind::Output, nullptr, false);
            outPorts.push_back(port);
            this->feedbackIdentity = feedbackNode->GetIdentity();
        }
    }
}
