-- Class-value callback protocol. The frame owns native value transport;
-- this layer closes its temporary argument and copies the selected result.
return function(demo)
    local function invoke(frame, input, callback)
        assert(type(callback) == 'function', 'class callback must be a function')
        return frame:transform(input, function()
            local argument <close> = frame:argument()
            local result = callback(argument)
            frame:set_result(result)
        end)
    end
    return {
        with_frame = invoke,
        transform = function(input, callback)
            local frame <close> = demo.demo__ClassCallbackFrame.new()
            return invoke(frame,input,callback)
        end,
    }
end
