-- Check that README.mepml's runnable chunks select a flake shell that exists
-- and run with their language's own command: the readme-LANGUAGE shell, not
-- a `cmd` override, sets up the generated modules.
local function read(path)
    local file = assert(io.open(path, 'r'))
    local text = file:read('a')
    file:close()
    return text
end

local flake = read('flake.nix')
local shells, chunk_shells = {}, {}
for name in flake:gmatch('devShells%.(readme%-[%w-]+)%s*=') do shells[name] = true end
for name in flake:gmatch('devShells%.readme%-([%w-]+)%s*=%s*chunkShell') do chunk_shells[name] = true end

local count = 0
for line in read('README.mepml'):gmatch('[^\n]*') do
    local header = line:match('^```{(.*)}%s*$')
    if header then
    local language = header:match('^%s*([%w-]+)')
    local shell = header:match('nix%-shell="([^"]*)"')
    if shell then
        count = count + 1
        local name = shell:match('^%.#(readme%-[%w-]+)$')
        assert(name, 'README chunk must select a local flake shell: ' .. header)
        assert(shells[name], 'flake.nix has no devShells.' .. name .. ': ' .. header)
        -- MEP has no Common Lisp runner; Lisp chunks name sbcl and the shell's prelude.
        local cmd = header:match('cmd="([^"]*)"')
        if language == 'lisp' then
            assert(cmd == 'sbcl --noinform --no-userinit --non-interactive --load $ROSETTA_README_PRELUDE --load {file}',
                'Lisp chunks run sbcl with the readme-lisp prelude: ' .. header)
        else
            assert(not cmd, 'chunks run with the language command: ' .. header)
        end
        if language == 'sh' then
            assert(name == 'readme-shell', 'shell chunks use readme-shell: ' .. header)
        else
            assert(name == 'readme-' .. language, 'chunk language and shell differ: ' .. header)
            assert(chunk_shells[language], 'devShells.' .. name .. ' is not a chunkShell')
        end
        if language == 'java' then assert(header:find('main=yes', 1, true), 'Java chunks need main=yes') end
    end
    end
end
assert(count > 0, 'README.mepml has no nix-shell chunks')
print(('README chunk headers checked: %d'):format(count))
