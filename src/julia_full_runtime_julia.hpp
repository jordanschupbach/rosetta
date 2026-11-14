#pragma once

// Julia side of the full Julia backend (--julia-api full): emitted into every generated module as
// the internal submodule _Rosetta. It speaks the value encoding of lisp_full_runtime.hpp (the C++
// runtime both full C ABI backends share): Julia encodes a call's arguments into one buffer, calls
// the module's single entry point, and decodes the result. @PKG@ is the Julia module, @C@ the
// prefix of the native entry points, @LIB@ the shared library's base name and @ENV@ the
// environment variable that may name the library file.
//
// The submodule keeps its own namespace: generated functions of the parent module may be named
// like Base functions (length, push, sum, ...) without changing what the runtime calls.
namespace rosetta::detail {
inline constexpr const char* julia_full_runtime_julia = R"jl(
module _Rosetta
import Libdl

"""
    NativeError(kind, message)

A C++ exception or a failed conversion in a call to native module @PKG@. `kind` is `:binding`
(conversion, overload or closed-object errors), `:invalid_argument`, `:out_of_range`,
`:domain_error`, `:length_error`, `:arithmetic_error`, `:allocation` or `:native`.
"""
struct NativeError <: Exception
    kind::Symbol
    message::String
end
Base.showerror(io::IO, e::NativeError) = print(io, e.message)
fail(message) = throw(NativeError(:binding, message))

"""
    NativeObject

Supertype of the classes of native module @PKG@: each value owns (or borrows) a C++ object.
`close(object)` releases the C++ object now; otherwise garbage collection releases it.
"""
abstract type NativeObject end
struct Raw end

holder(o::NativeObject) = getfield(o, :_rosetta_holder)

# ---------------------------------------------------------------- native library
const LIB = Ref{Ptr{Cvoid}}(C_NULL)
const FN_CALL = Ref{Ptr{Cvoid}}(C_NULL)
const FN_FREE = Ref{Ptr{Cvoid}}(C_NULL)
const FN_ALLOC = Ref{Ptr{Cvoid}}(C_NULL)
const FN_RELEASE = Ref{Ptr{Cvoid}}(C_NULL)
const FN_INIT = Ref{Ptr{Cvoid}}(C_NULL)
function library_file()
    variable = get(ENV, "@ENV@", "")
    isempty(variable) || return variable
    name = "@LIB@." * Libdl.dlext
    for directory in (@__DIR__, joinpath(@__DIR__, "..", "deps"), joinpath(@__DIR__, "..", "lib"))
        file = joinpath(directory, name)
        isfile(file) && return file
    end
    return name
end

# ---------------------------------------------------------------- Julia values C++ refers to
# Functions passed as callbacks, subclass objects and objects native code borrows into: ids index
# these tables. C++ references keep entries strong (retain/release); the rest are weak.
const LOCK = ReentrantLock()
const NEXT = Ref{UInt64}(0)
const WEAK = Dict{UInt64,WeakRef}()
const STRONG = Dict{UInt64,Tuple{Any,Int}}()
function new_id()
    lock(LOCK) do
        NEXT[] += 1
        if NEXT[] % 4096 == 0
            filter!(entry -> entry.second.value !== nothing, WEAK)
        end
        NEXT[]
    end
end
function object_id(o::NativeObject)
    id = getfield(o, :_rosetta_id)
    id != 0 && return id
    id = new_id()
    lock(() -> (WEAK[id] = WeakRef(o)), LOCK)
    setfield!(o, :_rosetta_id, id)
    return id
end
function lookup(id::UInt64)
    lock(LOCK) do
        entry = get(STRONG, id, nothing)
        entry === nothing || return entry[1]
        weak = get(WEAK, id, nothing)
        (weak === nothing || weak.value === nothing) && fail("the Julia object of a native reference is gone")
        weak.value
    end
end
function adjust(id::UInt64, delta::Integer)
    lock(LOCK) do
        entry = get(STRONG, id, nothing)
        if delta > 0
            if entry !== nothing
                STRONG[id] = (entry[1], entry[2] + delta)
            else
                weak = get(WEAK, id, nothing)
                weak === nothing || weak.value === nothing || (STRONG[id] = (weak.value, Int(delta)))
            end
        elseif entry !== nothing
            count = entry[2] + delta
            count <= 0 ? delete!(STRONG, id) : (STRONG[id] = (entry[1], count))
        end
    end
    nothing
end

# ---------------------------------------------------------------- encoding (Julia -> C++)
struct Out
    io::IOBuffer
    functions::Vector{UInt64}
    refs::Vector{Any}
end
Out() = Out(IOBuffer(), UInt64[], Any[])
u8(o::Out, x) = write(o.io, UInt8(x))
u32(o::Out, x) = write(o.io, UInt32(x))
u64(o::Out, x) = write(o.io, UInt64(x))
i64(o::Out, x) = write(o.io, Int64(x))
f64(o::Out, x) = write(o.io, Float64(x))
function text(o::Out, s::AbstractString)
    s = String(s)
    u64(o, sizeof(s))
    write(o.io, s)
end
put(o::Out, ::Nothing) = u8(o, 0)
put(o::Out, ::Missing) = u8(o, 0)
put(o::Out, x::Bool) = u8(o, x ? 1 : 19)
put(o::Out, x::Union{Int8,Int16,Int32,Int64,UInt8,UInt16,UInt32}) = (u8(o, 2); i64(o, x))
function put(o::Out, x::Integer)
    if typemin(Int64) <= x <= typemax(Int64)
        u8(o, 2); i64(o, x)
    else
        u8(o, 3); text(o, string(x))
    end
end
put(o::Out, x::Real) = (u8(o, 4); f64(o, x))
put(o::Out, x::Complex) = (u8(o, 18); f64(o, real(x)); f64(o, imag(x)))
put(o::Out, x::AbstractString) = (u8(o, 5); text(o, x))
put(o::Out, x::AbstractChar) = (u8(o, 5); text(o, string(x)))
put(o::Out, x::Symbol) = (u8(o, 14); text(o, string(x)))
function put(o::Out, x::NativeObject)
    address = holder(x)
    u8(o, 10); u64(o, address); u64(o, address == 0 ? 0 : object_id(x))
end
function put(o::Out, x::Base.RefValue)
    push!(o.refs, x)
    u8(o, 13); u32(o, length(o.refs) - 1)
    put(o, x[])
end
function put(o::Out, f::Function)
    id = new_id()
    lock(() -> (STRONG[id] = (f, 1)), LOCK)
    push!(o.functions, id)
    u8(o, 12); u64(o, id)
end
function put(o::Out, x::AbstractDict)
    u8(o, 9); u64(o, length(x))
    for (k, v) in x
        put(o, k); put(o, v)
    end
end
put(o::Out, x::Pair) = (u8(o, 8); put(o, x.first); put(o, x.second))
function put(o::Out, x::AbstractVector{<:AbstractFloat})
    u8(o, 7); u64(o, length(x))
    for v in x
        f64(o, v)
    end
end
function put(o::Out, x::AbstractVector{UInt8})
    u8(o, 15); u64(o, length(x))
    for v in x
        u8(o, v)
    end
end
# A matrix is the sequence of its rows (std::vector<std::vector<T>>).
function put(o::Out, x::AbstractMatrix)
    u8(o, 6); u64(o, size(x, 1))
    for i in axes(x, 1)
        put(o, collect(x[i, :]))
    end
end
function put(o::Out, x::Union{AbstractVector,Tuple,AbstractSet})
    u8(o, 6); u64(o, length(x))
    for v in x
        put(o, v)
    end
end
put(o::Out, x) = fail("cannot pass a value of type $(typeof(x)) to C++")

# ---------------------------------------------------------------- decoding (C++ -> Julia)
mutable struct In
    data::Vector{UInt8}
    at::Int
end
function rd(r::In, ::Type{T}) where {T}
    r.at + sizeof(T) > length(r.data) && fail("malformed native value")
    value = GC.@preserve r unsafe_load(Ptr{T}(pointer(r.data) + r.at))
    r.at += sizeof(T)
    return value
end
function rdtext(r::In)
    n = Int(rd(r, UInt64))
    r.at + n > length(r.data) && fail("malformed native value")
    s = String(r.data[r.at+1:r.at+n])
    r.at += n
    isvalid(s) || fail("C++ string is not valid UTF-8")
    return s
end
# Vectors and dicts get the narrowest element type their values share.
function narrow(v::Vector{Any})
    isempty(v) && return v
    T = typeof(v[1])
    for x in v
        T = typejoin(T, typeof(x))
    end
    T === Any ? v : convert(Vector{T}, v)
end
function narrow(d::Dict{Any,Any})
    isempty(d) && return d
    K = mapreduce(typeof, typejoin, keys(d))
    V = mapreduce(typeof, typejoin, values(d))
    convert(Dict{K,V}, d)
end
const CLASSES = Ref{Vector{Any}}(Any[])
const GENERICS = Ref{Vector{Any}}(Any[])
function release_later(o::NativeObject)
    address = getfield(o, :_rosetta_holder)
    address != 0 && FN_RELEASE[] != C_NULL && ccall(FN_RELEASE[], Cvoid, (UInt64, Cint), address, 0)
    nothing
end
function adopt!(o::NativeObject, address::UInt64, parent)
    setfield!(o, :_rosetta_holder, address)
    setfield!(o, :_rosetta_parent, parent)
    finalizer(release_later, o)
end
function wrap(address::UInt64, class::Integer, parent)
    o = CLASSES[][class+1](Raw())
    adopt!(o, address, parent)
end
"""
    NativeFunction

A native C++ `std::function` returned to Julia; calling it calls the C++ function.
"""
mutable struct NativeFunction <: NativeObject
    _rosetta_holder::UInt64
    _rosetta_parent::Any
    _rosetta_id::UInt64
    NativeFunction(::Raw) = new(0, nothing, 0)
end
struct Callee
    f::NativeFunction
end
put(o::Out, x::Callee) = invoke(put, Tuple{Out,NativeObject}, o, x.f)
(f::NativeFunction)(args...) = call(0, (Callee(f), args...))
# Passed to C++ as a function argument: a Julia closure calling it.
put(o::Out, f::NativeFunction) = put(o, (args...) -> f(args...))
function getvalue(r::In, arguments)
    tag = rd(r, UInt8)
    tag == 0 && return nothing
    tag == 1 && return true
    tag == 19 && return false
    tag == 2 && return rd(r, Int64)
    if tag == 3
        n = parse(BigInt, rdtext(r))
        return n <= typemax(UInt64) && n >= 0 ? UInt64(n) : n
    end
    tag == 4 && return rd(r, Float64)
    tag == 18 && return complex(rd(r, Float64), rd(r, Float64))
    tag == 5 && return rdtext(r)
    tag == 14 && return Symbol(rdtext(r))
    if tag == 15
        n = Int(rd(r, UInt64))
        r.at + n > length(r.data) && fail("malformed native value")
        v = r.data[r.at+1:r.at+n]
        r.at += n
        return v
    end
    if tag == 6
        n = Int(rd(r, UInt64))
        return narrow(Any[getvalue(r, arguments) for _ in 1:n])
    end
    if tag == 16
        n = Int(rd(r, UInt64))
        return Tuple(getvalue(r, arguments) for _ in 1:n)
    end
    if tag == 7
        n = Int(rd(r, UInt64))
        r.at + 8n > length(r.data) && fail("malformed native value")
        v = Vector{Float64}(undef, n)
        GC.@preserve r v unsafe_copyto!(Ptr{UInt8}(pointer(v)), pointer(r.data) + r.at, 8n)
        r.at += 8n
        return v
    end
    if tag == 9
        d = Dict{Any,Any}()
        for _ in 1:Int(rd(r, UInt64))
            key = getvalue(r, arguments)
            d[key] = getvalue(r, arguments)
        end
        return narrow(d)
    end
    tag == 8 && return Pair(getvalue(r, arguments), getvalue(r, arguments))
    if tag == 10
        address = rd(r, UInt64)
        class = rd(r, UInt32)
        parent = rd(r, Int64)
        return wrap(address, class, parent >= 0 && arguments !== nothing ? arguments[parent+1] : nothing)
    end
    tag == 11 && return lookup(rd(r, UInt64))
    tag == 12 && return adopt!(NativeFunction(Raw()), rd(r, UInt64), nothing)
    tag == 17 && return arguments[rd(r, UInt32)+1]
    fail("malformed native value (tag $tag)")
end

# ---------------------------------------------------------------- calls
const CALLBACK_EXCEPTION = Ref{Any}(nothing)
error_kind(kind) = Symbol(replace(string(kind), "-" => "_"))
function call(index::Integer, arguments, into=nothing)
    FN_CALL[] == C_NULL && fail("native module @PKG@ is not loaded")
    out = Out()
    try
        # Julia and C++ buffer standard output separately: flush before C++ writes.
        flush(stdout)
        u32(out, length(arguments))
        for argument in arguments
            put(out, argument)
        end
        data = take!(out.io)
        result = Ref{Ptr{UInt8}}(C_NULL)
        result_size = Ref{Csize_t}(0)
        status = GC.@preserve data ccall(FN_CALL[], Cint, (UInt32, Ptr{UInt8}, Csize_t, Ptr{Ptr{UInt8}}, Ptr{Csize_t}),
                                         index, data, length(data), result, result_size)
        pointer = result[]
        try
            reader = In(pointer == C_NULL ? UInt8[] : copy(unsafe_wrap(Vector{UInt8}, pointer, Int(result_size[]))), 0)
            if status == 0
                value = if into !== nothing && !isempty(reader.data) && reader.data[1] == 0x0a
                    rd(reader, UInt8)
                    address = rd(reader, UInt64)
                    rd(reader, UInt32); rd(reader, Int64)
                    adopt!(into, address, nothing)
                else
                    getvalue(reader, arguments)
                end
                for _ in 1:Int(rd(reader, UInt32))
                    slot = rd(reader, UInt32)
                    out.refs[slot+1][] = getvalue(reader, arguments)
                end
                return value
            elseif status == 2
                exception = CALLBACK_EXCEPTION[]
                CALLBACK_EXCEPTION[] = nothing
                getvalue(reader, nothing)
                message = getvalue(reader, nothing)
                exception === nothing ? throw(NativeError(:julia, message)) : throw(exception)
            elseif status == 1
                kind = getvalue(reader, nothing)
                throw(NativeError(error_kind(kind), getvalue(reader, nothing)))
            else
                throw(NativeError(:allocation, "out of memory in a native call"))
            end
        finally
            pointer == C_NULL || ccall(FN_FREE[], Cvoid, (Ptr{UInt8},), pointer)
        end
    finally
        for id in out.functions
            adjust(id, -1)
        end
    end
end

# Calls from C++: callbacks (kind 0) and overrides of virtual methods (kind 1).
function respond(out::Out, result::Ptr{Ptr{UInt8}}, result_size::Ptr{Csize_t})
    bytes = take!(out.io)
    buffer = ccall(FN_ALLOC[], Ptr{UInt8}, (Csize_t,), max(1, length(bytes)))
    GC.@preserve bytes unsafe_copyto!(buffer, pointer(bytes), length(bytes))
    unsafe_store!(result, buffer)
    unsafe_store!(result_size, length(bytes))
end
function invoke_callback(kind::Cint, id::UInt64, generic::UInt32, data::Ptr{UInt8}, size::Csize_t,
                         result::Ptr{Ptr{UInt8}}, result_size::Ptr{Csize_t})::Cint
    status = Cint(0)
    out = Out()
    try
        reader = In(copy(unsafe_wrap(Vector{UInt8}, data, Int(size))), 0)
        arguments = Any[getvalue(reader, nothing) for _ in 1:Int(rd(reader, UInt32))]
        value = kind == 0 ? lookup(id)(arguments...) : GENERICS[][generic+1](arguments...)
        try
            put(out, value)
        catch
            # Not encodable (e.g. the result of a void override): C++ sees nothing.
            out = Out()
            put(out, nothing)
        end
    catch exception
        CALLBACK_EXCEPTION[] = exception
        out = Out()
        u8(out, 5)
        text(out, try sprint(showerror, exception) catch; "error in a Julia callback" end)
        status = Cint(2)
    end
    flush(stdout)
    respond(out, result, result_size)
    return status
end
retain_callback(id::UInt64, delta::Cint)::Cvoid = adjust(id, delta)

# ---------------------------------------------------------------- objects
# Native class (the abstract type) -> (constructor index, overridable from Julia, storage type).
const CONSTRUCTORS = Dict{Any,Tuple{Int,Bool,Any}}()
register_constructor(class, storage, index, director) = (CONSTRUCTORS[class] = (index, director, storage))
# The native class a Julia value is an instance of (its nearest generated supertype).
function native_class(T)
    while T !== Any
        haskey(CONSTRUCTORS, T) && return T
        T = supertype(T)
    end
    nothing
end
function construct(class, arguments)
    entry = get(CONSTRUCTORS, class, nothing)
    entry === nothing && fail("$(nameof(class)) has no public constructor")
    call(entry[1], (0, arguments...), entry[3](Raw()))
end
# Constructs the native part of a Julia subclass instance (see @subclass).
function construct!(self::NativeObject, arguments)
    class = native_class(supertype(typeof(self)))
    class === nothing && fail("$(nameof(typeof(self))) does not derive from a native class with a public constructor")
    index, director, _ = CONSTRUCTORS[class]
    call(index, (director ? object_id(self) : 0, arguments...), self)
end
function Base.close(o::NativeObject)
    address = getfield(o, :_rosetta_holder)
    if address != 0
        setfield!(o, :_rosetta_holder, UInt64(0))
        ccall(FN_RELEASE[], Cvoid, (UInt64, Cint), address, 1)
    end
    nothing
end
shown_name(T) = (name = string(nameof(T)); startswith(name, "_") && supertype(T) !== NativeObject ? string(nameof(supertype(T))) : name)
function Base.show(io::IO, o::NativeObject)
    print(io, shown_name(typeof(o)), getfield(o, :_rosetta_holder) == 0 ? "(closed)" : "()")
end
function show_with(io::IO, o::NativeObject, to_string)
    if getfield(o, :_rosetta_holder) == 0
        print(io, shown_name(typeof(o)), "(closed)")
    else
        text = try to_string(o) catch; "?" end
        print(io, shown_name(typeof(o)), "(", text, ")")
    end
end
# Fields: name -> (getter, setter) function indices.
function get_property(o::NativeObject, name::Symbol, fields)
    entry = get(fields, name, nothing)
    entry === nothing ? getfield(o, name) : call(entry[1], (o,))
end
function set_property!(o::NativeObject, name::Symbol, value, fields)
    entry = get(fields, name, nothing)
    if entry === nothing
        setfield!(o, name, convert(fieldtype(typeof(o), name), value))
    else
        call(entry[2], (o, value))
    end
    value
end
function property_names(o::NativeObject, fields)
    own = Tuple(name for name in fieldnames(typeof(o)) if !startswith(string(name), "_rosetta_"))
    (keys(fields)..., own...)
end

"""
    supercall(f, self, args...)

Calls the native C++ implementation of method `f` on `self`, an instance of a Julia subclass that
overrides it (like `super` or `call-next-method`).
"""
function supercall(f, self::NativeObject, args...)
    class = native_class(supertype(typeof(self)))
    class === nothing && fail("$(nameof(typeof(self))) does not derive from a native class")
    invoke(f, Tuple{class,Vararg{Any}}, self, args...)
end

"""
    @subclass mutable struct Name <: NativeClass
        field::Type
        ...
    end

Defines a Julia subclass of a native class of @PKG@. Methods of the module's functions defined
for it override the C++ virtual methods, also when C++ calls them:

    @subclass mutable struct Triangle <: Shape
        base::Float64
        height::Float64
    end
    @PKG@.area(t::Triangle) = t.base * t.height / 2
    t = Triangle(4.0, 3.0)                 # the fields, in order
    t = Triangle(4.0, 3.0; args = (2,))    # with arguments for the C++ constructor

`supercall(f, self, args...)` runs the C++ implementation of an overridden method.
"""
macro subclass(definition)
    (definition isa Expr && definition.head === :struct) || error("@subclass expects a struct definition")
    head = definition.args[2]
    (head isa Expr && head.head === :(<:)) || error("@subclass expects `struct Name <: NativeClass`")
    name = head.args[1]
    name isa Symbol || error("@subclass does not support type parameters")
    definition.args[1] = true # mutable
    body = definition.args[3]
    fields = Symbol[]
    for item in body.args
        item isa LineNumberNode && continue
        if item isa Symbol
            push!(fields, item)
        elseif item isa Expr && item.head === :(::) && item.args[1] isa Symbol
            push!(fields, item.args[1])
        else
            error("@subclass supports plain field declarations only")
        end
    end
    runtime = [:(_rosetta_holder::UInt64), :(_rosetta_parent::Any), :(_rosetta_id::UInt64)]
    constructor = :(function $name($(fields...); args = ())
        self = new(UInt64(0), nothing, UInt64(0), $(fields...))
        $(construct!)(self, args)
        self
    end)
    body.args = vcat(runtime, body.args, [constructor])
    esc(definition)
end

function open_module()
    LIB[] = Libdl.dlopen(library_file())
    FN_CALL[] = Libdl.dlsym(LIB[], :@C@_call)
    FN_FREE[] = Libdl.dlsym(LIB[], :@C@_free)
    FN_ALLOC[] = Libdl.dlsym(LIB[], :@C@_alloc)
    FN_RELEASE[] = Libdl.dlsym(LIB[], :@C@_release)
    FN_INIT[] = Libdl.dlsym(LIB[], :@C@_init)
    invoke = @cfunction(invoke_callback, Cint, (Cint, UInt64, UInt32, Ptr{UInt8}, Csize_t, Ptr{Ptr{UInt8}}, Ptr{Csize_t}))
    retain = @cfunction(retain_callback, Cvoid, (UInt64, Cint))
    ccall(FN_INIT[], Cint, (Cint, Ptr{Cvoid}, Ptr{Cvoid}), 1, invoke, retain) == 0 ||
        error("native module @PKG@ was generated by a different Rosetta version")
    nothing
end
end # module _Rosetta
)jl";
} // namespace rosetta::detail
