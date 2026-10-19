if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], 'luaopen_demo')) end
local demo = require('demo')
local function rejects(fn, expected)
    local ok, message = pcall(fn)
    assert(not ok and tostring(message):find(expected, 1, true), tostring(message))
end
local function frequencies(values)
    local counts = {}
    for _, value in ipairs(values) do counts[value] = (counts[value] or 0) + 1 end
    return counts
end
local words = {'z\0binary', 'a', 'z\0binary'}
local deque = demo.demo__deque_values(words)
assert(table.concat(deque, '|') == table.concat(words, '|'))
deque[1] = 'copied'; assert(words[1] == 'z\0binary')
local operations = demo.demo__list_operations({2, 1, 2})
assert(table.concat(operations, ',') == '2,1,2')
rejects(function() demo.demo__list_operations({1, 3}) end, 'unknown enum value')
local forward = demo.demo__forward_values({{index=1}, {index=3, value='binary\0'}, {index=5}})
assert(#forward == 3 and forward[1].index == 1 and forward[2].value == 'binary\0' and forward[3].index == 5)
assert(table.concat(demo.demo__ordered_set({'z\0', 'a'}), '|') == 'a|z\0')
rejects(function() demo.demo__ordered_set(words) end, 'duplicate native set element')
local hashed = frequencies(demo.demo__hashed_set({2,1}))
assert(hashed[1] == 1 and hashed[2] == 1)
rejects(function() demo.demo__hashed_set({1,1}) end, 'duplicate native set element')
assert(table.concat(demo.demo__multi_set(words), '|') == 'a|z\0binary|z\0binary')
local multi = frequencies(demo.demo__hashed_multi_set(words))
assert(multi.a == 1 and multi['z\0binary'] == 2)
local optional = demo.demo__optional_set({{'z'}, {}, {'a\0'}})
assert(#optional == 3 and next(optional[1]) == nil and optional[2][1] == 'a\0' and optional[3][1] == 'z')
rejects(function() demo.demo__optional_set({{}, {}}) end, 'duplicate native set element')
rejects(function() demo.demo__optional_set({'unboxed'}) end, 'product table expected')
local optional_hash = demo.demo__optional_hash_set({{}, {42}, {20}})
local empty, numbers = 0, {}
for _, box in ipairs(optional_hash) do
    if box[1] == nil then empty = empty + 1 else numbers[box[1]] = true end
end
assert(empty == 1 and numbers[42] and numbers[20])
local monostates = demo.demo__monostate_multi_set({{}, {}, {}})
assert(#monostates == 3 and next(monostates[1]) == nil)
local nested = demo.demo__nested_set({{2}, {1,2}, {1}})
assert(#nested == 3 and #nested[1] == 1 and nested[1][1] == 1 and nested[2][2] == 2 and nested[3][1] == 2)
rejects(function() demo.demo__nested_set({{1}, {1}}) end, 'duplicate native set element')
local entries = demo.demo__multi_map({{'key','first'}, {'other'}, {'key','second'}})
assert(#entries == 3 and entries[1][1] == 'key' and entries[1][2] == 'first' and entries[2][2] == 'second' and entries[3][2] == nil)
local unordered = demo.demo__hashed_multi_map({{'key','first'}, {'key','second'}, {'key','first'}})
local counts = {}
for _, entry in ipairs(unordered) do
    assert(entry[1] == 'key'); counts[entry[2]] = (counts[entry[2]] or 0) + 1
end
assert(counts.first == 2 and counts.second == 1)
rejects(function() demo.demo__hashed_multi_map({key='not an entry list'}) end, 'dense sequence')
assert(table.concat(demo.demo__sequence_callback(function(value)
    value[1], value[2] = value[2], value[1]; return value
end, {'first', 'second'}), ',') == 'second,first')
assert(table.concat(demo.demo__set_callback(function(value)
    assert(value[1] == 'a'); value[3] = 'c'; return value
end, {'b', 'a'}), ',') == 'a,b,c')
rejects(function() demo.demo__set_callback(function() return {'a','a'} end, {'a'}) end, 'duplicate native set element')
rejects(function() demo.demo__sequence_callback(function() error('container callback') end, words) end, 'container callback')
rejects(function() demo.demo__sequence_failure(words) end, 'sequence failure')
rejects(function() demo.demo__sequence_oversized_result(words) end, 'out of Lua integer range')
assert(demo.demo__sequence_view()[1] == string.rep('q',128))
do
    local source = {'z' .. string.rep('view',2000), 'a' .. string.rep('view',2000)}
    local copied = demo.demo__set_views_after(source, function()
        source[1] = nil; source[2] = nil; collectgarbage('collect')
    end)
    assert(copied[1] == 'a' .. string.rep('view',2000) and copied[2] == 'z' .. string.rep('view',2000))
end
local invoked = false
local raw = setmetatable({'b', 'a'}, {__len=function() invoked=true; error('len') end,
    __pairs=function() invoked=true; error('pairs') end, __index=function() invoked=true; error('index') end})
assert(demo.demo__ordered_set(raw)[1] == 'a' and not invoked)
for _, name in ipairs({'deque_values','list_operations','forward_values','ordered_set','hashed_set','multi_set',
    'hashed_multi_set','optional_set','optional_hash_set','monostate_multi_set','nested_set','multi_map','hashed_multi_map'}) do
    local call = demo['demo__' .. name]
    assert(next(call({})) == nil)
    assert(not pcall(call, false))
    assert(not pcall(call, {[0]='bad key'}))
    assert(not pcall(call, {metadata=true}))
end
assert(demo.demo__deque_values({'recovery'})[1] == 'recovery')
print('Deque/list/forward-list, sets, nil boxes, multimaps, callbacks and views passed')
