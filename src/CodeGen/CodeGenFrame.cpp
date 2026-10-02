#include "CodeGenFrame.hpp"

#include "ElaboratedDiagnosticScope.hpp"

#include <VCLG/Core/Diagnostics.hpp>
#include <VCLG/Graph/Converter.hpp>

#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>

#include <algorithm>
#include <map>


namespace {

    // The floating-point attributes VCL gives node code. LLVM doesn't inline a function into one
    // with different ones (research §5.1), so `Main` without them never inlines a node.
    constexpr const char* FloatingPointAttributes[] = {
        "denormal-fp-math", "denormal-fp-math-f32", "no-infs-fp-math", "no-nans-fp-math", "no-signed-zeros-fp-math", "unsafe-fp-math"
    };

    constexpr uint8_t CanaryByte = 0xA5;
    constexpr uint32_t CanaryWord = 0xA5A5A5A5u;

}

llvm::Constant* VCLG::MakeScalarInitializer(VCL::CodeGenModule& cgm, VCL::ConstantScalar value, VCL::Type* type, uint32_t width) {
    llvm::Constant* constant = cgm.GenerateConstantValue(&value);
    type = VCL::Type::GetCanonicalType(type);
    if (type->GetTypeClass() == VCL::Type::VectorTypeClass)
        return llvm::ConstantDataVector::getSplat(width, constant);
    if (type->GetTypeClass() == VCL::Type::LanesTypeClass) {
        llvm::SmallVector<llvm::Constant*> elements{};
        elements.assign(width, constant);
        return llvm::ConstantArray::get(llvm::ArrayType::get(constant->getType(), width), elements);
    }
    return constant;
}

VCLG::CodeGenFrame::CodeGenFrame(GraphContext& graphContext, VCL::CompilerContext& cc, llvm::Module& module, const ElaboratedGraph& graph,
        const SlotPlan& plan, llvm::ArrayRef<std::optional<TranslatedNode>> nodes, const CodeGenGraphOptions& options) :
        graphContext{ graphContext }, cc{ cc }, module{ module }, graph{ graph }, plan{ plan }, nodes{ nodes }, options{ options },
        cgm{ module, graphContext.GetGlobalASTContext(), cc.GetDiagnosticReporter(), cc.GetTarget(), imports,
            cc.GetAttributeTable(), cc.GetIdentifierTable() },
        builder{ module.getContext() } {
    regions.assign(plan.layout.regions.size(), nullptr);
    constants.assign(plan.slots.size(), nullptr);
}

bool VCLG::CodeGenFrame::EmitMain() {
    llvm::Function* function = CreateFrameFunction("Main");
    if (function == nullptr)
        return false;

    // Each temporary lives from its producer's call to its last reader (§5.3): an alloca of the
    // entry block, with lifetime markers around its uses.
    llvm::ArrayRef<NodeIndex> order = graph.GetExecutionOrder();
    const uint32_t end = (uint32_t)order.size();
    std::vector<std::vector<uint32_t>> startsAt(end + 1), endsAt(end + 1);
    for (uint32_t i = 0; i < plan.slots.size(); ++i) {
        const SlotPlan::Slot& slot = plan.slots[i];
        if (slot.slotClass != SlotPlan::SlotClass::Temporary)
            continue;
        startsAt[slot.first].push_back(i);
        endsAt[slot.last].push_back(i);
    }
    auto endLifetimes = [&](uint32_t position) {
        for (uint32_t slot : endsAt[position])
            builder.CreateLifetimeEnd(GetSlot(slot), builder.getInt64(plan.slots[slot].size));
    };

    for (uint32_t position = 0; position < end; ++position) {
        NodeIndex index = order[position];
        const ElaboratedGraph::Node& node = graph.GetNode(index);
        for (uint32_t i : startsAt[position]) {
            const SlotPlan::Slot& slot = plan.slots[i];
            llvm::Value* address = GetSlot(i);
            if (address == nullptr)
                return false;
            builder.CreateLifetimeStart(address, builder.getInt64(slot.size));
            // Zeros, so that a wrong [AlwaysWritten] promise reads zeros, deterministically; the
            // node's own stores make them dead when it keeps the promise (§3.3).
            bool canaryFill = options.checkAlwaysWritten && slot.isOutput;
            builder.CreateMemSet(address, builder.getInt8(canaryFill ? CanaryByte : 0), slot.size, llvm::MaybeAlign{ slot.alignment });
        }

        switch (node.kind) {
            case Node::NodeKind::Source:
                if (!EmitProcessCall(index))
                    return false;
                break;
            case Node::NodeKind::SubgraphInput:
            case Node::NodeKind::SubgraphOutput: {
                const ElaboratedGraph::Input& input = node.inputs[0];
                if (input.edge && input.edge->converter != nullptr
                        && !EmitConversion(index, *input.edge, ElaboratedGraph::TypeOf(input), GetSlot(plan.nodes[index].outputs[0])))
                    return false;
                break;
            }
            default:
                // Feedback Outputs are their region; Feedback Inputs copy at the end.
                break;
        }
        endLifetimes(position);
    }

    // The feedback values for the next call, once every reader of the previous ones ran (§5.6).
    for (const SlotPlan::FeedbackCopy& copy : plan.feedbackCopies) {
        const ElaboratedGraph::Input& input = graph.GetNode(copy.feedbackInput).inputs[0];
        if (input.edge->converter != nullptr) {
            if (!EmitConversion(copy.feedbackInput, *input.edge, ElaboratedGraph::TypeOf(input), GetSlot(copy.to)))
                return false;
            continue;
        }
        const SlotPlan::Slot& to = plan.slots[copy.to];
        const SlotPlan::Slot& from = plan.slots[copy.from];
        builder.CreateMemCpy(GetSlot(copy.to), llvm::MaybeAlign{ to.alignment }, GetSlot(copy.from), llvm::MaybeAlign{ from.alignment },
            std::min(to.size, from.size));
    }
    endLifetimes(end);
    builder.CreateRetVoid();
    return true;
}

bool VCLG::CodeGenFrame::EmitReset() {
    llvm::Function* function = CreateFrameFunction("Reset");
    if (function == nullptr)
        return false;

    // Connected inputs point at zeros (§5.7): `[NodeReset]` can't read them anyway (rule 2).
    uint64_t scratchSize = 0;
    uint64_t scratchAlignment = 1;
    for (NodeIndex index : graph.GetExecutionOrder()) {
        if (!nodes[index])
            continue;
        const ElaboratedGraph::Node& node = graph.GetNode(index);
        for (uint32_t i = 0; i < node.inputs.size(); ++i) {
            const NodeInterface::Port& port = nodes[index]->interface.ports[i];
            if (node.inputs[i].edge && port.byReference) {
                scratchSize = std::max(scratchSize, port.size);
                scratchAlignment = std::max(scratchAlignment, port.alignment);
            }
        }
    }
    if (scratchSize > 0) {
        llvm::AllocaInst* scratch = builder.CreateAlloca(llvm::ArrayType::get(builder.getInt8Ty(), scratchSize), nullptr, "zeros");
        scratch->setAlignment(llvm::Align{ scratchAlignment });
        builder.CreateMemSet(scratch, builder.getInt8(0), scratchSize, llvm::MaybeAlign{ scratchAlignment });
        zeros = scratch;
    }

    for (NodeIndex index : graph.GetExecutionOrder()) {
        if (!nodes[index])
            continue;
        const NodeInterface& interface = nodes[index]->interface;
        const SlotPlan::NodeSlots& slots = plan.nodes[index];
        llvm::Function* init = module.getFunction(interface.init);
        llvm::Function* reset = interface.reset.empty() ? nullptr : module.getFunction(interface.reset);
        if (!VCLG_CHECK(cc.GetDiagnosticReporter(), init != nullptr && (interface.reset.empty() || reset != nullptr)))
            return false;

        // Temporary outputs get a zeroed scratch of their own: nothing reads them before the node
        // writes them in `Main`.
        llvm::SmallVector<llvm::Value*, 4> outputs{};
        for (uint32_t slot : slots.outputs) {
            llvm::Value* address = GetSlot(slot);
            if (address == nullptr)
                return false;
            if (plan.slots[slot].slotClass == SlotPlan::SlotClass::Temporary)
                builder.CreateMemSet(address, builder.getInt8(0), plan.slots[slot].size, llvm::MaybeAlign{ plan.slots[slot].alignment });
            outputs.push_back(address);
        }

        llvm::SmallVector<llvm::Value*, 8> args{ GetSlot(slots.state) };
        args.append(outputs.begin(), outputs.end());
        if (!VCLG_CHECK(cc.GetDiagnosticReporter(), args.size() == init->arg_size() && !llvm::is_contained(args, nullptr)))
            return false;
        builder.CreateCall(init, args);

        if (reset != nullptr) {
            args.assign({ GetSlot(slots.state) });
            if (!GetPortArguments(index, reset, true, args))
                return false;
            args.append(outputs.begin(), outputs.end());
            if (!VCLG_CHECK(cc.GetDiagnosticReporter(), args.size() == reset->arg_size() && !llvm::is_contained(args, nullptr)))
                return false;
            builder.CreateCall(reset, args);
        }
    }

    // Each feedback region starts from the Feedback Input's value, or zero. That value is a scalar
    // the user gives the port; it only applies when the loop carries a value of its type (the
    // port's type follows what feeds it).
    std::map<uint32_t, NodeIndex> feedbackInputs{};
    for (const SlotPlan::FeedbackCopy& copy : plan.feedbackCopies)
        feedbackInputs[copy.to] = copy.feedbackInput;
    for (uint32_t i = 0; i < plan.slots.size(); ++i) {
        const SlotPlan::Slot& slot = plan.slots[i];
        if (slot.slotClass != SlotPlan::SlotClass::Feedback)
            continue;
        llvm::Constant* constant = nullptr;
        if (auto it = feedbackInputs.find(i); it != feedbackInputs.end())
            if (const std::optional<VCL::ConstantScalar>& value = graph.GetNode(it->second).inputs[0].initializer)
                constant = MakeScalarInitializer(cgm, *value, slot.type, cc.GetTarget().GetVectorWidthInElement());
        if (constant != nullptr && constant->getType() == ConvertType(slot.type)) {
            builder.CreateAlignedStore(constant, GetSlot(i), llvm::MaybeAlign{ slot.alignment });
        } else {
            builder.CreateMemSet(GetSlot(i), builder.getInt8(0), slot.size, llvm::MaybeAlign{ slot.alignment });
        }
    }
    builder.CreateRetVoid();
    return true;
}

bool VCLG::CodeGenFrame::EmitInitUI() {
    llvm::Function* function = CreateFrameFunction("InitUI", true);
    if (function == nullptr)
        return false;
    // Each live input starts from the value it would have as a constant (§6.4 step 5).
    for (uint32_t i = 0; i < plan.slots.size(); ++i) {
        const SlotPlan::Slot& slot = plan.slots[i];
        if (slot.slotClass != SlotPlan::SlotClass::UiEntry)
            continue;
        llvm::Value* to = GetSlot(i);
        llvm::GlobalVariable* from = GetConstant(i);
        if (to == nullptr || from == nullptr)
            return false;
        builder.CreateMemCpy(to, llvm::MaybeAlign{ slot.alignment }, from, from->getAlign(), slot.size);
    }
    builder.CreateRetVoid();
    return true;
}

llvm::Function* VCLG::CodeGenFrame::CreateFrameFunction(llvm::StringRef name, bool writesUi) {
    if (module.getNamedValue(name) != nullptr) {
        ReportError(ElaboratedGraph::Invalid, "the module already has a symbol `" + name.str() + "`");
        return nullptr;
    }
    llvm::LLVMContext& context = module.getContext();
    llvm::FunctionType* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context), { llvm::PointerType::get(context, 0) }, false);
    llvm::Function* function = llvm::Function::Create(type, llvm::GlobalValue::ExternalLinkage, name, module);
    function->setDSOLocal(true);

    // The UI block is written by the host only, between calls (§4.6 invariant 5).
    llvm::Argument* ui = function->getArg(0);
    ui->setName("ui");
    ui->addAttr(llvm::Attribute::NoAlias);
    ui->addAttr(llvm::Attribute::getWithCaptureInfo(context, llvm::CaptureInfo::none()));
    if (!writesUi)
        ui->addAttr(llvm::Attribute::ReadOnly);

    if (options.inlineNodes) {
        for (const std::optional<TranslatedNode>& node : nodes) {
            llvm::Function* process = node ? module.getFunction(node->interface.process) : nullptr;
            if (process == nullptr)
                continue;
            for (const char* attribute : FloatingPointAttributes)
                if (process->hasFnAttribute(attribute))
                    function->addFnAttr(process->getFnAttribute(attribute));
            break;
        }
    }

    builder.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", function));
    frameSlots.assign(plan.slots.size(), nullptr);
    zeros = nullptr;
    return function;
}

llvm::Value* VCLG::CodeGenFrame::GetSlot(uint32_t index) {
    if (frameSlots[index] != nullptr)
        return frameSlots[index];
    const SlotPlan::Slot& slot = plan.slots[index];
    llvm::Value* address = nullptr;
    switch (slot.slotClass) {
        case SlotPlan::SlotClass::HostState:
        case SlotPlan::SlotClass::HostPort:
        case SlotPlan::SlotClass::Feedback:
        case SlotPlan::SlotClass::Probe:
            address = GetRegion(slot.region);
            break;
        case SlotPlan::SlotClass::Constant:
            address = GetConstant(index);
            break;
        case SlotPlan::SlotClass::UiEntry: {
            // `ui + offset`, computed in the entry block, where `ui` is known.
            llvm::Function* function = builder.GetInsertBlock()->getParent();
            llvm::IRBuilder<> entry{ &function->getEntryBlock(), function->getEntryBlock().begin() };
            address = entry.CreateConstInBoundsGEP1_64(entry.getInt8Ty(), function->getArg(0),
                plan.layout.uiEntries[slot.region].offset, slot.name);
            break;
        }
        case SlotPlan::SlotClass::Temporary: {
            // In the entry block, so that SROA and mem2reg see it.
            llvm::Function* function = builder.GetInsertBlock()->getParent();
            llvm::IRBuilder<> entry{ &function->getEntryBlock(), function->getEntryBlock().begin() };
            llvm::Type* type = ConvertType(slot.type);
            if (type == nullptr)
                return nullptr;
            llvm::AllocaInst* alloca = entry.CreateAlloca(type, nullptr, slot.name);
            alloca->setAlignment(llvm::Align{ slot.alignment });
            address = alloca;
            break;
        }
    }
    frameSlots[index] = address;
    return address;
}

llvm::GlobalVariable* VCLG::CodeGenFrame::GetRegion(uint32_t index) {
    if (regions[index] != nullptr)
        return regions[index];
    // An external declaration the host binds to `state block + offset`: to LLVM, a distinct object
    // (§5.2), which is what separates one node's accesses from another's.
    const GraphLayout::Region& region = plan.layout.regions[index];
    llvm::Type* type = llvm::ArrayType::get(llvm::Type::getInt8Ty(module.getContext()), std::max<uint64_t>(region.size, 1));
    auto* variable = new llvm::GlobalVariable(module, type, false, llvm::GlobalValue::ExternalLinkage, nullptr, region.symbol);
    variable->setAlignment(llvm::Align{ region.alignment });
    if (variable->getName() != region.symbol) {
        // LLVM renamed it: the host couldn't bind it.
        variable->eraseFromParent();
        ReportError(ElaboratedGraph::Invalid, "the module already has a symbol `" + region.symbol + "`");
        return nullptr;
    }
    regions[index] = variable;
    return variable;
}

llvm::GlobalVariable* VCLG::CodeGenFrame::GetConstant(uint32_t index) {
    if (constants[index] != nullptr)
        return constants[index];
    const SlotPlan::Slot& slot = plan.slots[index];
    llvm::GlobalVariable* variable = nullptr;
    if (!slot.value && !slot.defaultValue.empty()) {
        // The variant's constant holding the input's declared initializer.
        variable = module.getGlobalVariable(slot.defaultValue, true);
        if (!VCLG_CHECK(cc.GetDiagnosticReporter(), variable != nullptr))
            return nullptr;
        if (!slot.symbol.empty()) {
            // Exposed: a copy the host can find, still a constant LLVM folds loads from.
            variable = new llvm::GlobalVariable(module, variable->getValueType(), true, llvm::GlobalValue::ExternalLinkage,
                variable->getInitializer(), slot.symbol);
            variable->setAlignment(llvm::Align{ slot.alignment });
        }
    } else {
        llvm::Type* type = ConvertType(slot.type);
        if (type == nullptr)
            return nullptr;
        llvm::Constant* value = slot.value
            ? MakeScalarInitializer(cgm, *slot.value, slot.type, cc.GetTarget().GetVectorWidthInElement())
            : llvm::Constant::getNullValue(type);
        if (!VCLG_CHECK(cc.GetDiagnosticReporter(), value->getType() == type))
            return nullptr;
        if (slot.symbol.empty()) {
            variable = new llvm::GlobalVariable(module, type, true, llvm::GlobalValue::PrivateLinkage, value, slot.name);
            variable->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
        } else {
            variable = new llvm::GlobalVariable(module, type, true, llvm::GlobalValue::ExternalLinkage, value, slot.symbol);
        }
        variable->setAlignment(llvm::Align{ slot.alignment });
    }
    if (!slot.symbol.empty() && variable->getName() != slot.symbol) {
        variable->eraseFromParent();
        ReportError(ElaboratedGraph::Invalid, "the module already has a symbol `" + slot.symbol + "`");
        return nullptr;
    }
    constants[index] = variable;
    return variable;
}

llvm::Type* VCLG::CodeGenFrame::ConvertType(VCL::Type* type) {
    return cgm.GetCGT().ConvertType(VCL::QualType{ type });
}

bool VCLG::CodeGenFrame::EmitConversion(NodeIndex node, const ElaboratedGraph::Edge& edge, VCL::Type* toType, llvm::Value* to) {
    ElaboratedDiagnosticScope scope{ graph, node };
    VCL::Type* fromType = ElaboratedGraph::TypeOf(graph.GetNode(edge.node).outputs[edge.output]);
    llvm::Value* from = GetSlot(plan.nodes[edge.node].outputs[edge.output]);
    if (!VCLG_CHECK(cc.GetDiagnosticReporter(), from != nullptr && to != nullptr))
        return false;
    return edge.converter->Emit(builder, fromType, toType, from, to);
}

bool VCLG::CodeGenFrame::GetPortArguments(NodeIndex index, llvm::Function* callee, bool resetInputs, llvm::SmallVectorImpl<llvm::Value*>& args) {
    const ElaboratedGraph::Node& node = graph.GetNode(index);
    const NodeInterface& interface = nodes[index]->interface;
    const SlotPlan::NodeSlots& slots = plan.nodes[index];
    if (!VCLG_CHECK(cc.GetDiagnosticReporter(), callee->arg_size() == 1 + interface.ports.size()))
        return false;
    for (uint32_t i = 0; i < node.inputs.size(); ++i) {
        llvm::Type* type = callee->getArg(1 + i)->getType();
        bool byReference = interface.ports[i].byReference;
        if (resetInputs && node.inputs[i].edge) {
            args.push_back(byReference ? zeros : llvm::Constant::getNullValue(type));
            continue;
        }
        // §5.5: an input passed by value is loaded from its slot; any other gets the slot.
        llvm::Value* address = GetSlot(slots.inputs[i]);
        if (address == nullptr)
            return false;
        args.push_back(byReference ? address
            : builder.CreateAlignedLoad(type, address, llvm::MaybeAlign{ plan.slots[slots.inputs[i]].alignment }));
    }
    return true;
}

bool VCLG::CodeGenFrame::EmitProcessCall(NodeIndex index) {
    const ElaboratedGraph::Node& node = graph.GetNode(index);
    const SlotPlan::NodeSlots& slots = plan.nodes[index];
    if (!VCLG_CHECK(cc.GetDiagnosticReporter(), nodes[index].has_value()))
        return false;
    llvm::Function* process = module.getFunction(nodes[index]->interface.process);
    if (!VCLG_CHECK(cc.GetDiagnosticReporter(), process != nullptr))
        return false;

    // Converted inputs, right before the call.
    for (uint32_t i = 0; i < node.inputs.size(); ++i) {
        const ElaboratedGraph::Input& input = node.inputs[i];
        if (input.edge && input.edge->converter != nullptr
                && !EmitConversion(index, *input.edge, ElaboratedGraph::TypeOf(input), GetSlot(slots.inputs[i])))
            return false;
    }

    // What the node reads, for the host (§5.5).
    for (const SlotPlan::ProbeCopy& probe : slots.probes) {
        llvm::Value* from = GetSlot(probe.from);
        llvm::Value* to = GetSlot(probe.to);
        if (from == nullptr || to == nullptr)
            return false;
        builder.CreateMemCpy(to, llvm::MaybeAlign{ plan.slots[probe.to].alignment }, from, llvm::MaybeAlign{ plan.slots[probe.from].alignment },
            plan.slots[probe.to].size);
    }

    // The slots are distinct objects, a node's outputs never share memory with its inputs, and
    // inputs are never written during the call: the entry point's `noalias` holds (§4.6).
    llvm::SmallVector<llvm::Value*, 8> args{ GetSlot(slots.state) };
    if (!GetPortArguments(index, process, false, args))
        return false;
    for (uint32_t slot : slots.outputs)
        args.push_back(GetSlot(slot));
    if (!VCLG_CHECK(cc.GetDiagnosticReporter(), args.size() == process->arg_size() && !llvm::is_contained(args, nullptr)))
        return false;
    builder.CreateCall(process, args);

    if (options.checkAlwaysWritten)
        for (uint32_t slot : slots.outputs)
            if (plan.slots[slot].slotClass == SlotPlan::SlotClass::Temporary)
                EmitCanaryCheck(slot);
    return true;
}

void VCLG::CodeGenFrame::EmitCanaryCheck(uint32_t index) {
    const SlotPlan::Slot& slot = plan.slots[index];
    llvm::LLVMContext& context = module.getContext();
    llvm::Type* countType = llvm::Type::getInt32Ty(context);
    llvm::Type* pointerType = llvm::PointerType::get(context, 0);
    auto global = [&](const char* name, llvm::Type* type) {
        if (llvm::GlobalVariable* variable = module.getGlobalVariable(name))
            return variable;
        return new llvm::GlobalVariable(module, type, false, llvm::GlobalValue::ExternalLinkage, llvm::Constant::getNullValue(type), name);
    };
    llvm::GlobalVariable* violations = global("vclg.always_written.violations", countType);
    llvm::GlobalVariable* last = global("vclg.always_written.last", pointerType);

    llvm::Value* left = builder.CreateCall(GetCanaryFunction(), { GetSlot(index), builder.getInt64(slot.size) });
    llvm::Value* count = builder.CreateLoad(countType, violations);
    builder.CreateStore(builder.CreateAdd(count, builder.CreateZExt(left, countType)), violations);
    llvm::Value* key = builder.CreateGlobalString(slot.name, "vclg.key");
    llvm::Value* previous = builder.CreateLoad(pointerType, last);
    builder.CreateStore(builder.CreateSelect(left, key, previous), last);
}

llvm::Function* VCLG::CodeGenFrame::GetCanaryFunction() {
    if (canary != nullptr)
        return canary;
    // i1 vclg.canary_left(ptr p, i64 n): whether a 4-byte word (then, in the last n % 4 bytes, a
    // byte) of p[0, n) still holds the canary pattern.
    llvm::LLVMContext& context = module.getContext();
    llvm::Type* i64 = llvm::Type::getInt64Ty(context);
    llvm::FunctionType* type = llvm::FunctionType::get(llvm::Type::getInt1Ty(context), { llvm::PointerType::get(context, 0), i64 }, false);
    canary = llvm::Function::Create(type, llvm::GlobalValue::InternalLinkage, "vclg.canary_left", module);
    llvm::Argument* p = canary->getArg(0);
    llvm::Argument* n = canary->getArg(1);

    llvm::IRBuilder<> b{ context };
    auto* entry = llvm::BasicBlock::Create(context, "entry", canary);
    auto* wordLoop = llvm::BasicBlock::Create(context, "words", canary);
    auto* wordNext = llvm::BasicBlock::Create(context, "words.next", canary);
    auto* byteLoop = llvm::BasicBlock::Create(context, "bytes", canary);
    auto* byteNext = llvm::BasicBlock::Create(context, "bytes.next", canary);
    auto* found = llvm::BasicBlock::Create(context, "found", canary);
    auto* none = llvm::BasicBlock::Create(context, "none", canary);

    b.SetInsertPoint(entry);
    llvm::Value* wordEnd = b.CreateAnd(n, b.getInt64(~(uint64_t)3));
    b.CreateBr(wordLoop);

    b.SetInsertPoint(wordLoop);
    llvm::PHINode* i = b.CreatePHI(i64, 2);
    i->addIncoming(b.getInt64(0), entry);
    llvm::Value* wordsLeft = b.CreateICmpULT(i, wordEnd);
    b.CreateCondBr(wordsLeft, wordNext, byteLoop);

    b.SetInsertPoint(wordNext);
    llvm::Value* word = b.CreateAlignedLoad(b.getInt32Ty(), b.CreateGEP(b.getInt8Ty(), p, i), llvm::MaybeAlign{ 1 });
    llvm::Value* nextWord = b.CreateAdd(i, b.getInt64(4));
    i->addIncoming(nextWord, wordNext);
    b.CreateCondBr(b.CreateICmpEQ(word, b.getInt32(CanaryWord)), found, wordLoop);

    b.SetInsertPoint(byteLoop);
    llvm::PHINode* j = b.CreatePHI(i64, 2);
    j->addIncoming(i, wordLoop);
    b.CreateCondBr(b.CreateICmpULT(j, n), byteNext, none);

    b.SetInsertPoint(byteNext);
    llvm::Value* byte = b.CreateLoad(b.getInt8Ty(), b.CreateGEP(b.getInt8Ty(), p, j));
    j->addIncoming(b.CreateAdd(j, b.getInt64(1)), byteNext);
    b.CreateCondBr(b.CreateICmpEQ(byte, b.getInt8(CanaryByte)), found, byteLoop);

    b.SetInsertPoint(found);
    b.CreateRet(b.getTrue());
    b.SetInsertPoint(none);
    b.CreateRet(b.getFalse());
    return canary;
}

bool VCLG::CodeGenFrame::ReportError(NodeIndex node, const std::string& message) {
    std::optional<ElaboratedDiagnosticScope> scope{};
    if (node != ElaboratedGraph::Invalid)
        scope.emplace(graph, node);
    cc.GetDiagnosticReporter().Error(VCL::Diagnostic::CustomDiagnostic, message)
        .SetCompilerInfo(__FILE__, __func__, __LINE__)
        .Report();
    return false;
}
