#pragma once

#include <VCL/AST/Type.hpp>

#include <llvm/IR/IRBuilder.h>


namespace VCLG {
    class GraphContext;

    /**
     * Connects an output to an input of a different type (e.g. a host's control-rate conversion).
     * A connection records the converter chosen when it was made; elaboration then asks it, for
     * each copy of the connection, which type the input takes.
     */
    class Converter {
    public:
        Converter() = default;
        virtual ~Converter() = default;

        inline void SetGraphContext(GraphContext* context) { this->context = context; }
        inline GraphContext& GetGraphContext() { return *context; }

        /** Whether a value of `outType` can feed an input declared as `inType`. */
        virtual bool Convertible(VCL::Type* outType, VCL::Type* inType) = 0;

        /**
         * The type an input declared as `inType` takes when fed `outType` through this converter
         * (`inType` itself when unchanged). Only called when `Convertible` is true.
         */
        virtual VCL::Type* GetInputType(VCL::Type* outType, VCL::Type* inType) = 0;

        /**
         * Emits the conversion into `builder`: reads `outGV` (of `outType`), writes `inGV` (of
         * `inType`, the type `GetInputType` returned).
         */
        virtual bool Emit(llvm::IRBuilder<>& builder, VCL::Type* outType, VCL::Type* inType,
            llvm::GlobalVariable* outGV, llvm::GlobalVariable* inGV) = 0;

    private:
        GraphContext* context;
    };

}
