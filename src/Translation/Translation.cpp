#include <VCLG/Translation/Translation.hpp>

#include <VCLG/Translation/NodeModel.hpp>
#include <VCLG/Translation/SourceNodeCompilation.hpp>
#include <VCLG/Core/Diagnostics.hpp>

#include "Lowering.hpp"

#include <VCL/AST/TypePrinter.hpp>
#include <VCL/CodeGen/CodeGenModule.hpp>
#include <VCL/Core/SourceManager.hpp>
#include <VCL/Frontend/ModuleCache.hpp>
#include <VCL/Sema/ModuleTable.hpp>

#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/ConstantRangeList.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/xxhash.h>
#include <llvm/Transforms/Utils/Cloning.h>

#include <bit>


namespace {

    // A scalar in the variant key: its kind and exact value (floats by their bits).
    std::string KeyScalar(const VCL::ConstantScalar& value) {
        using Kind = VCL::BuiltinType::Kind;
        std::string text = std::to_string((int)value.GetKind()) + ":";
        switch (value.GetKind()) {
            case Kind::Bool: return text + (value.Get<bool>() ? "1" : "0");
            case Kind::Float32: return text + llvm::utohexstr(std::bit_cast<uint32_t>(value.Get<float>()));
            case Kind::Float64: return text + llvm::utohexstr(std::bit_cast<uint64_t>(value.Get<double>()));
            case Kind::Int8: return text + std::to_string(value.Get<int8_t>());
            case Kind::Int16: return text + std::to_string(value.Get<int16_t>());
            case Kind::Int32: return text + std::to_string(value.Get<int32_t>());
            case Kind::Int64: return text + std::to_string(value.Get<int64_t>());
            case Kind::UInt8: return text + std::to_string(value.Get<uint8_t>());
            case Kind::UInt16: return text + std::to_string(value.Get<uint16_t>());
            case Kind::UInt32: return text + std::to_string(value.Get<uint32_t>());
            case Kind::UInt64: return text + std::to_string(value.Get<uint64_t>());
            default: return text + "?";
        }
    }

    // The libraries `table` imports, and theirs (§7.3).
    void CollectLibraries(VCL::ModuleTable& table, bool direct, VCL::SourceManager& sources,
            std::vector<VCLG::LibraryDependency>& libraries, llvm::SmallPtrSetImpl<VCL::Module*>& seen) {
        for (auto import : table) {
            VCL::Module* module = import.second;
            if (!seen.insert(module).second) {
                // Already listed as a transitive import: a direct one is linked.
                if (direct)
                    for (VCLG::LibraryDependency& library : libraries)
                        if (library.source == module->GetSourceName())
                            library.direct = true, library.name = import.first;
                continue;
            }
            VCLG::LibraryDependency library{};
            library.name = import.first;
            library.source = module->GetSourceName();
            library.direct = direct;
            if (VCL::Source* source = sources.GetSourceFromName(library.source)) {
                library.buffer = source->GetBufferRef().getBufferStart();
                library.hash = llvm::xxh3_64bits(source->GetBufferRef().getBuffer());
            }
            libraries.push_back(library);
            std::shared_ptr<VCL::CompilerInstance> instance = module->GetCompilerInstance();
            if (instance && instance->HasImportModuleTable())
                CollectLibraries(instance->GetImportModuleTable(), false, sources, libraries, seen);
        }
    }

    // `State`'s layout and each port's size, from the emitted types (`state-as-data.md` §4.3):
    // later lowerings (packing) change the LLVM types, not the AST's.
    bool ReadLayout(VCL::CodeGenModule& cgm, const VCLG::TranslationContext& context, VCLG::NodeInterface& interface) {
        const llvm::DataLayout& layout = cgm.GetLLVMModule().getDataLayout();
        auto* stateType = llvm::dyn_cast_or_null<llvm::StructType>(cgm.GetCGT().ConvertType(VCL::QualType{ context.state->GetType() }));
        if (stateType == nullptr)
            return false;
        const llvm::StructLayout* stateLayout = layout.getStructLayout(stateType);
        interface.stateSize = layout.getTypeAllocSize(stateType);
        interface.stateAlignment = layout.getABITypeAlign(stateType).value();
        for (uint32_t i = 0; i < stateType->getNumElements(); ++i)
            interface.stateFields.push_back({ stateLayout->getElementOffset(i), layout.getTypeAllocSize(stateType->getElementType(i)) });

        // The process entry point's parameters: `self`, then the ports.
        llvm::SmallVector<VCL::ParamDecl*, 8> params{};
        for (auto it = context.process->Begin(); it != context.process->End(); ++it)
            if (it->GetDeclClass() == VCL::Decl::ParamDeclClass)
                params.push_back((VCL::ParamDecl*)it.Get());
        if (params.size() != context.model.GetPorts().size() + 1)
            return false;
        for (uint32_t i = 0; i < context.model.GetPorts().size(); ++i) {
            llvm::Type* type = cgm.GetCGT().ConvertType(context.model.GetPorts()[i]->GetValueType());
            if (type == nullptr)
                return false;
            VCLG::NodeInterface::Port port{};
            port.byReference = params[i + 1]->GetValueType().GetType()->GetTypeClass() == VCL::Type::ReferenceTypeClass;
            port.size = layout.getTypeAllocSize(type);
            port.alignment = layout.getABITypeAlign(type).value();
            interface.ports.push_back(port);
        }
        return true;
    }

    // Whether the entry block of `function` loads from `arg`'s memory before anything writes it:
    // the output is read before being written on every call.
    bool ReadsBeforeWriting(llvm::Function& function, llvm::Argument* arg) {
        for (llvm::Instruction& inst : function.getEntryBlock()) {
            if (auto* load = llvm::dyn_cast<llvm::LoadInst>(&inst)) {
                if (llvm::getUnderlyingObject(load->getPointerOperand()) == arg)
                    return true;
            } else if (auto* store = llvm::dyn_cast<llvm::StoreInst>(&inst)) {
                if (llvm::getUnderlyingObject(store->getPointerOperand()) == arg)
                    return false;
            } else if (auto* call = llvm::dyn_cast<llvm::CallBase>(&inst)) {
                // A call that may write the output ends what can be concluded.
                for (llvm::Value* operand : call->args())
                    if (llvm::getUnderlyingObject(operand) == arg)
                        return false;
            }
        }
        return false;
    }

    // The always-written proof (`state-as-data.md` §3.3, §7.3): LLVM's default pipeline on a copy
    // of the variant, with its libraries linked, infers `initializes((0, N))` on an output
    // parameter when every call writes its N bytes before reading any of them.
    bool ProveAlwaysWritten(llvm::Module& module, VCL::CompilerInstance& instance, const VCLG::TranslationContext& context,
            VCLG::NodeInterface& interface) {
        std::unique_ptr<llvm::Module> copy = llvm::CloneModule(module);
        if (!VCLG::LinkLibraries(*copy, instance, context.reporter))
            return false;

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
        mpm.run(*copy, mam);

        llvm::Function* process = copy->getFunction(interface.process);
        if (process == nullptr)
            return false;
        const VCLG::NodeModel& model = context.model;
        for (uint32_t i = model.GetInputCount(); i < model.GetPorts().size(); ++i) {
            llvm::Argument* arg = process->getArg(i + 1);
            llvm::Attribute attribute = process->getParamAttribute(i + 1, llvm::Attribute::Initializes);
            uint64_t size = interface.ports[i].size;
            if (attribute.isValid()) {
                for (const llvm::ConstantRange& range : attribute.getInitializes()) {
                    if (range.getLower().getSExtValue() <= 0 && range.getUpper().getSExtValue() >= (int64_t)size)
                        interface.ports[i].provenAlwaysWritten = true;
                }
            }
            // A promise the code contradicts: the output is read before being written.
            VCL::VarDecl* output = model.GetPorts()[i];
            if (model.IsAlwaysWritten(output) && ReadsBeforeWriting(*process, arg)) {
                context.reporter.Warn(VCL::Diagnostic::CustomDiagnostic, "output '" + output->GetIdentifierInfo()->GetName().str()
                        + "' is [AlwaysWritten], but the node reads it before writing it")
                    .AddHint(VCL::DiagnosticHint{ output->GetSourceRange() })
                    .SetCompilerInfo(__FILE__, __func__, __LINE__)
                    .Report();
            }
        }
        return true;
    }

    std::string PrintParam(VCL::ParamDecl* param) {
        std::string text{};
        VCL::Decl::VarAttrBitfield attr = param->GetVarAttrBitfield();
        if (attr.hasInAttribute && attr.hasOutAttribute)
            text += "inout ";
        else if (attr.hasOutAttribute)
            text += "out ";
        VCL::QualType type = param->GetValueType();
        std::string reference{};
        if (type.GetType()->GetTypeClass() == VCL::Type::ReferenceTypeClass) {
            reference = type.HasQualifier(VCL::Qualifier::Const) ? "const& " : "& ";
            type = ((VCL::ReferenceType*)type.GetType())->GetType();
        }
        text += VCL::TypePrinter::Print(type) + " " + reference + param->GetIdentifierInfo()->GetName().str();

        std::vector<std::string> flags{};
        using F = VCL::ParamDecl;
        if (param->HasCodeGenFlag(F::NoAlias)) flags.push_back("noalias");
        if (param->HasCodeGenFlag(F::NoCapture)) flags.push_back("nocapture");
        if (param->HasCodeGenFlag(F::ReadOnly)) flags.push_back("readonly");
        if (param->HasCodeGenFlag(F::Aligned)) flags.push_back("align");
        if (param->HasCodeGenFlag(F::Dereferenceable)) flags.push_back("dereferenceable");
        if (!flags.empty()) {
            text += " [";
            for (size_t i = 0; i < flags.size(); ++i)
                text += (i ? " " : "") + flags[i];
            text += "]";
        }
        return text;
    }

}

std::string VCLG::VariantKey::ManglingPrefix() const {
    std::string hex = llvm::utohexstr(hash, true);
    return "v" + std::string(16 - hex.size(), '0') + hex;
}

std::optional<VCLG::VariantKey> VCLG::MakeVariantKey(VCL::CompilerContext& cc, const ElaboratedGraph::Node& node,
        const TranslationProfile& profile) {
    VCL::DiagnosticReporter& reporter = cc.GetDiagnosticReporter();
    VCL::Source* source = cc.GetSourceManager().GetSourceFromName(node.source);
    if (!VCLG_CHECK(reporter, source != nullptr && node.definition != nullptr))
        return std::nullopt;
    const SourceNodeDefinition& definition = *node.definition;

    std::string text = "source " + node.source + "#" + llvm::utohexstr(llvm::xxh3_64bits(source->GetBufferRef().getBuffer()));
    text += ";width " + std::to_string(cc.GetTarget().GetVectorWidthInByte());
    text += ";profile";
    for (TranslationProfile::Step step : profile.steps)
        text += " " + std::to_string((int)step);
    for (uint32_t i = 0; i < definition.GetParameters().size(); ++i) {
        const std::optional<VCL::ConstantScalar>* value = i < node.parameters.size() ? &node.parameters[i] : nullptr;
        text += ";parameter " + definition.GetParameters()[i].GetName() + "=" + (value && *value ? KeyScalar(**value) : "default");
    }
    for (const SourceAutoParameterDefinition& parameter : definition.GetAutoParameters()) {
        text += ";auto " + parameter.GetName() + "=";
        VCL::Decl* decl = parameter.GetDecl();
        if (!node.substitutions.HasDecl(decl))
            text += "none";
        else if (decl->GetDeclClass() == VCL::Decl::TypeAliasDeclClass) {
            VCL::Type* type = node.substitutions.GetTypeSubstitution((VCL::TypeAliasDecl*)decl);
            text += type ? VCL::TypePrinter::Print(VCL::QualType{ type }) : "none";
        } else {
            VCL::ConstantScalar* value = node.substitutions.GetScalarSubstitution((VCL::VarDecl*)decl);
            text += value ? KeyScalar(*value) : "none";
        }
    }
    // The port type overrides SourceNodeCompilation applies.
    for (const ElaboratedGraph::Input& input : node.inputs)
        if (!input.isDependent && input.type != input.declaredType && input.type != nullptr)
            text += ";input " + input.name + "=" + VCL::TypePrinter::Print(VCL::QualType{ input.type });

    VariantKey key{};
    key.hash = llvm::xxh3_64bits(text);
    key.text = std::move(text);
    return key;
}

bool VCLG::LinkLibraries(llvm::Module& module, VCL::CompilerInstance& instance, VCL::DiagnosticReporter& reporter) {
    llvm::Linker linker{ module };
    for (auto import : instance.GetImportModuleTable()) {
        std::unique_ptr<llvm::Module> library = import.second->GetModule().withModuleDo([](llvm::Module& m) {
            return llvm::CloneModule(m);
        });
        if (linker.linkInModule(std::move(library))) {
            reporter.Error(VCL::Diagnostic::CustomDiagnostic, "failed to link imported module `" + import.first->GetName().str() + "`")
                .SetCompilerInfo(__FILE__, __func__, __LINE__)
                .Report();
            return false;
        }
    }
    return true;
}

std::optional<VCLG::TranslatedNode> VCLG::TranslateSourceNode(GraphContext& graphContext, VCL::CompilerContext& cc,
        const ElaboratedGraph::Node& node, llvm::Module& module, const TranslationProfile& profile) {
    VCL::DiagnosticReporter& reporter = cc.GetDiagnosticReporter();
    std::optional<VariantKey> key = MakeVariantKey(cc, node, profile);
    if (!key)
        return std::nullopt;
    VCL::Source* source = cc.GetSourceManager().GetSourceFromName(node.source);

    // 1-2. Parse with what makes this copy, and check. Every instance of the variant has the same
    // symbols (§7.2).
    SourceNodeCompilation compilation{ graphContext, cc, node, source, key->ManglingPrefix() };
    if (!compilation.Parse())
        return std::nullopt;
    std::shared_ptr<VCL::CompilerInstance> instance = compilation.GetInstance();
    VCL::ASTContext& ast = instance->GetASTContext();

    // 3.1 The node model. The rules (3.2) were checked when the definition was loaded.
    std::optional<NodeModel> model = NodeModel::Build(ast.GetTranslationUnitDecl(),
        graphContext.GetDefinitionRegistry().GetNodeAttributes(), reporter, node.source);
    if (!model)
        return std::nullopt;

    // 3.3-3.4 The lowering steps; storage lowering synthesizes the entry points.
    TranslationContext context{ compilation.GetSema(), ast, cc.GetIdentifierTable(), reporter, *model, ast.GetTranslationUnitDecl() };
    if (profile.steps.empty() || profile.steps.front() != TranslationProfile::Step::StorageLowering)
        return VCLG_CHECK(reporter, !"storage lowering is the first step"), std::nullopt;
    for (TranslationProfile::Step step : profile.steps) {
        switch (step) {
            case TranslationProfile::Step::StorageLowering:
                if (!RunStorageLowering(context))
                    return std::nullopt;
                break;
        }
    }

    // 4. Codegen.
    VCL::CodeGenModule cgm{ module, ast, reporter, cc.GetTarget(), instance->GetImportModuleTable(),
        cc.GetAttributeTable(), cc.GetIdentifierTable() };
    cgm.SetOptions(cc.GetInvocation()->GetCodeGenOptions());
    if (!cgm.Emit(context.translationUnit))
        return std::nullopt;

    TranslatedNode translated{};
    translated.key = std::move(*key);
    llvm::SmallPtrSet<VCL::Module*, 8> seen{};
    CollectLibraries(instance->GetImportModuleTable(), true, cc.GetSourceManager(), translated.libraries, seen);
    translated.instance = instance;
    translated.translationUnit = context.translationUnit;
    NodeInterface& interface = translated.interface;
    interface.definition = node.definition;
    interface.sourceHash = llvm::xxh3_64bits(source->GetBufferRef().getBuffer());
    interface.process = ast.GetMangledName(context.process);
    interface.reset = context.reset ? ast.GetMangledName(context.reset) : std::string{};
    interface.init = ast.GetMangledName(context.init);
    interface.hostSymbols.assign(context.hostSymbols.begin(), context.hostSymbols.end());

    // 5. Variant analysis.
    if (!VCLG_CHECK(reporter, ReadLayout(cgm, context, interface)))
        return std::nullopt;
    for (uint32_t i = 0; i < context.inputDefaults.size(); ++i)
        if (context.inputDefaults[i] != nullptr)
            interface.ports[i].defaultValue = ast.GetMangledName(context.inputDefaults[i]);
    if (!ProveAlwaysWritten(module, *instance, context, interface))
        return std::nullopt;
    return translated;
}

std::string VCLG::PrintTranslation(const TranslatedNode& node) {
    std::string text{};
    for (auto it = node.translationUnit->Begin(); it != node.translationUnit->End(); ++it) {
        switch (it->GetDeclClass()) {
            case VCL::Decl::VarDeclClass: {
                auto* var = (VCL::VarDecl*)it.Get();
                text += var->IsExported() ? "export " : "";
                text += VCL::TypePrinter::Print(var->GetValueType()) + " " + var->GetIdentifierInfo()->GetName().str() + "\n";
                break;
            }
            case VCL::Decl::RecordDeclClass: {
                auto* record = (VCL::RecordDecl*)it.Get();
                text += "struct " + record->GetIdentifierInfo()->GetName().str() + " {";
                for (auto field = record->Begin(); field != record->End(); ++field)
                    if (field->GetDeclClass() == VCL::Decl::FieldDeclClass)
                        text += " " + VCL::TypePrinter::Print(((VCL::FieldDecl*)field.Get())->GetType()) + " "
                            + ((VCL::FieldDecl*)field.Get())->GetIdentifierInfo()->GetName().str() + ";";
                text += " }\n";
                break;
            }
            case VCL::Decl::FunctionDeclClass: {
                auto* function = (VCL::FunctionDecl*)it.Get();
                text += function->IsExported() ? "export " : "";
                text += VCL::TypePrinter::Print(function->GetType()->GetReturnType()) + " " + function->GetIdentifierInfo()->GetName().str() + "(";
                bool first = true;
                for (auto param = function->Begin(); param != function->End(); ++param) {
                    if (param->GetDeclClass() != VCL::Decl::ParamDeclClass)
                        continue;
                    text += (first ? "" : ", ") + PrintParam((VCL::ParamDecl*)param.Get());
                    first = false;
                }
                text += ")\n";
                break;
            }
            default:
                break;
        }
    }
    return text;
}
