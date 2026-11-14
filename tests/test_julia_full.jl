# Full Julia API (--julia-api full) on the library-shaped example shared with the Python, R6,
# Lua and Common Lisp backends. Argument: the generated module (Features.jl); the library is
# found through FEATURES_JULIA_LIBRARY.
include(abspath(ARGS[1]))
using .Features
const F = Features

checks = 0
function check(ok::Bool, what="")
    global checks += 1
    ok || error("check failed" * (isempty(what) ? "" : ": " * what))
end
macro check(expression, what="")
    text = string(expression)
    :(check($(esc(expression)), $text * " " * $what))
end
function fails(thunk, pattern)
    global checks += 1
    message = try
        thunk()
        nothing
    catch error
        sprint(showerror, error)
    end
    message === nothing && error("expected an error containing $(repr(pattern)), got none")
    occursin(pattern, message) || error("expected an error containing $(repr(pattern)), got: $message")
end
gcfull() = (GC.gc(true); GC.gc(true); sleep(0.05))
import REPL # Docs.doc lives there
doc(name::Symbol, m::Module=Features) = string(Base.Docs.doc(Base.Docs.Binding(m, name)))

# Classes, inheritance and virtual dispatch.
c1 = Circle(2)
@check area(c1) == 12 && name(c1) == "circle" && twice_area(c1) == 24 && radius(c1) == 2
@check occursin("Circle", repr(c1))
@check c1 isa Shape && c1 isa NativeObject
fails(() -> Circle(-1), "negative radius")
fails(() -> Shape(), "abstract")
s = Square()
@check s.side == 1 && area(s) == 1
s.side = 3
@check area(s) == 9 && area(Square(2)) == 4
fails(() -> (s.side = "wide"), "field side")
@check :side in propertynames(s)
@check area_of(s) == 9 && describe(c1) == "circle:0"
# Enums are Symbols; arguments also accept integers.
@check kind(c1) === :circle && kind(s) === :square
@check filled(:solid) && !filled(:none) && filled(1)
fails(() -> filled(:bogus), "unknown enumerator")
@check :custom in Kind
@check Shape_sides_unknown == -1 && abs(golden - 1.618) < 1e-12

# Most-derived results, ownership transfer and shared ownership.
made = make_shape(:circle, 1)
@check made isa Circle && radius(made) == 1
canvas = Canvas()
add(canvas, make_shape(:square, 2))
share(canvas, shared_square(3))
transferred = Circle(1)
add(canvas, transferred)
fails(() -> area(transferred), "closed")
@check size(canvas) == 3 && total(canvas) == 4 + 9 + 3
let first_shape = at(canvas, 0)
    @check first_shape isa Square && area(first_shape) == 4
end
let shared = shared_square(2)
    share(canvas, shared)
    @check area(shared) == 4 && total(canvas) == 20
    @check total_area([Circle(1), shared]) == 7
end

# Overload resolution by Julia type (integer vs float).
@check pick(1) == "int" && pick(1.5) == "double" && pick(2.0) == "double" && pick("x") == "string"
@check pick([1, 2]) == "vector" && pick(s) == "shape"
@check add(1) == 111 && add(1, 2) == 103 && add(1, 2, 3) == 6
fails(() -> add(1, 2, 3, 4), "supported signatures")
fails(() -> add("x"), "argument 'a'")

# Containers: vectors and tuples in, vectors out; Dicts; pairs as tuples.
let counts = count_words(["a", "b", "a"])
    @check counts isa Dict && counts["a"] == 2 && counts["b"] == 1
end
@check collect(min_max((3, 1, 2))) == [1, 3]
fails(() -> min_max(Int[]), "empty input")
@check F.find([5, 7], 7) == 1 && F.find([5, 7], 9) === nothing && F.find([5.0, 7.0], 7) == 1
let tr = F.transpose([[1, 2, 3], [4, 5, 6]])
    @check length(tr) == 3 && tr[1] == [1, 4]
end
@check byte_sum([1, 2, 250]) == 253 && bytes_of("AZ") == UInt8[65, 90]
let cs = circles(3)
    @check length(cs) == 3 && radius(cs[3]) == 3
end
@check F.repeat("z", 3) == "zzz" && initial("hello") == "h"
# Mutable reference parameters update a Ref; plain vectors are passed by value.
let values = Ref([1, 2])
    scale(values, 3)
    @check values[] == [3, 6]
end
let plain = [1, 2]
    scale(plain, 3)
    @check plain == [1, 2]
end
fails(() -> scale(Ref("x"), 2), "argument 'values'")
fails(() -> F.repeat("z", -1), "negative count")
fails(() -> F.repeat("z", 2^40), "argument 'count'")
@check defaults() == "x*1-1.500000"
@check occursin("Must override: `area`.", doc(:Shape)) "overridable methods named"
@check occursin("weight::Float64=0.5", doc(:defaults)) "double defaults in Julia syntax"

# Callbacks both ways.
@check F.apply(x -> x * 2, 21) == 42
fails(() -> F.apply(x -> error("inside callback"), 1), "inside callback")
# The Julia exception comes back as itself.
@check (try F.apply(x -> throw(DivideError()), 1) catch e; e end) isa DivideError
fails(() -> F.apply(x -> "text", 1), "does not convert")
let plus = adder(40)
    @check plus(2) == 42
    @check F.apply(adder(1), 3) == 4
end
# Callbacks from another Julia thread (run with --threads 2 or more).
@check fetch(Threads.@spawn F.apply(x -> x + 1, 1)) == 2

# Value type operators, chaining and borrowed references.
let v = Vec2(1, 2), w = v + Vec2(3, 4)
    @check w.x == 4 && w.y == 6 && (v * 2).y == 4 && (-v).x == -1 && (v - w).x == -3
    @check v == Vec2(1, 2) && !(v == w)
    @check v[1] == 2 && inner(v, Vec2(1, 1)) == 3
    fails(() -> v[2], "Vec2 index")
    chained = set_y(set_x(v, 10), 20)
    @check chained === v && v.x == 10 && v.y == 20
end
let seg = Segment()
    setproperty!(seg, :end, Vec2(3, 4))  # `end` is a keyword: seg.end does not parse
    @check length2(seg) == 25
    start = first(seg)
    start.x = 3
    @check seg.start.x == 3 && length2(seg) == 16
    seg.start.y = 4
    @check length2(seg) == 0
end
@check clone(Vec2(1, 2)).y == 2
@check F.sum([Vec2(1, 2), Vec2(3, 4)]) == 10
let held = first(Segment())
    gcfull()
    @check held.x == 0
end

# Templates, extensions, qualified names and copy types.
let stack = IntStack()
    push(stack, 1); push(stack, 2)
    @check size(stack) == 2 && items(stack) == [1, 2] && pop(stack) == 2
    fails(() -> push(stack, 2.5), "argument 'value'")
end
@check clamp_int(42) == 10 && clamp_int(3, 5) == 3 && clamp_real(2.5) == 2.5
@check label(c1) == "#circle" && label(c1, "@") == "@circle" && radius(unit(Circle)) == 1
@check features__legacy__version() == 2 && features__legacy__Tag().id == 7
@check :off in features__legacy__Mode
@check features__legacy__Circle === Circle && :square in features__legacy__Kind
@check diameter(Circle(2)) == 4
@check features__legacy__area_of(Square()) == 1
let ledger = Ledger(), copy = latest(ledger)
    copy.value = 5
    @check ledger.stamp.value == 1
end
let aim = Aim()
    aim.origin.x = 2
    @check aim.target.point.x == 2
    fails(() -> (aim.target.point = Vec2()), "read-only")
end

# Errors, closing and garbage collection.
fails(() -> area_of(1), "argument 'shape': expected Shape, got Integer")
fails(() -> area(c1, 1), "incompatible arguments")
@check (try area_of(1) catch e; e.kind end) === :binding
@check (try Circle(-1) catch e; e.kind end) === :invalid_argument
let x = Circle(1)
    @check area(x) == 3
    close(x)
    fails(() -> area(x), "closed")
    close(x)
end
for i in 1:200
    tmp = make_shape(:square, i)
    @check area(tmp) == i * i
end
gcfull()

# Directors: Julia subclasses implement C++ virtual methods.
@subclass mutable struct Triangle <: Shape
    base::Float64
    height::Float64
end
F.area(self::Triangle) = self.base * self.height / 2
F.name(self::Triangle) = "triangle"
let tr = Triangle(4, 3)
    @check area(tr) == 6 && area_of(tr) == 6 && twice_area(tr) == 12 && describe(tr) == "triangle:7"
    @check total_area([tr, Circle(1), Square(2)]) == 13 && label(tr, "@") == "@triangle"
    @check tr.base == 4.0 && :base in propertynames(tr)
end
@subclass mutable struct Incomplete <: Shape end
fails(() -> area_of(Incomplete()), "pure virtual method")
@subclass mutable struct Failing <: Shape end
F.area(self::Failing) = error("from Julia")
fails(() -> area_of(Failing()), "from Julia")
@subclass mutable struct Wrong <: Shape end
F.area(self::Wrong) = "not a number"
fails(() -> area_of(Wrong()), "does not convert")
# supercall runs the native implementation rather than dispatching back to the override.
@subclass mutable struct Loud <: Circle end
F.name(self::Loud) = uppercase(supercall(name, self))
@check describe(Loud(; args = (1,))) == "CIRCLE:0" && area(Loud(; args = (2,))) == 12
# Deeper hierarchies go through Julia abstract types (concrete types are final in Julia).
abstract type Noisy <: Circle end
F.name(self::Noisy) = lowercase(supercall(name, self)) * "!"
@subclass mutable struct Louder <: Noisy end
@check describe(Louder(; args = (1,))) == "circle!:0"
# Ownership transfer keeps the Julia object alive; the same Julia object comes back.
@subclass mutable struct UnitShape <: Shape
    tag::Any
end
F.area(self::UnitShape) = 1
canvas = Canvas()
let u = UnitShape(nothing)
    u.tag = "kept"
    add(canvas, u)
    @check area(u) == 1
end
gcfull()
share(canvas, Square(2))
@check total(canvas) == 5 && size(canvas) == 2
let back = at(canvas, 0)
    @check back isa UnitShape && back.tag == "kept" && back === at(canvas, 0)
end
clear(canvas)
gcfull()
share(canvas, UnitShape(nothing))
gcfull()
@check total(canvas) == 1
clear(canvas)
gcfull()
# Callback interfaces with a native default implementation.
@subclass mutable struct Summer <: Visitor
    seen::Any
end
F.visit(self::Summer, value) = value * 10
F.finished(self::Summer, values) = (self.seen = values; nothing)
let summer = Summer(nothing)
    @check run_visitor(summer, [1, 2]) == 30 && summer.seen == [1, 2]
end
@check run_visitor(Visitor(), [1, 2]) == 3

# Docstrings carry the C++ documentation (help mode, editors).
let d = doc(:add)
    @check occursin("Adds three integers, Socha & Dorigo style.", d) "add brief"
    @check occursin("add(a::Int, b::Int=10, c::Int=100) -> Int", d) "add signature"
    @check occursin("`a::Int`: first term", d) "parameter a"
    @check occursin("the sum", d) "returns"
    @check occursin("add(self::Canvas", d) "add is also a Canvas method"
end
@check occursin("area(self::Circle) -> Float64", doc(:area)) "method signature"
@check occursin("Circle(radius::Float64) -> Circle", doc(:Circle)) "class constructors"
@check occursin("`x::Float64`: Horizontal component.", doc(:Vec2)) "fields"
@check occursin("Values: `:circle` (0), `:square` (1), `:custom` (7)", doc(:Kind)) "enum"
@check occursin("@subclass mutable struct MyShape <: Shape", doc(:Shape)) "subclass example"
@check occursin("kind::Symbol=:square", doc(:defaults)) "enum default"
@check occursin("values::Vector{Float64}", doc(:run_visitor)) "vector type"
@check occursin("shapes::Vector{Shape}", doc(:total_area)) "class vector type"
@check occursin("Function (Float64) -> Float64", doc(:apply)) "callback type"
# Policy modules: features::data functions live in submodule Features.data.
let u = data.unit()
    @check u isa Vec2 && u.x == 1 && u.y == 0 "submodule result"
end
@check data.scaled(3) == 6 && data.scaled(2.5) == 6.25 "submodule overloads"
@check data.repeat("ab") == "abab" && F.repeat("x", 3) == "xxx" "submodule names are separate"
@check !isdefined(F, :scaled) "nothing leaks to the main module"
@check occursin("unit() -> Vec2", doc(:unit, data)) && occursin("Mapped by the policy's modules", doc(:unit, data)) "submodule docs"
gcfull()
println("Julia full API passed ($checks checks)")
