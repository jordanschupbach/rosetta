local startup = assert(arg[2], "expected the REPL startup script")
local previous_path = package.cpath
dofile(startup)

assert(type(demo) == "table")
assert(demo.demo__add(20, 22) == 42)
assert(require("demo") == demo)
assert(Calls.choose(41) == 42 and Calls.scale(21) == 42 and Calls.sum({20, 22}) == 42)
assert(demo.demo__time_convert(42, 's', 'ms') == 42000)
assert(demo.demo__product_identity({'tuple', 42})[2] == 42)
assert(demo.demo__feature_identity(demo.demo__Feature.classes | demo.demo__Feature.callbacks) == 5)
assert(demo.demo__operation_vector({demo.demo__Operation.add})[1] == demo.demo__Operation.add)
assert(demo.demo__string_map({answer='42'}).answer == '42')
assert(demo.demo__variant_identity({index=3, value='text'}).value == 'text')
assert(demo.demo__duplicate_variant({index=2, value=42}).index == 2)
assert(table.concat(demo.demo__ordered_set({'z', 'a'}), ',') == 'a,z')
assert(#demo.demo__multi_map({{'key','first'}, {'key','second'}}) == 2)
assert(demo.demo_names__imported_count() == 42)
do
    local object <close> = demo.demo__MemberObject.new(20, 21, 'example', true)
    local method <close> = demo.demo__MemberSelector.new(demo.demo__MemberChoice.measure)
    assert(method:invoke_read(object, 0) == 42)
end
do
    local state <close> = demo.demo__ConcurrentState.new(0, 42)
    state:parallel_add(2, 21, 1); assert(state:load() == 42)
end
assert(demo.demo_names__using_vector({demo.demo__Operation.add})[1] == demo.demo__Operation.add)
do
    local tasks <close> = demo.demo__Worklist.new(demo.demo__WorkOrder.queue)
    tasks:push('first'); tasks:push('second'); assert(tasks:pop() == 'first')
end
do
    local files <close> = demo.demo__FileWorkspace.new()
    files:write('message', 'hello')
    assert(files:read('message') == 'hello')
end
do
    local owner <close> = demo.demo__NestedOwner.new(41)
    local child <close> = owner:item()
    assert(child:shifted() == 42)
end
do
    local point <close> = demo.demo__TimePoint.new(42, true)
    assert(point:ticks() == 42 and point:steady())
end
do
    local generator <close> = demo.demo__Generator.new(42, 1, "", -1)
    local next_value = Generators.values(generator)
    assert(next_value() == "42" and next_value() == nil)
end
assert(package.cpath:sub(-#previous_path) == previous_path)
do
    local series <close> = Series({20, 21})
    local result <close> = series * 2
    assert(result[2] == 42)
    local signed <close> = Series({-7, 7})
    local quotient <close> = signed / 3
    local floor <close> = signed // 3
    local modulo <close> = signed % 3
    assert(quotient[1] == -2 and floor[1] == -3 and modulo[1] == 2)
end

-- Confirm regular require can find the shared library without a cached module
-- or package.preload entry, just as it would in a fresh interactive session.
package.loaded.demo = nil
assert(package.preload.demo == nil)
assert(require("demo").demo__greet("Lua") == "Hello, Lua!")
print("Lua REPL startup passed")
do
    local native <close> = demo.demo__NativeBoundary.new(demo.demo__BoundaryOperation.multiply, 3, '')
    assert(native:apply(14) == 42 and demo.demo__native_abi_info().flags == 3)
end
assert(demo.demo__native_pch_shift(40) == 42)
if demo.demo__native_modules_available() then
    assert(demo.demo__native_module_shift(40) == 42 and demo.demo__native_header_unit_answer() == 42)
end
assert(demo.demo__native_internal_add(20,22) == 42)
assert(demo.demo__native_matrix_shift({{1,2,3},{4,5,6}},40)[1][2] == 42)
do
    local callable <close> = Callables.capture('answer:',40)
    assert(callable(2) == 'answer:42')
end
do
    local input <close> = demo.demo__CallbackRecord.new('record',40)
    local result <close> = ClassCallbacks.transform(input,function(value) value:set_number(value:number()+2); return value end)
    assert(result:number()==42 and input:number()==40)
end
do
    local executor <close> = demo.demo__AwaitExecutor.new()
    local task <close> = executor:submit(40,2,'answer:',false,nil)
    assert(Awaitables.result(executor,task,3)=='answer:42')
end
do
    local a <close> = Interop.new_a('shared',40)
    local b <close> = Interop.to_b(a)
    assert(b:shift(2)==42 and a:value()==42)
end
