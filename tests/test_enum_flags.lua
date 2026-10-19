if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], 'luaopen_demo')) end
local demo = require('demo')
local function rejects(fn, expected)
    local ok, message = pcall(fn)
    assert(not ok and tostring(message):find(expected, 1, true), tostring(message))
end
local Feature = demo.demo__Feature
for bits = 0, 7 do assert(demo.demo__feature_identity(bits) == bits) end
local combined = demo.demo__combine_features(Feature.classes, Feature.templates | Feature.callbacks)
assert(combined == 7 and demo.demo__has_features(combined, Feature.classes | Feature.callbacks))
assert(not demo.demo__has_features(Feature.classes, Feature.templates))
assert(demo.demo__has_features(combined, 0))
local Byte, Signed, Wide, Bool = demo.demo__ByteFlags, demo.demo__SignedFlags, demo.demo__WideFlags, demo.demo__BoolFlags
assert(Byte.alias == Byte.low and Byte.high == 128)
for bits = 0, 255 do
    local allowed = (bits & ~(Byte.low | Byte.high)) == 0
    if allowed then assert(demo.demo__byte_flags(bits) == bits)
    else rejects(function() demo.demo__byte_flags(bits) end, 'unknown enum flag bits') end
end
for bits = -128, 127 do
    if bits >= 0 and bits <= 3 then assert(demo.demo__signed_flags(bits) == bits)
    else rejects(function() demo.demo__signed_flags(bits) end, 'unknown enum flag bits') end
end
assert(Signed.invalid == -1)
Byte.high = 2
assert(demo.demo__byte_flags(128) == 128)
rejects(function() demo.demo__byte_flags(Byte.high) end, 'unknown enum flag bits')
Byte.high = 128 -- changing exported tables never changes the compiled policy
assert(demo.demo__wide_flags(Wide.low | Wide.high) == (1 | (1 << 62)))
assert(demo.demo__wide_flags(0) == 0)
assert(Bool.enabled == 1 and demo.demo__bool_flags(1) == 1 and demo.demo__bool_flags(0) == 0)
rejects(function() demo.demo__bool_flags(true) end, 'number expected')
rejects(function() demo.demo__bool_flags(2) end, 'range')
rejects(function() demo.demo__wide_flags(Wide.high | 2) end, 'unknown enum flag bits')
rejects(function() demo.demo__wide_flags(-1) end, 'range')
rejects(function() demo.demo__byte_flags(-1) end, 'range')
rejects(function() demo.demo__byte_flags(256) end, 'range')
rejects(function() demo.demo__byte_flags(1.5) end, 'integer')
rejects(function() demo.demo__byte_flags('1') end, 'number expected')
rejects(function() demo.demo__feature_identity(8) end, 'unknown enum flag bits')
-- Unconfigured enums keep their original closed named-value validation.
rejects(function() demo.demo__operation_identity(demo.demo__Operation.add | demo.demo__Operation.multiply) end, 'unknown enum value')
local binary = string.rep('flags\0', 128)
assert(demo.demo__flags_text(binary, Byte.high | Byte.low) == binary .. '129')
rejects(function() demo.demo__flags_text(binary, Byte.reserved) end, 'unknown enum flag bits')
assert(demo.demo__byte_flags(Byte.high) == 128) -- recovery
print('Explicit enum flags, basis masks, signed/unsigned/bool widths and strict enums passed')
