-- CTest supplies the exact module path, including the platform suffix.
if arg[1] then
    package.preload.demo = assert(package.loadlib(arg[1], "luaopen_demo"))
end
if arg[2] then package.preload.interop_a=assert(package.loadlib(arg[2],"luaopen_interop_a")) end
if arg[3] then package.preload.interop_b=assert(package.loadlib(arg[3],"luaopen_interop_b")) end
local demo = require("demo")
demo.demo__reset()
assert(demo.demo__add(20, 22) == 42)
assert(demo.demo__count() == 1)
assert(demo.demo__scale(2.5, 4) == 10)
assert(demo.demo__scale(21) == 42)
assert(demo.demo__text_then_integer() == string.rep('d', 128) .. '42')
assert(demo.demo__text_then_integer('answer: ') == 'answer: 42')
local sequence = demo.demo__next_default()
assert(demo.demo__default_text('item: ') == 'item: ' .. (sequence + 1))
assert(demo.demo__default_text('item: ') == 'item: ' .. (sequence + 2))
assert(demo.demo__default_text('fixed: ', 42) == 'fixed: 42')
assert(demo.demo__next_default() == sequence + 3) -- explicit argument skipped evaluation
assert(demo.demo__default_view() == string.rep('v', 128))
assert(demo.demo__default_cstring() == string.rep('p', 128))
assert(demo.demo__default_failure('safe: ', 42) == 'safe: 42')
assert(demo.demo__negate(true) == false)
assert(demo.demo__greet("Lua") == "Hello, Lua!")
assert(demo.demo__nothing() == nil)
assert(demo.demo__byte(255) == 255)

-- Scoped/unscoped enums and enum aliases are bound automatically.
local Operation = demo.demo__Operation
assert(Operation.add == Operation.plus)
assert(demo.demo__apply(Operation.add, 20, 22) == 42)
assert(demo.demo__apply(Operation.multiply, 6, 7) == 42)
assert(demo.demo__operation_identity(Operation.add) == Operation.add)
assert(demo.demo__operation_identity() == Operation.add)
assert(demo.demo__feature_identity(demo.demo__Feature.templates) == 2)
local features = demo.demo__combine_features(demo.demo__Feature.classes, demo.demo__Feature.templates)
assert(features == 3 and demo.demo__has_features(features, demo.demo__Feature.templates))
assert(demo.demo__Boundary.minimum == math.mininteger)
assert(demo.demo__Boundary.maximum == math.maxinteger)
assert(demo.demo__Boundary.next == -6)
assert(demo.demo__boundary_identity(demo.demo__Boundary.minimum) == math.mininteger)
assert(demo.demo__boundary_identity(demo.demo__Boundary.maximum) == math.maxinteger)

-- C++ facades select finite template instantiations for Lua.
assert(demo.demo__template_max_int(20, 42) == 42)
assert(demo.demo__template_max_double(4.25, 2.5) == 4.25)
assert(demo.demo__template_box_int(42) == 42)
assert(demo.demo__template_sum_three(10, 20, 12) == 42)

-- Values are copied across the boundary; views borrow only during the call.
local binary = "hello\0Lua\255"
assert(demo.demo__echo(binary) == binary)
assert(demo.demo__append(binary, "!") == binary .. "!")
assert(demo.demo__view_identity(binary) == binary)
assert(demo.demo__echo("") == "")
assert(demo.demo__text_then_integer(binary, 42) == binary .. "42")
assert(demo.demo__optional_text(nil) == nil)
assert(demo.demo__optional_text() == string.rep('o', 128))
assert(demo.demo__optional_text(binary) == binary)
local input = {1, 2, 42}
local reversed = demo.demo__reverse_numbers(input)
assert(reversed[1] == 42 and reversed[2] == 2 and reversed[3] == 1)
assert(input[1] == 1) -- native reversal did not mutate the Lua input
local words = demo.demo__reverse_words({binary, "two", "three"})
assert(words[1] == "three" and words[3] == binary)
local matrix = demo.demo__matrix_identity({{1, 2}, {}, {42}})
assert(matrix[1][2] == 2 and #matrix[2] == 0 and matrix[3][1] == 42)
assert(demo.demo__fixed_three({1, 2, 42})[3] == 42)
local bits = demo.demo__bits_identity({true, false, true})
assert(bits[1] and not bits[2] and bits[3])

local function rejects(fn, message)
    local ok, err = pcall(fn)
    assert(not ok and err:find(message, 1, true), tostring(err))
end
rejects(function() demo.demo__fail() end, "demo failure")
rejects(function() demo.demo__default_failure() end, "default expression failed")
local before_rejection = demo.demo__next_default()
rejects(function() demo.demo__default_text(false) end, "string expected")
assert(demo.demo__next_default() == before_rejection + 1)
rejects(function() demo.demo__text_then_integer(nil) end, "string expected")
rejects(function() demo.demo__scale() end, "expected 1 to 2 arguments")
rejects(function() demo.demo__scale(1, 2, 3) end, "expected 1 to 2 arguments")
rejects(function() demo.demo__add(1) end, "expected 2 arguments")
rejects(function() demo.demo__add(1, 2, 3) end, "expected 2 arguments")
rejects(function() demo.demo__add("1", 2) end, "number expected")
rejects(function() demo.demo__add(1.5, 2) end, "integer")
rejects(function() demo.demo__add(2147483648, 2) end, "range")
rejects(function() demo.demo__byte(-1) end, "range")
rejects(function() demo.demo__byte(256) end, "range")
rejects(function() demo.demo__negate(1) end, "boolean expected")
rejects(function() demo.demo__greet(123) end, "string expected")
rejects(function() demo.demo__greet("a\0b") end, "embedded NUL")
rejects(function() demo.demo__scale(math.huge, 1) end, "range")
rejects(function() demo.demo__too_large() end, "Lua integer range")
rejects(function() demo.demo__apply(99, 1, 2) end, "unknown enum value")
rejects(function() demo.demo__apply(256, 1, 2) end, "range")
rejects(function() demo.demo__apply(-1, 1, 2) end, "range")
rejects(function() demo.demo__apply("add", 1, 2) end, "number expected")
rejects(function() demo.demo__feature_identity(8) end, "unknown enum flag bits")
rejects(function() demo.demo__echo(123) end, "string expected")
rejects(function() demo.demo__text_then_integer(binary, "bad") end, "number expected")
rejects(function() demo.demo__text_failure(binary) end, "text failure")
rejects(function() demo.demo__reverse_numbers({1, "bad"}) end, "number expected")
rejects(function() demo.demo__reverse_numbers({1, 2, extra = 3}) end, "dense sequence")
rejects(function() demo.demo__reverse_numbers({[1]=1, [3]=3}) end, "expected")
local trap = setmetatable({}, {
    __index = function() error("metamethod called") end,
    __len = function() error("metamethod called") end,
    __pairs = function() error("metamethod called") end,
})
assert(#demo.demo__reverse_numbers(trap) == 0)
rejects(function() demo.demo__fixed_three({1, 2}) end, "wrong fixed array length")
rejects(function() demo.demo__fixed_three({1, 2, 3, 4}) end, "wrong fixed array length")
rejects(function() demo.demo__optional_text(12) end, "string expected")
-- The state remains usable after translated exceptions and conversion errors.
assert(demo.demo__add(1, 2) == 3)

-- Selected classes are native objects with explicit ownership and Lua 5.4 close.
do
    local counter <close> = demo.demo__Counter.new(20, "example")
    counter:add(22)
    assert(counter:value() == 42)
    counter:set_label("renamed")
    assert(counter:get_label() == "renamed")
    local child <close> = counter:point()
    child:set_x(20); child:set_y(22)
    assert(child:sum() == 42)
    local view <close> = counter:readonly() -- borrowed, anchored to counter
    assert(view:value() == 42)
    rejects(function() view:add(1) end, "readonly")
    local copy <close> = counter:clone()
    copy:add(1)
    assert(copy:value() == 43 and counter:value() == 42)
    local point <close> = demo.demo__Point.new()
    point:set_x(20); point:set_y(22)
    assert(point:sum() == 42 and point:get_version() == 1)
    local unique <close> = demo.demo__make_counter(42)
    assert(unique:value() == 42)
    local shared <close> = demo.demo__make_shared_counter(42)
    local another <close> = demo.demo__share_counter(shared)
    shared:close()
    assert(another:value() == 42) -- shared ownership keeps the native object alive
    local moved <close> = demo.demo__make_move_only("move-only object")
    assert(moved:text() == "move-only object")
end
assert(demo.demo__Counter.live() == 0 and demo.demo__MoveOnly.live() == 0)

-- Synchronous closures and explicitly retained event callbacks share the same
-- protected bridge; Lua errors become C++ exceptions before reaching Lua again.
assert(demo.demo__invoke_callback(function(v) return v + 2 end, 40) == 42)
assert(demo.demo__c_style_callback(function(v) return v + 2 end, 40) == 42)
assert(demo.demo__transform_text("a\0b", function(s) return s .. "!" end) == "a\0b!")
do
    local events <close> = demo.demo__EventSource.new()
    local increment = 2
    events:set_callback(function(v) return v + increment end)
    assert(events:emit(40) == 42)
    events:clear_callback()
end
assert(demo.demo__EventSource.live() == 0)

-- Native multiple/virtual inheritance stays behind reusable C++ facades.
do
    local root <close> = demo.demo__make_diamond("diamond", 42)
    local named <close> = root:as_named()
    local value <close> = named:as_value() -- checked crosscast
    local base <close> = value:as_entity() -- virtual base conversion
    assert(base:same_entity(root) and named:name() == "diamond")
    root:close()
    assert(value:value() == 42) -- independent shared view owns the native object
    local scripted <close> = demo.demo__make_scripted("Lua override", 40, function(v) return v + 2 end)
    local virtual <close> = scripted:as_value()
    assert(virtual:value() == 42) -- dispatches through C++ Valued::value
end
assert(demo.demo__EntityView.live() == 0)

-- Lua protocols are a supplied layer over target-independent native adapters.
local directory = debug.getinfo(1, "S").source:sub(2):match("^(.*[/\\])") or "./"
local Series = dofile(directory .. "demo_protocols.lua")(demo)
do
    local a <close> = Series({20, 21})
    local b <close> = Series({22, 21})
    local result <close> = a + b
    assert(#result == 2 and result[1] == 42 and tostring(result) == "[42, 42]")
    local scaled <close> = a(2)
    assert(scaled[2] == 42)
    local count = 0
    for index, value in pairs(result) do
        count = count + 1
        assert(index == count and value == 42)
    end
    assert(count == 2)
    local signed <close> = Series({-7, 7})
    local quotient <close> = signed / 3
    local remainder <close> = signed:remainder(3)
    local floor <close> = signed // 3
    local modulo <close> = signed % 3
    assert(quotient[1] == -2 and remainder[1] == -1) -- C++ truncates toward zero
    assert(floor[1] == -3 and modulo[1] == 2) -- Lua floor convention
end
assert(demo.demo__IntSeries.live() == 0 and demo.demo__SeriesCursor.live() == 0)
do
    local task <close> = demo.demo__AsyncTask.new(21, 0, false)
    local notified = false
    task:on_complete(function(state, result)
        assert(state == demo.demo__TaskState.succeeded and result == 42)
        notified = true
    end)
    assert(task:wait(2000) and task:result() == 42)
    assert(not notified) -- waiting never runs host callbacks
    assert(task:dispatch() and notified) -- explicit delivery on the Lua thread
end
assert(demo.demo__AsyncTask.live() == 0 and demo.demo__AsyncTask.workers() == 0)
do
    local record <close> = demo.demo__TaggedRecord.new()
    record:set_text("a\0b"); record:set_priority(7)
    assert(record:kind() == demo.demo__ValueKind.text and record:text() == "a\0b")
    assert(record:priority() == 7 and demo.demo__TaggedRecord.packed_size() == 5)
    record:set_integer(42) -- destroys the union's active std::string member
    local values <close> = demo.demo__ValueStore.new()
    values:put_integer("answer", record:integer())
    values:scratch_pair(values:integer("answer"), "tuple inside any")
    assert(values:pair_integer() == 42 and values:pair_text() == "tuple inside any")
end
assert(demo.demo__TaggedRecord.live() == 0 and demo.demo__ValueStore.live() == 0)
assert(demo.demo__constexpr_answer() == 42 and demo.demo__immediate_answer() == 42)
assert(demo.demo__template_pattern(40)[3] == 42)
assert(demo.demo__constrained_sum({10, 20, 12}) == 42)
assert(demo.demo__literal_answer() == 42 and demo.demo__compare_ints(7, 2) == 1)
do
    local buffer <close> = demo.demo__ByteBuffer.new("a\0b")
    local view <close> = buffer:slice(1, 2, false)
    view:write(0, "42")
    assert(buffer:read() == "a42")
    buffer:append("!")
    rejects(function() view:read() end, "invalidated")
    assert(demo.demo__decode_u16("\42\0") == 42)
    assert(demo.demo__out_buffer(3, 255) == "\255\255\255")
end
assert(demo.demo__ByteBuffer.live() == 0 and demo.demo__ByteSlice.live() == 0)
local Calls = dofile(directory .. "demo_calls.lua")(demo)
assert(Calls.choose(41) == 42 and Calls.scale(21) == 42 and Calls.sum({10, 20, 12}) == 42)
rejects(function() Calls.ambiguous(42) end, "ambiguous integer overload")
do
    local object <close> = demo.demo__make_call_text("20")
    assert(object:adjust_mutable(22) == 42 and object:take() == 42)
end
assert(demo.demo__CallObject.live() == 0)
do
    local parsed <close> = demo.demo__parse_result("42")
    assert(parsed:ok() and parsed:value() == 42)
    local error <close> = demo.demo__divide_result(1, 0)
    assert(not error:ok() and error:error_kind() == demo.demo__ErrorKind.arithmetic)
    assert(error:message() == "division by zero" and error:value_or(42) == 42)
    rejects(function() error:value() end, "division by zero")
    local nested <close> = demo.demo__nested_result()
    assert(#nested:causes() == 2)
end
assert(demo.demo__IntResult.live() == 0)
do
    local source <close> = demo.demo__make_resource("custom allocation", 42, false)
    local target <close> = demo.demo__empty_resource()
    source:move_to(target)
    assert(not source:has_value() and target:aligned())
    local shared <close> = demo.demo__SharedResource.new()
    target:promote_to(shared)
    local weak <close> = shared:weak()
    local locked <close> = weak:lock()
    shared:close()
    assert(locked:deleter_tag() == 42)
    locked:close()
    assert(weak:expired())
end
assert(demo.demo__ResourceSlot.live() == 0)
local Generators = dofile(directory .. "demo_generators.lua")()
do
    local source <close> = demo.demo__Generator.new(40, 3, "", -1)
    local last
    for value in Generators.values(source) do last = value end
    assert(last == "42" and source:done())
end
assert(demo.demo__Generator.frames() == 0 and demo.demo__Generator.bodies() == 0)
local unicode = "A\0" .. utf8.char(0x20ac, 0x1f600)
assert(demo.demo__unicode_scalar_count(unicode) == 4)
assert(demo.demo__unicode_from_utf16(demo.demo__unicode_utf16(unicode)) == unicode)
assert(demo.demo__unicode_char8(unicode) == unicode and demo.demo__unicode_wide(unicode) == unicode)
assert(demo.demo__u64_decimal("18446744073709551615") == "18446744073709551615")
if demo.demo__int128_available() then
    assert(demo.demo__unsigned128_add("18446744073709551615", "1") == "18446744073709551616")
end
assert(demo.demo__long_double_compare(demo.demo__long_double_add("40", "2"), "42") == 0)
assert(demo.demo__time_convert(-1500, 'ms', 's', 'nearest') == -2)
do
    local epoch <close> = demo.demo__TimePoint.new(0)
    local later <close> = epoch:shifted(42)
    assert(later:difference(epoch) == 42 and epoch:before(later))
end
assert(demo.demo__TimePoint.live() == 0)
local product = demo.demo__product_identity({'tuple\0text', 42})
assert(product[1] == 'tuple\0text' and product[2] == 42 and product[3] == nil)
local pair = demo.demo__pair_identity({'constant native key', true})
assert(pair[1] == 'constant native key' and pair[2] == true)
assert(next(demo.demo__empty_product({})) == nil)
local enum_values = demo.demo__operation_vector({demo.demo__Operation.add, demo.demo__Operation.multiply})
assert(enum_values[2] == demo.demo__Operation.multiply)
assert(demo.demo__enum_callback(function(_) return demo.demo__Operation.multiply end, demo.demo__Operation.add) == demo.demo__Operation.multiply)
do
    local owner <close> = demo.demo__NestedOwner.new(41)
    local child <close> = owner:item()
    assert(child:shifted() == 42)
    owner:set_mode(demo.demo__NestedOwner__Mode.active)
    assert(owner:mode() == demo.demo__NestedOwner__Mode.active)
end
assert(demo.demo__NestedOwner.live() == 0 and demo.demo__NestedOwner__Item.live() == 0)
local map = demo.demo__string_map({answer='42', ['binary\0key']='value\0'})
assert(map.answer == '42' and map['binary\0key'] == 'value\0')
local entries = demo.demo__optional_map({{'empty'}, {'present', '42'}})
assert(entries[1][1] == 'empty' and entries[1][2] == nil and entries[2][2] == '42')
local sum = demo.demo__variant_callback(function(value)
    assert(value.index == 3 and value.value == 'variant')
    return {index=2, value=42}
end, {index=3, value='variant'})
assert(sum.index == 2 and sum.value == 42)
assert(demo.demo__variant_identity({index=1}).index == 1) -- monostate
assert(demo.demo__variant_identity({index=5}).index == 5) -- empty optional
do
    local files <close> = demo.demo__FileWorkspace.new()
    files:directories('example')
    files:write('example/message', 'hello\0filesystem')
    assert(files:read('example/message') == 'hello\0filesystem')
    assert(files:entries('', true)[2] == 'example/message')
end
assert(demo.demo__FileWorkspace.live() == 0 and demo.demo__FileWorkspace.cleanup_errors() == 0)
assert(table.concat(demo.demo__ordered_set({'z', 'a'}), ',') == 'a,z')
assert(table.concat(demo.demo__multi_set({'b', 'a', 'a'}), ',') == 'a,a,b')
assert(demo.demo__optional_set({{'text'}, {}})[1][1] == nil)
assert(#demo.demo__multi_map({{'key','first'}, {'key','second'}}) == 2)
assert(demo.demo_names__imported_count() == 42)
assert(demo.demo_names__imported_operation() == demo.demo__Operation.add)
do
    local tasks <close> = demo.demo__Worklist.new(demo.demo__WorkOrder.highest)
    tasks:push('a'); tasks:push('z'); assert(tasks:pop() == 'z')
end
assert(demo.demo_names__using_vector({demo.demo__Operation.add})[1] == demo.demo__Operation.add)
do
    local state <close> = demo.demo__ConcurrentState.new(0, 42)
    state:parallel_add(4, 100, 1)
    assert(state:load() == 400 and state:register_load() == 42)
end
do
    local object <close> = demo.demo__MemberObject.new(20, 21, 'example', true)
    local method <close> = demo.demo__MemberSelector.new(demo.demo__MemberChoice.measure)
    assert(method:invoke_read(object, 0) == 42)
end
do
    local boundary <close> = demo.demo__NativeBoundary.new(demo.demo__BoundaryOperation.add, 20, 'native\0payload')
    assert(boundary:with_callback(function(v) return v + 1 end, 21) == 42)
    assert(boundary:payload() == 'native\0payload')
    assert(demo.demo__native_abi_info().flags == 3)
end
assert(demo.demo__native_pch_shift(40) == 42)
if demo.demo__native_modules_available() then
    assert(demo.demo__native_module_shift(40) == 42)
    assert(demo.demo__native_header_unit_answer() == 42)
    assert(demo.demo__native_module_text('module', false) == 'modulemodule')
end
do
    local stationary <close> = demo.demo__NativeStationary.new('stable', false)
    local shared <close> = stationary:share()
    assert(stationary:same(shared) and stationary:text() == 'stable')
end
assert(demo.demo__native_internal_add(20,22) == 42)
assert(demo.demo__native_matrix_shift({{1,2,3},{4,5,6}},40)[1][2] == 42)
do
    local callable <close> = demo.demo__make_native_capture('answer:',40)
    local bound <close> = demo.demo__make_native_bound('',40)
    assert(callable:invoke(2) == 'answer:42' and bound:invoke(2) == '42')
end
do
    local input <close> = demo.demo__CallbackRecord.new('record',40)
    local frame <close> = demo.demo__ClassCallbackFrame.new()
    local result <close> = frame:transform(input,function()
        local argument <close> = frame:argument()
        argument:set_number(argument:number()+2)
        frame:set_result(argument)
    end)
    assert(result:number()==42 and input:number()==40)
end
do
    local executor <close> = demo.demo__AwaitExecutor.new()
    local task <close> = executor:submit(40,2,'answer:',false,nil)
    assert(executor:run_one() and executor:run_one() and executor:run_one())
    assert(task:result()=='answer:42' and not executor:run_one())
end
do
    local a <close> = require('interop_a').interop_a__Record.new('shared',40)
    local b <close> = require('interop_b').interop_b__Record.from_ticket(a:ticket())
    assert(b:shift(2)==42 and a:value()==42)
end
print("Lua integration passed")
