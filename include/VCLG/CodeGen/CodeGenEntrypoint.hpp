#pragma once

#include <llvm/ADT/DenseMap.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>


namespace VCLG {
    class CodeGenGraph;

    class CodeGenEntrypoint {
    public:
        CodeGenEntrypoint() = delete;
        CodeGenEntrypoint(CodeGenGraph& cgg);
        CodeGenEntrypoint(const CodeGenEntrypoint& other) = delete;
        CodeGenEntrypoint(CodeGenEntrypoint&& other) = delete;
        ~CodeGenEntrypoint() = default;

        CodeGenEntrypoint& operator=(const CodeGenEntrypoint& other) = delete;
        CodeGenEntrypoint& operator=(CodeGenEntrypoint&& other) = delete;

        void Begin();
        void End();

        bool AddNodeEntrypoint(llvm::Function* callee);

    private:
        CodeGenGraph& cgg;

        llvm::Function* function;
        llvm::IRBuilder<> builder;
    };

}