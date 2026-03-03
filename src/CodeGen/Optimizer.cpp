#include <VCLG/CodeGen/Optimizer.hpp>

#include <VCLG/CodeGen/CodeGenGraph.hpp>

#include <llvm/Passes/PassBuilder.h>


bool VCLG::Optimizer::Optimize(CodeGenGraph& cgg) {
    llvm::LoopAnalysisManager lam{};
    llvm::FunctionAnalysisManager fam{};
    llvm::CGSCCAnalysisManager cgam{};
    llvm::ModuleAnalysisManager mam{};

    llvm::PassBuilder pb{};

    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);

    pb.crossRegisterProxies(lam, fam, cgam, mam);

    llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O3);

    if (!cgg.LinkNow())
        return false;

    mpm.run(cgg.GetLLVMModule(), mam);
    return true;
}