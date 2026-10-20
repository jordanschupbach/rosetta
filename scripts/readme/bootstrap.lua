local build = assert(os.getenv('ROSETTA_README_BUILD'))
package.cpath = build .. '/?.so;' .. package.cpath
demo = require('demo')
local source = table.remove(arg, 1)
arg[0] = source
assert(loadfile(source))()
