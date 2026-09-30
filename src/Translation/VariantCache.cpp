#include <VCLG/Translation/VariantCache.hpp>

#include <VCL/Core/SourceManager.hpp>
#include <VCL/Frontend/ModuleCache.hpp>

#include <llvm/Support/xxhash.h>
#include <llvm/Transforms/Utils/Cloning.h>


VCLG::VariantCache::Entry* VCLG::VariantCache::Find(const VariantKey& key, VCL::CompilerContext& cc, llvm::LLVMContext& context) {
    auto it = entries.find(key.text);
    if (it == entries.end())
        return nullptr;
    Entry& entry = *it->second;
    if (&entry.module->getContext() != &context || !IsValid(entry, cc))
        return nullptr;
    entry.lastUsed = compile;
    return &entry;
}

void VCLG::VariantCache::Insert(const TranslatedNode& node, const llvm::Module& module, std::vector<VCL::Diagnostic> diagnostics,
        VCL::Source& source) {
    auto entry = std::make_unique<Entry>();
    entry->node = node;
    // The AST isn't needed any more: the module and the interface are what the graph uses.
    entry->node.instance = nullptr;
    entry->node.translationUnit = nullptr;
    entry->module = llvm::CloneModule(module);
    entry->diagnostics = std::move(diagnostics);
    entry->source = source.GetBufferIdentifier().str();
    entry->buffer = source.GetBufferRef().getBufferStart();
    entry->lastUsed = compile;
    entries[node.key.text] = std::move(entry);
}

void VCLG::VariantCache::EndCompile(VCL::CompilerContext& cc) {
    ++compile;
    for (auto it = entries.begin(); it != entries.end();) {
        auto current = it++;
        const Entry& entry = *current->second;
        if (entry.lastUsed + maxIdleCompiles < compile || !IsValid(entry, cc))
            entries.erase(current);
    }
}

bool VCLG::VariantCache::IsValid(const Entry& entry, VCL::CompilerContext& cc) const {
    VCL::SourceManager& sources = cc.GetSourceManager();
    // The node's source. Its hash is in the key, so an edited source is a new key; but a reloaded
    // buffer, even with the same text, invalidates the locations the diagnostics hold.
    VCL::Source* source = sources.GetSourceFromName(entry.source);
    if (source == nullptr || source->GetBufferRef().getBufferStart() != entry.buffer
            || llvm::xxh3_64bits(source->GetBufferRef().getBuffer()) != entry.node.interface.sourceHash)
        return false;
    for (const LibraryDependency& library : entry.node.libraries) {
        VCL::Source* librarySource = sources.GetSourceFromName(library.source);
        if (librarySource == nullptr || librarySource->GetBufferRef().getBufferStart() != library.buffer
                || llvm::xxh3_64bits(librarySource->GetBufferRef().getBuffer()) != library.hash)
            return false;
        // A direct import is linked with the graph: it must be compiled.
        if (library.direct && cc.GetModuleCache().Get(librarySource) == nullptr)
            return false;
    }
    return true;
}
