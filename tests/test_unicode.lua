if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], "luaopen_demo")) end
local demo = require("demo")
local function rejects(fn, expected)
    local ok, message = pcall(fn)
    assert(not ok and tostring(message):find(expected, 1, true), tostring(message))
end
local sample = "A\0" .. utf8.char(0x20ac, 0x1f600)
local points = demo.demo__unicode_decode(sample)
assert(#points == 4 and points[1] == 65 and points[2] == 0 and points[3] == 0x20ac and points[4] == 0x1f600)
assert(demo.demo__unicode_encode(points) == sample)
local units = demo.demo__unicode_utf16(sample)
assert(#units == 5 and units[1] == 65 and units[2] == 0 and units[3] == 0x20ac and units[4] == 0xd83d and units[5] == 0xde00)
assert(demo.demo__unicode_from_utf16(units) == sample)
assert(demo.demo__unicode_char8(sample) == sample and demo.demo__unicode_wide(sample) == sample)
local bits = demo.demo__unicode_wchar_bits()
assert(bits == 16 or bits == 32)
assert(demo.demo__unicode_wide_length(sample) == (bits == 16 and 5 or 4))
assert(demo.demo__unicode_scalar_count(sample) == 4)
assert(demo.demo__unicode_scalar_count(utf8.char(0x65, 0x301)) == 2) -- code points, not graphemes
assert(demo.demo__unicode_encode({0xfeff}) == "\xef\xbb\xbf") -- preserve a BOM as data
assert(demo.demo__unicode_from_utf16({0xd800, 0xdc00}) == utf8.char(0x10000))
assert(demo.demo__unicode_from_utf16({0xdbff, 0xdfff}) == utf8.char(0x10ffff))
assert(#demo.demo__unicode_decode("") == 0 and #demo.demo__unicode_utf16("") == 0)
assert(demo.demo__unicode_encode({}) == "" and demo.demo__unicode_from_utf16({}) == "")
assert(demo.demo__unicode_char8("") == "" and demo.demo__unicode_wide("") == "")
for _, bad in ipairs({
    "\x80", "\xbf", "\xc0\x80", "\xc1\xbf", "\xc2", "\xc2A", "\xe2\x82",
    "\xe0\x9f\x80", "\xed\xa0\x80", "\xed\xbf\xbf", "\xe2(\xa1", "\xf0\x9f\x98",
    "\xf0\x80\x80\x80", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xff",
}) do
    for _, convert in ipairs({demo.demo__unicode_decode, demo.demo__unicode_utf16,
        demo.demo__unicode_char8, demo.demo__unicode_wide, demo.demo__unicode_scalar_count}) do
        rejects(function() convert(bad) end, "UTF-8")
        rejects(function() convert(string.rep("prefix", 100) .. bad) end, "UTF-8")
    end
    assert(demo.demo__echo(bad) == bad) -- byte-string binding remains independent of encoding
end
for _, bad in ipairs({{0xd800}, {0xdc00}, {0xd800, 65}, {0xdbff, 0xdbff}, {0xd800, 0xdc00, 0xdc00}, {0x10000}}) do
    rejects(function() demo.demo__unicode_from_utf16(bad) end, "UTF-16")
end
for _, point in ipairs({0xd800, 0xdfff, 0x110000, 0xffffffff}) do
    rejects(function() demo.demo__unicode_encode({point}) end, "scalar value")
end
rejects(function() demo.demo__unicode_encode({-1}) end, "range")
rejects(function() demo.demo__unicode_encode({1.5}) end, "integer")
rejects(function() demo.demo__unicode_decode(42) end, "string expected")
rejects(function() demo.demo__unicode_from_utf16({"a"}) end, "number expected")

-- Every Unicode scalar, batched to bound memory. Lua's separate UTF-8 encoder
-- provides an independent byte oracle; roundtrips alone could share a bug.
local count = 0
for first = 0, 0x10ffff, 4096 do
    local values = {}
    for point = first, math.min(first + 4095, 0x10ffff) do
        if point < 0xd800 or point > 0xdfff then values[#values + 1] = point end
    end
    local expected = utf8.char(table.unpack(values))
    local encoded = demo.demo__unicode_encode(values)
    assert(encoded == expected)
    local decoded = demo.demo__unicode_decode(encoded)
    assert(#decoded == #values)
    for i, point in ipairs(values) do assert(decoded[i] == point) end
    assert(demo.demo__unicode_from_utf16(demo.demo__unicode_utf16(encoded)) == expected)
    assert(demo.demo__unicode_char8(encoded) == expected and demo.demo__unicode_wide(encoded) == expected)
    count = count + #values
end
assert(count == 0x110000 - 0x800)
collectgarbage("collect"); collectgarbage("collect")
print("Lua Unicode adapters passed all " .. count .. " scalar values")
