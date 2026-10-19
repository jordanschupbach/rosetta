if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], 'luaopen_demo')) end
local demo = require('demo')
local Record, Frame = demo.demo__CallbackRecord, demo.demo__ClassCallbackFrame
local directory = debug.getinfo(1,'S').source:sub(2):match('^(.*[/\\])') or './'
local Callbacks = dofile(directory..'../examples/demo_class_callbacks.lua')(demo)
local function rejects(fn, text)
    local ok, err = pcall(fn)
    assert(not ok, 'expected failure')
    if text then assert(tostring(err):find(text,1,true), tostring(err)) end
end
do
    local input <close> = Record.new('input\0text',20)
    local frame <close> = Frame.new()
    assert(frame:depth()==0)
    rejects(function() frame:argument() end,'frame is inactive')
    rejects(function() frame:set_result(input) end,'frame is inactive')
    rejects(function() frame:transform(input,nil) end,'callback is required')
    rejects(function() frame:transform(input,function() end) end,'did not set a result')
    local kept
    local result <close> = frame:transform(input,function()
        assert(frame:depth()==1)
        rejects(function() frame:close() end)
        rejects(function() input:close() end)
        local argument <close> = frame:argument()
        assert(argument:text()=='input\0text' and argument:number()==20)
        argument:set_number(42); argument:set_text('changed\0text')
        frame:set_result(input)
        frame:set_result(argument)
        kept = argument:copy()
        argument:set_number(7) -- result has already copied the selected value
        argument:set_text('later')
        local nested <close> = frame:transform(argument,function()
            assert(frame:depth()==2)
            local inner <close> = frame:argument()
            assert(inner:number()==7)
            inner:set_number(99); frame:set_result(inner)
        end)
        assert(frame:depth()==1 and nested:number()==99)
        local original <close> = frame:argument()
        assert(original:number()==20) -- outer input is unaffected by nested calls
    end)
    assert(result:number()==42 and result:text()=='changed\0text')
    assert(input:number()==20 and input:text()=='input\0text' and frame:depth()==0)
    assert(kept:number()==42); kept:close()
    rejects(function() frame:argument() end,'frame is inactive')
    rejects(function() frame:transform(input,function() frame:set_result(input); error('after result') end) end,'after result')
    assert(frame:depth()==0 and input:number()==20)
    local recovered <close> = frame:transform(input,function() frame:set_result(input) end)
    assert(recovered:number()==20)
end
do
    local input <close> = Record.new('protocol',20)
    local kept, ephemeral
    local result <close> = Callbacks.transform(input,function(argument)
        ephemeral=argument
        kept=argument:copy()
        argument:set_number(42)
        return argument
    end)
    rejects(function() ephemeral:number() end)
    assert(kept:number()==20 and result:number()==42 and input:number()==20)
    input:close()
    assert(result:text()=='protocol' and kept:text()=='protocol')
    kept:close()
end
do
    local input <close> = Record.new('original',20)
    local external <close> = Record.new('external',42)
    local readonly <close> = external:readonly()
    local result <close> = Callbacks.transform(input,function() return readonly end)
    assert(external:number()==42 and result:text()=='external')
    external:set_number(7)
    assert(result:number()==42) -- result ownership is independent
    rejects(function() Callbacks.transform(input,function() return {} end) end)
    rejects(function() Callbacks.transform(input,function() return nil end) end)
    rejects(function() Callbacks.transform(input,function(argument) argument:close(); return argument end) end)
    rejects(function() Callbacks.transform(input,function() error('record callback marker') end) end,'record callback marker')
    rejects(function() Callbacks.transform(input,function() coroutine.yield() end) end)
end
do
    local input <close> = Record.new('nested',40)
    local frame <close> = Frame.new()
    local result <close> = Callbacks.with_frame(frame,input,function(outer)
        local nested <close> = Callbacks.with_frame(frame,outer,function(inner)
            inner:set_number(inner:number()+2); return inner
        end)
        assert(nested:number()==42 and outer:number()==40)
        return nested:copy() -- return an open independent handle
    end)
    assert(result:number()==42 and frame:depth()==0)
end
collectgarbage('collect')
assert(Record.live()==0 and Frame.live()==0)
print('Class-value callback adapter tests passed')
