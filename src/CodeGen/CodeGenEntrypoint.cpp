#include <VCLG/CodeGen/CodeGenEntrypoint.hpp>

#include <VCLG/CodeGen/CodeGenGraph.hpp>


VCLG::CodeGenEntrypoint::CodeGenEntrypoint(CodeGenGraph& cgg, llvm::StringRef name) : cgg{ cgg }, name{ name }, function{}, builder{ cgg.GetLLVMContext() } {}

void VCLG::CodeGenEntrypoint::Begin() {
    llvm::FunctionType* functionType = llvm::FunctionType::get(llvm::Type::getVoidTy(cgg.GetLLVMContext()), false);
    function = llvm::cast<llvm::Function>(cgg.GetLLVMModule().getOrInsertFunction(name, functionType).getCallee());
    function->setLinkage(llvm::GlobalValue::ExternalLinkage);
    function->setDSOLocal(true);
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(cgg.GetLLVMContext(), "entry", function);
    builder.SetInsertPoint(bb);
}

void VCLG::CodeGenEntrypoint::End() {
    builder.CreateRetVoid();
}

bool VCLG::CodeGenEntrypoint::AddNodeEntrypoint(llvm::Function* callee) {
    builder.CreateCall(callee);
    return true;
}