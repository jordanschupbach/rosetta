#pragma once
#include "cabi_runtime.hpp"

namespace rosetta::detail {
inline constexpr auto julia_runtime = cabi_runtime;

inline constexpr const char* julia_interface_runtime = R"jl(
using Libdl
import Serialization
struct RosettaError <: Exception
    kind::Symbol
    message::String
end
Base.showerror(io::IO, e::RosettaError) = print(io, "Rosetta ", e.kind, ": ", e.message)
struct _Value
    tag::UInt64
    integer::Int64
    unsigned_integer::UInt64
    real::Float64
    data::Ptr{UInt8}
    size::Csize_t
end
struct _Result
    value::_Value
    error::UInt64
end
const _empty = _Value(0, 0, 0, 0, C_NULL, 0)
_pack(x::Bool) = _Value(4, 0, x, 0, C_NULL, 0)
_pack(x::Union{Int8,Int16,Int32,Int64}) = _Value(1, x, 0, 0, C_NULL, 0)
_pack(x::Union{UInt8,UInt16,UInt32,UInt64}) = _Value(2, 0, x, 0, C_NULL, 0)
_pack(x::Float64) = _Value(3, 0, 0, x, C_NULL, 0)
_pack(x::Float32) = _Value(6, 0, 0, x, C_NULL, 0)
_pack(x::String) = _Value(5, 0, 0, 0, pointer(x), sizeof(x))
_pack(x) = throw(RosettaError(:conversion, "expected a machine integer, float, Bool or string"))
_text(v::_Value) = v.size == 0 ? "" : unsafe_string(v.data, v.size)
function _decode(v::_Value)
    v.tag == 0 && return nothing
    v.tag == 1 && return v.integer
    v.tag == 2 && return v.unsigned_integer
    v.tag == 3 && return v.real
    v.tag == 4 && return v.unsigned_integer != 0
    v.tag == 5 && return _text(v)
    v.tag == 6 && return Float32(v.real)
    throw(RosettaError(:conversion, "invalid native result tag"))
end
abstract type _Owner end
Base.copy(::_Owner) = throw(RosettaError(:ownership, "native owners cannot be copied"))
Base.deepcopy_internal(::_Owner, ::IdDict) = throw(RosettaError(:ownership, "native owners cannot be copied"))
Serialization.serialize(::Serialization.AbstractSerializer, ::_Owner) = throw(RosettaError(:ownership, "native owners cannot be serialized"))
)jl";
} // namespace rosetta::detail
