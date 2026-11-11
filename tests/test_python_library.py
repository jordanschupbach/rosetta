import gc
import importlib.util
import sys
import unittest
import weakref

spec = importlib.util.spec_from_file_location("python_features", sys.argv[1])
f = importlib.util.module_from_spec(spec)
sys.modules["python_features"] = f
spec.loader.exec_module(f)


class Hierarchies(unittest.TestCase):
    def test_inheritance_and_downcasts(self):
        circle = f.Circle(2)
        self.assertIsInstance(circle, f.Shape)
        self.assertEqual(circle.area(), 12)
        self.assertEqual(circle.twice_area(), 24)  # inherited non-virtual method
        self.assertEqual(f.area_of(circle), 12)    # derived passed as base reference
        made = f.make_shape(f.Kind.circle, 1)
        self.assertIs(type(made), f.Circle)        # unique_ptr<Shape> returns the most-derived class
        self.assertEqual(made.radius(), 1)
        self.assertIs(type(f.shared_square(3)), f.Square)
        self.assertEqual(f.shared_square(3).area(), 9)
        with self.assertRaises(TypeError):
            f.Shape()                               # abstract
        with self.assertRaisesRegex(ValueError, "negative radius"):
            f.Circle(-1)
        self.assertEqual(f.Shape.sides_unknown, -1)

    def test_python_subclass_overrides_virtuals(self):
        class Triangle(f.Shape):
            def __init__(self, base, height):
                super().__init__()
                self.base, self.height = base, height

            def area(self):
                return self.base * self.height / 2

            def name(self):
                return "triangle"

        t = Triangle(4, 3)
        self.assertEqual(t.area(), 6)
        self.assertEqual(f.area_of(t), 6)            # C++ calls back into Python
        self.assertEqual(t.twice_area(), 12)         # C++ method calling a Python override
        self.assertEqual(f.describe(t), "triangle:7")  # kind() falls back to the C++ default
        self.assertEqual(f.total_area([t, f.Circle(1), f.Square(2)]), 13)
        self.assertEqual(t.label("@"), "@triangle")  # extension method sees the override

        class Incomplete(f.Shape):
            pass

        with self.assertRaisesRegex(NotImplementedError, "area"):
            f.area_of(Incomplete())

        class Failing(f.Shape):
            def area(self):
                raise KeyError("from python")

        with self.assertRaises(KeyError):
            f.area_of(Failing())

        class Wrong(f.Shape):
            def area(self):
                return "not a number"

        with self.assertRaises(TypeError):
            f.area_of(Wrong())

    def test_super_calls_native_implementation(self):
        class Loud(f.Circle):
            def name(self):
                return super().name().upper()

        self.assertEqual(f.describe(Loud(1)), "CIRCLE:0")

    def test_ownership_transfer(self):
        class Unit(f.Shape):
            def area(self):
                return 1.0

        canvas = f.Canvas()
        circle = f.Circle(1)
        canvas.add(circle)
        with self.assertRaisesRegex(RuntimeError, "moved"):
            circle.area()
        unit = Unit()
        ref = weakref.ref(unit)
        canvas.add(unit)                      # C++ now owns the Python implementation
        del unit
        gc.collect()
        self.assertIsNotNone(ref())
        canvas.share(f.Square(2))
        self.assertEqual(canvas.total(), 3 + 1 + 4)
        self.assertEqual(canvas.size(), 3)
        self.assertIsInstance(canvas.at(1), Unit)  # the same Python object comes back
        canvas.clear()
        gc.collect()
        self.assertIsNone(ref())

    def test_borrowed_references_keep_owner_alive(self):
        segment = f.Segment()
        first = segment.first()
        first.x = 5
        self.assertEqual(segment.start.x, 5)
        segment.end.y = 2                     # field access returns a live reference
        self.assertEqual(segment.length2(), 25 + 4)
        ref = weakref.ref(segment)
        del segment
        gc.collect()
        self.assertIsNotNone(ref())           # first keeps the segment alive
        self.assertEqual(first.x, 5)


class Calls(unittest.TestCase):
    def test_overloads(self):
        self.assertEqual(f.pick(1), "int")
        self.assertEqual(f.pick(1.5), "double")
        self.assertEqual(f.pick("a"), "string")
        self.assertEqual(f.pick([1.0, 2.0]), "vector")
        self.assertEqual(f.pick([1, 2]), "vector")
        self.assertEqual(f.pick(f.Square()), "shape")
        with self.assertRaisesRegex(TypeError, "supported signatures"):
            f.pick(None)

    def test_defaults_and_keywords(self):
        self.assertEqual(f.add(1), 111)
        self.assertEqual(f.add(1, 2), 103)
        self.assertEqual(f.add(1, 2, 3), 6)
        self.assertEqual(f.add(a=1, b=2), 103)
        self.assertEqual(f.add(1, b=0, c=0), 1)
        with self.assertRaises(TypeError):
            f.add(1, c=0)  # C++ defaults cannot skip b

    def test_containers(self):
        values = [1.0, 2.0, 3.0]
        f.scale(values, 2)
        self.assertEqual(values, [2.0, 4.0, 6.0])  # mutable references write back
        self.assertEqual(f.count_words(["a", "b", "a"]), {"a": 2, "b": 1})
        self.assertEqual(f.min_max([3, 1, 2]), (1.0, 3.0))
        with self.assertRaises(IndexError):
            f.min_max([])
        self.assertEqual(f.find([5, 6, 7], 6), 1)
        self.assertIsNone(f.find([5, 6, 7], 8))
        self.assertEqual(f.transpose([[1, 2], [3, 4]]), [[1.0, 3.0], [2.0, 4.0]])
        self.assertEqual(f.transpose((x for x in [])) if False else f.transpose([]), [])
        circles = f.circles(3)
        self.assertEqual([c.radius() for c in circles], [1, 2, 3])

    def test_vectors_pairs_and_chars(self):
        values = f.circles(2)
        self.assertEqual(values.size(), 2)              # sequence results are std::vector-like lists
        values.push_back(f.Circle(5))
        values.erase(0)
        self.assertEqual(values.size(), 2)
        self.assertEqual(values.back().radius(), 5)
        lo_hi = f.min_max([3, 1, 2])
        self.assertEqual((lo_hi.first, lo_hi.second), (1.0, 3.0))
        self.assertEqual(lo_hi, (1.0, 3.0))
        self.assertEqual(f._Pair(1, 2).second, 2)
        self.assertEqual(f.repeat("ab"[0], 3), "aaa")
        self.assertEqual(f.initial("xyz"), "x")
        with self.assertRaises(TypeError):
            f.repeat(7, 1)
        with self.assertRaises(RuntimeError):           # policy maps this function's errors
            f.repeat("a", -1)
        with self.assertRaises(ValueError):             # default mapping elsewhere
            f.Circle(-1)

    def test_thisown(self):
        circle = f.Circle(1)
        self.assertTrue(circle.thisown)
        self.assertFalse(f.Segment().first().thisown)  # a borrowed reference

    def test_callbacks(self):
        self.assertEqual(f.apply(lambda x: x * 3, 2), 6)
        with self.assertRaises(ZeroDivisionError):
            f.apply(lambda x: 1 / 0, 2)
        add5 = f.adder(5)
        self.assertEqual(add5(1), 6)
        self.assertEqual(f.apply(f.adder(1), 2.0) if False else add5(10), 15)

        class Summer(f.Visitor):
            def __init__(self):
                super().__init__()
                self.seen = None

            def visit(self, value):
                return value * 10

            def finished(self, values):
                self.seen = list(values)

        summer = Summer()
        self.assertEqual(f.run_visitor(summer, [1, 2]), 30)
        self.assertEqual(summer.seen, [1.0, 2.0])
        self.assertEqual(f.run_visitor(f.Visitor(), [1, 2]), 3)


class Values(unittest.TestCase):
    def test_enums(self):
        self.assertEqual(f.Kind.square.value, 1)
        self.assertEqual(f.Kind_custom, f.Kind.custom)  # SWIG-style constant
        self.assertEqual(f.legacy_b, 2)                 # unscoped enumerator
        self.assertEqual(f.Fill._None, 0)               # keyword enumerators: SWIG's names
        self.assertFalse(f.filled(f.Fill__None))
        self.assertIs(f.Square().kind(), f.Kind.square)
        self.assertIs(type(f.make_shape(1, 2)), f.Square)  # ints convert to enums

    def test_operators_and_chaining(self):
        a, b = f.Vec2(1, 2), f.Vec2(3, 4)
        self.assertEqual((a + b).x, 4)
        self.assertEqual((b - a).y, 2)
        self.assertEqual((a * 2).y, 4)
        self.assertEqual((-a).x, -1)
        self.assertTrue(a == f.Vec2(1, 2))
        self.assertFalse(a == "other")
        self.assertEqual(a[1], 2)
        with self.assertRaises(IndexError):
            a[2]
        c = f.Vec2()
        self.assertIs(c.set_x(3).set_y(4), c)  # methods returning *this return self
        c += a
        self.assertEqual((c.x, c.y), (4, 6))
        self.assertEqual(a.inner(b), 11)       # renamed
        self.assertFalse(hasattr(a, "dot"))

    def test_templates_extensions_constants(self):
        stack = f.IntStack()
        stack.push(1)
        stack.push(2)
        self.assertEqual(stack.items(), [1, 2])
        self.assertEqual(stack.pop(), 2)
        self.assertEqual(stack.size(), 1)
        self.assertEqual(f.Circle(1).label(), "#circle")
        self.assertEqual(f.Circle.unit().radius(), 1)
        self.assertAlmostEqual(f.golden, 1.618)
        self.assertFalse(hasattr(f, "borrowed_shape"))
        self.assertFalse(hasattr(f, "shape_label"))

    def test_docstrings(self):
        self.assertIn("Area of the shape.", f.Shape.area.__doc__)
        self.assertIn("pick(arg0: int) -> str", f.pick.__doc__)
        self.assertIn("Abstract shape", f.Shape.__doc__)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0], "-v"])
