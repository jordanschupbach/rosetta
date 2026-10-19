-- Explicit bounded owner-thread pumping; no hidden event loop or worker entry.
return function(demo)
    local function limit(value)
        value = value or 1000
        assert(math.type(value)=='integer' and value>=0, 'pump limit must be a nonnegative integer')
        return value
    end
    return {
        drain = function(executor, budget)
            local count = 0
            budget = limit(budget)
            while count < budget and executor:run_one() do count = count + 1 end
            return count, executor:pending()
        end,
        result = function(executor, task, budget)
            local count = 0
            budget = limit(budget)
            while task:state()==demo.demo__AwaitPhase.pending and count<budget do
                if not executor:run_one() then break end
                count = count + 1
            end
            return task:result()
        end,
    }
end
