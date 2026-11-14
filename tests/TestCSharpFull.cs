// Full C# API (--csharp-api full) on the library-shaped example shared with the Python, R6, Lua,
// Common Lisp and Julia backends. Argument: the generated interface (Features.cs), for the
// documentation checks; the library is found through FEATURES_CSHARP_LIBRARY.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Features;
using static Features.Functions;

// Directors: C# subclasses implement C++ virtual methods.
class Triangle : Shape
{
    public double Base, Height;
    public Triangle(double b, double h) { Base = b; Height = h; }
    public override double Area() => Base * Height / 2;
    public override string Name() => "triangle";
}
class Incomplete : Shape { }
class Failing : Shape { public override double Area() => throw new InvalidOperationException("from C#"); }
// base.Name() runs the native implementation rather than dispatching back to the override.
class Loud : Circle
{
    public Loud(double radius) : base(radius) { }
    public override string Name() => base.Name().ToUpperInvariant();
}
class Noisy : Circle
{
    public Noisy(double radius) : base(radius) { }
    public override string Name() => base.Name().ToLowerInvariant() + "!";
}
class Louder : Noisy { public Louder(double radius) : base(radius) { } }
class UnitShape : Shape
{
    public object? Tag;
    public override double Area() => 1;
}
class Summer : Visitor
{
    public List<double>? Seen;
    public override double Visit(double value) => value * 10;
    public override void Finished(IEnumerable<double> values) => Seen = values.ToList();
}

static class Program
{
    static int checks;
    static void Check(bool ok, string what)
    {
        ++checks;
        if (!ok) throw new Exception("check failed: " + what);
    }
    static void Fails(Action action, string pattern)
    {
        ++checks;
        string? message = null;
        try { action(); }
        catch (Exception error) { message = error.Message; }
        if (message == null) throw new Exception($"expected an error containing \"{pattern}\", got none");
        if (!message.Contains(pattern)) throw new Exception($"expected an error containing \"{pattern}\", got: {message}");
    }
    static void Collect() { for (var i = 0; i < 2; ++i) { GC.Collect(); GC.WaitForPendingFinalizers(); } }

    static int Main(string[] args)
    {
        var source = File.ReadAllText(args[0]);
        string Doc(string declaration)
        {
            // The /// block right above the first line containing `declaration`.
            var lines = source.Split('\n');
            var at = Array.FindIndex(lines, line => line.Contains(declaration) && !line.TrimStart().StartsWith("///"));
            if (at < 0) throw new Exception("not declared: " + declaration);
            var block = new List<string>();
            for (var i = at - 1; i >= 0 && lines[i].TrimStart().StartsWith("///"); --i) block.Insert(0, lines[i].TrimStart().Substring(3).Trim());
            return string.Join("\n", block);
        }

        // Classes, inheritance and virtual dispatch.
        var c1 = new Circle(2);
        Check(c1.Area() == 12 && c1.Name() == "circle" && c1.TwiceArea() == 24 && c1.Radius() == 2, "circle methods");
        Check(c1.ToString().Contains("Circle"), "ToString names the class");
        Check(c1 is Shape && c1 is NativeObject, "class hierarchy");
        Fails(() => new Circle(-1), "negative radius");
        var s = new Square();
        Check(s.Side == 1 && s.Area() == 1, "default constructor");
        s.Side = 3;
        Check(s.Area() == 9 && new Square(2).Area() == 4, "field set");
        Check(AreaOf(s) == 9 && Describe(c1) == "circle:0", "functions on classes");
        // Enums.
        Check(c1.Kind() == Kind.circle && s.Kind() == Kind.square, "enum results");
        Check(Filled(Fill.solid) && !Filled(Fill.None), "enum arguments");
        Check(Enum.GetNames<Kind>().Contains("custom"), "enum members");
        Check(Shape.SidesUnknown == -1 && Math.Abs(Golden - 1.618) < 1e-12, "constants");

        // Most-derived results, ownership transfer and shared ownership.
        var made = MakeShape(Kind.circle, 1);
        Check(made is Circle circle && circle.Radius() == 1, "most-derived result");
        var canvas = new Canvas();
        canvas.Add(MakeShape(Kind.square, 2));
        canvas.Share(SharedSquare(3));
        var transferred = new Circle(1);
        canvas.Add(transferred);
        Fails(() => transferred.Area(), "closed");
        Check(canvas.Size() == 3 && canvas.Total() == 4 + 9 + 3, "ownership transfer");
        Check(canvas.At(0) is Square first && first.Area() == 4, "borrowed element");
        {
            var shared = SharedSquare(2);
            canvas.Share(shared);
            Check(shared!.Area() == 4 && canvas.Total() == 20, "shared ownership");
            Check(TotalArea(new Shape?[] { new Circle(1), shared }) == 7, "vector of objects");
        }

        // Overload resolution by C# type, checked again in C++.
        Check(Pick(1) == "int" && Pick(1.5) == "double" && Pick(2.0) == "double" && Pick("x") == "string", "scalar overloads");
        Check(Pick(new double[] { 1, 2 }) == "vector" && Pick(s) == "shape", "container and class overloads");
        Check(Add(1) == 111 && Add(1, 2) == 103 && Add(1, 2, 3) == 6, "default arguments");

        // Containers.
        var counts = CountWords(new[] { "a", "b", "a" });
        Check(counts["a"] == 2 && counts["b"] == 1, "dictionary result");
        Check(MinMax(new double[] { 3, 1, 2 }) == (1, 3), "tuple result");
        Fails(() => MinMax(Array.Empty<double>()), "empty input");
        Check(Find(new[] { 5, 7 }, 7) == 1 && Find(new[] { 5, 7 }, 9) == null, "optional result");
        var tr = Transpose(new[] { new double[] { 1, 2, 3 }, new double[] { 4, 5, 6 } });
        Check(tr.Count == 3 && tr[0].SequenceEqual(new double[] { 1, 4 }), "nested vectors");
        Check(ByteSum(new byte[] { 1, 2, 250 }) == 253 && BytesOf("AZ").SequenceEqual(new byte[] { 65, 90 }), "bytes");
        var cs = Circles(3);
        Check(cs.Count == 3 && cs[2].Radius() == 3, "vector of class results");
        Check(Repeat('z', 3) == "zzz" && Initial("hello") == 'h', "chars");
        // Mutable reference parameters are ref arguments.
        var values = new List<double> { 1, 2 };
        Scale(ref values, 3);
        Check(values.SequenceEqual(new double[] { 3, 6 }), "ref argument written back");
        Fails(() => Repeat('z', -1), "negative count");
        Check(Defaults() == "x*1-1.500000", "all defaults");
        Check(Doc("public static string Defaults(Kind kind, string label, bool loud)").Contains("<c>weight = 0.5</c>"), "omitted defaults named");

        // Callbacks both ways.
        Check(Apply(x => x * 2, 21) == 42, "callback");
        Fails(() => Apply(x => throw new InvalidOperationException("inside callback"), 1), "inside callback");
        // The C# exception comes back as itself.
        try { Apply(x => throw new DivideByZeroException(), 1); Check(false, "callback exception"); }
        catch (DivideByZeroException) { Check(true, "callback exception type"); }
        var plus = Adder(40);
        Check(plus(2) == 42, "native function as delegate");
        Check(Apply(x => Adder(1)((int)x), 3) == 4, "native function inside a callback");
        Check(Task.Run(() => Apply(x => x + 1, 1)).Result == 2, "callback from another thread");

        // Value type operators, chaining and borrowed references.
        {
            var v = new Vec2(1, 2);
            var w = v + new Vec2(3, 4);
            Check(w.X == 4 && w.Y == 6 && (v * 2).Y == 4 && (-v).X == -1 && (v - w).X == -3, "operators");
            Check(v == new Vec2(1, 2) && !(v == w) && v != w, "equality operators");
            Check(v[1] == 2 && v.Inner(new Vec2(1, 1)) == 3, "indexer");
            Fails(() => { var _ = v[2]; }, "Vec2 index");
            var chained = v.SetX(10).SetY(20);
            Check(v.X == 10 && v.Y == 20 && chained.X == 10, "chaining");
            Check(!(v == null) && (null as Vec2) == null, "null equality");
        }
        {
            var seg = new Segment();
            seg.End = new Vec2(3, 4);
            Check(seg.Length2() == 25, "field object set");
            var start = seg.First();
            start.X = 3;
            Check(seg.Start.X == 3 && seg.Length2() == 16, "borrowed reference writes through");
            seg.Start.Y = 4;
            Check(seg.Length2() == 0, "field object borrowed");
        }
        Check(new Vec2(1, 2).Clone().Y == 2, "clone");
        Check(Sum(new[] { new Vec2(1, 2), new Vec2(3, 4) }) == 10, "vector of values");
        {
            var held = new Segment().First();
            Collect();
            Check(held.X == 0, "borrowed reference keeps its owner alive");
        }

        // Templates, extensions, qualified names and copy types.
        {
            var stack = new IntStack();
            stack.Push(1); stack.Push(2);
            Check(stack.Size() == 2 && stack.Items().SequenceEqual(new[] { 1, 2 }) && stack.Pop() == 2, "template instance");
        }
        Check(ClampInt(42) == 10 && ClampInt(3, 5) == 3 && ClampReal(2.5) == 2.5, "function templates");
        Check(c1.Label() == "#circle" && c1.Label("@") == "@circle" && Circle.Unit().Radius() == 1, "extensions");
        Check(FeaturesLegacyVersion() == 2 && new FeaturesLegacyTag().Id == 7, "qualified names");
        Check(Enum.GetNames<FeaturesLegacyMode>().Contains("off"), "qualified enum");
        Check(new Circle(2).Diameter() == 4, "extension method");
        {
            var ledger = new Ledger();
            var copy = ledger.Latest();
            copy.Value = 5;
            Check(ledger.Stamp.Value == 1, "copy type");
        }
        {
            var aim = new Aim();
            aim.Origin.X = 2;
            Check(aim.Target.Point.X == 2, "nested borrowed fields");
        }
        Check(!typeof(Target).GetProperty("Point")!.CanWrite, "read-only field has no setter");

        // Errors, disposing and garbage collection.
        try { new Circle(-1); } catch (NativeException error) { Check(error.Kind == "invalid_argument", "exception kind"); }
        Fails(() => AreaOf(null!), "argument 'shape'");
        try { AreaOf(null!); } catch (NativeException error) { Check(error.Kind == "binding", "binding error kind"); }
        {
            var x = new Circle(1);
            Check(x.Area() == 3, "before dispose");
            x.Dispose();
            Check(x.IsDisposed, "disposed");
            Fails(() => x.Area(), "closed");
            x.Dispose();
        }
        for (var i = 1; i <= 200; ++i) using (var tmp = MakeShape(Kind.square, i)) Check(tmp!.Area() == i * i, "many objects");
        Collect();

        // Directors.
        {
            var t = new Triangle(4, 3);
            Check(t.Area() == 6 && AreaOf(t) == 6 && t.TwiceArea() == 12 && Describe(t) == "triangle:7", "C# override called from C++");
            Check(TotalArea(new Shape?[] { t, new Circle(1), new Square(2) }) == 13 && t.Label("@") == "@triangle", "mixed overrides");
        }
        Fails(() => AreaOf(new Incomplete()), "pure virtual method");
        Fails(() => AreaOf(new Failing()), "from C#");
        Check(Describe(new Loud(1)) == "CIRCLE:0" && new Loud(2).Area() == 12, "base call runs the C++ method");
        Check(Describe(new Louder(1)) == "circle!:0", "deeper subclasses");
        canvas = new Canvas();
        {
            var u = new UnitShape { Tag = "kept" };
            canvas.Add(u);
            Check(u.Area() == 1, "subclass object owned by C++");
        }
        Collect();
        canvas.Share(new Square(2));
        Check(canvas.Total() == 5 && canvas.Size() == 2, "C++ keeps the C# object alive");
        {
            var back = canvas.At(0);
            Check(back is UnitShape unit && (string?)unit.Tag == "kept" && ReferenceEquals(back, canvas.At(0)), "the same C# object comes back");
        }
        canvas.Clear();
        Collect();
        canvas.Share(new UnitShape());
        Collect();
        Check(canvas.Total() == 1, "shared subclass object");
        canvas.Clear();
        Collect();
        {
            var summer = new Summer();
            Check(RunVisitor(summer, new double[] { 1, 2 }) == 30 && summer.Seen!.SequenceEqual(new double[] { 1, 2 }), "callback interface");
        }
        Check(RunVisitor(new Visitor(), new double[] { 1, 2 }) == 3, "native default implementation");

        // XML documentation carries the C++ documentation.
        {
            var d = Doc("public static int Add(int a, int b, int c)");
            Check(d.Contains("Adds three integers, Socha &amp; Dorigo style."), "summary");
            Check(d.Contains("<param name=\"a\">first term</param>"), "parameter");
            Check(d.Contains("<returns>the sum</returns>"), "returns");
        }
        Check(Doc("public Circle(double radius)").Contains("<exception cref=\"NativeException\">"), "exceptions");
        Check(Doc("public double X").Contains("Horizontal component."), "field documentation");
        Check(Doc("public partial class Shape").Contains("derive a C# class from it"), "class remarks");

        // Policy modules: features::data functions live in the static class data.
        {
            var u = data.Unit();
            Check(u.X == 1 && u.Y == 0, "submodule result");
        }
        Check(data.Scaled(3) == 6 && data.Scaled(2.5) == 6.25, "submodule overloads");
        Check(data.Repeat("ab") == "abab" && Repeat('x', 3) == "xxx", "submodule names are separate");
        Check(Doc("public static Vec2 Unit()").Contains("Mapped by the policy's modules"), "submodule docs");
        Collect();
        Console.WriteLine($"C# full API passed ({checks} checks)");
        return 0;
    }
}
