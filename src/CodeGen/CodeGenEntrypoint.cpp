#include <VCLG/CodeGen/CodeGenEntrypoint.hpp>

#include <VCLG/CodeGen/CodeGenGraph.hpp>

#include <iostream>


VCLG::CodeGenEntrypoint::CodeGenEntrypoint(CodeGenGraph& cgg, llvm::StringRef name) : cgg{ cgg }, name{ name }, function{}, builder{ cgg.GetLLVMContext() } {}

void VCLG::CodeGenEntrypoint::Begin() {
    llvm::FunctionType* functionType = llvm::FunctionType::get(llvm::Type::getVoidTy(cgg.GetLLVMContext()), false);
    function = llvm::cast<llvm::Function>(cgg.GetLLVMModule().getOrInsertFunction(name, functionType).getCallee());
    function->setLinkage(llvm::GlobalValue::ExternalLinkage);
    function->setDSOLocal(true);
    if (function->size() > 0) {
        bb = &function->front();
        owner = false;
    } else {
        bb = llvm::BasicBlock::Create(cgg.GetLLVMContext(), "entry", function);
        owner = true;
    }
    
    builder.SetInsertPoint(bb, bb->end());
}

void VCLG::CodeGenEntrypoint::End() {
    if (!bb->getTerminator() && owner == true) {
        builder.SetInsertPoint(bb, bb->end());
        builder.CreateRetVoid();
    }
}

bool VCLG::CodeGenEntrypoint::AddNodeEntrypoint(llvm::Function* callee) {
    builder.SetInsertPoint(bb, bb->end());
    builder.CreateCall(callee);
    return true;
}