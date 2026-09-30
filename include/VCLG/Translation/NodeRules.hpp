#pragma once

#include <VCLG/Translation/NodeModel.hpp>

#include <VCL/Core/Diagnostic.hpp>


namespace VCLG {

    /**
     * The rules a node's source must follow (`state-as-data.md` §3.5, §3.6), checked once per
     * definition, when it's loaded:
     * - rule 1: a port is named only in the body of an entry point; helpers receive what they need
     *   as arguments;
     * - rule 2: `[NodeReset]` can't read an input (nothing has produced it yet), and what it writes
     *   to an `[AlwaysWritten]` output is never observed (warning);
     * - `[AlwaysWritten]` applies only to an `[Output]`, whose initializer it makes useless (warning).
     *
     * Errors and warnings point at the user's source. Returns false if there was an error.
     */
    bool CheckNodeRules(const NodeModel& model, VCL::DiagnosticReporter& reporter);

}
