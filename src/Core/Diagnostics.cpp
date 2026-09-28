#include <VCLG/Core/Diagnostics.hpp>


// Per thread: diagnostics are delivered synchronously on the thread that reports them, so a
// background compile and edits on the UI thread each see their own scopes.
static thread_local VCLG::NodeDiagnosticScope* currentScope = nullptr;

VCLG::NodeDiagnosticScope::NodeDiagnosticScope(std::string path, std::string displayName) :
        path{ std::move(path) }, displayName{ std::move(displayName) }, parent{ currentScope } {
    currentScope = this;
}

VCLG::NodeDiagnosticScope::~NodeDiagnosticScope() {
    assert(currentScope == this && "NodeDiagnosticScope destroyed out of order");
    currentScope = parent;
}

const VCLG::NodeDiagnosticScope* VCLG::NodeDiagnosticScope::Current() {
    return currentScope;
}
