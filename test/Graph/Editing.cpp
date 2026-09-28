#include "Common/GraphTest.hpp"


TEST_CASE_METHOD(Test::GraphTest, "Ports of the same type connect", "[Graph][Editing]") {
    auto graph = context.CreateInstance();
    auto* a = AddNode(*graph, "Add");
    auto* b = AddNode(*graph, "Add");

    VCLG::Identity connection = Connect(*graph, a->GetOutputs()[0], b->GetInputs()[0]);
    REQUIRE(connection != INVALID_IDENTITY);
    REQUIRE(graph->GetConnections().size() == 1);

    SECTION("in either argument order") {
        REQUIRE(Connect(*graph, b->GetInputs()[1], a->GetOutputs()[0]) != INVALID_IDENTITY);
        REQUIRE(graph->GetConnections().size() == 2);
    }

    SECTION("connecting the same ports again returns the existing connection") {
        REQUIRE(Connect(*graph, a->GetOutputs()[0], b->GetInputs()[0]) == connection);
        REQUIRE(graph->GetConnections().size() == 1);
    }
}

TEST_CASE_METHOD(Test::GraphTest, "An input takes a single connection", "[Graph][Editing]") {
    auto graph = context.CreateInstance();
    auto* a = AddNode(*graph, "Add");
    auto* b = AddNode(*graph, "Add");
    auto* c = AddNode(*graph, "Add");

    REQUIRE(Connect(*graph, a->GetOutputs()[0], c->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(Connect(*graph, b->GetOutputs()[0], c->GetInputs()[0]) == INVALID_IDENTITY);
    REQUIRE(graph->GetConnections().size() == 1);
}

TEST_CASE_METHOD(Test::GraphTest, "Two outputs or two inputs don't connect", "[Graph][Editing]") {
    auto graph = context.CreateInstance();
    auto* a = AddNode(*graph, "Add");
    auto* b = AddNode(*graph, "Add");

    REQUIRE(Connect(*graph, a->GetOutputs()[0], b->GetOutputs()[0]) == INVALID_IDENTITY);
    REQUIRE(Connect(*graph, a->GetInputs()[0], b->GetInputs()[0]) == INVALID_IDENTITY);
    REQUIRE(graph->GetConnections().empty());
}

TEST_CASE_METHOD(Test::GraphTest, "Ports of different types don't connect without a converter", "[Graph][Editing]") {
    auto graph = context.CreateInstance();
    auto* source = AddNode(*graph, "IntSource");
    auto* add = AddNode(*graph, "Add");

    REQUIRE(Connect(*graph, source->GetOutputs()[0], add->GetInputs()[0]) == INVALID_IDENTITY);
    REQUIRE(graph->GetConnections().empty());
}

TEST_CASE_METHOD(Test::GraphTest, "Connections that would close a cycle are refused", "[Graph][Editing][Regression]") {
    // Used to be accepted, after which Validate and Emit looped forever.
    auto graph = context.CreateInstance();
    auto* a = AddNode(*graph, "Add");
    auto* b = AddNode(*graph, "Add");
    auto* c = AddNode(*graph, "Add");

    REQUIRE(Connect(*graph, a->GetOutputs()[0], b->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(Connect(*graph, b->GetOutputs()[0], c->GetInputs()[0]) != INVALID_IDENTITY);

    SECTION("a loop through several nodes") {
        REQUIRE(Connect(*graph, c->GetOutputs()[0], a->GetInputs()[0]) == INVALID_IDENTITY);
    }
    SECTION("a direct loop") {
        REQUIRE(Connect(*graph, b->GetOutputs()[0], a->GetInputs()[1]) == INVALID_IDENTITY);
    }
    SECTION("a self-loop") {
        REQUIRE(Connect(*graph, a->GetOutputs()[0], a->GetInputs()[1]) == INVALID_IDENTITY);
    }
    SECTION("a parallel path is not a cycle") {
        REQUIRE(Connect(*graph, a->GetOutputs()[0], c->GetInputs()[1]) != INVALID_IDENTITY);
    }

    REQUIRE(graph->Validate());
}

TEST_CASE_METHOD(Test::GraphTest, "Destroying a node removes its connections", "[Graph][Editing]") {
    auto graph = context.CreateInstance();
    auto* a = AddNode(*graph, "Add");
    auto* b = AddNode(*graph, "Add");
    auto* c = AddNode(*graph, "Add");
    REQUIRE(Connect(*graph, a->GetOutputs()[0], b->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(Connect(*graph, b->GetOutputs()[0], c->GetInputs()[0]) != INVALID_IDENTITY);

    graph->DestroyNode(b);
    REQUIRE(graph->GetConnections().empty());
    REQUIRE(graph->GetNodes().size() == 2);

    // The freed input can be connected again.
    REQUIRE(Connect(*graph, a->GetOutputs()[0], c->GetInputs()[0]) != INVALID_IDENTITY);
}

TEST_CASE_METHOD(Test::GraphTest, "Destroying a connection frees the input", "[Graph][Editing]") {
    auto graph = context.CreateInstance();
    auto* a = AddNode(*graph, "Add");
    auto* b = AddNode(*graph, "Add");
    auto* c = AddNode(*graph, "Add");

    VCLG::Identity connection = Connect(*graph, a->GetOutputs()[0], c->GetInputs()[0]);
    REQUIRE(connection != INVALID_IDENTITY);
    graph->DestroyConnection(connection);
    REQUIRE(graph->GetConnections().empty());
    REQUIRE(Connect(*graph, b->GetOutputs()[0], c->GetInputs()[0]) != INVALID_IDENTITY);
}

TEST_CASE_METHOD(Test::GraphTest, "A node without [NodeProcess] is reported", "[Graph][Editing][Regression]") {
    // Used to be an internal compiler error, and leaked the definitions allocated so far.
    auto graph = context.CreateInstance();
    REQUIRE(graph->InstantiateSourceNode(LoadNode("NoProcess")) == nullptr);
    REQUIRE(consumer.HasError("no [NodeProcess] function"));
}

TEST_CASE_METHOD(Test::GraphTest, "Feedback Output keeps a live port when retargeted", "[Graph][Editing][Regression]") {
    // OverwritePort used to return the port it had just destroyed.
    auto graph = context.CreateInstance();
    auto* scalarLoop = graph->InstantiateTransientNode<VCLG::FeedbackInputNode>();
    auto* arrayLoop = graph->InstantiateTransientNode<VCLG::FeedbackInputNode>();
    auto* source = AddNode(*graph, "ArraySource");
    REQUIRE(Connect(*graph, source->GetOutputs()[0], arrayLoop->GetInputs()[0]) != INVALID_IDENTITY);

    auto* reader = graph->InstantiateTransientNode<VCLG::FeedbackOutputNode>();
    reader->Update(scalarLoop->GetIdentity());
    REQUIRE(reader->GetOutputs().size() == 1);

    reader->Update(arrayLoop->GetIdentity());   // different type: the port is replaced
    REQUIRE(reader->GetOutputs().size() == 1);
    VCLG::Port* port = reader->GetOutputs()[0];
    REQUIRE(graph->GetPortByIdentity(port->GetIdentity()) == port);
    REQUIRE(VCL::Type::IsCanonicallyEqual(port->GetType(), arrayLoop->GetType()));
}
