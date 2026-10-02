// Library smoke test: compiles and runs every node of a real node library, one node per graph,
// with the pipeline Grog uses (CodeGenGraph::Emit, VCLG::Optimizer, VCL::ExecutionSession, the host
// symbols of Grog's ExecutionContext, the state block bound as its layout says). Every output is
// observed (a host port). Each node is compiled and run twice from scratch (Reset, then 64 calls to
// Main), and the two runs must hash its outputs (and the host's audio output, when the node imports
// it) to the same value; within each, a second Reset then the same 64 calls must give the same
// outputs again (Reset runs `__Init`).
//
// Each node is also translated on its own (A2 Phase 3: `VCLG::TranslateSourceNode`); the output
// file's fifth column lists its outputs as `<name>:<proven|promised|held>` (always written as
// LLVM proves, as the author promises with [AlwaysWritten], or neither: the output persists).
//
// Until plan P4.8 the legacy codegen ran too, and the planned one had to match it (bit for bit,
// or within 1e-5 for the 11 IIR filters that round differently): the reference checksums of
// `research/state-storage/results/2026-09-30-smoke-p4-final-vclg-db886f2-dirty.tsv` come from
// the planned codegen once it did.
//
// Enabled when GROG_RESOURCES points at a Grog resources directory (holding `Nodes/` and
// `Libraries/`), skipped otherwise. Two more variables are optional:
//   - GROG_SMOKE_OUT: writes one line per node to this file:
//     `<node>\t<status>\t<checksum>\t<compile + JIT time, ms>\t<outputs>`;
//   - GROG_SMOKE_REFERENCE: a file written by GROG_SMOKE_OUT with another build; each node's status
//     and checksum must match it (equivalence of two codegens, on one machine).
//   - GROG_SMOKE_DUMP: a directory; the first run of each node writes the bytes it hashes to
//     `<node>.bin` there (with `/` in the node name replaced by `_`), to compare two builds sample by
//     sample when their checksums differ.

#include "Common/GraphTest.hpp"

#include <VCLG/CodeGen/Optimizer.hpp>
#include <VCLG/Graph/Definition.hpp>
#include <VCLG/Translation/Translation.hpp>

#include <VCL/Core/Target.hpp>

#include <llvm/Support/Alignment.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>


namespace {

    // The host side of one graph, as Grog's ExecutionContext and Processor::run provide it: the
    // `in` symbols of `Grog/IO.vcl`, a time position advancing by one vector per call, a fixed
    // audio input, macro values, and a note on / note off.
    class Host {
    public:
        struct ShortMidiEvent {
            uint32_t frameOffset;
            uint8_t data[4];
        };

        template<typename T>
        struct Span {
            T* ptr;
            uint64_t size;
        };

        explicit Host(uint32_t vectorWidth) : vectorWidth{ vectorWidth } {
            // Larger than any channel count or struct layout of the library, zero-filled.
            audioInput = Allocate<float>(vectorWidth * 16);
            timePosition = Allocate<uint8_t>(256);
            midiEvents = Allocate<Span<ShortMidiEvent>>(1);
            midiEvents->ptr = Allocate<ShortMidiEvent>(128);
            parameters = Allocate<Span<float>>(1);
            parameters->ptr = Allocate<float>(16);
            parameters->size = 16;
            for (uint32_t i = 0; i < 16; ++i)
                parameters->ptr[i] = 0.0625f * (float)(i + 1);
        }

        ~Host() {
            for (void* buffer : buffers)
                std::free(buffer);
        }

        void Bind(VCL::ExecutionSession& session) {
            session.DefineDefaultMemIntrinsic();
            session.DefineDefaultMathIntrinsic();
            session.DefineSymbolPtr("AudioInput", audioInput);
            session.DefineSymbolPtr("TimePosition", timePosition);
            session.DefineSymbolPtr("MidiEvents", midiEvents);
            session.DefineSymbolPtr("Parameters", parameters);
        }

        // Prepares call `call` of a run: time position, input signal and MIDI.
        void Prepare(uint32_t call) {
            // TimePosition { bool playing; uint64 frame; ... }
            timePosition[0] = 1;
            uint64_t frame = (uint64_t)call * vectorWidth;
            std::memcpy(timePosition + 8, &frame, sizeof(frame));
            for (uint32_t i = 0; i < vectorWidth * 16; ++i) {
                uint32_t n = call * vectorWidth + i;
                audioInput[i] = (float)((n * 37u) % 101u) / 50.0f - 1.0f;
            }
            midiEvents->size = 0;
            if (call == 0 || call == 32) {
                ShortMidiEvent& event = midiEvents->ptr[0];
                event.frameOffset = 3 % vectorWidth;
                event.data[0] = call == 0 ? 0x90 : 0x80;
                event.data[1] = 60;
                event.data[2] = call == 0 ? 100 : 0;
                event.data[3] = 0;
                midiEvents->size = 1;
            }
        }

    private:
        template<typename T>
        T* Allocate(size_t count) {
            size_t size = llvm::alignTo(std::max<size_t>(sizeof(T) * count, 64), 64);
            void* buffer = std::aligned_alloc(64, size);
            std::memset(buffer, 0, size);
            buffers.push_back(buffer);
            return (T*)buffer;
        }

        uint32_t vectorWidth;
        std::vector<void*> buffers{};
        float* audioInput;
        uint8_t* timePosition;
        Span<ShortMidiEvent>* midiEvents;
        Span<float>* parameters;
    };

    // FNV-1a, over the raw bytes of the outputs.
    struct Checksum {
        uint64_t value = 1469598103934665603ull;

        void Add(const void* data, size_t size) {
            const uint8_t* bytes = (const uint8_t*)data;
            for (size_t i = 0; i < size; ++i)
                value = (value ^ bytes[i]) * 1099511628211ull;
        }
    };

    // A fixed value for a scalar input whose default is zero (so that the node computes
    // something), small enough to be valid for sizes, indices and modes.
    VCL::ConstantScalar FixedInputValue(VCL::BuiltinType::Kind kind, uint32_t index) {
        using Kind = VCL::BuiltinType::Kind;
        switch (kind) {
        case Kind::Float32: return VCL::ConstantScalar{ 0.5f + 0.125f * (float)index };
        case Kind::Float64: return VCL::ConstantScalar{ 0.5 + 0.125 * (double)index };
        case Kind::Int8: return VCL::ConstantScalar{ (int8_t)(index + 1) };
        case Kind::Int16: return VCL::ConstantScalar{ (int16_t)(index + 1) };
        case Kind::Int32: return VCL::ConstantScalar{ (int32_t)(index + 1) };
        case Kind::Int64: return VCL::ConstantScalar{ (int64_t)(index + 1) };
        case Kind::UInt8: return VCL::ConstantScalar{ (uint8_t)(index + 1) };
        case Kind::UInt16: return VCL::ConstantScalar{ (uint16_t)(index + 1) };
        case Kind::UInt32: return VCL::ConstantScalar{ (uint32_t)(index + 1) };
        case Kind::UInt64: return VCL::ConstantScalar{ (uint64_t)(index + 1) };
        default: {
            // Bool: the first byte.
            VCL::ConstantScalar value{ kind };
            *(uint8_t*)value.Data() = 1;
            return value;
        }
        }
    }

    bool IsZero(VCL::ConstantScalar value) {
        return value == VCL::ConstantScalar{ value.GetKind() };
    }

    struct NodeResult {
        std::string status{};
        uint64_t checksum = 0;
        double compileMs = 0.0;
        std::string outputs{};
    };

    std::map<std::string, std::string> ReadReference(const char* path) {
        std::map<std::string, std::string> reference{};
        std::ifstream file{ path };
        std::string line{};
        while (std::getline(file, line)) {
            std::istringstream fields{ line };
            std::string name{}, status{}, checksum{};
            if (std::getline(fields, name, '\t') && std::getline(fields, status, '\t') && std::getline(fields, checksum, '\t'))
                reference[name] = status + "\t" + checksum;
        }
        return reference;
    }

    constexpr uint32_t CallsPerRun = 64;

}

TEST_CASE("Every node of the Grog library compiles and runs", "[Library][Smoke]") {
    const char* resources = std::getenv("GROG_RESOURCES");
    if (resources == nullptr)
        SKIP("GROG_RESOURCES is not set");
    std::filesystem::path root{ resources };
    REQUIRE(std::filesystem::is_directory(root / "Nodes"));

    Test::RecordingDiagnosticConsumer consumer{};
    auto invocation = std::make_shared<VCL::CompilerInvocation>();
    invocation->GetDiagnosticOptions().SetDiagnosticConsumer(&consumer);
    VCLG::GraphContext context{ invocation };
    VCL::CompilerContext& cc = context.GetCompilerContext();
    cc.GetDirectiveRegistry().CreateDirectiveHandler<VCL::ImportDirective>(cc.GetIdentifierTable().Get("import"), cc,
        (root / "Libraries").string());
    // Attributes Grog defines for its UI (`Grog::NodeWidgets`): nodes use them, the graph ignores them.
    VCL::IdentifierTable& identifiers = cc.GetIdentifierTable();
    cc.GetAttributeTable().AddDefinition(identifiers.Get("Knob"), 2, 4,
        { identifiers.Get("min"), identifiers.Get("max"), identifiers.Get("unit"), identifiers.Get("scale") });
    const uint32_t vectorWidth = cc.GetTarget().GetVectorWidthInElement();

    // Sorted, so that the output file is stable. Empty files are skipped, as Grog does.
    std::vector<std::filesystem::path> files{};
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "Nodes"))
        if (entry.is_regular_file() && entry.path().extension() == ".vcl" && entry.file_size() > 0)
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    REQUIRE(!files.empty());

    // The source of audio for the inputs that aren't scalars.
    VCL::Source* audioInputSource = cc.GetSourceManager().LoadFromDisk((root / "Nodes/IO/Audio Input.vcl").string());

    std::map<std::string, NodeResult> results{};
    for (const std::filesystem::path& file : files) {
        std::string name = std::filesystem::relative(file, root / "Nodes").replace_extension("").string();
        NodeResult& result = results[name];
        consumer.errors.clear();
        consumer.errorPaths.clear();
        consumer.warnings.clear();

        auto report = [&](const std::string& status) {
            result.status = status;
            std::string errors{};
            for (const std::string& error : consumer.errors)
                errors += "\n  " + error;
            for (const std::string& warning : consumer.warnings)
                if (warning.find("[AlwaysWritten]") != std::string::npos)
                    errors += "\n  " + warning;
            UNSCOPED_INFO(name << ": " << status << errors);
            CHECK(status == "ok");
        };

        VCL::Source* source = cc.GetSourceManager().LoadFromDisk(file.string());
        std::shared_ptr<VCLG::GraphInstance> graph = context.CreateInstance();
        VCLG::SourceNode* node = source ? graph->InstantiateSourceNode(source) : nullptr;
        if (node == nullptr) {
            report("instantiate");
            continue;
        }
        node->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
        // Scalar inputs keep their default unless it's zero; the others read the audio input
        // when they accept it.
        VCLG::SourceNode* audioInput = nullptr;
        for (uint32_t i = 0; i < node->GetInputs().size(); ++i) {
            VCLG::Port* input = node->GetInputs()[i];
            if (VCL::ConstantScalar* value = input->GetInitializerOverride()) {
                if (IsZero(*value))
                    *value = FixedInputValue(value->GetKind(), i);
                continue;
            }
            if (audioInput == nullptr && audioInputSource != nullptr)
                audioInput = graph->InstantiateSourceNode(audioInputSource);
            if (audioInput != nullptr)
                graph->Connect(audioInput->GetOutputs()[0], input);
        }

        VCLG::SourceNodeDefinition* definition = context.GetDefinitionRegistry().GetOrCreateSourceNodeDefinition(source);
        // Compiles and runs the graph in a new session: Reset, then CallsPerRun calls to Main,
        // hashing the outputs after each call (`runs` times, each after a Reset). Returns the
        // status.
        auto compileAndRun = [&](std::vector<uint64_t>& checksums, uint32_t runs, double& compileMs, std::ostream* dump) -> std::string {
            llvm::orc::ThreadSafeModule module{
                cc.GetLLVMContext().withContextDo([](llvm::LLVMContext* c) {
                    return std::make_unique<llvm::Module>("smoke", *c);
                }),
                cc.GetLLVMContext() };
            VCLG::CodeGenGraphOptions options{};
            options.planner.observeAllOutputs = true;
            std::vector<std::pair<std::string, uint64_t>> outputs{};
            VCLG::GraphLayout layout{};
            auto start = std::chrono::steady_clock::now();
            bool compiled = module.withModuleDo([&](llvm::Module& m) {
                VCLG::CodeGenGraph cgg{ context, *graph, m, options };
                VCLG::Optimizer optimizer{};
                if (!cgg.Emit())
                    return false;
                layout = cgg.GetLayout();
                for (uint32_t i = 0; i < definition->GetPorts().size(); ++i) {
                    const VCLG::SourcePortDefinition& port = definition->GetPorts()[i];
                    if (port.IsInput())
                        continue;
                    // A host port: a region of the state block. The bytes hashed are those of
                    // the output's type.
                    {
                        std::string path = Test::GraphTest::NodePath(*graph, node);
                        std::string key = VCLG::GraphLayout::OutputKey(path, port.GetName());
                        const VCLG::ElaboratedGraph::Node* elaboratedNode = nullptr;
                        for (const VCLG::ElaboratedGraph::Node& candidate : cgg.GetElaboratedGraph().GetNodes())
                            if (candidate.path == path)
                                elaboratedNode = &candidate;
                        if (layout.FindRegion(key) == nullptr || elaboratedNode == nullptr)
                            return false;
                        uint32_t output = i - (uint32_t)elaboratedNode->inputs.size();
                        llvm::Type* type = context.ConvertType(VCLG::ElaboratedGraph::TypeOf(elaboratedNode->outputs[output]));
                        if (type == nullptr)
                            return false;
                        outputs.emplace_back(key, m.getDataLayout().getTypeStoreSize(type));
                    }
                }
                if (!optimizer.Optimize(cgg))
                    return false;
                // Audio Output writes the host's output, defined by the library linked in Optimize.
                if (llvm::GlobalVariable* global = m.getGlobalVariable("AudioOutput"); global && !global->isDeclaration())
                    outputs.emplace_back("AudioOutput", m.getDataLayout().getTypeStoreSize(global->getValueType()));
                return true;
            });
            if (!compiled)
                return "compile";

            Host host{ vectorWidth };
            VCL::ExecutionSession session{};
            if (!session.SubmitModule(std::move(module)))
                return "jit";
            host.Bind(session);
            Test::HostBlock state{ layout.state.size, layout.state.alignment };
            Test::HostBlock ui{ layout.ui.size, layout.ui.alignment };
            for (const VCLG::GraphLayout::Region& region : layout.regions)
                session.DefineSymbolPtr(region.symbol, state.Bytes() + region.offset);
            void* main = session.Lookup("Main");
            void* reset = session.Lookup("Reset");
            if (main == nullptr || reset == nullptr)
                return "link";
            auto call = [&](void* function) { ((void (*)(const void*))function)(ui.data); };
            std::vector<std::pair<const uint8_t*, uint64_t>> outputData{};
            for (const auto& [symbol, size] : outputs) {
                const VCLG::GraphLayout::Region* region = layout.FindRegion(symbol);
                void* address = region != nullptr ? state.Bytes() + region->offset : session.Lookup(symbol);
                if (address == nullptr) {
                    consumer.errors.push_back("no symbol " + symbol);
                    return "link";
                }
                outputData.emplace_back((const uint8_t*)address, size);
            }
            // Session lookups materialize the module: this includes JIT compilation.
            compileMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

            for (uint32_t run = 0; run < runs; ++run) {
                Checksum sum{};
                call(reset);
                for (uint32_t i = 0; i < CallsPerRun; ++i) {
                    host.Prepare(i);
                    call(main);
                    for (const auto& [address, size] : outputData) {
                        sum.Add(address, size);
                        if (dump && run == 0)
                            dump->write((const char*)address, (std::streamsize)size);
                    }
                }
                checksums.push_back(sum.value);
            }
            return "ok";
        };

        // Twice from scratch: the result must not depend on anything but the graph. Within each,
        // a second Reset replays the same outputs.
        std::vector<uint64_t> checksums{};
        std::unique_ptr<std::ofstream> dump{};
        if (const char* dumpDirectory = std::getenv("GROG_SMOKE_DUMP")) {
            std::string fileName = name;
            std::replace(fileName.begin(), fileName.end(), '/', '_');
            dump = std::make_unique<std::ofstream>(std::filesystem::path{ dumpDirectory } / (fileName + ".bin"),
                std::ios::binary | std::ios::trunc);
        }
        std::string status = compileAndRun(checksums, 2, result.compileMs, dump.get());
        double ignored = 0.0;
        if (status == "ok")
            status = compileAndRun(checksums, 2, ignored, nullptr);
        if (status == "ok" && (checksums[0] != checksums[1] || checksums[2] != checksums[3]))
            status = "reset";
        else if (status == "ok" && checksums[0] != checksums[2])
            status = "nondeterministic";
        result.checksum = checksums.empty() ? 0 : checksums[0];

        // Examples of the planner's decisions on real nodes, nothing observed (plan P4.2): Gain's
        // output is always written, a temporary; Mono MIDI Note's gate must persist, a host port.
        if (status == "ok" && (name == "Utils/Gain" || name == "Midi/Mono MIDI Note")) {
            std::string plan{};
            llvm::orc::ThreadSafeModule module{
                cc.GetLLVMContext().withContextDo([](llvm::LLVMContext* c) { return std::make_unique<llvm::Module>("plan", *c); }),
                cc.GetLLVMContext() };
            module.withModuleDo([&](llvm::Module& m) {
                VCLG::CodeGenGraph cgg{ context, *graph, m };
                if (cgg.Emit() && cgg.GetPlan() != nullptr)
                    plan = VCLG::PrintPlan(cgg.GetElaboratedGraph(), *cgg.GetPlan());
            });
            std::string output = Test::GraphTest::NodePath(*graph, node) + (name == "Utils/Gain" ? ".output" : ".gate");
            std::string expected = name == "Utils/Gain" ? "S4 temporary" : "S3 host port";
            bool found = false;
            std::istringstream lines{ plan };
            for (std::string line{}; std::getline(lines, line);)
                if (line.find(" " + output + ":") != std::string::npos)
                    found = line.find(expected) != std::string::npos;
            INFO(plan);
            CHECK(found);
        }

        // The translation: the node translates, compiles, and gets its always-written proof.
        if (status == "ok") {
            VCLG::ElaboratedGraph elaborated = VCLG::Elaborate(*graph);
            std::string path = "g" + std::to_string(graph->GetIdentity()) + "/n" + std::to_string(node->GetIdentity());
            const VCLG::ElaboratedGraph::Node* elaboratedNode = nullptr;
            for (const VCLG::ElaboratedGraph::Node& candidate : elaborated.GetNodes())
                if (candidate.path == path)
                    elaboratedNode = &candidate;
            std::optional<VCLG::TranslatedNode> translated{};
            if (elaboratedNode != nullptr) {
                llvm::orc::ThreadSafeModule module{
                    cc.GetLLVMContext().withContextDo([](llvm::LLVMContext* c) {
                        return std::make_unique<llvm::Module>("translated", *c);
                    }),
                    cc.GetLLVMContext() };
                module.withModuleDo([&](llvm::Module& m) {
                    translated = VCLG::TranslateSourceNode(context, cc, *elaboratedNode, m);
                });
            }
            if (!translated) {
                status = "translate";
            } else {
                const VCLG::NodeInterface& interface = translated->interface;
                for (uint32_t i = 0; i < definition->GetPorts().size(); ++i) {
                    const VCLG::SourcePortDefinition& port = definition->GetPorts()[i];
                    if (port.IsInput())
                        continue;
                    const char* kind = interface.ports[i].provenAlwaysWritten ? "proven" : port.IsAlwaysWritten() ? "promised" : "held";
                    result.outputs += (result.outputs.empty() ? "" : ",") + port.GetName() + ":" + kind;
                }
            }
        }
        // No node warning: e.g. no [AlwaysWritten] promise the node's code contradicts.
        if (status == "ok" && std::any_of(consumer.warnings.begin(), consumer.warnings.end(),
                [](const std::string& warning) { return warning.find("[AlwaysWritten]") != std::string::npos; }))
            status = "warning";
        report(status);
    }

    if (const char* out = std::getenv("GROG_SMOKE_OUT")) {
        std::ofstream file{ out, std::ios::trunc };
        for (const auto& [name, result] : results)
            file << name << '\t' << result.status << '\t' << std::hex << result.checksum << std::dec << '\t'
                 << (int64_t)result.compileMs << '\t' << result.outputs << '\n';
    }

    if (const char* referencePath = std::getenv("GROG_SMOKE_REFERENCE")) {
        std::map<std::string, std::string> reference = ReadReference(referencePath);
        REQUIRE(!reference.empty());
        for (const auto& [name, result] : results) {
            std::ostringstream actual{};
            actual << result.status << '\t' << std::hex << result.checksum;
            INFO(name);
            CHECK(reference[name] == actual.str());
        }
    }

    // Outputs that must persist are never proven (research §6.3).
    if (auto it = results.find("Midi/Mono MIDI Note"); it != results.end() && it->second.status == "ok") {
        INFO(it->second.outputs);
        CHECK(it->second.outputs.find("proven") == std::string::npos);
    }

    size_t passed = std::count_if(results.begin(), results.end(), [](const auto& r) { return r.second.status == "ok"; });
    WARN(passed << " of " << results.size() << " nodes compile and run (vector width " << vectorWidth << ")");
}
