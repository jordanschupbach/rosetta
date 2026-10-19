if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], 'luaopen_demo')) end
local demo = require('demo')
local function rejects(fn, text)
    local ok, err = pcall(fn)
    assert(not ok and tostring(err):find(text, 1, true), tostring(err))
end
assert(demo.demo__native_pch_shift(40) == 42)
assert(demo.demo__native_pch_shift(-2147483648) == -2147483646)
rejects(function() demo.demo__native_pch_shift(2147483647) end, 'PCH shift overflow')
if demo.demo__native_modules_available() then
    assert(demo.demo__native_module_shift(40) == 42)
    assert(demo.demo__native_module_shift(-2147483648) == -2147483646)
    assert(demo.demo__native_header_unit_answer() == 42)
    assert(demo.demo__native_module_text('binary\0payload', false) == 'binary\0payloadbinary\0payload')
    assert(demo.demo__native_module_text('', false) == '')
    rejects(function() demo.demo__native_module_shift(2147483647) end, 'module shift overflow')
    rejects(function() demo.demo__native_module_text(string.rep('owned', 1000), true) end, 'module text failure')
    assert(demo.demo__native_module_shift(40) == 42 and demo.demo__native_module_live() == 0)
else
    rejects(function() demo.demo__native_module_shift(40) end, 'native module example disabled')
    rejects(function() demo.demo__native_module_text('', false) end, 'native module example disabled')
    rejects(function() demo.demo__native_header_unit_answer() end, 'native module example disabled')
end
collectgarbage('collect')
assert(demo.demo__native_module_live() == 0)
print('Native modules/header-unit/PCH facade tests passed')
