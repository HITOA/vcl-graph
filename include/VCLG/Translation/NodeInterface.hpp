#pragma once

#include <VCLG/Graph/Definition.hpp>

#include <cstdint>
#include <string>
#include <vector>


namespace VCLG {

    /**
     * What a compiled variant of a node offers the graph codegen (`state-as-data.md` §4.3): its
     * entry points, how each port is passed, the layout of its `State`, and which outputs it always
     * writes.
     *
     * Only what depends on the variant is here. What the author wrote (port names and direction,
     * `[AlwaysWritten]`, the state variables' names) is read from the definition, which outlives the
     * interface: definitions live as long as their DefinitionRegistry, that is their GraphContext.
     * Ports and state fields are indexed as in the definition.
     *
     * Every entry point takes `self` (a `State*`), then one parameter per port in port order: an
     * input by value when `byReference` is false, a pointer otherwise; an output always by pointer.
     * `__Init` takes `self`, then the outputs.
     */
    struct NodeInterface {
        struct Port {
            bool byReference = false;
            /** Allocation size and ABI alignment of the port's type. */
            uint64_t size = 0;
            uint64_t alignment = 1;
            /**
             * Outputs: LLVM proves that every call of the processing entry point writes the whole
             * output before reading it (`initializes`, P3.5). Only positive proofs are recorded.
             */
            bool provenAlwaysWritten = false;
            /**
             * Inputs: the symbol of the constant holding the declared initializer (a variable of
             * the port's type), or empty when the input has none (it's then zero).
             */
            std::string defaultValue{};
        };

        struct StateField {
            uint64_t offset = 0;
            uint64_t size = 0;
        };

        const SourceNodeDefinition* definition = nullptr;
        /** Hash of the source text the variant was compiled from: edited source, new signature (§6.3). */
        uint64_t sourceHash = 0;

        /** Symbols of the entry points; `reset` is empty when the node has no `[NodeReset]`. */
        std::string process{};
        std::string reset{};
        std::string init{};

        std::vector<Port> ports{};
        std::vector<StateField> stateFields{};
        uint64_t stateSize = 0;
        uint64_t stateAlignment = 1;

        /** Host variables the node reads or writes (`in`/`out` variables of the libraries it imports), by name. */
        std::vector<std::string> hostSymbols{};

        /** Whether output `port` can be a temporary: the author promised it, or LLVM proved it. */
        inline bool IsAlwaysWritten(uint32_t port) const {
            return definition->GetPorts()[port].IsAlwaysWritten() || ports[port].provenAlwaysWritten;
        }
    };

}
