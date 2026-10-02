#pragma once

#include <cstdint>
#include <string>


namespace VCL {
    class Type;
}

namespace VCLG {

    /**
     * How the host reads the bytes of a value it doesn't know the VCL type of (an exposed variable,
     * `state-as-data.md` §6.2): `count` elements of one scalar kind, packed, or else raw bytes.
     * Scalars, `Vec`, `Lanes`, arrays and structs of a single scalar kind without padding are
     * elements; anything else is `Bytes`.
     */
    struct ValueFormat {
        enum class Element { Bytes, Bool, Int8, Int16, Int32, Int64, UInt8, UInt16, UInt32, UInt64, Float32, Float64 };

        Element element = Element::Bytes;
        /** The number of elements; for `Bytes`, the size in bytes. */
        uint64_t count = 0;

        inline bool operator==(const ValueFormat& other) const = default;

        /** The element's size in bytes (1 for `Bytes`). */
        static uint64_t ElementSize(Element element);
        /** `float32`, `int32`, ..., `bytes`. */
        static const char* ElementName(Element element);

        /**
         * The format of a value of `type`, whose allocation size is `size` bytes; `vectorWidth` is the
         * number of elements of a `Vec` and of a `Lanes`. `Bytes` when the elements wouldn't cover
         * exactly `size` bytes (padding).
         */
        static ValueFormat Describe(VCL::Type* type, uint64_t size, uint32_t vectorWidth);
    };

}
