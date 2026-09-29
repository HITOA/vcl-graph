// Library smoke test: compiles and runs every node of a real node library, one node per graph,
// with the pipeline Grog uses (CodeGenGraph::Emit, VCLG::Optimizer, VCL::ExecutionSession, the host
// symbols of Grog's ExecutionContext). Each node is compiled and run twice from scratch (Reset, then
// 64 calls to Main), and the two runs must hash its outputs (and the host's audio output, when
// the node imports it) to the same value.
//
// Enabled when GROG_RESOURCES points at a Grog resources directory (holding `Nodes/` and
// `Libraries/`), skipped otherwise. Two more variables are optional:
//   - GROG_SMOKE_OUT: writes one line per node to this file:
//     `<node>\t<status>\t<checksum>\t<compile + JIT time, ms>`;
//   - GROG_SMOKE_REFERENCE: a file written by GROG_SMOKE_OUT with another build; each node's status
//     and checksum must match it (equivalence of two codegens, on one machine).

#include "Common/GraphTest.hpp"

#include <VCLG/CodeGen/Optimizer.hpp>
#include <VCLG/Graph/Definition.hpp>

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

        auto report = [&](const std::string& status) {
            result.status = status;
            std::string errors{};
            for (const std::string& error : consumer.errors)
                errors += "\n  " + error;
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

        // Compiles and runs the graph in a new session: Reset, then CallsPerRun calls to Main,
        // hashing the outputs after each call. Returns the status.
        VCLG::SourceNodeDefinition* definition = context.GetDefinitionRegistry().GetOrCreateSourceNodeDefinition(source);
        auto compileAndRun = [&](uint64_t& checksum, double& compileMs) -> std::string {
            llvm::orc::ThreadSafeModule module{
                cc.GetLLVMContext().withContextDo([](llvm::LLVMContext* c) {
                    return std::make_unique<llvm::Module>("smoke", *c);
                }),
                cc.GetLLVMContext() };
            std::vector<std::pair<std::string, uint64_t>> outputs{};
            auto start = std::chrono::steady_clock::now();
            bool compiled = module.withModuleDo([&](llvm::Module& m) {
                VCLG::CodeGenGraph cgg{ context, *graph, m };
                VCLG::Optimizer optimizer{};
                if (!cgg.Emit())
                    return false;
                // Port variables are internal, so the optimizer would fold the outputs away: they
                // are made external, as observing an output requires.
                for (const VCLG::SourcePortDefinition& port : definition->GetPorts()) {
                    if (port.IsInput())
                        continue;
                    std::string symbol = Test::GraphTest::NodeSymbol(*graph, node, port.GetName());
                    llvm::GlobalVariable* global = m.getGlobalVariable(symbol, true);
                    if (global == nullptr)
                        return false;
                    global->setLinkage(llvm::GlobalValue::ExternalLinkage);
                    global->setDSOLocal(false);
                    outputs.emplace_back(symbol, m.getDataLayout().getTypeStoreSize(global->getValueType()));
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
            auto main = (void (*)())session.Lookup("Main");
            auto reset = (void (*)())session.Lookup("Reset");
            if (main == nullptr || reset == nullptr)
                return "link";
            std::vector<std::pair<const uint8_t*, uint64_t>> outputData{};
            for (const auto& [symbol, size] : outputs) {
                void* address = session.Lookup(symbol);
                if (address == nullptr) {
                    consumer.errors.push_back("no symbol " + symbol);
                    return "link";
                }
                outputData.emplace_back((const uint8_t*)address, size);
            }
            // Session lookups materialize the module: this includes JIT compilation.
            compileMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

            Checksum sum{};
            reset();
            for (uint32_t call = 0; call < CallsPerRun; ++call) {
                host.Prepare(call);
                main();
                for (const auto& [address, size] : outputData)
                    sum.Add(address, size);
            }
            checksum = sum.value;
            return "ok";
        };

        // Twice, from scratch: the result must not depend on anything but the graph.
        uint64_t checksums[2]{};
        std::string status = compileAndRun(checksums[0], result.compileMs);
        double ignored = 0.0;
        if (status == "ok")
            status = compileAndRun(checksums[1], ignored);
        if (status == "ok" && checksums[0] != checksums[1])
            status = "nondeterministic";
        result.checksum = checksums[0];
        report(status);
    }

    if (const char* out = std::getenv("GROG_SMOKE_OUT")) {
        std::ofstream file{ out, std::ios::trunc };
        for (const auto& [name, result] : results)
            file << name << '\t' << result.status << '\t' << std::hex << result.checksum << std::dec << '\t'
                 << (int64_t)result.compileMs << '\n';
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

    size_t passed = std::count_if(results.begin(), results.end(), [](const auto& r) { return r.second.status == "ok"; });
    WARN(passed << " of " << results.size() << " nodes compile and run (vector width " << vectorWidth << ")");
}
