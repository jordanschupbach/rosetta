-- Exercise the MEP extension against the same descriptor/header interface MEP
-- uses, without needing an editor window or a host language interpreter.
mep = {org_babel_langs = {}}
function mep.getcwd() return '/project with spaces' end
function mep.filename() return 'README.mepml' end
function mep.org_header_arg(args, key)
    return args:match(':' .. key:gsub('(%W)', '%%%1') .. ' ([%w-]+)')
end
local languages = {'sh', 'lua', 'r', 'python', 'ruby', 'octave', 'julia', 'csharp'}
for _, name in ipairs(languages) do
    mep.org_babel_langs[name] = {
        executable = name,
        extension = '.' .. name,
        run_cmd = function(executable, source) return {executable, source, '--original'} end,
    }
end
mep.org_babel_langs.java = {executable = 'javac', compiled = true, wrap_main = function() end}
mep_org_babel_lang_ts_ft = {}
dofile('scripts/readme/mep.lua')
for _, name in ipairs(languages) do
    local def = mep.org_babel_langs[name]
    local selected = 'readme-' .. (name == 'sh' and 'shell' or name)
    local args = ':nix-shell ' .. selected
    assert(def.exe_for(args) == 'nix')
    local command = def.run_cmd('nix', '/tmp/chunk with spaces.' .. name, args)
    assert(command[1] == 'nix' and command[3] == 'path:/project with spaces/.#' .. selected)
    assert(command[5] == 'python3' and command[7] == name)
    assert(command[8] == '/tmp/chunk with spaces.' .. name)
    assert(def.exe_for('') == nil)
    assert(def.run_cmd(name, '/tmp/file', '')[3] == '--original')
    assert(not pcall(def.exe_for, ':nix-shell readme-wrong'))
    local before = def
    dofile('scripts/readme/mep.lua')
    assert(mep.org_babel_langs[name] == before)
end
print('README MEP runner selection, argv boundaries, fallback and repeated setup passed')

assert(mep.org_babel_langs.java.compiled == true)
local java = mep.org_babel_langs['rosetta-java']
assert(java.compiled == false and java.wrap_main == mep.org_babel_langs.java.wrap_main)
assert(mep_org_babel_lang_ts_ft['rosetta-java'] == 'java')
local command = java.run_cmd('nix', '/tmp/Main.java', ':nix-shell readme-java')
assert(command[3] == 'path:/project with spaces/.#readme-java' and command[7] == 'java')
assert(not pcall(java.run_cmd, 'nix', '/tmp/Main.java', ':nix-shell readme-wrong'))

local fortran = mep.org_babel_langs['rosetta-fortran']
assert(fortran.compiled == false and fortran.extension == '.f90')
assert(mep_org_babel_lang_ts_ft['rosetta-fortran'] == 'fortran')
local fortran_command = fortran.run_cmd('nix', '/tmp/Fortran chunk.f90', ':nix-shell readme-fortran')
assert(fortran_command[3] == 'path:/project with spaces/.#readme-fortran' and fortran_command[7] == 'fortran')
assert(fortran_command[8] == '/tmp/Fortran chunk.f90')
assert(not pcall(fortran.run_cmd, 'nix', '/tmp/chunk.f90', ':nix-shell readme-wrong'))
