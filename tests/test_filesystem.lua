if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], 'luaopen_demo')) end
local demo = require('demo')
local Workspace = demo.demo__FileWorkspace
local function rejects(fn, expected)
    local ok, message = pcall(fn)
    assert(not ok and tostring(message):find(expected, 1, true), tostring(message))
end
assert(demo.demo__path_normalize('alpha//beta/../gamma/./file.txt') == 'alpha/gamma/file.txt')
assert(demo.demo__path_normalize('../outside') == '../outside') -- lexical only
assert(demo.demo__path_join('alpha', 'beta') == 'alpha/beta')
assert(demo.demo__path_join('alpha', '/absolute') == '/absolute') -- native POSIX path semantics
local parts = demo.demo__path_parts('alpha/archive.tar.gz')
assert(parts[1] == 'alpha' and parts[2] == 'archive.tar.gz' and parts[3] == '.gz')
local components = demo.demo__path_components('/alpha/beta')
assert(table.concat(components, '|') == '/|alpha|beta')
assert(demo.demo__path_normalize('unicode/' .. utf8.char(0x1f600)) == 'unicode/' .. utf8.char(0x1f600))
rejects(function() demo.demo__path_normalize('bad\0path') end, 'path contains NUL')
local cleanup = Workspace.cleanup_errors()
local root
do
    local files <close> = Workspace.new()
    root = files:location()
    assert(files:exists() and not files:exists('absent'))
    assert(#files:entries() == 0)
    files:directories('nested/deeper')
    local name = 'nested/deeper/' .. utf8.char(0x1f600) .. '.txt'
    local bytes = 'first\0' .. string.rep('binary\255', 1000)
    files:write(name, bytes)
    assert(files:read(name) == bytes and files:size(name) == #bytes)
    files:write('empty', '')
    rejects(function() files:write('oversized', string.rep('b', 16 * 1024 * 1024 + 1)) end, '16 MiB')
    assert(not files:exists('oversized'))
    assert(files:read('empty') == '' and files:size('empty') == 0)
    local snapshot = files:entries('', true)
    assert(table.concat(snapshot, '|') == 'empty|nested|nested/deeper|' .. name)
    snapshot[1] = 'lua-only'
    assert(files:entries()[1] == 'empty')
    files:rename('empty', 'renamed')
    assert(not files:exists('empty') and files:exists('renamed'))
    rejects(function() files:rename('renamed', name) end, 'destination exists')
    rejects(function() files:rename('missing', 'other') end, 'filesystem error')
    rejects(function() files:read('nested') end, 'regular file expected')
    rejects(function() files:write('no-parent/file', 'data') end, 'create staged file')
    rejects(function() files:erase('nested') end, 'filesystem error')
    rejects(function() files:write('nested', 'cannot replace directory') end, 'filesystem error')
    assert(#files:entries() == 2) -- failed publication left no staging files
    for _, invalid in ipairs({'', '.', '..', '../outside', 'nested/../../outside', '/absolute', 'nested/./file', 'nul\0name'}) do
        assert(not pcall(function() files:write(invalid, 'data') end))
        assert(not pcall(function() files:erase(invalid, true) end))
    end
    rejects(function() files:directories(string.rep('deep/', 33)) end, '32 components')
    assert(files:erase('nested', true) and not files:erase('nested', true))
    files:write('renamed', 'replacement')
    assert(files:read('renamed') == 'replacement')
    local shared <close> = files:share()
    files:close()
    assert(shared:read('renamed') == 'replacement' and Workspace.live() == 1)
    rejects(function() files:read('renamed') end, 'closed')
end
assert(Workspace.live() == 0 and Workspace.cleanup_errors() == cleanup)
assert(io.open(root .. '/renamed', 'rb') == nil)
do
    local discarded = Workspace.new()
    discarded:directories('left/for/gc')
    discarded:write('left/for/gc/file', 'data')
end
collectgarbage('collect')
assert(Workspace.live() == 0 and Workspace.cleanup_errors() == cleanup)
print('Filesystem paths, binary I/O, directory snapshots, errors, sharing and cleanup passed')
