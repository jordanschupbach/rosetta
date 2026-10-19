-- Target protocols over generated, target-independent named C++ adapters.
-- Loading this layer does not modify the native module or userdata metatables.
return function(demo)
    local handles = setmetatable({}, {__mode = "k"})
    local methods, meta = {}, {__metatable = "Rosetta series protocol"}
    local function native(value)
        local handle = handles[value]
        if not handle then error("expected series protocol object", 3) end
        return handle
    end
    local function wrap(handle)
        local value = setmetatable({}, meta)
        handles[value] = handle
        return value
    end
    function methods:close() native(self):close() end
    function methods:clone() return wrap(native(self):clone()) end
    function methods:nonempty() return native(self):nonempty() end
    function methods:at(index) return native(self):at(index) end
    function methods:set(index, value) native(self):set(index, value) end
    function methods:quotient(divisor) return wrap(native(self):quotient(divisor)) end
    function methods:remainder(divisor) return wrap(native(self):remainder(divisor)) end
    function methods:floor_quotient(divisor) return wrap(native(self):floor_quotient(divisor)) end
    function methods:floor_modulo(divisor) return wrap(native(self):floor_modulo(divisor)) end
    function meta.__index(self, key)
        if type(key) == "number" then return native(self):at(key) end
        return methods[key]
    end
    function meta.__newindex(self, key, value)
        if type(key) ~= "number" then error("series members are read-only", 2) end
        native(self):set(key, value)
    end
    function meta.__len(self) return native(self):length() end
    function meta.__add(left, right) return wrap(native(left):plus(native(right))) end
    function meta.__sub(left, right) return wrap(native(left):minus(native(right))) end
    function meta.__mul(left, right)
        if type(left) == "number" then left, right = right, left end
        return wrap(native(left):times(right))
    end
    function meta.__unm(self) return wrap(native(self):negative()) end
    -- / exposes C++ integer division. // and % use Lua's floor convention.
    function meta.__div(self, divisor) return methods.quotient(self, divisor) end
    function meta.__idiv(self, divisor) return methods.floor_quotient(self, divisor) end
    function meta.__mod(self, divisor) return methods.floor_modulo(self, divisor) end
    function meta.__band(left, right) return wrap(native(left):bits_and(native(right))) end
    function meta.__bor(left, right) return wrap(native(left):bits_or(native(right))) end
    function meta.__bxor(left, right) return wrap(native(left):bits_xor(native(right))) end
    function meta.__bnot(self) return wrap(native(self):bits_not()) end
    function meta.__shl(self, count) return wrap(native(self):shift_left(count)) end
    function meta.__shr(self, count) return wrap(native(self):shift_right(count)) end
    function meta.__eq(left, right)
        if not handles[left] or not handles[right] then return false end
        return native(left):equals(native(right))
    end
    function meta.__lt(left, right) return native(left):less(native(right)) end
    function meta.__le(left, right) return native(left):less_equal(native(right)) end
    function meta.__call(self, factor) return wrap(native(self):invoke(factor)) end
    function meta.__tostring(self) return native(self):text() end
    meta.__close = methods.close
    function meta.__pairs(self)
        local cursor = native(self):cursor()
        local index = 0
        return function()
            if not cursor then return nil end
            local ok, value = pcall(cursor.next, cursor)
            if not ok or value == nil then
                cursor:close()
                cursor = nil
                if not ok then error(value, 0) end
                return nil
            end
            index = index + 1
            return index, value
        end, nil, nil
    end
    local Series = {}
    function Series.new(values) return wrap(demo.demo__IntSeries.new(values)) end
    return setmetatable(Series, {__call = function(_, values) return Series.new(values) end})
end
