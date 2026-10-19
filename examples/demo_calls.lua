-- Explicit target dispatch rules over reusable named C++ adapters.
return function(demo)
    local Calls = {}
    local minimum, maximum, unsigned_maximum = demo.demo__call_int_min(), demo.demo__call_int_max(), demo.demo__call_uint_max()
    function Calls.choose(...)
        if select("#", ...) ~= 1 then error("choose expects exactly 1 argument", 2) end
        local value = ...
        if type(value) == "string" then return demo.demo__overload_text(value) end
        if type(value) == "number" then
            if math.type(value) == "integer" then return demo.demo__overload_integer(value) end
            return demo.demo__overload_real(value)
        end
        error("no matching choose overload", 2)
    end
    function Calls.scale(...)
        local count = select("#", ...)
        if count == 1 then return demo.demo__default_scale(...) end
        if count == 2 then return demo.demo__explicit_scale(...) end
        error("scale expects 1 or 2 arguments", 2)
    end
    function Calls.ambiguous(...)
        if select("#", ...) ~= 1 then error("ambiguous expects exactly 1 argument", 2) end
        local value = ...
        if type(value) ~= "number" or math.type(value) ~= "integer" then error("no matching integer overload", 2) end
        local signed = value >= minimum and value <= maximum
        local unsigned = value >= 0 and value <= unsigned_maximum
        if signed and unsigned then error("ambiguous integer overload: use Calls.signed or Calls.unsigned", 2) end
        if signed then return demo.demo__signed_choice(value) end
        if unsigned then return demo.demo__unsigned_choice(value) end
        error("no matching integer overload", 2)
    end
    Calls.signed = demo.demo__signed_choice
    Calls.unsigned = demo.demo__unsigned_choice
    Calls.sum = demo.demo__variadic_sum
    return Calls
end
