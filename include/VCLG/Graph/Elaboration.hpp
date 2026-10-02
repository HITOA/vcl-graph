#pragma once

#include <VCLG/Core/IdentityProvider.hpp>
#include <VCLG/Core/SubstitutionTable.hpp>
#include <VCLG/Graph/Node.hpp>

#include <VCL/AST/Type.hpp>
#include <VCL/AST/ConstantValue.hpp>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/Allocator.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>


namespace VCLG {
    class GraphInstance;
    class Port;
    class Converter;
    class SourceNodeDefinition;
    class Elaborator;

    /**
     * A graph as the compiler sees it, built by `Elaborate` and never written back into the editor
     * graph:
     * - subgraphs are flattened: every use of a subgraph is its own copy of the subgraph's nodes,
     *   identified by its graph path ("g1/n3/n7": node 7 of the subgraph used by node 3 of graph
     *   1, the same path the symbol mangling uses);
     * - every port's type is inferred per copy, so two uses of a subgraph fed different types each
     *   get their own;
     * - it holds copies of what the compiler reads from the editor graph (sources, parameter and
     *   initializer values), so it stays valid while the editor graph changes.
     *
     * SubgraphNodes disappear. Each copy of a Subgraph Input is a node whose output carries the
     * value given to the SubgraphNode's input port (connection or initializer), and each copy of a
     * Subgraph Output is a node whose output is read by the SubgraphNode's consumers.
     *
     * The editor uses it too, to show inferred types (`GetPortType`) and to refuse connections that
     * make the graph's types unresolvable.
     */
    class ElaboratedGraph {
    public:
        using NodeIndex = uint32_t;
        using ScopeIndex = uint32_t;
        static constexpr uint32_t Invalid = UINT32_MAX;

        /** Where an input takes its value from: an output of another elaborated node. */
        struct Edge {
            NodeIndex node;
            uint32_t output;
            /** Null for a plain connection. */
            Converter* converter;
        };

        struct Input {
            std::string name;
            /** The type the node declares (it may name the node's AutoParameters). */
            VCL::Type* declaredType;
            /**
             * The type of this copy. For a dependent port, null while unresolved. For an input fed
             * through a converter, the type the converter gives it (e.g. promoted to audio rate).
             */
            VCL::Type* type;
            bool isDependent;
            std::optional<Edge> edge;
            /** The value used while the input is unconnected, if any. */
            std::optional<VCL::ConstantScalar> initializer;
        };

        struct Output {
            std::string name;
            VCL::Type* declaredType;
            /** The type of this copy; null for a dependent port while unresolved. */
            VCL::Type* type;
            bool isDependent;
        };

        struct Node {
            /** Never Subgraph: subgraph uses are flattened. */
            VCLG::Node::NodeKind kind;
            std::string path;
            std::string displayName;
            /** The subgraph use this node belongs to (0: the root graph). */
            ScopeIndex scope;
            bool isOutputNode;
            llvm::SmallVector<Input, 4> inputs;
            llvm::SmallVector<Output, 4> outputs;

            /** Source nodes: source name, definition and parameter values. */
            std::string source;
            SourceNodeDefinition* definition;
            llvm::SmallVector<std::optional<VCL::ConstantScalar>, 4> parameters;

            /**
             * Values inferred for the node's AutoParameters (Source nodes), or for the "Generic" alias
             * of a Subgraph Output or Feedback Input. Null values are unresolved.
             */
            SubstitutionTable substitutions;

            /** Feedback Outputs: the Feedback Input they read, Invalid when not linked. */
            NodeIndex feedbackInput;
        };

        /** A use of a subgraph. Scope 0 is the root graph itself. */
        struct Scope {
            ScopeIndex parent;
            /** Path of the SubgraphNode ("g1/n3"), or of the root graph ("g1"). */
            std::string path;
            std::string displayName;
            /**
             * The graph whose nodes this scope copies, and its generation (`GraphInstance::
             * GetGeneration`): node identities, hence paths, only mean the same node within one
             * generation of one graph.
             */
            Identity graph;
            uint64_t generation;
        };

        /**
         * The first problem found. `node` is the node it belongs to, or Invalid when it belongs to
         * the SubgraphNode of `scope` (or to the whole graph, in scope 0).
         */
        struct Error {
            NodeIndex node;
            ScopeIndex scope;
            std::string message;
        };

    public:
        ElaboratedGraph();
        ElaboratedGraph(const ElaboratedGraph& other) = delete;
        ElaboratedGraph(ElaboratedGraph&& other) = default;
        ~ElaboratedGraph() = default;

        ElaboratedGraph& operator=(const ElaboratedGraph& other) = delete;
        ElaboratedGraph& operator=(ElaboratedGraph&& other) = default;

        inline bool Succeeded() const { return !error.has_value(); }
        inline const std::optional<Error>& GetError() const { return error; }

        inline llvm::ArrayRef<Node> GetNodes() const { return nodes; }
        inline const Node& GetNode(NodeIndex index) const { return nodes[index]; }
        inline llvm::ArrayRef<Scope> GetScopes() const { return scopes; }

        /**
         * The nodes to run, in execution order: the nodes feeding an output node of the root graph,
         * and, in each subgraph use that is run, the nodes feeding the subgraph's own output
         * nodes. Empty when elaboration failed.
         */
        inline llvm::ArrayRef<NodeIndex> GetExecutionOrder() const { return executionOrder; }

        /**
         * Type of a port of the elaborated (root) graph, SubgraphNode ports included. Null for a
         * dependent port whose type isn't resolved, and for a port that isn't part of the graph.
         */
        VCL::Type* GetPortType(const Port* port) const;

        /** Type of an output or input, falling back to its declared type while unresolved. */
        static inline VCL::Type* TypeOf(const Output& output) { return output.type ? output.type : output.declaredType; }
        static inline VCL::Type* TypeOf(const Input& input) { return input.type ? input.type : input.declaredType; }

    private:
        friend class Elaborator;

        struct PortSlot {
            NodeIndex node;
            uint32_t index;
            bool isInput;
        };

        std::vector<Node> nodes;
        std::vector<Scope> scopes;
        std::vector<NodeIndex> executionOrder;
        llvm::DenseMap<const Port*, PortSlot> rootPorts;
        std::optional<Error> error;

        /** Holds the constants created while inferring (template arguments). */
        std::unique_ptr<llvm::BumpPtrAllocator> allocator;
    };

    /**
     * Elaborates `root`: flattens its subgraphs, infers every port's type per copy and computes the
     * execution order. Pure: `root` and its subgraphs are only read. Problems are recorded in the
     * result (`GetError`), not reported; the compiler reports them.
     */
    ElaboratedGraph Elaborate(GraphInstance& root);

    /**
     * The paths `Elaborate(root)` gives the copies of node `node` of `graph`: one per use of
     * `graph`, through nested uses; the node's own path when `graph` is `root`; none when `graph`
     * isn't used. Walks the graphs only (no inference), so it's cheap.
     */
    std::vector<std::string> FindNodePaths(GraphInstance& root, const GraphInstance& graph, Identity node);

}
