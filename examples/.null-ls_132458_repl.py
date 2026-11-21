"""Load the built extensions by their actual platform-specific filenames."""
import importlib.util
import sys


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


demo = load("python_demo", sys.argv[1])
verify = load("python_verify", sys.argv[2])
Counter = demo.r_demo__Counter
Series = verify.r_verify__Series
Worklist = verify.r_verify__Worklist
AsyncTask = verify.r_verify__AsyncTask
print("Rosetta Python extensions loaded as demo and verify.")
print("Try: demo.r_demo__add(20, 22)")
print("Classes: with Counter(20) as counter: print(counter.add(22))")
print("Features: with Series(20, 2) as series: print(series.scaled_sum(2))")
print("List exports: dir(demo), dir(verify). Exit with exit() or Ctrl-D.")
