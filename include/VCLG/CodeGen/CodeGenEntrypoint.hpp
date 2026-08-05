#pragma once

#include <llvm/ADT/DenseMap.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/BasicBlock.h>


namespace VCLG {
    class CodeGenGraph;

    class CodeGenEntrypoint {
    public:
        CodeGenEntrypoint() = delete;
        CodeGenEntrypoint(CodeGenGraph& cgg, llvm::StringRef name);
        CodeGenEntrypoint(const CodeGenEntrypoint& other) = delete;
        CodeGenEntrypoint(CodeGenEntrypoint&& other) = delete;
        ~CodeGenEntrypoint() = default;

        CodeGenEntrypoint& operator=(const CodeGenEntrypoint& other) = delete;
        CodeGenEntrypoint& operator=(CodeGenEntrypoint&& other) = delete;

        inline llvm::IRBuilder<>& GetIRBuilder() { return builder; }

        void Begin();
        void End();

        bool AddNodeEntrypoint(llvm::Function* callee);

    private:
        CodeGenGraph& cgg;
        llvm::StringRef name;
        
        llvm::Function* function;
        llvm::BasicBlock* bb;
        llvm::IRBuilder<> builder;
        bool owner;
    };

}