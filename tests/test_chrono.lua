if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], 'luaopen_demo')) end
local demo = require('demo')
local Point, convert = demo.demo__TimePoint, demo.demo__time_convert
local function rejects(fn, expected)
    local ok, message = pcall(fn)
    assert(not ok and tostring(message):find(expected, 1, true), tostring(message))
end
assert(Point.live() == 0)
assert(convert(42, 's', 'ms') == 42000)
assert(convert(42, 'h', 'ns', 'exact') == 151200000000000)
for _, value in ipairs({-2500, -1500, -500, 500, 1500, 2500}) do
    local expected = {[-2500]=-2, [-1500]=-2, [-500]=0, [500]=0, [1500]=2, [2500]=2}
    assert(convert(value, 'ms', 's', 'nearest') == expected[value]) -- ties to even
end
assert(convert(-1001, 'ms', 's') == -1)
assert(convert(-1001, 'ms', 's', 'floor') == -2)
assert(convert(-1001, 'ms', 's', 'ceil') == -1)
assert(convert(1001, 'ms', 's', 'floor') == 1 and convert(1001, 'ms', 's', 'ceil') == 2)
rejects(function() convert(1001, 'ms', 's', 'exact') end, 'inexact')
assert(convert(-2000, 'ms', 's', 'exact') == -2)

-- Independent Lua 64-bit integer oracle for reducing every supported unit pair.
-- This also exercises extrema where rebuilding a rounded source duration would overflow.
local units = {'ns', 'us', 'ms', 's', 'min', 'h'}
local factors = {1, 1000, 1000000, 1000000000, 60000000000, 3600000000000}
local values = {math.mininteger, math.mininteger + 1, -9999, -1500, -1, 0, 1, 1500, 9999, math.maxinteger-1, math.maxinteger}
local seed = 42
for _ = 1, 128 do seed = (seed * 48271) % 2147483647; values[#values+1] = (seed-1073741824)*1234567 end
for from = 1, #units do
    for to = from, #units do
        local divisor = factors[to] // factors[from]
        for _, value in ipairs(values) do
            local fq, remainder = value // divisor, value % divisor
            local cq = remainder == 0 and fq or fq + 1
            local tq = value < 0 and cq or fq
            local nearest = fq
            if remainder > divisor // 2 or (divisor % 2 == 0 and remainder == divisor // 2 and fq % 2 ~= 0) then nearest = fq + 1 end
            assert(convert(value, units[from], units[to]) == tq)
            assert(convert(value, units[from], units[to], 'floor') == fq)
            assert(convert(value, units[from], units[to], 'ceil') == cq)
            assert(convert(value, units[from], units[to], 'nearest') == nearest)
            if remainder == 0 then assert(convert(value, units[from], units[to], 'exact') == fq)
            else rejects(function() convert(value, units[from], units[to], 'exact') end, 'inexact') end
        end
    end
end
for from = 2, #units do
    for to = 1, from-1 do
        local factor = factors[from] // factors[to]
        local high = math.maxinteger // factor
        local low = math.mininteger // factor + 1 -- unit ratios have factors 2 and 5, none divides 2^63
        assert(convert(high, units[from], units[to]) == high * factor)
        assert(convert(low, units[from], units[to]) == low * factor)
        rejects(function() convert(high+1, units[from], units[to]) end, 'overflow')
        rejects(function() convert(low-1, units[from], units[to]) end, 'overflow')
    end
end
for _, mode in ipairs({'truncate', 'floor', 'ceil', 'nearest', 'exact'}) do
    assert(convert(math.mininteger, 'ns', 'ns', mode) == math.mininteger)
    assert(convert(math.maxinteger, 'ns', 'ns', mode) == math.maxinteger)
end
rejects(function() convert(1, 'MS', 's') end, 'unknown time unit')
rejects(function() convert(1, 'ms\0', 's') end, 'unknown time unit')
rejects(function() convert(1, 'ms', 's', 'round') end, 'unknown time rounding')
rejects(function() convert(1.5, 'ms', 's') end, 'integer')
rejects(function() convert('1', 'ms', 's') end, 'number expected')
rejects(function() convert(1, 'ms', 's', nil) end, 'string expected')

do
    local epoch <close> = Point.new(0)
    local future <close> = epoch:shifted(42)
    local past <close> = epoch:shifted(-42)
    assert(not epoch:steady() and epoch:ticks() == 0 and future:ticks() == 42)
    assert(future:difference(epoch) == 42 and epoch:difference(future) == -42)
    assert(past:before(epoch) and epoch:before(future) and not epoch:before(epoch))
    local steady <close> = Point.new(0, true)
    rejects(function() epoch:difference(steady) end, 'different clocks')
    rejects(function() steady:before(epoch) end, 'different clocks')
    local high <close> = Point.new(math.maxinteger)
    local low <close> = Point.new(math.mininteger)
    rejects(function() high:shifted(1) end, 'overflow')
    rejects(function() low:shifted(-1) end, 'overflow')
    rejects(function() high:difference(low) end, 'overflow')
    rejects(function() low:difference(high) end, 'overflow')
    assert(high:ticks() == math.maxinteger and low:ticks() == math.mininteger)
    local minimum_shift <close> = epoch:shifted(math.mininteger)
    local maximum_shift <close> = epoch:shifted(math.maxinteger)
    assert(minimum_shift:ticks() == math.mininteger and maximum_shift:ticks() == math.maxinteger)
    assert(high:difference(epoch) == math.maxinteger and low:difference(epoch) == math.mininteger)
    epoch:close(); epoch:close()
    assert(future:ticks() == 42)
    rejects(function() future:difference(epoch) end, 'closed')
    rejects(function() epoch:shifted(1) end, 'closed')
end
for _, steady in ipairs({false, true}) do
    local info = demo.demo__time_clock_info(steady)
    assert(#info == 3 and info[1] > 0 and info[2] > 0)
    if steady then assert(info[3] == 1) end
    local first <close> = demo.demo__time_now(steady)
    local second <close> = demo.demo__time_now(steady)
    assert(first:steady() == steady and second:steady() == steady)
    if steady then assert(not second:before(first)) end -- no wall-clock monotonicity assumption
end
rejects(function() Point.new(0, 1) end, 'boolean expected')
do local abandoned = Point.new(42, true); assert(abandoned:ticks() == 42) end
collectgarbage('collect'); collectgarbage('collect')
assert(Point.live() == 0)
print('Chrono durations, rounding, clock domains and time points passed')
