if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], 'luaopen_demo')) end
local demo = require('demo')
local C = demo.demo__NativeCallable
local function rejects(fn, text)
    local ok, message = pcall(fn)
    assert(not ok, 'expected failure')
    if text then assert(tostring(message):find(text, 1, true), tostring(message)) end
end
assert(C.new == nil)
do
    local f <close> = demo.demo__make_native_capture('capture\0',20)
    assert(f:invoke(1) == 'capture\x0021')
    local copy <close> = f:copy()
    local shared <close> = f:share()
    assert(not f:same_slot(copy) and f:same_slot(shared))
    assert(f:invoke(1) == 'capture\x0022')
    assert(copy:invoke(1) == 'capture\x0022')
    assert(shared:invoke(1) == 'capture\x0023')
    assert(C.captures() == 2)
    f:close()
    rejects(function() f:invoke(1) end)
    assert(shared:invoke(0) == 'capture\x0023')
    shared:clear()
    assert(shared:empty() and C.captures() == 1)
    local cleared <close> = shared:copy()
    assert(cleared:empty())
    rejects(function() shared:invoke(1) end)
    assert(copy:invoke(0) == 'capture\x0022')
end
do
    local bound <close> = demo.demo__make_native_bound('member:',20)
    local copy <close> = bound:copy()
    assert(C.targets() == 1 and not bound:same_slot(copy))
    assert(bound:invoke(1) == 'member:21' and copy:invoke(1) == 'member:22')
    bound:clear()
    assert(bound:empty() and C.targets() == 1 and copy:invoke(0) == 'member:22')
end
do
    local pointer <close> = demo.demo__make_native_pointer()
    local empty <close> = demo.demo__make_native_empty()
    local nil_callback <close> = demo.demo__make_native_callback(nil)
    assert(pointer:invoke(-42) == '-42' and empty:empty() and nil_callback:empty())
    rejects(function() empty:invoke(1) end)
    rejects(function() nil_callback:invoke(1) end)
end
for _, factory in ipairs({demo.demo__make_native_capture, demo.demo__make_native_bound}) do
    local f <close> = factory('limit:',2147483647)
    rejects(function() f:invoke(1) end, 'native callable overflow')
    assert(f:invoke(-1) == 'limit:2147483646')
end
do
    local f
    f = demo.demo__make_native_callback(function(v)
        rejects(function() f:close() end)
        if v > 0 then return f:invoke(v-1) end
        return 'recursive'
    end)
    assert(f:invoke(2) == 'recursive')
    f:clear()
    assert(f:empty())
    f:close()
end
do
    local f
    f = demo.demo__make_native_callback(function(v) f:clear(); collectgarbage('collect'); return tostring(v) end)
    assert(f:invoke(42) == '42' and f:empty())
    f:close()
end
for _, callback in ipairs({function() error('callable callback marker') end, function() return {} end, function() coroutine.yield() end}) do
    local f <close> = demo.demo__make_native_callback(callback)
    rejects(function() f:invoke(1) end)
end
do
    local f <close> = demo.demo__make_native_callback(function(v) return 'lua:'..v end)
    local copy <close> = f:copy()
    f:clear()
    collectgarbage('collect')
    assert(copy:invoke(42) == 'lua:42')
end
do
    local weak = setmetatable({}, {__mode='v'})
    local f, copy
    do
        local captured = {'retained'}
        weak[1] = captured
        f = demo.demo__make_native_callback(function(v) return captured[1]..v end)
        copy = f:copy()
    end
    f:close()
    collectgarbage('collect')
    assert(weak[1] and copy:invoke(42) == 'retained42')
    copy:close()
    collectgarbage('collect')
    assert(weak[1] == nil)
end
local directory = debug.getinfo(1,'S').source:sub(2):match('^(.*[/\\])') or './'
local Callables = dofile(directory..'../examples/demo_callables.lua')(demo)
do
    local f <close> = Callables.capture('protocol:',40)
    local copy <close> = f:copy()
    assert(f(2) == 'protocol:42' and copy(1) == 'protocol:41')
    local pointer <close> = Callables.pointer()
    local bound <close> = Callables.bound('',40)
    local callback <close> = Callables.from_lua(function(v) return tostring(v) end)
    local empty <close> = Callables.empty()
    assert(pointer(42)=='42' and bound(2)=='42' and callback(42)=='42' and empty:empty())
end
collectgarbage('collect')
assert(C.live()==0 and C.captures()==0 and C.targets()==0)
print('Native returned callable tests passed')
