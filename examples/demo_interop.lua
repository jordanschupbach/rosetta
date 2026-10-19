-- Explicit process-local ticket adaptation between independently generated modules.
return function(a,b)
    local A,B=a.interop_a__Record,b.interop_b__Record
    return {
        new_a=function(text,value) return A.new(text,value) end,
        new_b=function(text,value) return B.new(text,value) end,
        to_a=function(record) return A.from_ticket(record:ticket()) end,
        to_b=function(record) return B.from_ticket(record:ticket()) end,
    }
end
