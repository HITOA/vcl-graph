#pragma once

#include <VCLG/CodeGen/CodeGenGraph.hpp>
#include <VCLG/CodeGen/SlotPlanner.hpp>
#include <VCLG/Translation/Translation.hpp>

#include <VCL/AST/ConstantValue.hpp>
#include <VCL/CodeGen/CodeGenModule.hpp>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>

#include <optional>
#include <vector>


namespace VCLG {

    /** The initial value of a variable of `type` given as a scalar: splat across a vector, or across each lane. */
    llvm::Constant* MakeScalarInitializer(VCL::CodeGenModule& cgm, VCL::ConstantScalar value, VCL::Type* type, uint32_t width);

    /**
     * Emits a frame from its slot plan (`state-as-data.md` §5.4-§5.7): `Main(ui)`, which calls
     * every node's processing entry point with its slots, and `Reset(ui)`, which runs each node's
     * `__Init` and `[NodeReset]` and initializes the feedback regions. Only the root frame so far:
     * host slots are external globals the host binds (the layout's symbols).
     */
    class CodeGenFrame {
    public:
        CodeGenFrame(GraphContext& graphContext, VCL::CompilerContext& cc, llvm::Module& module, const ElaboratedGraph& graph,
            const SlotPlan& plan, llvm::ArrayRef<std::optional<TranslatedNode>> nodes, const CodeGenGraphOptions& options);

        bool EmitMain();
        bool EmitReset();

    private:
        using NodeIndex = ElaboratedGraph::NodeIndex;

        llvm::Function* CreateFrameFunction(llvm::StringRef name);
        /** The address of `slot`, in the frame being emitted (temporaries: its alloca). */
        llvm::Value* GetSlot(uint32_t slot);
        llvm::GlobalVariable* GetRegion(uint32_t region);
        llvm::GlobalVariable* GetConstant(uint32_t slot);
        llvm::Type* ConvertType(VCL::Type* type);

        /** A converter's call: reads `edge`'s producer output, writes the slot `to`. */
        bool EmitConversion(NodeIndex node, const ElaboratedGraph::Edge& edge, VCL::Type* toType, llvm::Value* to);
        bool EmitProcessCall(NodeIndex index);
        /** The node's port arguments as `Main` passes them; `resetInputs` gives connected inputs zeros instead. */
        bool GetPortArguments(NodeIndex index, llvm::Function* callee, bool resetInputs, llvm::SmallVectorImpl<llvm::Value*>& args);
        void EmitCanaryCheck(uint32_t slot);
        llvm::Function* GetCanaryFunction();
        bool ReportError(NodeIndex node, const std::string& message);

    private:
        GraphContext& graphContext;
        VCL::CompilerContext& cc;
        llvm::Module& module;
        const ElaboratedGraph& graph;
        const SlotPlan& plan;
        llvm::ArrayRef<std::optional<TranslatedNode>> nodes;
        const CodeGenGraphOptions& options;
        VCL::ModuleTable imports{};
        VCL::CodeGenModule cgm;
        llvm::IRBuilder<> builder;

        std::vector<llvm::GlobalVariable*> regions{};
        std::vector<llvm::GlobalVariable*> constants{};
        /** The frame being emitted: the address of each slot used so far (temporaries are per frame). */
        std::vector<llvm::Value*> frameSlots{};
        /** `Reset`: the zeros connected inputs passed by reference point at. */
        llvm::Value* zeros = nullptr;
        llvm::Function* canary = nullptr;
    };

}
