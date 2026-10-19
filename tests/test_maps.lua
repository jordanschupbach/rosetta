if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], 'luaopen_demo')) end
local demo = require('demo')
local function rejects(fn, expected)
    local ok, message = pcall(fn)
    assert(not ok and tostring(message):find(expected, 1, true), tostring(message))
end
local binary = string.rep('map\0', 128)
local key = 'key\0' .. binary
local input = {[key]=binary, empty='', ['1']='numeric-looking string'}
local result = demo.demo__string_map(input)
assert(result ~= input and result[key] == binary and result.empty == '' and result['1'] == input['1'])
result[key] = 'changed'; assert(input[key] == binary)
assert(next(demo.demo__string_map({})) == nil)
rejects(function() demo.demo__string_map({[1]='value'}) end, 'string map keys')
rejects(function() demo.demo__string_map({[key]=false}) end, 'string expected')
rejects(function() demo.demo__string_map(nil) end, 'map table expected')
local invoked = false
local raw = setmetatable({key=binary}, {__pairs=function() invoked=true; error('pairs') end,
    __index=function() invoked=true; error('index') end, __len=function() invoked=true; error('len') end})
assert(demo.demo__string_map(raw).key == binary and not invoked)
local Op = demo.demo__Operation
local enums = demo.demo__unordered_enum_map({add=Op.add, multiply=Op.multiply})
assert(enums.add == Op.add and enums.multiply == Op.multiply)
rejects(function() demo.demo__unordered_enum_map({bad=3}) end, 'unknown enum value')

-- Optional mapped values use entry lists so a present empty value survives.
local optional = demo.demo__optional_map({{'z', binary}, {'a'}})
assert(#optional == 2 and optional[1][1] == 'a' and optional[1][2] == nil and optional[2][2] == binary)
rejects(function() demo.demo__optional_map({{'same'}, {'same', binary}}) end, 'duplicate native map key')
rejects(function() demo.demo__optional_map({named=binary}) end, 'dense sequence')
local complex = demo.demo__complex_map({{{nil, 'b'}, {Op.add}}, {{42, 'a'}, {Op.multiply, binary}}})
assert(#complex == 2 and complex[1][1][1] == nil and complex[1][1][2] == 'b' and complex[1][2][1] == Op.add)
assert(complex[2][1][1] == 42 and complex[2][2][2] == binary)
rejects(function() demo.demo__complex_map({{{42, 'same'}, {Op.add}}, {{42, 'same'}, {Op.multiply}}}) end, 'duplicate native map key')
rejects(function() demo.demo__complex_map({{{42, 'key'}, {3, binary}}}) end, 'unknown enum value')
rejects(function() demo.demo__complex_map({{{42, 'key'}, {Op.add}, 'extra'}}) end, 'product keys')
local numbers = demo.demo__number_map({{1.5, 'positive'}, {-2.25, 'negative'}})
assert(numbers[1][1] == -2.25 and numbers[2][1] == 1.5)
rejects(function() demo.demo__number_map({{0.0, 'positive zero'}, {-0.0, 'negative zero'}}) end, 'duplicate native map key')
rejects(function() demo.demo__number_map({{math.huge, 'infinite'}}) end, 'range')
rejects(function() demo.demo__number_map({{0/0, 'nan'}}) end, 'range')
local hashed = demo.demo__hashed_enum_map({{Op.multiply, 'multiply'}, {Op.add, binary}})
local found = {}; for _, entry in ipairs(hashed) do found[entry[1]] = entry[2] end
assert(found[Op.add] == binary and found[Op.multiply] == 'multiply')
rejects(function() demo.demo__hashed_enum_map({{Op.add, 'one'}, {Op.plus, 'alias'}}) end, 'duplicate native map key')
local hashed_optional = demo.demo__hashed_optional_map({{nil, 'empty'}, {0, 'zero'}})
assert(#hashed_optional == 2)
local empty, zero = false, false
for _, entry in ipairs(hashed_optional) do
    if entry[1] == nil then empty = entry[2] == 'empty' else zero = entry[1] == 0 and entry[2] == 'zero' end
end
assert(empty and zero)
rejects(function() demo.demo__hashed_optional_map({{nil, 'empty'}, {nil, 'duplicate'}}) end, 'duplicate native map key')
local bits = demo.demo__hashed_bits_map({{{true, false}, binary}, {{false, true}, 'second'}})
assert(#bits == 2)
for _, entry in ipairs(bits) do assert(entry[2] == (entry[1][1] and binary or 'second')) end
rejects(function() demo.demo__hashed_bits_map({{{true, false}, 'one'}, {{true, false}, 'duplicate'}}) end, 'duplicate native map key')

local callback = demo.demo__map_callback(function(value)
    assert(value[key] == binary); value[key] = 'callback'; return value
end, input)
assert(callback[key] == 'callback' and input[key] == binary)
rejects(function() demo.demo__map_callback(function() return {[key]=false} end, input) end, 'string expected')
rejects(function() demo.demo__map_callback(function() error('map callback failure') end, input) end, 'map callback failure')
rejects(function() demo.demo__map_failure(input) end, 'map failure')
rejects(function() demo.demo__map_oversized_result(input) end, 'Lua integer range')
do
    local borrowed = {first='first:' .. string.rep('v', 8192), second='second:' .. string.rep('s', 8192)}
    local copied = demo.demo__map_views_after(borrowed, function()
        borrowed.first = nil; borrowed.second = nil; collectgarbage('collect')
    end)
    assert(copied.first == 'first:' .. string.rep('v', 8192) and copied.second == 'second:' .. string.rep('s', 8192))
end
-- Mutating/re-hashing a table can make next() raise while native entries exist.
-- A call hook targets the second traversal step without depending on C symbols.
do
    local changing = {original=binary}
    local inside, traversal, calls = false, nil, 0
    debug.sethook(function()
        local info = debug.getinfo(2, 'fS')
        if info.func == demo.demo__string_map then inside = true
        elseif inside and info.what == 'C' then
            traversal = traversal or info.func
            if info.func == traversal then
                calls = calls + 1
                if calls == 2 then
                    debug.sethook()
                    changing.original = nil
                    for i=1,1024 do changing['new' .. i] = 'new' end
                end
            end
        end
    end, 'c')
    local ok, message = pcall(demo.demo__string_map, changing)
    debug.sethook()
    assert(calls == 2 and not ok and tostring(message):find("invalid key to 'next'", 1, true), tostring(message))
end
assert(demo.demo__string_map(input)[key] == binary)
print('Automatic ordered/unordered maps, dictionaries, entries, duplicates, callbacks and views passed')
