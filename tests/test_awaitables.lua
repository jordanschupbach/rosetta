if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], 'luaopen_demo')) end
local demo = require('demo')
local E,T,P = demo.demo__AwaitExecutor,demo.demo__AwaitTask,demo.demo__AwaitPhase
local directory = debug.getinfo(1,'S').source:sub(2):match('^(.*[/\\])') or './'
local Awaitables = dofile(directory..'../examples/demo_awaitables.lua')(demo)
local function rejects(fn, text)
    local ok,err = pcall(fn)
    assert(not ok,'expected failure')
    if text then assert(tostring(err):find(text,1,true),tostring(err)) end
end
do
    local e <close> = E.new()
    local a <close> = e:submit(40,2,'result\0',false,nil)
    local b <close> = e:submit(20,1,'callback:',false,function(v) return v*2 end)
    assert(e:pending()==2 and E.frames()==2 and E.bodies()==0)
    assert(a:state()==P.pending)
    rejects(function() a:result() end,'not ready')
    assert(e:run_one() and e:run_one() and E.bodies()==2)
    local count, pending = Awaitables.drain(e,10)
    assert(count==3 and pending==0 and E.frames()==0 and E.bodies()==0)
    assert(a:state()==P.succeeded and a:result()=='result\00042')
    assert(b:result()=='callback:40' and b:result()=='callback:40')
    assert(not a:cancel() and not e:run_one())
    local shared <close> = a:share()
    a:close(); assert(shared:result()=='result\00042')
end
for _,started in ipairs({false,true}) do
    local e <close> = E.new()
    local t <close> = e:submit(40,2,'',false,nil)
    if started then assert(e:run_one() and E.bodies()==1) end
    assert(t:cancel() and not t:cancel() and t:state()==P.cancelled)
    assert(E.frames()==0 and E.bodies()==0 and e:pending()==0 and not e:run_one())
    rejects(function() t:result() end,'cancelled')
end
do
    local e <close> = E.new()
    local native_error <close> = e:submit(40,1,'',true,nil)
    local overflow <close> = e:submit(2147483647,1,'',false,nil)
    local callback_error <close> = e:submit(1,1,'',false,function() error('await callback marker') end)
    local bad_return <close> = e:submit(1,1,'',false,function() return {} end)
    local yielding <close> = e:submit(1,1,'',false,function() coroutine.yield() end)
    Awaitables.drain(e,20)
    for _,task in ipairs({native_error,overflow,callback_error,bad_return,yielding}) do assert(task:state()==P.failed) end
    rejects(function() native_error:result() end,'native awaitable failure')
    rejects(function() overflow:result() end,'integer overflow')
    rejects(function() callback_error:result() end,'await callback marker')
    rejects(function() callback_error:result() end,'await callback marker')
    rejects(function() bad_return:result() end)
    rejects(function() yielding:result() end)
    assert(E.frames()==0 and E.bodies()==0)
end
do
    local e <close> = E.new()
    local t
    t = e:submit(40,1,'',false,function(v)
        rejects(function() e:run_one() end,'dispatch is already active')
        rejects(function() e:close() end)
        rejects(function() t:cancel() end,'running awaitable')
        return v+2
    end)
    assert(Awaitables.result(e,t,2)=='42')
    t:close()
    local closing
    closing=e:submit(1,2,'',false,function(v) closing:close(); collectgarbage('collect'); return v end)
    assert(e:run_one() and e:run_one())
    assert(T.live()==0 and E.frames()==0 and E.bodies()==0 and not e:run_one())
end
do
    local e <close> = E.new()
    local share <close> = e:share()
    local t <close> = e:submit(40,2,'',false,nil)
    e:close()
    assert(Awaitables.result(share,t,3)=='42')
end
do
    local e <close> = E.new()
    local weak=setmetatable({},{__mode='v'})
    local t
    do
        local captured={2}; weak[1]=captured
        t=e:submit(40,1,'',false,function(v) return v+captured[1] end)
    end
    collectgarbage('collect'); assert(weak[1])
    assert(Awaitables.result(e,t,2)=='42')
    collectgarbage('collect'); assert(weak[1]==nil) -- completion releases callbacks before task close
    t:close()
    local cancelled
    do
        local captured={2}; weak[1]=captured
        cancelled=e:submit(40,1,'',false,function(v) return v+captured[1] end)
    end
    cancelled:cancel(); collectgarbage('collect'); assert(weak[1]==nil)
    cancelled:close()
end
do
    local e <close> = E.new()
    rejects(function() e:submit(0,-1,'',false,nil) end,'nonnegative')
    local t <close> = e:submit(42,0,'',false,nil)
    rejects(function() Awaitables.result(e,t,0) end,'not ready')
    assert(Awaitables.result(e,t,1)=='42')
end
collectgarbage('collect')
assert(E.live()==0 and T.live()==0 and E.frames()==0 and E.bodies()==0)
assert(E.allocations()==E.releases())
print('Native awaitable/executor tests passed')
