import copy
import gc
import importlib.util
import pickle
import runpy
import subprocess
import sys
import unittest
import weakref

family, demo_path, verify_path = sys.argv[1:4]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


demo = load("python_demo", demo_path)
verify = load("python_verify", verify_path)
Counter = demo.r_demo__Counter
Series = verify.r_verify__Series
Worklist = verify.r_verify__Worklist
Task = verify.r_verify__AsyncTask


def v(name):
    return getattr(verify, "r_verify__" + name)


class Scalars(unittest.TestCase):
    def test_integer_types_and_ranges(self):
        self.assertEqual(v("int_bits")(), 32)
        for name in ("char", "schar", "uchar", "short", "ushort", "int", "uint", "long", "ulong", "ll", "ull"):
            echo = v("echo_" + name)
            self.assertEqual(echo(42), 42)
            for bad in (None, True, False, 1.0, "42", b"42", [], {}, 1j):
                with self.subTest(name=name, bad=repr(bad)), self.assertRaises(TypeError):
                    echo(bad)
            with self.assertRaises(OverflowError):
                echo(1 << 100)
        for name, lo, hi in (("schar", -128, 127), ("uchar", 0, 255), ("short", -32768, 32767),
                             ("ushort", 0, 65535), ("int", -(1 << 31), (1 << 31)-1),
                             ("uint", 0, (1 << 32)-1), ("ll", -(1 << 63), (1 << 63)-1), ("ull", 0, (1 << 64)-1)):
            echo = v("echo_" + name)
            self.assertEqual((echo(lo), echo(hi)), (lo, hi))
            for bad in (lo-1, hi+1):
                with self.assertRaises(OverflowError):
                    echo(bad)
        for name in ("uchar", "ushort", "uint", "ulong", "ull"):
            with self.assertRaises(OverflowError):
                v("echo_" + name)(-1)
        self.assertEqual(demo.r_demo__too_large(), 9007199254740993)

    def test_floats_bools_strings(self):
        class Integer(int):
            def __float__(self):
                raise AssertionError("conversion hooks must not run")

        class Float(float):
            def __float__(self):
                raise AssertionError("conversion hooks must not run")

        self.assertEqual(v("echo_double")(Integer(42)), 42.0)
        self.assertEqual(v("echo_double")(Float(1.25)), 1.25)
        self.assertEqual(v("echo_float")(1.25), 1.25)
        self.assertEqual(v("echo_double")(sys.float_info.max), sys.float_info.max)
        for bad in (float("inf"), -float("inf"), float("nan")):
            with self.assertRaises(ValueError):
                v("echo_double")(bad)
        with self.assertRaises(OverflowError):
            v("echo_float")(1e39)
        self.assertIs(v("echo_bool")(True), True)
        with self.assertRaises(TypeError):
            v("echo_bool")(1)
        for text in ("", "world", "世界😀", "a\0b", "long" * 10000):
            self.assertEqual(v("echo_string")(text), text)
        self.assertEqual(v("embedded_nul")(), "a\0b")
        with self.assertRaises(UnicodeEncodeError):
            v("echo_string")("\ud800")
        with self.assertRaises(TypeError):
            v("echo_string")(b"bytes")

    def test_defaults_errors_and_recovery(self):
        self.assertEqual(v("defaulted")(), 42)
        self.assertEqual(v("defaulted")(12), 12)
        self.assertEqual(v("alias_identity")(42), 42)
        self.assertIsNone(v("do_nothing")())
        with self.assertRaises(TypeError):
            v("echo_int")()
        with self.assertRaises(TypeError):
            v("echo_int")(1, 2)
        with self.assertRaises(TypeError):
            v("echo_int")(value=1)
        for _ in range(20):
            with self.assertRaisesRegex(RuntimeError, r"unknown C\+\+ exception"):
                v("unknown_error")()
            with self.assertRaises(MemoryError):
                v("allocation_error")()
            with self.assertRaises(ValueError):
                v("nonfinite_result")()
            self.assertEqual(v("echo_string")("still alive"), "still alive")


class Objects(unittest.TestCase):
    def tearDown(self):
        gc.collect()
        self.assertEqual(Counter.live(), 0)

    def test_fields_methods_context_and_aliases(self):
        with Counter(20) as x:
            alias = x
            self.assertEqual(x.add(22), 42)
            self.assertEqual(Counter.answer(), 42)
            self.assertEqual(x.answer(), 42)
            x.count = 10
            x.label = "计数器\0😀"
            self.assertEqual(alias.value(), 10)
            self.assertEqual(alias.label, "计数器\0😀")
            self.assertEqual(x.initial, 20)
            with self.assertRaises(AttributeError):
                x.initial = 1
            with self.assertRaises(AttributeError):
                del x.count
            with self.assertRaises(TypeError):
                x.count = True
            with self.assertRaisesRegex(RuntimeError, "already initialized"):
                x.__init__(1)
            self.assertTrue(gc.is_tracked(x))
        alias.close()
        with self.assertRaisesRegex(RuntimeError, "closed"):
            alias.value()
        with self.assertRaisesRegex(RuntimeError, "closed"):
            alias.label = "closed"
        with self.assertRaisesRegex(ValueError, "body error"):
            with Counter(1):
                raise ValueError("body error")

    def test_construction_and_type_guards(self):
        with self.assertRaisesRegex(RuntimeError, "negative initial"):
            Counter(-1)
        with self.assertRaises(TypeError):
            Counter(value=1)
        x = Counter.__new__(Counter)
        with self.assertRaisesRegex(RuntimeError, "uninitialized"):
            x.value()
        x.__init__(42)
        self.assertEqual(x.value(), 42)
        with Series(0, 0) as other:
            with self.assertRaises(TypeError):
                Counter.value(other)
            with self.assertRaises(TypeError):
                Counter.count.__get__(other)
        with self.assertRaises(TypeError):
            Counter.__new__(Series)
        for action in (copy.copy, copy.deepcopy, pickle.dumps):
            with self.assertRaisesRegex(TypeError, "copied or pickled"):
                action(x)
        x.close()
        with self.assertRaises(TypeError):
            type("Derived", (Counter,), {})

    def test_type_references_and_cycles(self):
        before = sys.getrefcount(Counter)
        for _ in range(100):
            x = Counter(1)
            x.close()
            del x
        gc.collect()
        self.assertEqual(sys.getrefcount(Counter), before)
        temporary = load("python_demo", demo_path)
        cls = temporary.r_demo__Counter
        obj = cls(3)
        cls.owner_cycle = obj
        type_ref, module_ref = weakref.ref(cls), weakref.ref(temporary)
        del sys.modules["python_demo"]
        del temporary, cls, obj
        gc.collect()
        sys.modules["python_demo"] = demo
        self.assertIsNone(type_ref())
        self.assertIsNone(module_ref())


class Features(unittest.TestCase):
    def test_native_facades(self):
        with Series() as defaulted:
            self.assertEqual(defaulted.get(), 20)
            self.assertEqual(defaulted.text(), "[20, 21]")
        with Series(40) as partial:
            self.assertEqual(partial.get(), 40)
        self.assertEqual(v("maximum")(20, 42), 42)
        self.assertEqual(v("template_box")(42), 42)
        self.assertEqual(v("variadic_sum")(10, 20, 12), 42)
        self.assertEqual(v("constant_answer")(), 42)
        self.assertEqual(v("constrained_sum")(20, 22), 42)
        self.assertEqual(v("hierarchy_value")(42), 42)
        self.assertTrue(v("hierarchy_crosscast")())
        self.assertEqual(v("rosetta_r_c_identity")(42), 42)
        with self.assertRaises(OverflowError):
            v("constrained_sum")((1 << 31)-1, 1)
        with Series(20, 2) as s:
            self.assertEqual(s.scaled_sum(2), 82)
            self.assertEqual(s.text(), "[20, 21]")
            s.set(1, 22)
            self.assertEqual(s.scaled_sum(1), 42)
            with self.assertRaises(IndexError):
                s.get(2)
        self.assertEqual(Series.live(), 0)
        expected = (("m", "z", "a"), ("a", "z", "m"), ("z", "m", "a"), ("a", "m", "z"))
        for order in range(1, 5):
            with Worklist(order) as q:
                for text in ("a", "z", "m"):
                    q.push(text)
                self.assertEqual(tuple(q.pop() for _ in range(3)), expected[order-1])
                with self.assertRaises(IndexError):
                    q.pop()
        self.assertEqual(Worklist.live(), 0)

    def test_async_lifetime(self):
        with Task(21, 0, False) as task:
            self.assertTrue(task.wait(2000))
            self.assertEqual(task.result(), 42)
            self.assertEqual(task.result(), 42)
        with Task(1, 0, True) as task:
            self.assertTrue(task.wait(2000))
            with self.assertRaisesRegex(RuntimeError, "async worker failure"):
                task.result()
        with Task(1, 60000, False) as task:
            self.assertFalse(task.ready())
            self.assertTrue(task.cancel())
            self.assertTrue(task.wait(2000))
            with self.assertRaisesRegex(RuntimeError, "cancelled"):
                task.result()
        task = Task(1, 60000, False)
        del task
        gc.collect()
        self.assertEqual(Task.live(), 0)
        self.assertEqual(Task.workers(), 0)

    def test_interpreter_shutdown_joins_workers(self):
        code = """
import importlib.util, sys
spec = importlib.util.spec_from_file_location('python_verify', sys.argv[1])
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
task = module.r_verify__AsyncTask(1, 60000, False)
"""
        subprocess.run([sys.executable, "-I", "-c", code, verify_path], check=True, timeout=10)


class Repl(unittest.TestCase):
    def test_startup(self):
        startup = sys.argv[4]
        previous = sys.argv
        try:
            sys.argv = [startup, demo_path, verify_path]
            env = runpy.run_path(startup)
        finally:
            sys.argv = previous
        self.assertEqual(env["demo"].r_demo__add(20, 22), 42)
        with env["Counter"](20) as x:
            self.assertEqual(x.add(22), 42)
        with env["Series"](20, 2) as s:
            self.assertEqual(s.scaled_sum(2), 82)


suite = unittest.defaultTestLoader.loadTestsFromTestCase({
    "scalars": Scalars, "objects": Objects, "features": Features, "repl": Repl,
}[family])
result = unittest.TextTestRunner(verbosity=2).run(suite)
raise SystemExit(not result.wasSuccessful())
