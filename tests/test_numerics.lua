if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], "luaopen_demo")) end
local demo = require("demo")
local function rejects(fn, expected)
    local ok, message = pcall(fn)
    assert(not ok and type(message) == "string" and #message > 0)
    if expected then assert(message:find(expected, 1, true), message) end
end
local u64max = "18446744073709551615"
assert(demo.demo__u64_decimal(u64max) == u64max)
assert(demo.demo__u64_decimal("00042") == "42")
assert(demo.demo__u64_add("18446744073709551614", "1") == u64max)
rejects(function() demo.demo__u64_add(u64max, "1") end, "overflow")
rejects(function() demo.demo__u64_decimal("18446744073709551616") end, "range")
for _, bad in ipairs({"", "-1", "+1", " 1", "1 ", "1\0", "1.0"}) do
    rejects(function() demo.demo__u64_decimal(bad) end, "decimal")
end
if demo.demo__int128_available() then
    local imax = "170141183460469231731687303715884105727"
    local imin = "-170141183460469231731687303715884105728"
    local umax = "340282366920938463463374607431768211455"
    assert(demo.demo__signed128_decimal(imax) == imax and demo.demo__signed128_decimal(imin) == imin)
    assert(demo.demo__unsigned128_decimal(umax) == umax)
    assert(demo.demo__signed128_decimal("-00042") == "-42" and demo.demo__signed128_decimal("-0") == "0")
    assert(demo.demo__unsigned128_add(u64max, "1") == "18446744073709551616")
    assert(demo.demo__signed128_add(imin, imax) == "-1")
    assert(demo.demo__signed128_multiply(imin, "1") == imin)
    assert(demo.demo__signed128_multiply(imin, "0") == "0")
    assert(demo.demo__unsigned128_multiply(u64max, u64max) == "340282366920938463426481119284349108225")
    rejects(function() demo.demo__signed128_add(imax, "1") end, "overflow")
    rejects(function() demo.demo__signed128_add(imin, "-1") end, "overflow")
    rejects(function() demo.demo__signed128_multiply(imin, "-1") end, "overflow")
    rejects(function() demo.demo__unsigned128_add(umax, "1") end, "overflow")
    rejects(function() demo.demo__unsigned128_multiply("18446744073709551616", "18446744073709551616") end, "overflow")
    rejects(function() demo.demo__signed128_decimal("170141183460469231731687303715884105728") end, "range")
    rejects(function() demo.demo__unsigned128_decimal(umax .. "0") end, "range")
    rejects(function() demo.demo__signed128_decimal(42) end, "string expected")
    rejects(function() demo.demo__unsigned128_decimal("-1") end, "decimal")

    -- Independent decimal arithmetic in Lua, using small digit cells rather
    -- than Lua numbers large enough to lose the native operand's precision.
    local function normalized(text) return (text:gsub("^0+", ""):gsub("^$", "0")) end
    local function add(left, right)
        local result, carry = {}, 0
        for index = 0, math.max(#left, #right) - 1 do
            local a, b = left:byte(#left - index) or 48, right:byte(#right - index) or 48
            local sum = a + b - 96 + carry
            result[#result + 1], carry = tostring(sum % 10), sum // 10
        end
        if carry ~= 0 then result[#result + 1] = tostring(carry) end
        return normalized(table.concat(result):reverse())
    end
    local function multiply(left, right)
        local cells = {}
        for a = 1, #left do
            for b = 1, #right do
                local index = (#left - a) + (#right - b) + 1
                cells[index] = (cells[index] or 0) + (left:byte(a) - 48) * (right:byte(b) - 48)
            end
        end
        local result, carry = {}, 0
        for i = 1, #left + #right do
            local value = (cells[i] or 0) + carry
            result[i], carry = tostring(value % 10), value // 10
        end
        return normalized(table.concat(result):reverse())
    end
    local state = 42
    local function digits()
        local result = {}
        for i = 1, 19 do state = (state * 48271) % 2147483647; result[i] = tostring(state % 10) end
        return table.concat(result)
    end
    for _ = 1, 256 do
        local left, right = digits(), digits()
        local sum, product = add(left, right), multiply(left, right)
        assert(demo.demo__unsigned128_add(left, right) == sum)
        assert(demo.demo__signed128_add(left, right) == sum)
        assert(demo.demo__unsigned128_multiply(left, right) == product)
        assert(demo.demo__signed128_multiply("-" .. left, right) == (product == "0" and "0" or "-" .. product))
    end
else
    rejects(function() demo.demo__signed128_decimal("42") end, "unavailable in this build")
end

local bounds = demo.demo__long_double_bounds()
assert(#bounds == 4)
for _, value in ipairs(bounds) do assert(demo.demo__long_double_decimal(value) == value) end
for _, value in ipairs({"0", "-0", "0.1", "-0.1", "42", "1e10", "1e-10"}) do
    local canonical = demo.demo__long_double_decimal(value)
    assert(demo.demo__long_double_decimal(canonical) == canonical)
    assert(demo.demo__long_double_compare(canonical, value) == 0)
end
assert(demo.demo__long_double_signbit("-0") and not demo.demo__long_double_signbit("0"))
assert(demo.demo__long_double_decimal("-0"):sub(1, 1) == "-")
assert(demo.demo__long_double_compare(demo.demo__long_double_add("40", "2"), "42") == 0)
assert(demo.demo__long_double_compare(demo.demo__long_double_multiply("0.5", "84"), "42") == 0)
assert(demo.demo__long_double_compare(demo.demo__long_double_multiply(bounds[4], "1"), bounds[4]) == 0)
if demo.demo__long_double_precision() > 53 then
    local value = demo.demo__long_double_add("9007199254740992", "1")
    assert(demo.demo__long_double_compare(value, "9007199254740993") == 0)
    assert(demo.demo__long_double_compare(value, "9007199254740992") == 1)
end
for _, bad in ipairs({"", "+1", " 1", "1 ", "1\0", "0x1p0", "nan", "inf", "-inf", "1e999999", "1e-999999"}) do
    rejects(function() demo.demo__long_double_decimal(bad) end)
end
rejects(function() demo.demo__long_double_multiply(bounds[2], "2") end, "overflow")
rejects(function() demo.demo__long_double_add(bounds[2], bounds[2]) end, "overflow")
rejects(function() demo.demo__long_double_add(bounds[1], bounds[1]) end, "overflow")
assert(demo.demo__long_double_compare(demo.demo__long_double_add(bounds[1], bounds[2]), "0") == 0)
assert(demo.demo__long_double_compare(demo.demo__long_double_add("40", "2"), "42") == 0)
collectgarbage("collect"); collectgarbage("collect")
print("Lua extended numeric adapters passed")
