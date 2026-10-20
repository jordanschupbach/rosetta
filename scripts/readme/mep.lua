-- Loaded by the README's mep-lua setup block. Ordinary Babel invocations keep
-- their existing behavior; only chunks opting into nix-shell use this runner.
local root = (mep.filename():match('^(.*)/') or '.')
if root:sub(1, 1) ~= '/' then root = mep.getcwd() .. '/' .. root end
for _, language in ipairs({'sh', 'lua', 'r', 'python', 'ruby', 'octave', 'julia', 'csharp'}) do
    local original = assert(mep.org_babel_langs[language], 'Missing Babel language: ' .. language)
    if not original.rosetta_nix_runner then
        local descriptor = {}
        for key, value in pairs(original) do descriptor[key] = value end
        descriptor.rosetta_nix_runner = true
        local function shell(args)
            local value = mep.org_header_arg(args or '', 'nix-shell')
            if not value or value == '' then return nil end
            local expected = 'readme-' .. (language == 'sh' and 'shell' or language)
            assert(value == expected, 'Expected nix-shell=' .. expected .. ' for ' .. language)
            return value
        end
        descriptor.exe_for = function(args)
            if shell(args) then return 'nix' end
            return original.exe_for and original.exe_for(args) or nil
        end
        descriptor.run_cmd = function(executable, source, args)
            local selected = shell(args)
            if selected then
                return {'nix', 'develop', 'path:' .. root .. '#' .. selected, '--command',
                    'python3', root .. '/scripts/readme/run.py', language, source}
            end
            return original.run_cmd and original.run_cmd(executable, source, args) or {executable, source}
        end
        mep.org_babel_langs[language] = descriptor
    end
end
-- Java has a separate javac phase. Give its Nix compile/run path a distinct
-- descriptor so ordinary java blocks retain MEP's standard compilation path.
if not mep.org_babel_langs['rosetta-java'] then
    local original = assert(mep.org_babel_langs.java, 'Missing Babel language: java')
    mep.org_babel_langs['rosetta-java'] = {
        executable = 'nix', extension = '.java', compiled = false,
        wrap_main = original.wrap_main, var_stmt = original.var_stmt, print_stmt = original.print_stmt,
        run_cmd = function(_, source, args)
            assert(mep.org_header_arg(args or '', 'nix-shell') == 'readme-java', 'Expected nix-shell=readme-java')
            return {'nix', 'develop', 'path:' .. root .. '#readme-java', '--command',
                'python3', root .. '/scripts/readme/run.py', 'java', source}
        end,
    }
    if mep_org_babel_lang_ts_ft then mep_org_babel_lang_ts_ft['rosetta-java'] = 'java' end
end
-- Fortran chunks contain a complete program and compile against generated modules.
if not mep.org_babel_langs['rosetta-fortran'] then
    mep.org_babel_langs['rosetta-fortran'] = {
        executable = 'nix', extension = '.f90', compiled = false,
        run_cmd = function(_, source, args)
            assert(mep.org_header_arg(args or '', 'nix-shell') == 'readme-fortran', 'Expected nix-shell=readme-fortran')
            return {'nix', 'develop', 'path:' .. root .. '#readme-fortran', '--command',
                'python3', root .. '/scripts/readme/run.py', 'fortran', source}
        end,
    }
    if mep_org_babel_lang_ts_ft then mep_org_babel_lang_ts_ft['rosetta-fortran'] = 'fortran' end
end
return 'README Nix runners ready: shell, lua, r, python, ruby, octave, julia, java, csharp, fortran'
