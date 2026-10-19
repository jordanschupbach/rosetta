-- Keep an unconsumed native yield available if publishing value() raises.
return function()
    local Generators = {}
    function Generators.values(source)
        local advance = true
        return function()
            if not source then return nil end
            if advance then
                if not source:advance() then source = nil; return nil end
                advance = false
            end
            local value = source:value()
            advance = true
            return value
        end
    end
    return Generators
end
