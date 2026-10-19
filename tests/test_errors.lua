if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], "luaopen_demo")) end
local demo = require("demo")
local Result, Kind = demo.demo__IntResult, demo.demo__ErrorKind
local function collect() collectgarbage("collect"); collectgarbage("collect") end
local function rejects(fn, expected)
    local ok, message = pcall(fn)
    assert(not ok and type(message) == "string" and #message > 0)
    if expected then assert(message:find(expected, 1, true), message) end
end
assert(Result.live() == 0 and Result.new == nil)
do
    local result <close> = demo.demo__parse_result("42")
    assert(result:ok() and result:value() == 42 and result:value_or(-1) == 42)
    assert(result:error_kind() == Kind.none and result:code() == 0)
    assert(result:category() == "" and result:message() == "" and #result:causes() == 0)
    result:close(); result:close()
    rejects(function() result:ok() end, "closed")
end
for _, text in ipairs({"", "42\0junk", " 42", "+42", "2147483648"}) do
    local result <close> = demo.demo__parse_result(text)
    assert(not result:ok() and result:error_kind() == Kind.invalid_input)
    assert(result:category() == "demo.parse" and result:code() == (text == "2147483648" and 12 or 11))
    assert(result:value_or(42) == 42 and #result:causes() == 1)
    rejects(function() result:value() end, result:message())
    rejects(function() result:value() end, result:message()) -- stored typed exception is repeatable
end
for _, operation in ipairs({demo.demo__divide_result, demo.demo__status_divide_result}) do
    local success <close> = operation(84, 2)
    assert(success:ok() and success:value() == 42)
    local negative <close> = operation(-85, 2)
    assert(negative:value() == -42) -- C++ integer division truncates toward zero
    local zero <close> = operation(1, 0)
    assert(not zero:ok() and zero:error_kind() == Kind.arithmetic and zero:code() == 1)
    assert(zero:category() == "demo.divide" and zero:message() == "division by zero")
    rejects(function() zero:value() end, "division by zero")
    local overflow <close> = operation(-2147483648, -1)
    assert(not overflow:ok() and overflow:code() == 2)
    rejects(function() overflow:value() end, "division overflow")
end
do
    local thrown <close> = demo.demo__system_result(true)
    local code <close> = demo.demo__error_code_result(true)
    assert(thrown:error_kind() == Kind.system and code:error_kind() == Kind.system)
    assert(thrown:code() ~= 0 and thrown:code() == code:code())
    assert(thrown:category() == "generic" and code:category() == thrown:category())
    rejects(function() thrown:value() end)
    rejects(function() code:value() end)
    local ok <close> = demo.demo__system_result(false)
    local ok_code <close> = demo.demo__error_code_result(false)
    assert(ok:value() == 42 and ok_code:value() == 42)
end
do
    local nested <close> = demo.demo__nested_result()
    local causes = nested:causes()
    assert(nested:error_kind() == Kind.nested and nested:message() == "request failed")
    assert(#causes == 2 and causes[1] == "request failed" and causes[2] == "expected a complete decimal integer")
    causes[1] = "edited snapshot"
    assert(nested:causes()[1] == "request failed")
    rejects(function() nested:value() end, "request failed")
    local unknown <close> = demo.demo__unknown_result()
    assert(unknown:error_kind() == Kind.unknown and unknown:message() == "unknown C++ exception")
    rejects(function() unknown:value() end, "unknown C++ exception")
    local allocation <close> = demo.demo__allocation_result()
    assert(allocation:error_kind() == Kind.allocation and allocation:code() == -2)
    rejects(function() allocation:value() end)
end
do
    local binary = string.rep("detail\0\255", 1000)
    local status <close> = demo.demo__status_result(true, binary)
    assert(status:error_kind() == Kind.invalid_input and status:code() == 20)
    assert(status:message() == binary and status:category() == "demo.status" and #status:causes() == 0)
    local ok <close> = demo.demo__status_result(false, binary)
    assert(ok:ok() and ok:value() == 42)
end
do
    local marker = {}
    local weak = setmetatable({marker}, {__mode = "v"})
    local result <close> = demo.demo__callback_result((function(capture)
        return function() assert(capture); error("structured callback failure") end
    end)(marker), 42)
    marker = nil
    collect()
    assert(weak[1] == nil) -- the stored exception does not retain the callback
    assert(not result:ok() and result:error_kind() == Kind.callback)
    assert(result:message():find("structured callback failure", 1, true))
    rejects(function() result:value() end, "structured callback failure")
    local success <close> = demo.demo__callback_result(function(value) return value + 1 end, 41)
    assert(success:value() == 42)
    local invalid <close> = demo.demo__callback_result(function() return "42" end, 0)
    assert(invalid:error_kind() == Kind.callback)
    rejects(function() invalid:value() end, "number expected")
end
rejects(function() demo.demo__parse_result(42) end, "string expected")
rejects(function() demo.demo__callback_result(42, 0) end, "function expected")
do local abandoned = demo.demo__nested_result(); assert(not abandoned:ok()) end
collect()
assert(Result.live() == 0)
print("Lua structured error adapters passed")
