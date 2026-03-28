#pragma once

#include <llvm/IR/IRBuilder.h>


namespace VCLG {
    class Port;
    class GraphContext;

    class Converter {
    public:
        Converter() = default;
        virtual ~Converter() = default;

        inline void SetGraphContext(GraphContext* context) { this->context = context; }
        inline GraphContext& GetGraphContext() { return *context; }

        virtual bool Convertible(Port* outPort, Port* inPort) = 0;

        virtual void OnLinkCreated(VCLG::Port* outPort, VCLG::Port* inPort) = 0;
        virtual void OnLinkDestroyed(VCLG::Port* outPort, VCLG::Port* inPort) = 0;

        virtual bool Emit(llvm::IRBuilder<>& builder, 
            VCLG::Port* outPort, VCLG::Port* inPort, 
            llvm::GlobalVariable* outGV, llvm::GlobalVariable* inGV) = 0;

    private:
        GraphContext* context;
    };

}