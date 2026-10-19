using Test
using Serialization
family, directory = ARGS[1:2]
if family == "repl"
    saved = copy(ARGS)
    empty!(ARGS); push!(ARGS, directory)
    include(saved[3])
else
    include(joinpath(directory, "julia_demo.jl"))
    include(joinpath(directory, "julia_verify.jl"))
    const demo = julia_demo
    const verify = julia_verify
    const Counter = demo.r_demo__Counter
    const Series = verify.r_verify__Series
    const Worklist = verify.r_verify__Worklist
    const AsyncTask = verify.r_verify__AsyncTask
end
v(name, args...) = getproperty(verify, Symbol("r_verify__", name))(args...)
function fails(action, fragment, kind=nothing)
    caught = false
    try
        action()
    catch e
        caught = true
        @test occursin(fragment, sprint(showerror, e))
        kind === nothing || @test e.kind === kind
    end
    @test caught
end
function temporary_owners()
    c = Counter(1)
    aliases = [c, c]
    s = Series()
    q = Worklist(2)
    task = AsyncTask(1, 60000, false)
    # Deterministic finalizer invocation complements last-reference GC below.
    finalize(c); finalize(s); finalize(q); finalize(task)
end
function abandoned_counter()
    owner = Counter(1)
    WeakRef(owner)
end
@testset "Julia $family" begin
    if family == "scalars"
        include(joinpath(directory, "julia_names.jl"))
        @test julia_names.sum() == 42
        @test julia_names.identity(42) == 42
        @test julia_names.sum !== Base.sum && julia_names.identity !== Base.identity
        @test Base.sum([20,22]) == 42
        @test Base.identity("untouched") == "untouched"
        names = ("char", "schar", "uchar", "short", "ushort", "int", "uint", "long", "ulong", "ll", "ull")
        for name in names
            @test v("echo_" * name, 42) == 42
            @test v("echo_" * name, Int64(42)) == 42
            @test v("echo_" * name, UInt64(42)) == 42
            for bad in (true, false, "42", nothing, [42], (42,), Dict(:n => 42), 1+2im, NaN, Inf, 1.5)
                fails(() -> v("echo_" * name, bad), "expected", :conversion)
            end
            fails(() -> v("echo_" * name, 2.0^54), "exact integer")
            fails(() -> v("echo_" * name, Float32(2.0^25)), "exact integer")
        end
        for (name, type) in (("schar",Int8), ("short",Int16), ("int",Int32), ("ll",Int64))
            @test v("echo_" * name, typemin(type)) === Int64(typemin(type))
            @test v("echo_" * name, typemax(type)) === Int64(typemax(type))
            fails(() -> v("echo_" * name, typemax(UInt64)), "range")
            if type !== Int64
                fails(() -> v("echo_" * name, Int64(typemin(type))-1), "range")
                fails(() -> v("echo_" * name, Int64(typemax(type))+1), "range")
            end
        end
        for (name, type) in (("uchar",UInt8), ("ushort",UInt16), ("uint",UInt32), ("ull",UInt64))
            @test v("echo_" * name, typemax(type)) === UInt64(typemax(type))
            fails(() -> v("echo_" * name, -1), "range")
            type === UInt64 || fails(() -> v("echo_" * name, UInt64(typemax(type))+1), "range")
        end
        exact = (UInt64(1) << 53) + 1
        @test demo.r_demo__too_large() === exact
        @test v("echo_ull", exact) === exact
        @test v("echo_float", 1.25) === Float32(1.25)
        @test v("echo_double", floatmax(Float64)) === floatmax(Float64)
        fails(() -> v("echo_float", 1e39), "finite")
        fails(() -> v("nonfinite_result"), "non-finite")
        @test v("echo_bool", true) === true
        @test v("echo_bool", false) === false
        for bad in (0, 1, "true", [true])
            fails(() -> v("echo_bool", bad), "expected", :conversion)
        end
        for text in ("", "world", "世界😀", "a\0b", String(UInt8[0xff,0xfe]), repeat("long",10000), SubString("hello",2,4))
            @test codeunits(v("echo_string", text)) == codeunits(text)
        end
        @test v("embedded_nul") == "a\0b"
        @test v("defaulted") == 42
        @test v("defaulted",12) == 12
        @test v("alias_identity",42) == 42
        @test v("do_nothing") === nothing
        fails(() -> v("echo_int"), "number")
        fails(() -> v("echo_int",1,2), "number")
        for i in 1:20
            fails(() -> demo.r_demo__fail(), "native failure", :native)
            fails(() -> v("unknown_error"), "unknown C++ exception", :native)
            fails(() -> v("allocation_error"), "allocation", :allocation)
            @test demo.r_demo__add(20,22) == 42
        end
        # Root strings across calls even when collections run on another thread.
        collector = Threads.@spawn for i in 1:20; GC.gc(); yield(); end
        for i in 1:100
            text = repeat("gc\0😀",1000) * string(i)
            @test v("echo_string",text) == text
        end
        fetch(collector)
    elseif family == "objects"
        live() = demo.r_demo__Counter__live()
        @test live() == 0
        fails(() -> Counter(-1), "negative initial")
        @test live() == 0
        fails(() -> Counter(), "number")
        c = Counter(20); alias_owner = c
        @test live() == 1
        @test c.add(22) == 42
        @test demo.r_demo__Counter__value(c) == 42
        @test c.count == 42 && c.initial == 20 && demo.r_demo__Counter__answer() == 42
        c.count = 7
        @test alias_owner.value() == 7
        c.label = "世界😀\0"
        @test alias_owner.label == "世界😀\0"
        fails(() -> setproperty!(c,:initial,1), "readonly")
        fails(() -> setproperty!(c,:count,true), "numeric")
        fails(() -> setproperty!(c,:label,1), "string")
        fails(() -> setproperty!(c,:unknown,1), "unknown native property")
        fails(() -> setproperty!(c,:_id,UInt64(1)), "unknown native property")
        fails(() -> c.unknown, "unknown native property")
        fails(() -> c.add(1,2), "number")
        @test :count in propertynames(c)
        foreign = Series()
        fails(() -> c.add(foreign), "expected")
        # A forged internal ID is checked against the native record type.
        other = Worklist(2)
        fails(() -> verify._invoke("r_verify__Series.get",getfield(other,:_id)), "foreign native owner")
        close(other)
        close(foreign)
        for action in (() -> copy(c), () -> deepcopy(c), () -> serialize(IOBuffer(),c))
            fails(action,"native owners cannot",:ownership)
        end
        close(c); close(c)
        @test live() == 0
        fails(() -> alias_owner.value(), "closed")
        fails(() -> alias_owner.count, "closed")
        fails(() -> setproperty!(alias_owner,:count,1), "closed")
        temporary_owners()
        @test live() == 0 && verify.r_verify__Series__live() == 0 && verify.r_verify__AsyncTask__workers() == 0
        weak = abandoned_counter()
        for i in 1:5; GC.gc(); yield(); end
        @test weak.value === nothing && live() == 0
        # Concurrent calls and close are serialized inside the generated library.
        c = Counter(1)
        jobs = [Threads.@spawn(try c.add(1) catch e; @test occursin("closed",sprint(showerror,e)); end) for i in 1:20]
        close(c)
        foreach(fetch,jobs)
        @test live() == 0
    elseif family == "features"
        @test v("maximum",20,42) == 42
        @test v("template_box",42) == 42
        @test v("variadic_sum",10,20,12) == 42
        @test v("constant_answer") == 42
        @test v("constrained_sum",20,22) == 42
        @test v("hierarchy_value",42) == 42
        @test v("hierarchy_crosscast")
        @test v("rosetta_r_c_identity",42) == 42
        fails(() -> v("constrained_sum",typemax(Int32),1), "overflow", :range)
        fails(() -> Series(0,-1), "series count", :index)
        s = Series()
        @test s.size() == 2 && s.get() == 20 && s.scaled_sum(2) == 82
        @test s.text() == "[20, 21]"
        s.set(1,22)
        @test s.scaled_sum(1) == 42
        fails(() -> s.get(2), "out of range", :index)
        fails(() -> s.set(-1,1), "out of range")
        close(s)
        expected = [("m","z","a"),("a","z","m"),("z","m","a"),("a","m","z")]
        for order in 1:4
            q = Worklist(order)
            foreach(x -> q.push(x),("a","z","m"))
            @test q.size() == 3
            @test (q.pop(),q.pop(),q.pop()) == expected[order]
            fails(() -> q.pop(), "empty", :index)
            close(q)
        end
        fails(() -> Worklist(0), "unknown worklist order", :argument)
        task = AsyncTask(21,0,false)
        @test task.wait(2000) && task.result() == 42 && task.result() == 42
        close(task)
        task = AsyncTask(1,0,true)
        @test task.wait(2000)
        fails(() -> task.result(), "async worker failure")
        close(task)
        task = AsyncTask(1,60000,false)
        @test !task.ready() && !task.wait(0) && task.cancel() && task.wait(2000)
        fails(() -> task.result(), "cancelled")
        close(task)
        fails(() -> AsyncTask(1,-1,false), "delay", :index)
        temporary_owners()
        @test verify.r_verify__AsyncTask__workers() == 0
        # Leave a 60s worker rooted until shutdown, bounded by the CTest timeout.
        global pending = AsyncTask(1,60000,false)
    elseif family == "repl"
        @test demo.r_demo__add(20,22) == 42
        c = Counter(20)
        @test c.add(22) == 42
        close(c)
        s = Series()
        @test s.scaled_sum(2) == 82
        close(s)
    else
        error("unknown family $family")
    end
end
