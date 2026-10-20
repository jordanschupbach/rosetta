using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text;
using D = CSharpDemo;
using V = CSharpVerify;
using E = EnumApi;
using Mode = EnumApi.enum_test__Mode;
using Big = EnumApi.enum_test__Big;
using Signed = EnumApi.enum_test__Signed;

static class TestCSharp {
    static void Check(bool value) { if (!value) throw new Exception("check failed"); }
    static void Fails<T>(Action action, string text) where T : Exception {
        try { action(); }
        catch (Exception error) {
            if (error is TargetInvocationException) error = error.InnerException!;
            if (error is T && error.Message.Contains(text)) return;
            throw new Exception($"expected {typeof(T).Name} containing '{text}'", error);
        }
        throw new Exception($"expected {typeof(T).Name}: {text}");
    }
    static object Call(string name, object value) => typeof(V).GetMethod("r_verify__" + name)!.Invoke(null, [value])!;
    static void Scalars() {
        string[] signed = ["schar", "short", "int", "ll"];
        string[] unsigned = ["uchar", "ushort", "uint", "ull"];
        int[] widths = [8, 16, 32, 64];
        for (int i = 0; i < widths.Length; ++i) {
            int bits = widths[i]; string name = "echo_" + signed[i];
            long low = bits == 64 ? long.MinValue : -(1L << (bits - 1));
            long high = bits == 64 ? long.MaxValue : (1L << (bits - 1)) - 1;
            Check((long)Call(name, low) == low && (long)Call(name, high) == high);
            if (bits < 64) {
                Fails<ArgumentException>(() => Call(name, low - 1), "range");
                Fails<ArgumentException>(() => Call(name, high + 1), "range");
            }
            name = "echo_" + unsigned[i];
            ulong maximum = bits == 64 ? ulong.MaxValue : (1UL << bits) - 1;
            Check((ulong)Call(name, maximum) == maximum);
            if (bits < 64) Fails<ArgumentException>(() => Call(name, maximum + 1), "range");
        }
        Check(V.r_verify__echo_char(42) == 42 && V.r_verify__echo_long(42) == 42);
        Check(V.r_verify__echo_ulong(42) == 42 && V.r_verify__alias_identity(42) == 42);
        Check(Rosetta.Bindings.NamespacedApi.r_demo__add(20, 22) == 42);
        Check(D.r_demo__too_large() == (1UL << 53) + 1);
        Check(V.r_verify__echo_bool(true) && !V.r_verify__echo_bool(false));
        Check(V.r_verify__echo_float(1.25f) == 1.25f && V.r_verify__echo_double(double.MaxValue) == double.MaxValue);
        Fails<ArgumentException>(() => V.r_verify__echo_float(float.PositiveInfinity), "finite");
        Fails<ArgumentException>(() => V.r_verify__echo_double(double.NaN), "finite");
        Fails<ArgumentException>(() => V.r_verify__nonfinite_result(), "non-finite");
        byte[][] strings = [[], Encoding.UTF8.GetBytes("世界😀"), [97, 0, 98], [255, 254], new byte[100000]];
        foreach (byte[] bytes in strings) {
            var result = V.r_verify__echo_string(bytes);
            Check(!ReferenceEquals(bytes, result) && bytes.SequenceEqual(result));
        }
        Check(V.r_verify__embedded_nul().SequenceEqual(new byte[] {97, 0, 98}));
        Fails<ArgumentException>(() => V.r_verify__echo_string(null!), "non-null");
        Check(V.r_verify__defaulted() == 42 && V.r_verify__defaulted(12) == 12);
        V.r_verify__do_nothing();
        for (int i = 0; i < 100; ++i) {
            Fails<InvalidOperationException>(() => D.r_demo__fail(), "native failure");
            Fails<InvalidOperationException>(() => V.r_verify__unknown_error(), "unknown C++ exception");
            Fails<OutOfMemoryException>(() => V.r_verify__allocation_error(), "allocation");
            Check(D.r_demo__add(20, 22) == 42);
        }
        for (int i = 0; i < 1000; ++i) Check(V.r_verify__echo_string(strings[1]).SequenceEqual(strings[1]));
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static WeakReference Abandoned() => new(new D.r_demo__Counter(1));
    static void Objects() {
        Check(D.r_demo__Counter.live() == 0);
        Fails<InvalidOperationException>(() => new D.r_demo__Counter(-1), "negative initial");
        Check(D.r_demo__Counter.live() == 0);
        var counter = new D.r_demo__Counter(20); var alias = counter;
        Check(counter.add(22) == 42 && counter.count == 42 && counter.initial == 20);
        Check(D.r_demo__Counter.answer() == 42);
        counter.count = 7; Check(alias.value() == 7);
        var label = Encoding.UTF8.GetBytes("C# 😀\0"); counter.label = label;
        Check(alias.label.SequenceEqual(label));
        Fails<ArgumentException>(() => counter.count = long.MaxValue, "range");
        Fails<ArgumentException>(() => counter.label = null!, "non-null");
        Check(counter.value() == 7 && counter.label.SequenceEqual(label));
        Check(typeof(D.r_demo__Counter).GetProperty("initial")!.SetMethod == null);
        counter.Dispose(); counter.Dispose(); Check(D.r_demo__Counter.live() == 0);
        Fails<ObjectDisposedException>(() => alias.value(), "");
        Fails<ObjectDisposedException>(() => _ = alias.count, "");
        Fails<ObjectDisposedException>(() => alias.count = 1, "");
        var weak = Abandoned();
        for (int i = 0; i < 100 && (weak.IsAlive || D.r_demo__Counter.live() != 0); ++i) {
            GC.Collect(); GC.WaitForPendingFinalizers(); Thread.Sleep(10);
        }
        Check(!weak.IsAlive && D.r_demo__Counter.live() == 0);
        var racing = new D.r_demo__Counter(1);
        var jobs = Enumerable.Range(0, 20).Select(_ => Task.Run(() => {
            try { racing.add(1); } catch (ObjectDisposedException) { }
        })).ToArray();
        racing.Dispose(); Task.WaitAll(jobs);
        Check(D.r_demo__Counter.live() == 0);
        using (var c = new D.r_demo__Counter(1)) Check(c.value() == 1);
        Check(D.r_demo__Counter.live() == 0);
    }
    static V.r_verify__AsyncTask? pending;
    static void Features() {
        Check(V.r_verify__maximum(20, 42) == 42 && V.r_verify__template_box(42) == 42);
        Check(V.r_verify__variadic_sum(10, 20, 12) == 42 && V.r_verify__constant_answer() == 42);
        Check(V.r_verify__constrained_sum(20, 22) == 42 && V.r_verify__hierarchy_value(42) == 42);
        Check(V.r_verify__hierarchy_crosscast() && V.r_verify__rosetta_r_c_identity(42) == 42);
        Fails<OverflowException>(() => V.r_verify__constrained_sum(int.MaxValue, 1), "overflow");
        Fails<IndexOutOfRangeException>(() => new V.r_verify__Series(0, -1), "series count");
        using (var series = new V.r_verify__Series()) {
            Check(series.size() == 2 && series.get() == 20 && series.scaled_sum(2) == 82);
            Check(Encoding.UTF8.GetString(series.text()) == "[20, 21]");
            series.set(1, 22); Check(series.scaled_sum(1) == 42);
            Fails<IndexOutOfRangeException>(() => series.get(2), "out of range");
        }
        string[][] expected = [["m","z","a"],["a","z","m"],["z","m","a"],["a","m","z"]];
        for (int order = 1; order <= 4; ++order) {
            using var queue = new V.r_verify__Worklist(order);
            foreach (string text in new[] {"a", "z", "m"}) queue.push(Encoding.UTF8.GetBytes(text));
            Check(queue.size() == 3);
            foreach (string text in expected[order - 1]) Check(Encoding.UTF8.GetString(queue.pop()) == text);
            Fails<IndexOutOfRangeException>(() => queue.pop(), "empty");
        }
        Fails<ArgumentException>(() => new V.r_verify__Worklist(0), "unknown worklist order");
        using (var task = new V.r_verify__AsyncTask(21, 0, false)) Check(task.wait(2000) && task.result() == 42 && task.result() == 42);
        using (var task = new V.r_verify__AsyncTask(1, 0, true)) {
            Check(task.wait(2000)); Fails<InvalidOperationException>(() => task.result(), "async worker failure");
        }
        using (var task = new V.r_verify__AsyncTask(1, 60000, false)) {
            Check(!task.ready() && !task.wait(0) && task.cancel() && task.wait(2000));
            Fails<InvalidOperationException>(() => task.result(), "cancelled");
        }
        Fails<IndexOutOfRangeException>(() => new V.r_verify__AsyncTask(1, -1, false), "delay");
        using (var task = new V.r_verify__AsyncTask(1, 60000, false)) { }
        Check(V.r_verify__AsyncTask.workers() == 0);
        pending = new V.r_verify__AsyncTask(1, 60000, false);
    }
    static void Enums() {
        Check((long)Mode.low == -2 && (long)Mode.next == -1 && (long)Mode.zero == 0);
        Check(Mode.answer == Mode.alias && (long)Mode.answer == 42);
        foreach (var value in Enum.GetValues<Mode>()) Check(E.enum_test__echo(value) == value);
        Check(E.enum_test__echo() == Mode.answer);
        Check(E.enum_test__echo_plain(E.enum_test__Plain.third) == E.enum_test__Plain.third);
        Check((ulong)Big.maximum == ulong.MaxValue && E.enum_test__echo_big(Big.maximum) == Big.maximum);
        Check((long)Signed.minimum == long.MinValue && (long)Signed.next == long.MinValue + 1);
        Check(E.enum_test__echo_signed(Signed.minimum) == Signed.minimum);
        Check(E.enum_test__echo_signed(Signed.maximum) == Signed.maximum);
        Check(E.enum_test__echo_bool(E.enum_test__Boolean.yes) == E.enum_test__Boolean.yes);
        Check(E.enum_test__echo_bool(E.enum_test__Boolean.no) == E.enum_test__Boolean.no);
        using var holder = new E.enum_test__Holder();
        Check(holder.mode == Mode.answer && holder.initial == Mode.answer);
        holder.mode = Mode.next; Check(holder.get() == Mode.next);
        holder.set(Mode.low); Check(holder.mode == Mode.low);
        Check(E.enum_test__Holder.identity(Mode.zero) == Mode.zero);
        Check(typeof(E.enum_test__Holder).GetProperty("initial")!.SetMethod == null);
        Fails<ArgumentException>(() => E.enum_test__echo((Mode)9), "unknown enum value");
        Fails<ArgumentException>(() => E.enum_test__echo((Mode)long.MaxValue), "range");
        Fails<ArgumentException>(() => E.enum_test__echo_big((Big)1UL), "unknown enum value");
        Fails<ArgumentException>(() => E.enum_test__echo_bool((E.enum_test__Boolean)2), "unknown enum value");
        Fails<ArgumentException>(() => E.enum_test__invalid_result(), "unknown enum value");
        Fails<ArgumentException>(() => holder.mode = (Mode)9, "unknown enum value");
        Fails<ArgumentException>(() => new E.enum_test__Holder((Mode)9), "unknown enum value");
        Check(holder.mode == Mode.low && E.enum_test__echo() == Mode.answer);
        using var explicitHolder = new E.enum_test__Holder(Mode.zero);
        Check(explicitHolder.initial == Mode.zero);
        holder.Dispose();
        Fails<ObjectDisposedException>(() => holder.get(), "");
    }
    public static void Main(string[] args) {
        switch (args[0]) {
            case "scalars": Scalars(); break;
            case "objects": Objects(); break;
            case "features": Features(); break;
            case "enums": Enums(); break;
            default: throw new Exception("unknown family");
        }
        Console.WriteLine($"C# {args[0]} checks passed");
        GC.KeepAlive(pending);
    }
}
