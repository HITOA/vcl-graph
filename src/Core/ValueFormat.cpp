#include <VCLG/Core/ValueFormat.hpp>

#include <VCL/AST/Decl.hpp>
#include <VCL/AST/Type.hpp>

#include <optional>


namespace {

    using Element = VCLG::ValueFormat::Element;

    std::optional<Element> BuiltinElement(VCL::BuiltinType::Kind kind) {
        using Kind = VCL::BuiltinType::Kind;
        switch (kind) {
            case Kind::Bool: return Element::Bool;
            case Kind::Int8: return Element::Int8;
            case Kind::Int16: return Element::Int16;
            case Kind::Int32: return Element::Int32;
            case Kind::Int64: return Element::Int64;
            case Kind::UInt8: return Element::UInt8;
            case Kind::UInt16: return Element::UInt16;
            case Kind::UInt32: return Element::UInt32;
            case Kind::UInt64: return Element::UInt64;
            case Kind::Float32: return Element::Float32;
            case Kind::Float64: return Element::Float64;
            default: return std::nullopt;
        }
    }

    // The scalar kind `type` is made of and how many of them, or nullopt when it mixes kinds or
    // holds something that isn't a scalar (a span).
    std::optional<std::pair<Element, uint64_t>> Flatten(VCL::Type* type, uint32_t vectorWidth) {
        type = VCL::Type::GetCanonicalType(type);
        if (type == nullptr)
            return std::nullopt;
        auto repeat = [&](VCL::Type* element, uint64_t count) -> std::optional<std::pair<Element, uint64_t>> {
            std::optional<std::pair<Element, uint64_t>> flat = Flatten(element, vectorWidth);
            if (!flat)
                return std::nullopt;
            return std::make_pair(flat->first, flat->second * count);
        };
        switch (type->GetTypeClass()) {
            case VCL::Type::BuiltinTypeClass: {
                std::optional<Element> element = BuiltinElement(((VCL::BuiltinType*)type)->GetKind());
                if (!element)
                    return std::nullopt;
                return std::make_pair(*element, (uint64_t)1);
            }
            case VCL::Type::VectorTypeClass: return repeat(((VCL::VectorType*)type)->GetElementType().GetType(), vectorWidth);
            case VCL::Type::LanesTypeClass: return repeat(((VCL::LanesType*)type)->GetElementType().GetType(), vectorWidth);
            case VCL::Type::ArrayTypeClass: {
                auto* array = (VCL::ArrayType*)type;
                return repeat(array->GetElementType().GetType(), array->GetElementCount());
            }
            case VCL::Type::RecordTypeClass: {
                VCL::RecordDecl* record = ((VCL::RecordType*)type)->GetRecordDecl();
                std::optional<std::pair<Element, uint64_t>> result{};
                for (auto it = record->Begin(); it != record->End(); ++it) {
                    if (it->GetDeclClass() != VCL::Decl::FieldDeclClass)
                        continue;
                    std::optional<std::pair<Element, uint64_t>> field = Flatten(((VCL::FieldDecl*)it.Get())->GetType().GetType(), vectorWidth);
                    if (!field || (result && result->first != field->first))
                        return std::nullopt;
                    result = result ? std::make_pair(result->first, result->second + field->second) : *field;
                }
                return result;
            }
            default:
                return std::nullopt;
        }
    }

}

uint64_t VCLG::ValueFormat::ElementSize(Element element) {
    switch (element) {
        case Element::Int16: case Element::UInt16: return 2;
        case Element::Int32: case Element::UInt32: case Element::Float32: return 4;
        case Element::Int64: case Element::UInt64: case Element::Float64: return 8;
        default: return 1;
    }
}

const char* VCLG::ValueFormat::ElementName(Element element) {
    switch (element) {
        case Element::Bool: return "bool";
        case Element::Int8: return "int8";
        case Element::Int16: return "int16";
        case Element::Int32: return "int32";
        case Element::Int64: return "int64";
        case Element::UInt8: return "uint8";
        case Element::UInt16: return "uint16";
        case Element::UInt32: return "uint32";
        case Element::UInt64: return "uint64";
        case Element::Float32: return "float32";
        case Element::Float64: return "float64";
        default: return "bytes";
    }
}

VCLG::ValueFormat VCLG::ValueFormat::Describe(VCL::Type* type, uint64_t size, uint32_t vectorWidth) {
    std::optional<std::pair<Element, uint64_t>> flat = Flatten(type, vectorWidth);
    if (flat && flat->second * ElementSize(flat->first) == size)
        return ValueFormat{ flat->first, flat->second };
    return ValueFormat{ Element::Bytes, size };
}
