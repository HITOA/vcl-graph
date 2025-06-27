#pragma once

#include <VCL/ExecutionSession.hpp>

#include <atomic>


namespace VCLG {

    struct ExecutionContext {
        std::unique_ptr<VCL::ExecutionSession> session;
        std::atomic_uint32_t useCount;
    };

}