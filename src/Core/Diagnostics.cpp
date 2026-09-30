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

VCL::Diagnostic VCLG::CopyDiagnostic(VCL::Diagnostic& diagnostic) {
    VCL::Diagnostic copy{ diagnostic.GetSeverity(), diagnostic.GetMessage(), diagnostic.GetCompilerFile(),
        diagnostic.GetCompilerFunc(), diagnostic.GetCompilerLine() };
    for (auto it = diagnostic.GetArgsBegin(); it != diagnostic.GetArgsEnd(); ++it)
        copy.AddArgument(*it);
    for (auto it = diagnostic.GetHintsBegin(); it != diagnostic.GetHintsEnd(); ++it)
        copy.AddHints(*it);
    return copy;
}

void VCLG::DiagnosticRecorder::HandleDiagnostic(VCL::Diagnostic&& diagnostic) {
    if (recording)
        recorded.push_back(CopyDiagnostic(diagnostic));
    if (target != nullptr)
        target->HandleDiagnostic(std::move(diagnostic));
}

std::vector<VCL::Diagnostic> VCLG::DiagnosticRecorder::Stop() {
    recording = false;
    return std::move(recorded);
}
