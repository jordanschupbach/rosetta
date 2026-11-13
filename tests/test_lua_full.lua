-- Full Lua API (--lua-api full) on the library-shaped example shared with the Python and R6 backends.
-- Arguments: the compiled module, then the generated LuaCATS stub.
local module_path, meta_path = arg[1], arg[2]
local api = assert(package.loadlib(module_path, "luaopen_features"))()

local function check(condition, message)
    if not condition then error(message or "check failed", 2) end
end
local function fails(fn, pattern)
    local ok, message = pcall(fn)
    if ok then error("expected error containing '" .. pattern .. "', got no error", 2) end
    message = tostring(message)
    if not message:find(pattern, 1, true) then
        error("expected error containing '" .. pattern .. "', got: " .. message, 2)
    end
end
local function same(a, b)
    if type(a) ~= "table" or type(b) ~= "table" then return a == b end
    for k, v in pairs(a) do if not same(v, b[k]) then return false end end
    for k in pairs(b) do if a[k] == nil then return false end end
    return true
end

-- Classes, inheritance and virtual dispatch.
local Circle, Square = api.Circle, api.Square
local c1 = Circle(2)
check(c1:area() == 12 and c1:name() == "circle" and c1:twice_area() == 24 and c1:radius() == 2)
check(tostring(c1):find("^Circle: ") ~= nil)
fails(function() return Circle(-1) end, "negative radius")
fails(function() return api.Shape() end, "abstract")
local s = Square()
check(s.side == 1 and s:area() == 1)
s.side = 3
check(s:area() == 9 and Square(2):area() == 4)
fails(function() s.missing = 1 end, "Square has no field 'missing'")
fails(function() s.side = "wide" end, "field 'side'")
check(api.area_of(s) == 9 and api.describe(c1) == "circle:0")
-- Enums are integers; arguments also accept enumerator names.
check(c1:kind() == api.Kind.circle and api.Kind.custom == 7 and s:kind() == api.Kind.square)
check(api.filled("solid") and not api.filled(api.Fill.None))
fails(function() return api.filled("bogus") end, "unknown enumerator")
check(api.Shape.sides_unknown == -1 and math.abs(api.golden - 1.618) < 1e-12)

-- Most-derived results, ownership transfer and shared ownership.
local made = api.make_shape("circle", 1)
check(getmetatable(made).__name == "Circle" and made:radius() == 1)
local canvas = api.Canvas()
canvas:add(api.make_shape(api.Kind.square, 2))
canvas:share(api.shared_square(3))
local transferred = Circle(1)
canvas:add(transferred)
fails(function() return transferred:area() end, "closed")
check(canvas:size() == 3 and #canvas == 3 and canvas:total() == 4 + 9 + 3)
local first = canvas:at(0)
check(getmetatable(first).__name == "Square" and first:area() == 4)
local shared = api.shared_square(2)
canvas:share(shared)
check(shared:area() == 4 and canvas:total() == 20)
check(api.total_area({Circle(1), shared}) == 7)

-- Overload resolution by Lua type (integer vs float subtypes).
check(api.pick(1) == "int" and api.pick(1.5) == "double" and api.pick(2.0) == "double" and api.pick("x") == "string")
check(api.pick({1, 2}) == "vector" and api.pick(s) == "shape")
check(api.add(1) == 111 and api.add(1, 2) == 103 and api.add(1, 2, 3) == 6 and api.add(1, nil) == 111)
fails(function() return api.add() end, "incompatible arguments")
fails(function() return api.add(1, 2, 3, 4) end, "supported signatures")
fails(function() return api.add("x") end, "argument 'a'")

-- Containers.
check(same(api.count_words({"a", "b", "a"}), {a = 2, b = 1}))
local mm = api.min_max({3, 1, 2})
check(mm[1] == 1 and mm[2] == 3)
fails(function() return api.min_max({}) end, "empty input")
check(api.find({5, 7}, 7) == 1 and api.find({5, 7}, 9) == nil and api.find({5.0, 7.0}, 7) == 1)
local tr = api.transpose({{1, 2, 3}, {4, 5, 6}})
check(#tr == 3 and same(tr[1], {1, 4}))
check(api.byte_sum({1, 2, 250}) == 253 and same(api.bytes_of("AZ"), {65, 90}))
local cs = api.circles(3)
check(#cs == 3 and cs[3]:radius() == 3)
check(api.repeat_("z", 3) == "zzz" and api.initial("hello") == "h")
-- Mutable reference parameters update a ref() cell; plain tables are passed by value.
local values = api.ref({1, 2})
api.scale(values, 3)
check(same(values.value, {3, 6}))
local plain = {1, 2}
api.scale(plain, 3)
check(same(plain, {1, 2}))
fails(function() api.scale(api.ref("x"), 2) end, "argument 'values'")
fails(function() return api.repeat_("z", -1) end, "negative count")
fails(function() return api.repeat_("z", 2 ^ 40) end, "argument 'count'")

-- Callbacks both ways.
check(api.apply(function(x) return x * 2 end, 21) == 42)
fails(function() return api.apply(function() error("inside callback") end, 1) end, "inside callback")
fails(function() return api.apply(function() return "text" end, 1) end, "does not convert")
local plus = api.adder(40)
check(plus(2) == 42)
check(api.apply(api.adder(1), 3) == 4)

-- Value type operators, chaining and borrowed references.
local Vec2 = api.Vec2
local v = Vec2(1, 2)
local w = v + Vec2(3, 4)
check(w.x == 4 and w.y == 6 and (v * 2).y == 4 and (-v).x == -1 and (v - w).x == -3)
check(v == Vec2(1, 2) and not (v == w) and v ~= w)
check(v[1] == 2 and v:inner(Vec2(1, 1)) == 3)
fails(function() return v[2] end, "Vec2 index")
local chained = v:set_x(10):set_y(20)
check(rawequal(chained, v) and v.x == 10 and v.y == 20)
local seg = api.Segment()
seg.end_ = Vec2(3, 4)
check(seg:length2() == 25)
local start = seg:first()
start.x = 3
check(seg.start.x == 3 and seg:length2() == 16)
seg.start.y = 4
check(seg:length2() == 0)
check(Vec2(1, 2):clone().y == 2)
check(api.sum({Vec2(1, 2), Vec2(3, 4)}) == 10)
local held = api.Segment():first()
collectgarbage(); collectgarbage()
check(held.x == 0)

-- Templates, extensions, qualified names and copy types.
local stack = api.IntStack()
stack:push(1); stack:push(2)
check(stack:size() == 2 and same(stack:items(), {1, 2}) and stack:pop() == 2)
fails(function() stack:push(2.5) end, "argument 'value'")
check(api.clamp_int(42) == 10 and api.clamp_int(3, 5) == 3 and api.clamp_real(2.5) == 2.5)
check(c1:label() == "#circle" and c1:label("@") == "@circle" and Circle.unit():radius() == 1)
check(api.features__legacy__version() == 2 and api.features__legacy__Tag().id == 7)
check(api.features__legacy__Mode.off == 1)
check(api.features__legacy__Circle == Circle and api.features__legacy__Kind == api.Kind and Circle(2):diameter() == 4)
check(api.features__legacy__area_of(Square()) == 1)
local ledger = api.Ledger()
local copy = ledger:latest()
copy.value = 5
check(ledger.stamp.value == 1)
local aim = api.Aim()
aim.origin.x = 2
check(aim.target.point.x == 2)
fails(function() aim.target.point = Vec2() end, "read-only")

-- Errors, closing and garbage collection.
fails(function() return api.area_of(1) end, "argument 'shape': expected Shape, got integer")
fails(function() return c1:area(1) end, "incompatible arguments")
fails(function() return c1.area() end, "expected a Circle object as self")
do
    local x <close> = Circle(1)
    check(x:area() == 3)
    getmetatable(x).__close(x)
    fails(function() return x:area() end, "closed")
end
for i = 1, 200 do local tmp = api.make_shape("square", i); check(tmp:area() == i * i) end
collectgarbage(); collectgarbage()

-- Directors: Lua subclasses implement C++ virtual methods.
local Triangle = api.Shape:extend{}
function Triangle:area() return self.base * self.height / 2 end
function Triangle:name() return "triangle" end
local function triangle(base, height)
    local t = Triangle()
    t.base, t.height = base, height
    return t
end
local t = triangle(4, 3)
check(t:area() == 6 and api.area_of(t) == 6 and t:twice_area() == 12 and api.describe(t) == "triangle:7")
check(api.total_area({t, Circle(1), Square(2)}) == 13 and t:label("@") == "@triangle")
local Incomplete = api.Shape:extend{}
fails(function() return api.area_of(Incomplete()) end, "pure virtual method")
local Failing = api.Shape:extend{area = function() error("from Lua") end}
fails(function() return api.area_of(Failing()) end, "from Lua")
local Wrong = api.Shape:extend{area = function() return "not a number" end}
fails(function() return api.area_of(Wrong()) end, "does not convert")
-- Base.method(self) calls the native implementation rather than dispatching back to the override.
local Loud = Circle:extend{name = function(self) return Circle.name(self):upper() end}
check(api.describe(Loud(1)) == "CIRCLE:0" and Loud(2):area() == 12)
-- Subclasses of subclasses.
local Louder = Loud:extend{}
check(api.describe(Louder(1)) == "CIRCLE:0")
-- Ownership transfer keeps the Lua implementation alive; the same Lua object comes back.
local Unit = api.Shape:extend{area = function() return 1 end}
canvas = api.Canvas()
local unit = Unit()
unit.tag = "kept"
canvas:add(unit)
check(unit:area() == 1)
unit = nil
collectgarbage(); collectgarbage()
canvas:share(Square(2))
check(canvas:total() == 5 and canvas:size() == 2)
local back = canvas:at(0)
check(back.tag == "kept" and rawequal(back, canvas:at(0)))
canvas:clear(); back = nil
collectgarbage(); collectgarbage()
local shared_unit = Unit()
canvas:share(shared_unit)
shared_unit = nil
collectgarbage(); collectgarbage()
check(canvas:total() == 1)
canvas:clear()
collectgarbage(); collectgarbage()
-- Callback interfaces with a native default implementation.
local Summer = api.Visitor:extend{}
function Summer:visit(value) return value * 10 end
function Summer:finished(seen) self.seen = seen end
local summer = Summer()
check(api.run_visitor(summer, {1, 2}) == 30 and same(summer.seen, {1, 2}))
check(api.run_visitor(api.Visitor(), {1, 2}) == 3)
-- Coroutines: callbacks run on the calling thread.
local co = coroutine.wrap(function() return api.apply(function(x) return x + 1 end, 1) end)
check(co() == 2)

-- Policy modules: features::data functions live in a nested table, not in the module.
check(type(api.data) == "table", "data submodule")
local unit_vec = api.data.unit()
check(getmetatable(unit_vec).__name == "Vec2" and unit_vec.x == 1 and unit_vec.y == 0, "submodule result")
check(api.data.scaled(3) == 6 and api.data.scaled(2.5) == 6.25, "submodule overloads")
check(api.data.repeat_("ab") == "abab" and api.repeat_("x", 3) == "xxx", "submodule names are separate")
check(api.unit == nil and api.scaled == nil, "nothing leaks to the module")

-- The LuaCATS stub documents the module for editors.
local file = assert(io.open(meta_path))
local stub = file:read("a")
file:close()
local function has(line)
    for l in (stub .. "\n"):gmatch("([^\n]*)\n") do if l == line then return true end end
    return false
end
check(has("---@meta features"), "meta header")
check(has("---Adds three integers, Socha & Dorigo style.") or has("---Adds three integers, Socha & Dorigo style. The brief continues on a second line."), "add brief")
check(has("---@param a integer first term"), "param a")
check(has("---@param b? integer second term (default: `10`)"), "param b")
check(has("---@return integer # the sum"), "return")
check(has("function features.add(a, b, c) end"), "add declaration")
check(has("---@class features.Circle: features.Shape"), "class with base")
check(has("---@overload fun(radius: number): features.Circle"), "constructor overload")
check(has("---@field x number Horizontal component."), "field")
check(has("---@enum features.Kind"), "enum")
check(has("function features.Shape:extend(methods) end"), "extend")
check(has("function features.Circle.unit() end"), "static extension")
check(has("---@param values number[]"), "sequence type")
check(has("---@param shapes features.Shape[]"), "class sequence type")
check(has("---@param kind? features.Kind (default: `features.Kind.square`)"), "enum default")
check(has("---@param f fun(a1: number): number"), "callback type")
check(has("features.data = {}"), "submodule table")
check(has("function features.data.unit() end"), "submodule function")
check(has("return features"), "return")
-- The stub is valid Lua.
assert(load(stub, "=stub"))
print("Lua full API passed")
