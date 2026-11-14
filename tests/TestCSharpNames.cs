// The C# full API for tests/csharp_full_names.hpp, whose classes are named like the .NET types
// (Complex, List, Func, ...) that generated code and the runtime use.
using System;
using System.Linq;
using Shadow;

static class TestCSharpNames
{
    static int checks;
    static void Check(bool ok, string what)
    {
        checks++;
        if (!ok) throw new InvalidOperationException("check failed: " + what);
    }

    static int Main()
    {
        Check(Functions.Twice(new System.Numerics.Complex(1, 2)) == new System.Numerics.Complex(2, 4), "std::complex is System.Numerics.Complex");
        using (var c = new Complex()) Check(c.Norm() == 25 && Functions.NormOf(c) == 25, "the module's own Complex");
        using (var l = new List()) Check(l.Size() == 3, "the module's own List");
        Check(Functions.Range(3).SequenceEqual(new[] { 0.0, 1.0, 2.0 }), "std::vector is System.Collections.Generic.List");
        var counts = Functions.Counts(new[] { "a", "b", "a" });
        Check(counts["a"] == 2 && counts["b"] == 1, "std::map is a Dictionary");
        Check(Functions.Unique(new[] { 3, 1, 3 }).SetEquals(new[] { 1, 3 }), "std::set is a HashSet");
        Check(Functions.Apply(x => x * 10, 1.5) == 15, "std::function is System.Func");
        var seen = 0;
        Functions.Each(new[] { 1, 2, 3 }, v => seen += v);
        Check(seen == 6, "a void std::function is System.Action");
        Check(Functions.Pair() == (1, 2.5), "std::pair is a tuple");
        try { Functions.Fail(-1); Check(false, "no exception"); }
        catch (NativeException e) { Check(e.Kind == "invalid_argument", "NativeException is a System.Exception"); }
        using (var f = new Func()) { f.Calls = 2; Check(f.Calls == 2, "the module's own Func"); }
        Check(Functions.Total(new[] { 1.0, 2.5 }) == 3.5, "a converted parameter takes IEnumerable<double>");
        System.Collections.Generic.List<double> twice = Functions.Doubled(new[] { 1.0, 2.0 });
        Check(twice.SequenceEqual(new[] { 2.0, 4.0 }), "a converted result is a List<double>");
        Console.WriteLine($"C# names passed ({checks} checks)");
        return 0;
    }
}
