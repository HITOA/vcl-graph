#pragma once

#include <VCL/Core/Diagnostic.hpp>
#include <VCL/Core/DiagnosticConsumer.hpp>

#include <cassert>
#include <string>
#include <vector>


namespace VCLG {

    /**
     * Reports an error in a node's source, as "invalid node: <message>". VCL knows nothing of nodes:
     * this is its generic CustomDiagnostic.
     */
    inline VCL::DiagnosticReporter::ReportHandle ReportNodeError(VCL::DiagnosticReporter& reporter, const std::string& message) {
        return reporter.Error(VCL::Diagnostic::CustomDiagnostic, "invalid node: " + message);
    }

    /**
     * Marks the diagnostics reported on this thread, while the scope is alive, as coming from one
     * node of the graph. The compile passes open a scope around each node they process; a
     * DiagnosticConsumer reads `Current()` from `HandleDiagnostic` to know which node failed.
     *
     * Scopes nest: a SubgraphNode's scope is the parent of the scopes of the nodes compiled inside
     * its subgraph. The innermost scope's path is the full path ("g1/n3/n7": node 7 of the subgraph
     * used by node 3 of graph 1), the same path the symbol mangling uses.
     *
     * VCL's Diagnostic is left untouched: the node is context of the reporting thread, not a field
     * of the diagnostic.
     */
    class NodeDiagnosticScope {
    public:
        NodeDiagnosticScope() = delete;
        NodeDiagnosticScope(std::string path, std::string displayName);
        NodeDiagnosticScope(const NodeDiagnosticScope& other) = delete;
        NodeDiagnosticScope(NodeDiagnosticScope&& other) = delete;
        ~NodeDiagnosticScope();

        NodeDiagnosticScope& operator=(const NodeDiagnosticScope& other) = delete;
        NodeDiagnosticScope& operator=(NodeDiagnosticScope&& other) = delete;

        /** Graph path of the node: "g<graph>/n<node>", with one "/n<node>" per enclosing subgraph use. */
        inline const std::string& GetPath() const { return path; }
        inline const std::string& GetDisplayName() const { return displayName; }
        /** Scope of the enclosing SubgraphNode, or nullptr at the root graph. */
        inline const NodeDiagnosticScope* GetParent() const { return parent; }

        /** Innermost scope open on this thread, or nullptr outside any node. */
        static const NodeDiagnosticScope* Current();

    private:
        std::string path;
        std::string displayName;
        NodeDiagnosticScope* parent;
    };

    /** A copy of `diagnostic` (VCL's Diagnostic is move-only). */
    VCL::Diagnostic CopyDiagnostic(VCL::Diagnostic& diagnostic);

    /**
     * Passes every diagnostic on to `target` and, while recording, keeps a copy: the variant cache
     * replays what a variant's translation reported at every instance using it (`state-as-data.md`
     * §7.3).
     */
    class DiagnosticRecorder : public VCL::DiagnosticConsumer {
    public:
        explicit DiagnosticRecorder(VCL::DiagnosticConsumer* target) : target{ target } {}

        void HandleDiagnostic(VCL::Diagnostic&& diagnostic) override;

        inline void Start() { recording = true; recorded.clear(); }
        /** Stops recording; returns what was reported since Start. */
        std::vector<VCL::Diagnostic> Stop();

    private:
        VCL::DiagnosticConsumer* target;
        bool recording = false;
        std::vector<VCL::Diagnostic> recorded{};
    };

}

/**
 * An invariant of vcl-graph itself: when it fails, it's a bug in vcl-graph, not something the user
 * did. Debug builds stop on the assert; release builds report an internal error (attributed to the
 * current node, see NodeDiagnosticScope) instead of crashing. Evaluates to `cond`.
 */
#define VCLG_CHECK(reporter, cond)                                                                  \
    ((cond) ? true : (assert(!"vcl-graph invariant failed: " #cond),                                \
        (reporter).Error(VCL::Diagnostic::InternalError).SetCompilerInfo(__FILE__, __func__, __LINE__).Report(), \
        false))
