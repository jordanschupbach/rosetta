if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], "luaopen_demo")) end
local demo = require("demo")
local directory = debug.getinfo(1, "S").source:sub(2):match("^(.*[/\\])") or "./"
local Series = dofile(directory .. "../examples/demo_protocols.lua")(demo)
local Native, Cursor = demo.demo__IntSeries, demo.demo__SeriesCursor
local function collect() collectgarbage("collect"); collectgarbage("collect") end
local function rejects(fn, expected)
    local ok, message = pcall(fn)
    assert(not ok and tostring(message):find(expected, 1, true), tostring(message))
end
assert(Native.live() == 0 and Cursor.live() == 0 and Cursor.new == nil)

do
    local a <close> = Series({20, 21})
    local b <close> = Series.new({22, 21})
    local sum <close> = a + b
    local diff <close> = b - a
    local neg <close> = -a
    local scaled <close> = a * 2
    local reverse <close> = 2 * a
    local called <close> = a(2)
    assert(#a == 2 and a[1] == 20 and a[2] == 21)
    assert(tostring(sum) == "[42, 42]" and tostring(diff) == "[2, 0]")
    assert(tostring(neg) == "[-20, -21]" and tostring(scaled) == "[40, 42]")
    assert(reverse == scaled and scaled == called and not rawequal(scaled, called))
    assert(a < b and a <= b and b > a and b >= a and a ~= b)
    local same <close> = a:clone()
    assert(same == a and same <= a and not (same < a))
    same[1] = 0
    assert(same[1] == 0 and a[1] == 20)
    assert(a:nonempty() and getmetatable(a) == "Rosetta series protocol")
    rejects(function() a.extra = 1 end, "read-only")
    rejects(function() return a[0] end, "index out of range")
    rejects(function() return a[-2147483648] end, "index out of range")
    rejects(function() return a[3] end, "index out of range")
    rejects(function() return a[1.5] end, "integer")
    rejects(function() a[1] = "42" end, "number expected")
    rejects(function() return a * "2" end, "number expected")
    rejects(function() return a + {} end, "expected series")
    assert(a ~= {})
    local total, count = 0, 0
    for index, value in pairs(a) do
        count = count + 1
        assert(index == count)
        total = total + value
    end
    assert(total == 41 and count == 2 and Cursor.live() == 0)
    local iterator = pairs(a)
    assert(iterator() == 1)
    a[1] = 5
    rejects(function() iterator() end, "mutated during iteration")
    assert(iterator() == nil and Cursor.live() == 0)
    a:close(); a:close()
    rejects(function() return #a end, "closed")
    rejects(function() return a[1] end, "closed")
    rejects(function() return a + b end, "closed")
end
collect()
assert(Native.live() == 0 and Cursor.live() == 0)

do
    local empty <close> = Series({})
    assert(#empty == 0 and tostring(empty) == "[]" and not empty:nonempty())
    local count = 0
    for _ in pairs(empty) do count = count + 1 end
    assert(count == 0 and Cursor.live() == 0)
    -- Lua cannot overload truthiness: even an empty C++ series is truthy here.
    assert(empty)
    local short <close> = Series({1})
    rejects(function() return empty + short end, "lengths differ")
    rejects(function() return short - empty end, "lengths differ")
end

do
    local high <close> = Series({2147483647})
    local one <close> = Series({1})
    local low <close> = Series({-2147483648})
    rejects(function() return high + one end, "arithmetic overflow")
    rejects(function() return low - one end, "arithmetic overflow")
    rejects(function() return -low end, "arithmetic overflow")
    rejects(function() return high * 2 end, "arithmetic overflow")
    rejects(function() return high(2) end, "arithmetic overflow")
    assert(high[1] == 2147483647 and low[1] == -2147483648)
    rejects(function() return high << 1 end, "arithmetic overflow")
    rejects(function() return high << -1 end, "shift count out of range")
    rejects(function() return high >> 31 end, "shift count out of range")
    rejects(function() return low >> 1 end, "nonnegative")
end
do
    local a <close> = Series({6, 10})
    local b <close> = Series({3, 12})
    local band <close> = a & b
    local bor <close> = a | b
    local bxor <close> = a ~ b
    local bnot <close> = ~a
    local left <close> = a << 2
    local right <close> = a >> 1
    assert(tostring(band) == "[2, 8]" and tostring(bor) == "[7, 14]")
    assert(tostring(bxor) == "[5, 6]" and tostring(bnot) == "[-7, -11]")
    assert(tostring(left) == "[24, 40]" and tostring(right) == "[3, 5]")
end
collect()
assert(Native.live() == 0 and Cursor.live() == 0)

-- C++ truncation and Lua floor division differ for negative operands.
do
    local input <close> = Series({-7, 7, -6, 6, 0})
    local quotient <close> = input / 3
    local remainder <close> = input:remainder(3)
    local floor <close> = input // 3
    local modulo <close> = input % 3
    assert(tostring(quotient) == '[-2, 2, -2, 2, 0]')
    assert(tostring(remainder) == '[-1, 1, 0, 0, 0]')
    assert(tostring(floor) == '[-3, 2, -2, 2, 0]')
    assert(tostring(modulo) == '[2, 1, 0, 0, 0]')
    local negative_floor <close> = input:floor_quotient(-3)
    local negative_modulo <close> = input:floor_modulo(-3)
    assert(tostring(negative_floor) == '[2, -3, 2, -2, 0]')
    assert(tostring(negative_modulo) == '[-1, -2, 0, 0, 0]')
    for _, operation in ipairs({
        function(d) return input / d end,
        function(d) return input // d end,
        function(d) return input % d end,
        function(d) return input:remainder(d) end,
    }) do
        rejects(function() operation(0) end, 'division by zero')
        rejects(function() operation(1.5) end, 'integer')
        rejects(function() operation('3') end, 'number expected')
        rejects(function() operation(2147483648) end, 'range')
    end
    rejects(function() return 3 / input end, 'expected series')
    rejects(function() return 3 // input end, 'expected series')
    rejects(function() return 3 % input end, 'expected series')
    local empty <close> = Series({})
    local result <close> = empty / -1
    assert(#result == 0)
    rejects(function() return empty / 0 end, 'division by zero')
    -- Failure after earlier elements were processed leaves storage and cursors intact.
    local mixed <close> = Native.new({7, -2147483648, 9})
    local cursor <close> = mixed:cursor()
    assert(cursor:next() == 7)
    for _, method in ipairs({'quotient', 'remainder', 'floor_quotient', 'floor_modulo'}) do
        rejects(function() mixed[method](mixed, -1) end, 'division overflow')
        assert(mixed:at(1) == 7 and mixed:at(2) == -2147483648 and mixed:at(3) == 9)
    end
    assert(cursor:next() == -2147483648 and cursor:next() == 9 and cursor:next() == nil)
    input:close()
    rejects(function() return input / 3 end, 'closed')
    rejects(function() return input // 3 end, 'closed')
    rejects(function() return input % 3 end, 'closed')
end

-- Lua's 64-bit integer operations supply an independent floor/remainder oracle
-- for all native 32-bit endpoints and deterministic signed samples.
do
    local values = {-2147483648, -2147483647, -7, -1, 0, 1, 7, 2147483646, 2147483647}
    local divisors = {-2147483648, -2147483647, -3, -2, -1, 1, 2, 3, 2147483646, 2147483647}
    local seed = 42
    for _ = 1, 128 do
        seed = (seed * 48271) % 2147483647
        local value = seed - 1073741824
        values[#values + 1] = value
        divisors[#divisors + 1] = value == 0 and 1 or value
    end
    for _, divisor in ipairs(divisors) do
        local safe = {}
        for _, value in ipairs(values) do
            if value ~= -2147483648 or divisor ~= -1 then safe[#safe + 1] = value end
        end
        local input <close> = Series(safe)
        local quotient <close> = input:quotient(divisor)
        local remainder <close> = input:remainder(divisor)
        local floor <close> = input // divisor
        local modulo <close> = input % divisor
        for i, value in ipairs(safe) do
            local fq, fm = value // divisor, value % divisor
            local q = fq < 0 and fm ~= 0 and fq + 1 or fq
            local r = value - q * divisor
            assert(quotient[i] == q and remainder[i] == r)
            assert(floor[i] == fq and modulo[i] == fm)
            assert(q * divisor + r == value and fq * divisor + fm == value)
        end
    end
end
collect()
assert(Native.live() == 0 and Cursor.live() == 0)

-- Cursors own native storage independently, including after close of a proxy.
do
    local object = Series({4, 2})
    local next_value = pairs(object)
    object:close()
    object = nil
    collect()
    assert(Native.live() == 1 and Cursor.live() == 1)
    local i, value = next_value()
    assert(i == 1 and value == 4)
    i, value = next_value()
    assert(i == 2 and value == 2)
    assert(next_value() == nil and next_value() == nil)
    assert(Native.live() == 0 and Cursor.live() == 0)
end
-- Early exit and exceptions release abandoned cursors and unclosed proxies by GC.
do
    local object = Series({1, 2, 3})
    for _, _ in pairs(object) do break end
    local ok = pcall(function()
        for _, _ in pairs(object) do error("loop body") end
    end)
    assert(not ok)
end
collect()
assert(Native.live() == 0 and Cursor.live() == 0)

-- Native named adapters work without installing any Lua protocols.
do
    local object <close> = Native.new({20, 21})
    local result <close> = object:times(2)
    local cursor <close> = result:cursor()
    result:close()
    assert(cursor:next() == 40 and cursor:next() == 42 and cursor:next() == nil)
    rejects(function() object:at(-2147483648) end, "index out of range")
    object:set(1, 42)
    assert(object:at(1) == 42)
end
collect()
assert(Native.live() == 0 and Cursor.live() == 0)
print("Lua operator and iterator protocols passed")
