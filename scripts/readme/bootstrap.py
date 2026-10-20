"""Load the exact Python artifacts built by run.py, then run the chunk."""
import importlib.util
import os
from pathlib import Path
import sys
import sysconfig

build = Path(os.environ["ROSETTA_README_BUILD"])
def load(name):
    path = build / (name + sysconfig.get_config_var("EXT_SUFFIX"))
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module

demo = load("python_demo")
verify = load("python_verify")
Counter = demo.r_demo__Counter
Series = verify.r_verify__Series
Worklist = verify.r_verify__Worklist
AsyncTask = verify.r_verify__AsyncTask
sys.argv = sys.argv[1:]
__file__ = sys.argv[0]
exec(compile(Path(__file__).read_bytes(), __file__, "exec"), globals())
