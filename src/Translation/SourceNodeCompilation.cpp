#include <VCLG/Translation/SourceNodeCompilation.hpp>


VCLG::SourceNodeCompilation::SourceNodeCompilation(GraphContext& graphContext, VCL::CompilerContext& cc,
        const ElaboratedGraph::Node& node, VCL::Source* source, const std::string& manglingPrefix) : node{ node } {
    SourceNodeDefinition* definition = node.definition;

    instance = cc.CreateInstance();
    instance->SetManglingPrefix(manglingPrefix);
    instance->CreateASTContext();
    instance->CreateExportSymbolTable();
    instance->CreateImportModuleTable();
    instance->CreateDefineTable();

    lexer = std::make_unique<VCL::Lexer>(source->GetBufferRef(), cc.GetDiagnosticReporter(), cc.GetIdentifierTable());
    stream = std::make_unique<VCL::TokenStream>(*lexer);
    sema = std::make_unique<VCL::Sema>(cc, instance->GetASTContext(), cc.GetDiagnosticReporter(), cc.GetIdentifierTable(),
        cc.GetDirectiveRegistry(), instance->GetExportSymbolTable(), instance->GetImportModuleTable(), instance->GetDefineTable());
    parser = std::make_unique<VCL::Parser>(*stream, *sema, cc.GetAttributeTable());

    parameterWriter = std::make_unique<ASTParameterWriter>(instance->GetASTContext(), cc.GetIdentifierTable(),
        definition->GetParameters(), node.parameters);
    autoParameterWriter = std::make_unique<ASTAutoParameterSubstitution>(*sema, cc.GetIdentifierTable(),
        node.substitutions, definition->GetAutoParameters());

    // Concrete inputs whose type differs in this copy (e.g. promoted by a converter).
    for (const ElaboratedGraph::Input& input : node.inputs)
        portTypeOverrides.push_back(!input.isDependent && input.type != input.declaredType ? input.type : nullptr);
    portWriter = std::make_unique<ASTPortTypeOverrideWriter>(instance->GetASTContext(), cc.GetIdentifierTable(),
        definition->GetPorts(), portTypeOverrides);

    inputConstWriter = std::make_unique<ASTInputConstWriter>(graphContext.GetDefinitionRegistry().GetInputAttributeDefinition());

    consumer.PushConsumer(parameterWriter.get());
    consumer.PushConsumer(autoParameterWriter.get());
    consumer.PushConsumer(portWriter.get());
    consumer.PushConsumer(inputConstWriter.get()); // after portWriter, which replaces some input types
    parser->SetASTConsumer(&consumer);
}

bool VCLG::SourceNodeCompilation::Parse() {
    return parser->Parse();
}
