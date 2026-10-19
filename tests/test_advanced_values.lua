if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], "luaopen_demo")) end
local demo = require("demo")
local Record, Store, Kind = demo.demo__TaggedRecord, demo.demo__ValueStore, demo.demo__ValueKind
local function collect() collectgarbage("collect"); collectgarbage("collect") end
local function rejects(fn, expected)
    local ok, message = pcall(fn)
    assert(not ok and type(message) == "string" and #message > 0)
    if expected then assert(message:find(expected, 1, true), message) end
end
assert(Record.live() == 0 and Store.live() == 0)
local binary = string.rep("a\0b\255", 1000)
do
    local record <close> = Record.new()
    assert(record:kind() == Kind.integer and record:integer() == 0)
    assert(Record.packed_size() == 5 and record:counter() == 0)
    rejects(function() record:text() end, "does not contain text")
    record:set_priority(7); record:set_mask(31); record:set_counter(4294967295)
    assert(record:priority() == 7 and record:mask() == 31 and record:counter() == 4294967295)
    rejects(function() record:set_priority(8) end, "priority")
    rejects(function() record:set_priority(-1) end, "priority")
    rejects(function() record:set_mask(32) end, "mask")
    rejects(function() record:set_mask(-1) end, "mask")
    rejects(function() record:set_counter(-1) end, "range")
    rejects(function() record:set_counter(4294967296) end, "range")
    assert(record:priority() == 7 and record:mask() == 31)
    for i = 1, 20 do
        record:set_text(binary)
        assert(record:kind() == Kind.text and record:text() == binary)
        rejects(function() record:integer() end, "does not contain an integer")
        local copy <close> = record:clone()
        assert(copy:text() == binary and copy:priority() == 7 and copy:mask() == 31)
        assert(copy:counter() == 4294967295)
        record:set_text("replacement")
        assert(copy:text() == binary)
        record:set_integer(i)
        assert(record:kind() == Kind.integer and record:integer() == i)
    end
    record:close(); record:close()
    rejects(function() record:kind() end, "closed")
end
do
    local store <close> = Store.new()
    assert(#store:keys() == 0 and store:kind("missing") == Kind.missing)
    rejects(function() store:integer("missing") end, "key is missing")
    assert(not store:erase("missing"))
    store:put_text("b\0key", binary)
    store:put_integer("a", 42)
    store:put_text("", "empty key")
    local keys = store:keys()
    assert(#keys == 3 and keys[1] == "" and keys[2] == "a" and keys[3] == "b\0key")
    keys[1] = "changed" -- copied snapshot does not mutate the map
    assert(store:keys()[1] == "")
    assert(store:kind("a") == Kind.integer and store:integer("a") == 42)
    assert(store:kind("b\0key") == Kind.text and store:text("b\0key") == binary)
    rejects(function() store:text("a") end) -- std::bad_variant_access
    rejects(function() store:integer("b\0key") end)
    store:put_text("a", "new alternative")
    assert(store:kind("a") == Kind.text and store:text("a") == "new alternative")
    store:put_integer("a", 7)
    assert(store:integer("a") == 7)
    assert(store:erase("b\0key") and not store:erase("b\0key"))
    assert(store:scratch_kind() == Kind.missing)
    rejects(function() store:scratch_as_integer() end) -- std::bad_any_cast
    for _ = 1, 20 do
        store:scratch_integer(42)
        assert(store:scratch_kind() == Kind.integer and store:scratch_as_integer() == 42)
        rejects(function() store:scratch_as_text() end)
        store:scratch_text(binary)
        assert(store:scratch_kind() == Kind.text and store:scratch_as_text() == binary)
        rejects(function() store:pair_integer() end)
        store:scratch_pair(42, binary)
        assert(store:scratch_kind() == Kind.pair and store:pair_integer() == 42 and store:pair_text() == binary)
        rejects(function() store:scratch_as_integer() end)
        store:clear_scratch(); store:clear_scratch()
        assert(store:scratch_kind() == Kind.missing)
    end
    rejects(function() store:put_text("bad", 1) end, "string expected")
    assert(store:kind("bad") == Kind.missing)
    store:close(); store:close()
    rejects(function() store:keys() end, "closed")
end
-- Exercise GC while nontrivial union, variant and any alternatives are active.
do
    local record = Record.new(); record:set_text(binary)
    local store = Store.new(); store:put_text(binary, binary); store:scratch_pair(42, binary)
end
collect()
assert(Record.live() == 0 and Store.live() == 0)
print("Lua record and standard value adapters passed")
