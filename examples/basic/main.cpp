#include <VCLG/Graph.hpp>

#include <VCL/Source.hpp>
#include <VCL/ExecutionSession.hpp>
#include <VCL/Module.hpp>
#include <VCL/Error.hpp>
#include <VCL/PrettyPrinter.hpp>

#include <iostream>


class ConsoleLogger : public VCL::Logger {
public:
    void Log(VCL::Message& message) override {
        const char* severityStr[] = {
            "None",
            "Error",
            "Warning",
            "Info",
            "Debug"
        };
        int severityInt = (int)message.severity;
        printf("[%s] %s\n", severityStr[severityInt], message.message.c_str());
    };
};

int main() {
    std::shared_ptr<VCL::Logger> logger = std::make_shared<ConsoleLogger>();

    std::filesystem::path gainSourcePath{ "./nodes/gain.vcl" };
    std::filesystem::path oscSourcePath{ "./nodes/osc.vcl" };
    std::filesystem::path outputSourcePath{ "./nodes/output.vcl" };

    auto gainSource = VCL::Source::LoadFromDisk(gainSourcePath);
    auto oscSource = VCL::Source::LoadFromDisk(oscSourcePath);
    auto outputSource = VCL::Source::LoadFromDisk(outputSourcePath);

    if (!gainSource.has_value() || !oscSource.has_value() || !outputSource.has_value())
        return -1;

    VCLG::Graph graph{ logger };
    VCLG::Graph::NodeHandle gainHandle = graph.AddNode(std::make_unique<VCLG::Node>(*gainSource, logger));
    VCLG::Graph::NodeHandle oscHandle = graph.AddNode(std::make_unique<VCLG::Node>(*oscSource, logger));
    VCLG::Graph::NodeHandle outputHandle = graph.AddNode(std::make_unique<VCLG::Node>(*outputSource, logger));

    oscHandle.GetOutput(0).Connect(gainHandle.GetInput(0));
    gainHandle.GetOutput(0).Connect(outputHandle.GetInput(0));
    outputHandle.SetAsGraphOutput();
    std::unique_ptr<VCL::ASTProgram> program = graph.Compile();

    if (!program)
        return -1;

    VCL::PrettyPrinter prettyPrinter{};
    program->Accept(&prettyPrinter);

    std::cout << prettyPrinter.GetBuffer() << std::endl;

    try {
        std::unique_ptr<VCL::ExecutionSession> session = VCL::ExecutionSession::Create(logger);
        session->SetDebugInformation(true);
        std::unique_ptr<VCL::Module> module = session->CreateModule(std::move(program));
        VCL::ModuleDebugInformationSettings diSettings{};
        diSettings.generateDebugInformation = true;
        module->Emit(diSettings);
        module->Verify();
        session->SubmitModule(std::move(module));
    } catch (VCL::Exception& exception) {
        logger->Error("{}: {}\n{}", exception.location.ToString(), exception.what(), exception.location.ToStringDetailed());
    } catch (std::exception& exception) {
        logger->Error("Unexpected: {}", exception.what());
    }

    return 0;
}