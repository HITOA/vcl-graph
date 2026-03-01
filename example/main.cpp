#include <iostream>

#include <VCLG/Graph/GraphContext.hpp>
#include <VCLG/Graph/GraphInstance.hpp>
#include <VCLG/CodeGen/CodeGenGraph.hpp>

#include <VCL/AST/TypePrinter.hpp>
#include <VCL/Frontend/TextDiagnosticConsumer.hpp>

#include <format>
#include <string>

int main() { 
    VCL::TextDiagnosticConsumer diagnosticConsumer{};
    std::shared_ptr<VCL::CompilerInvocation> invocation = std::make_shared<VCL::CompilerInvocation>();
    invocation->GetDiagnosticOptions().SetDiagnosticConsumer(&diagnosticConsumer);

    VCLG::GraphContext context{ invocation };

    std::shared_ptr<VCLG::GraphInstance> graph = context.CreateInstance();

    VCL::Source* addNodeSource = context.GetCompilerContext().GetSourceManager().LoadFromDisk("./Nodes/Add.vcl");

    VCLG::SourceNode* addNode1 = graph->InstantiateSourceNode(addNodeSource);
    VCLG::SourceNode* addNode2 = graph->InstantiateSourceNode(addNodeSource);

    addNode1->AddFlag(VCLG::Node::NodeFlag::IsInputNode);
    addNode2->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);

    graph->Connect(addNode2->GetInputs()[0], addNode1->GetOutputs()[0]);

    llvm::orc::ThreadSafeModule module = llvm::orc::ThreadSafeModule{ 
        context.GetCompilerContext().GetLLVMContext().withContextDo([](llvm::LLVMContext* context){
            return std::make_unique<llvm::Module>("compiled graph", *context);
        }),
        context.GetCompilerContext().GetLLVMContext() };
    
    bool r = module.withModuleDo([&](llvm::Module& module){
        VCLG::CodeGenGraph codeGenGraph{ context, *graph, module };
        return codeGenGraph.Emit();
    });

    std::cout << (r == true ? "Success" : "Failed") << std::endl;

    module.getModuleUnlocked()->dump();

    return 0;
}