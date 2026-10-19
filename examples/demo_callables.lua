-- Optional target protocol over reusable C++ callable handles.
return function(demo)
    local methods, metatable = {}, {}
    local function wrap(handle) return setmetatable({handle = handle}, metatable) end
    function methods:close() self.handle:close() end
    function methods:clear() self.handle:clear() end
    function methods:empty() return self.handle:empty() end
    function methods:copy() return wrap(self.handle:copy()) end
    function methods:share() return wrap(self.handle:share()) end
    metatable.__index = methods
    metatable.__call = function(self, value) return self.handle:invoke(value) end
    metatable.__close = function(self) self:close() end
    return {
        wrap = wrap,
        capture = function(prefix, seed) return wrap(demo.demo__make_native_capture(prefix, seed)) end,
        bound = function(prefix, seed) return wrap(demo.demo__make_native_bound(prefix, seed)) end,
        pointer = function() return wrap(demo.demo__make_native_pointer()) end,
        empty = function() return wrap(demo.demo__make_native_empty()) end,
        from_lua = function(callback) return wrap(demo.demo__make_native_callback(callback)) end,
    }
end
