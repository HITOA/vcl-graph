#include "Common/GraphTest.hpp"


// The rules of `state-as-data.md` §3.5 and §3.6, checked when a definition is loaded: a node
// breaking one doesn't load (errors), or loads with a warning.

namespace {

    VCLG::SourceNodeDefinition* LoadDefinition(Test::GraphTest& test, const std::string& name) {
        VCL::Source* source = test.LoadNode(name);
        return test.context.GetDefinitionRegistry().GetOrCreateSourceNodeDefinition(source);
    }

    VCLG::SourceNodeDefinition* Load(Test::GraphTest& test, const std::string& name) {
        return LoadDefinition(test, "Rules/" + name);
    }

}

TEST_CASE_METHOD(Test::GraphTest, "Rule 1: ports are named only in entry points", "[Translation][Rules]") {
    SECTION("A helper writing an output") {
        REQUIRE(Load(*this, "HelperWritesOutput") == nullptr);
        REQUIRE(consumer.HasError("port 'output' can only be accessed in an entry point"));
        REQUIRE(consumer.HasError("pass it to 'Write' as an argument"));
    }
    SECTION("A helper reading an input") {
        REQUIRE(Load(*this, "HelperReadsInput") == nullptr);
        REQUIRE(consumer.HasError("port 'input' can only be accessed in an entry point"));
    }
    SECTION("An explicit specialization of a template helper reading an input") {
        REQUIRE(Load(*this, "SpecializationReadsInput") == nullptr);
        REQUIRE(consumer.HasError("port 'input' can only be accessed in an entry point"));
        REQUIRE(consumer.errors.size() == 1);
    }
    SECTION("An entry point passing an output to a helper by reference") {
        REQUIRE(Load(*this, "OutputToInout") != nullptr);
        REQUIRE(consumer.errors.empty());
    }
    SECTION("An entry point passing an input to a helper by reference") {
        // The const rule of §3.1 covers it.
        REQUIRE(LoadDefinition(*this, "InoutInput") == nullptr);
        REQUIRE(consumer.HasError("qualifiers dropped"));
    }
}

TEST_CASE_METHOD(Test::GraphTest, "Rule 2: what [NodeReset] may do with ports", "[Translation][Rules]") {
    SECTION("Reading an input is an error") {
        REQUIRE(Load(*this, "ResetReadsInput") == nullptr);
        REQUIRE(consumer.HasError("input 'input' can't be read in [NodeReset]"));
    }
    SECTION("Writing an [AlwaysWritten] output is a warning") {
        REQUIRE(Load(*this, "ResetWritesAlwaysWritten") != nullptr);
        REQUIRE(consumer.errors.empty());
        REQUIRE(consumer.HasWarning("what [NodeReset] writes to it is never observed"));
    }
    SECTION("Writing an output that persists is allowed") {
        REQUIRE(LoadDefinition(*this, "Counter") != nullptr);
        REQUIRE(consumer.warnings.empty());
    }
}

TEST_CASE_METHOD(Test::GraphTest, "[AlwaysWritten] rules", "[Translation][Rules]") {
    SECTION("Only on an output") {
        REQUIRE(Load(*this, "AlwaysWrittenOnState") == nullptr);
        REQUIRE(consumer.HasError("[AlwaysWritten] only applies to an [Output]"));
    }
    SECTION("An initializer is never observed") {
        VCLG::SourceNodeDefinition* definition = Load(*this, "AlwaysWrittenInitializer");
        REQUIRE(definition != nullptr);
        REQUIRE(consumer.HasWarning("its initializer is never observed"));
        REQUIRE(definition->GetPorts()[0].IsAlwaysWritten());
    }
    SECTION("Recorded on the port definition") {
        VCLG::SourceNodeDefinition* definition = Load(*this, "ResetWritesAlwaysWritten");
        REQUIRE(definition != nullptr);
        REQUIRE(definition->GetPorts()[0].IsAlwaysWritten());
        VCLG::SourceNodeDefinition* counter = context.GetDefinitionRegistry().GetOrCreateSourceNodeDefinition(LoadNode("Counter"));
        REQUIRE(!counter->GetPorts()[0].IsAlwaysWritten());
    }
}

TEST_CASE_METHOD(Test::GraphTest, "The definition lists the state variables", "[Translation][Model]") {
    VCLG::SourceNodeDefinition* counter = context.GetDefinitionRegistry().GetOrCreateSourceNodeDefinition(LoadNode("Counter"));
    REQUIRE(counter != nullptr);
    REQUIRE(counter->GetStateVariables().size() == 1);
    REQUIRE(counter->GetStateVariables()[0].GetName() == "state");

    // Constants (parameters, `const`) and ports aren't state.
    VCLG::SourceNodeDefinition* scale = context.GetDefinitionRegistry().GetOrCreateSourceNodeDefinition(LoadNode("Scale"));
    REQUIRE(scale != nullptr);
    REQUIRE(scale->GetStateVariables().empty());
    REQUIRE(scale->GetParameters().size() == 1);
    REQUIRE(scale->GetPorts().size() == 2);
}

TEST_CASE_METHOD(Test::GraphTest, "[Expose] rules", "[Translation][Rules][Expose]") {
    SECTION("An access other than Read and Write") {
        REQUIRE(Load(*this, "ExposeUnknownAccess") == nullptr);
        REQUIRE(consumer.HasError("unknown access in [Expose]: expected Read or Write"));
    }
    SECTION("An access given as a string") {
        REQUIRE(Load(*this, "ExposeStringAccess") == nullptr);
        REQUIRE(consumer.HasError("unknown access in [Expose]"));
    }
    SECTION("The same access twice") {
        REQUIRE(Load(*this, "ExposeDuplicateAccess") == nullptr);
        REQUIRE(consumer.HasError("access 'Read' is given twice in [Expose]"));
    }
    SECTION("[Expose] twice on one variable") {
        REQUIRE(Load(*this, "ExposeTwice") == nullptr);
        REQUIRE(consumer.HasError("[Expose] is given twice"));
    }
    SECTION("[Expose(Write)] on an output") {
        REQUIRE(Load(*this, "ExposeWriteOutput") == nullptr);
        REQUIRE(consumer.HasError("output 'output' can't be [Expose(Write)]"));
    }
    SECTION("On a constant, a parameter, an AutoParameter") {
        REQUIRE(Load(*this, "ExposeConstant") == nullptr);
        REQUIRE(consumer.HasError("[Expose] can't apply to 'scale': a compile-time value has no run-time storage"));
        REQUIRE(Load(*this, "ExposeParameter") == nullptr);
        REQUIRE(consumer.HasError("[Expose] can't apply to 'factor'"));
        REQUIRE(Load(*this, "ExposeAutoParameter") == nullptr);
        REQUIRE(consumer.HasError("[Expose] only applies to a variable"));
    }
    SECTION("On a function or a struct") {
        REQUIRE(Load(*this, "ExposeFunction") == nullptr);
        REQUIRE(consumer.HasError("[Expose] only applies to a variable"));
        consumer.errors.clear();
        REQUIRE(Load(*this, "ExposeStruct") == nullptr);
        INFO((consumer.errors.empty() ? std::string{} : consumer.errors[0]));
        REQUIRE(consumer.HasError("[Expose] only applies to a variable"));
    }
    SECTION("Every allowed spelling, recorded on the definition") {
        VCLG::SourceNodeDefinition* definition = Load(*this, "ExposeAllowed");
        INFO((consumer.errors.empty() ? std::string{} : consumer.errors[0]));
        REQUIRE(definition != nullptr);
        REQUIRE(consumer.errors.empty());
        llvm::ArrayRef<VCLG::SourcePortDefinition> ports = definition->GetPorts();
        REQUIRE(ports.size() == 5);
        REQUIRE(ports[0].GetExposure() == VCLG::Exposure::Write);
        REQUIRE(ports[1].GetExposure() == VCLG::Exposure::Write);
        REQUIRE(ports[2].GetExposure() == VCLG::Exposure::Read);
        // [AlwaysWritten] with [Expose]: allowed (§3.6).
        REQUIRE(ports[3].GetExposure() == VCLG::Exposure::Read);
        REQUIRE(ports[3].IsAlwaysWritten());
        REQUIRE(ports[4].GetExposure() == VCLG::Exposure::Read);
        llvm::ArrayRef<VCLG::SourceStateDefinition> state = definition->GetStateVariables();
        REQUIRE(state.size() == 3);
        REQUIRE(state[0].GetExposure() == VCLG::Exposure::Read);
        REQUIRE(state[1].GetExposure() == VCLG::Exposure::Write);
        REQUIRE(state[2].GetExposure() == VCLG::Exposure::None);
    }
}
