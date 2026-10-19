if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], 'luaopen_demo')) end
local demo = require('demo')
local Worklist, Order = demo.demo__Worklist, demo.demo__WorkOrder
local expectations = {
    [Order.stack] = {'a','b','c','a'}, [Order.queue] = {'a','c','b','a'},
    [Order.highest] = {'c','b','a','a'}, [Order.lowest] = {'a','a','b','c'}
}
for order, expected in pairs(expectations) do
    local values <close> = Worklist.new(order)
    assert(values:order() == order and values:size() == 0 and values:peek() == nil and values:pop() == nil)
    for _, text in ipairs({'a','c','b','a'}) do values:push(text) end
    assert(values:size() == 4 and values:peek() == expected[1])
    assert(table.concat(values:snapshot(), '|') == table.concat(expected, '|'))
    local snapshot = values:snapshot(); snapshot[1] = 'copied'
    assert(values:peek() == expected[1])
    local clone <close> = values:clone()
    assert(Worklist.live() == 2 and clone:order() == order)
    assert(clone:pop() == expected[1] and clone:size() == 3 and values:size() == 4)
    local shared <close> = values:share()
    values:close()
    for _, text in ipairs(expected) do assert(shared:pop() == text) end
    assert(shared:size() == 0 and shared:pop() == nil)
    shared:push(''); assert(shared:peek() == '' and shared:pop() == '')
    shared:push('binary\0\255'); assert(shared:pop() == 'binary\0\255')
    assert(not pcall(function() shared:push(42) end))
    assert(shared:size() == 0)
    assert(not pcall(function() values:peek() end))
end
assert(Worklist.live() == 0)
assert(not pcall(Worklist.new, 0) and not pcall(Worklist.new, 5) and not pcall(Worklist.new, true))
do local discarded = Worklist.new(Order.highest); discarded:push(string.rep('gc',1000)) end
collectgarbage('collect')
assert(Worklist.live() == 0)
print('Native stack/queue/priority-queue order, copies, sharing, binary values and cleanup passed')
