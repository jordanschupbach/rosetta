if arg and arg[1] then
    package.preload.interop_a=assert(package.loadlib(arg[1],'luaopen_interop_a'))
    package.preload.interop_b=assert(package.loadlib(arg[2],'luaopen_interop_b'))
end
local first = arg and arg[3]=='b' and 'b' or 'a'
local modules={}
modules[first]=require('interop_'..first)
local second=first=='a' and 'b' or 'a'
modules[second]=require('interop_'..second)
local A,B=modules.a,modules.b
local AC,BC=A.interop_a__Record,B.interop_b__Record
local function rejects(fn,text)
    local ok,err=pcall(fn)
    assert(not ok,'expected rejection')
    if text then assert(tostring(err):find(text,1,true),tostring(err)) end
end
local expired
do
    local a <close> = AC.new('shared\0text',20)
    local ticket=a:ticket()
    expired=ticket
    local b <close> = BC.from_ticket(ticket)
    assert(A.interop_a__live()==1 and B.interop_b__live()==1 and A.interop_a__tickets()==1)
    assert(b:ticket()==ticket and b:text()=='shared\0text' and b:value()==20)
    assert(b:shift(22)==42 and a:value()==42)
    b:set_text('changed\0text'); assert(a:text()=='changed\0text')
    local again <close> = AC.from_ticket(b:ticket())
    assert(again:ticket()==ticket and again:value()==42)
    local readonly <close> = a:readonly()
    assert(A.interop_a__take_value(readonly)==42)
    rejects(function() readonly:set_value(1) end,'readonly')
    rejects(function() readonly:ticket() end,'readonly')
    rejects(function() readonly:share() end,'readonly')
    rejects(function() B.interop_b__take_value(a) end,'expected class')
    rejects(function() A.interop_a__take_value(b) end,'expected class')
    rejects(function() BC.from_ticket(a) end,'string expected')
    local own <close> = b:share()
    a:close(); again:close()
    rejects(function() readonly:value() end,'expired')
    assert(own:value()==42 and own:ticket()==ticket)
    b:close()
    local from_other <close> = AC.from_ticket(ticket)
    own:close()
    assert(from_other:value()==42 and A.interop_a__live()==1)
    from_other:set_value(2147483647)
    rejects(function() from_other:shift(1) end,'overflow')
    assert(from_other:value()==2147483647)
end
collectgarbage('collect')
assert(A.interop_a__live()==0 and B.interop_b__tickets()==0)
rejects(function() AC.from_ticket(expired) end,'expired or unknown')
rejects(function() BC.from_ticket(expired) end,'expired or unknown')
for _,token in ipairs({'', 'rosetta.exchange.other.v1:1','rosetta.exchange.record.v2:1'}) do
    rejects(function() BC.from_ticket(token) end,'type/version mismatch')
end
for _,digits in ipairs({'','0','01','+1','-1',' 1','1x','1\0','18446744073709551616'}) do
    rejects(function() AC.from_ticket('rosetta.exchange.record.v1:'..digits) end,'malformed')
end
do
    local b <close> = BC.new('reverse',42)
    local a <close> = AC.from_ticket(b:ticket())
    assert(a:value()==42)
    assert(a:ticket()~=expired)
end
do
    local ticket
    do
        local orphan=AC.new('not retained by ticket',42)
        ticket=orphan:ticket()
        orphan=nil
    end
    collectgarbage('collect')
    rejects(function() BC.from_ticket(ticket) end,'expired or unknown')
end
-- Module tables can be collected while imported native ownership remains alive.
do
    local source=AC.new('module tables',42)
    local imported <close> = BC.from_ticket(source:ticket())
    source:close(); source=nil
    package.loaded.interop_a=nil
    A=nil; AC=nil; modules.a=nil
    collectgarbage('collect')
    assert(imported:value()==42)
    A=require('interop_a'); AC=A.interop_a__Record
    local restored <close> = AC.from_ticket(imported:ticket())
    assert(restored:value()==42)
end
collectgarbage('collect')
assert(A.interop_a__live()==0 and B.interop_b__tickets()==0)
print('Independent module interoperability passed ('..first..' first)')
