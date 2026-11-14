#pragma once

// C# side of the full C# backend (--csharp-api full): emitted into every generated interface. It
// speaks the value encoding of lisp_full_runtime.hpp (the C++ runtime the full C ABI backends
// share): C# encodes a call's arguments into one buffer, calls the module's single entry point,
// and decodes the result into plain values that Rosetta.To converts to the declared C# type.
// @NS@ is the C# namespace, @C@ the prefix of the native entry points, @LIB@ the shared
// library's base name and @ENV@ the environment variable that may name the library file.
namespace rosetta::detail {
// The public half (namespace @NS@); it names .NET types with global::, since generated types share it.
inline constexpr const char* csharp_full_runtime_cs = R"cs(
/// <summary>
/// A C++ exception, or a failed conversion, in a call to native module @NS@.
/// </summary>
/// <remarks>
/// <see cref="Kind"/> is <c>binding</c> (conversion, overload or disposed-object errors),
/// <c>invalid_argument</c>, <c>out_of_range</c>, <c>domain_error</c>, <c>length_error</c>,
/// <c>arithmetic_error</c>, <c>allocation</c> or <c>native</c>.
/// </remarks>
public class NativeException : global::System.Exception
{
    /// <summary>What failed: the C++ exception's kind, or <c>binding</c>.</summary>
    public string Kind { get; }
    /// <summary>A native error of kind <paramref name="kind"/>.</summary>
    public NativeException(string kind, string message) : base(message) { Kind = kind; }
}

/// <summary>Marks the constructors that wrap an existing C++ object (used by the binding).</summary>
[global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]
public readonly struct NativeRaw { }

/// <summary>
/// Base class of the classes of native module @NS@: each instance owns (or borrows) a C++ object.
/// </summary>
/// <remarks>
/// <see cref="Dispose"/> releases the C++ object now; otherwise the garbage collector releases it.
/// A C# class deriving from a native class overrides its virtual methods, also when C++ calls them.
/// </remarks>
public abstract class NativeObject : global::System.IDisposable
{
    internal ulong RosettaHolder;
    internal object? RosettaParent;
    internal ulong RosettaId;
    internal bool RosettaOwns;
    internal NativeObject(NativeRaw raw) { }
    /// <summary>Whether the C++ object is released (or was never created).</summary>
    public bool IsDisposed => RosettaHolder == 0;
    /// <summary>Releases the C++ object now.</summary>
    public void Dispose() { Rosetta.Close(this); global::System.GC.SuppressFinalize(this); }
    /// <summary>Releases the C++ object.</summary>
    ~NativeObject() { Rosetta.ReleaseLater(this); }
    /// <summary>The class name.</summary>
    public override string ToString() => GetType().Name + (RosettaHolder == 0 ? "(disposed)" : "()");
}

/// <summary>A C++ <c>std::function</c> returned to C#; convert it to a delegate type, or call <see cref="Invoke"/>.</summary>
public sealed class NativeFunction : NativeObject
{
    internal NativeFunction(NativeRaw raw) : base(raw) { }
    /// <summary>Calls the C++ function.</summary>
    public object? Invoke(params object?[] arguments)
    {
        var all = new object?[arguments.Length + 1];
        all[0] = new Rosetta.Callee(this);
        global::System.Array.Copy(arguments, 0, all, 1, arguments.Length);
        return Rosetta.Call(0, all);
    }
}
)cs";

// The internal half (namespace @NS@.RosettaRuntime, whose using directives come before the
// module's type names, so a generated class named like a .NET type does not hide it).
inline constexpr const char* csharp_full_runtime_internal_cs = R"cs(
internal static partial class Rosetta
{
    internal sealed class Symbol { internal readonly string Name; internal Symbol(string name) { Name = name; } public override string ToString() => Name; }
    internal sealed class Callee { internal readonly NativeFunction F; internal Callee(NativeFunction f) { F = f; } }
    // A ref argument: C++ writes its new value back into the slot.
    internal sealed class Slot { internal object? Value; internal Slot(object? value) { Value = value; } }

    // ------------------------------------------------------------ native library
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int CallFn(uint index, IntPtr input, UIntPtr size, out IntPtr output, out UIntPtr outputSize);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void FreeFn(IntPtr pointer);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate IntPtr AllocFn(UIntPtr size);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void ReleaseFn(IntPtr holder, int now);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int InitFn(int version, IntPtr invoke, IntPtr retain);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int InvokeFn(int kind, ulong id, uint generic, IntPtr data, UIntPtr size, IntPtr result, IntPtr resultSize);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void RetainFn(ulong id, int delta);
    static CallFn? callFn;
    static FreeFn? freeFn;
    static AllocFn? allocFn;
    static ReleaseFn? releaseFn;
    static readonly InvokeFn invokeHook = InvokeCallback;
    static readonly RetainFn retainHook = (id, delta) => Adjust(id, delta);

    static Rosetta()
    {
        var library = NativeLibrary.Load(LibraryFile(), typeof(Rosetta).Assembly, null);
        T Export<T>(string name) where T : Delegate => Marshal.GetDelegateForFunctionPointer<T>(NativeLibrary.GetExport(library, name));
        callFn = Export<CallFn>("@C@_call");
        freeFn = Export<FreeFn>("@C@_free");
        allocFn = Export<AllocFn>("@C@_alloc");
        releaseFn = Export<ReleaseFn>("@C@_release");
        var init = Export<InitFn>("@C@_init");
        if (init(1, Marshal.GetFunctionPointerForDelegate(invokeHook), Marshal.GetFunctionPointerForDelegate(retainHook)) != 0)
            throw new InvalidOperationException("native module @NS@ was generated by a different Rosetta version");
    }
    // @ENV@, else @LIB@ beside the assembly (or in lib/, ../lib), else the system's search.
    static string LibraryFile()
    {
        var variable = Environment.GetEnvironmentVariable("@ENV@");
        if (!string.IsNullOrEmpty(variable)) return variable;
        string[] names = OperatingSystem.IsWindows() ? new[] { "@LIB@.dll" }
            : OperatingSystem.IsMacOS() ? new[] { "@LIB@.dylib", "lib@LIB@.dylib", "@LIB@.so" } : new[] { "@LIB@.so", "lib@LIB@.so" };
        var directories = new List<string>();
        var assembly = Path.GetDirectoryName(typeof(Rosetta).Assembly.Location);
        foreach (var directory in new[] { AppContext.BaseDirectory, assembly })
            if (!string.IsNullOrEmpty(directory)) { directories.Add(directory); directories.Add(Path.Combine(directory, "lib")); directories.Add(Path.Combine(directory, "..", "lib")); }
        foreach (var directory in directories)
            foreach (var name in names)
            {
                var file = Path.Combine(directory, name);
                if (File.Exists(file)) return file;
            }
        return "@LIB@";
    }

    // ------------------------------------------------------------ C# values C++ refers to
    // Delegates passed as callbacks and subclass objects: ids index these tables. C++ references
    // keep entries strong (retain/release); the rest are weak.
    static readonly object gate = new();
    static ulong next;
    static readonly Dictionary<ulong, WeakReference> weak = new();
    static readonly Dictionary<ulong, (object Value, int Count)> strong = new();
    static ulong NewId()
    {
        lock (gate)
        {
            if (++next % 4096 == 0)
                foreach (var key in weak.Where(entry => !entry.Value.IsAlive).Select(entry => entry.Key).ToList()) weak.Remove(key);
            return next;
        }
    }
    internal static ulong ObjectId(NativeObject o)
    {
        if (o.RosettaId != 0) return o.RosettaId;
        var id = NewId();
        lock (gate) weak[id] = new WeakReference(o);
        o.RosettaId = id;
        return id;
    }
    static object Lookup(ulong id)
    {
        lock (gate)
        {
            if (strong.TryGetValue(id, out var entry)) return entry.Value;
            if (weak.TryGetValue(id, out var reference) && reference.Target is { } target) return target;
        }
        throw new NativeException("binding", "the C# object of a native reference is gone");
    }
    static void Adjust(ulong id, int delta)
    {
        lock (gate)
        {
            var found = strong.TryGetValue(id, out var entry);
            if (delta > 0)
            {
                if (found) strong[id] = (entry.Value, entry.Count + delta);
                else if (weak.TryGetValue(id, out var reference) && reference.Target is { } target) strong[id] = (target, delta);
            }
            else if (found)
            {
                var count = entry.Count + delta;
                if (count <= 0) strong.Remove(id); else strong[id] = (entry.Value, count);
            }
        }
    }

    // ------------------------------------------------------------ encoding (C# -> C++)
    sealed class Out
    {
        internal readonly MemoryStream Stream = new();
        internal readonly BinaryWriter W;
        internal readonly List<ulong> Functions = new();
        internal readonly List<Slot> Slots = new();
        internal Out() { W = new BinaryWriter(Stream); }
        internal void Text(string s) { var bytes = Encoding.UTF8.GetBytes(s); W.Write((ulong)bytes.Length); W.Write(bytes); }
    }
    static void Put(Out o, object? x, int depth = 0)
    {
        if (depth > 10000) throw new NativeException("binding", "value nesting is too deep");
        var w = o.W;
        switch (x)
        {
            case null: w.Write((byte)0); return;
            case bool b: w.Write((byte)(b ? 1 : 19)); return;
            case sbyte or byte or short or ushort or int or uint or long: w.Write((byte)2); w.Write(Convert.ToInt64(x)); return;
            case ulong u:
                if (u <= long.MaxValue) { w.Write((byte)2); w.Write((long)u); } else { w.Write((byte)3); o.Text(u.ToString(CultureInfo.InvariantCulture)); }
                return;
            case BigInteger big:
                if (big >= long.MinValue && big <= long.MaxValue) { w.Write((byte)2); w.Write((long)big); } else { w.Write((byte)3); o.Text(big.ToString(CultureInfo.InvariantCulture)); }
                return;
            case float or double or decimal: w.Write((byte)4); w.Write(Convert.ToDouble(x, CultureInfo.InvariantCulture)); return;
            case Complex c: w.Write((byte)18); w.Write(c.Real); w.Write(c.Imaginary); return;
            case string s: w.Write((byte)5); o.Text(s); return;
            case char ch: w.Write((byte)5); o.Text(ch.ToString()); return;
            case Symbol symbol: w.Write((byte)14); o.Text(symbol.Name); return;
            case Enum e:
                if (Enum.IsDefined(e.GetType(), e)) { w.Write((byte)14); o.Text(e.ToString()); }
                else { w.Write((byte)2); w.Write(Convert.ToInt64(e, CultureInfo.InvariantCulture)); }
                return;
            case Callee callee: PutObject(o, callee.F); return;
            case NativeObject native: PutObject(o, native); return;
            case Slot slot: o.Slots.Add(slot); w.Write((byte)13); w.Write((uint)(o.Slots.Count - 1)); Put(o, slot.Value, depth + 1); return;
            case Delegate f:
            {
                var id = NewId();
                lock (gate) strong[id] = (f, 1);
                o.Functions.Add(id);
                w.Write((byte)12); w.Write(id);
                return;
            }
            case byte[] bytes: w.Write((byte)15); w.Write((ulong)bytes.Length); w.Write(bytes); return;
            case double[] reals: w.Write((byte)7); w.Write((ulong)reals.Length); foreach (var v in reals) w.Write(v); return;
            case List<double> list: w.Write((byte)7); w.Write((ulong)list.Count); foreach (var v in list) w.Write(v); return;
            case IDictionary dictionary:
                w.Write((byte)9); w.Write((ulong)dictionary.Count);
                foreach (DictionaryEntry entry in dictionary) { Put(o, entry.Key, depth + 1); Put(o, entry.Value, depth + 1); }
                return;
            case ITuple tuple:
                w.Write((byte)6); w.Write((ulong)tuple.Length);
                for (var i = 0; i < tuple.Length; ++i) Put(o, tuple[i], depth + 1);
                return;
        }
        var type = x.GetType();
        if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(KeyValuePair<,>))
        {
            w.Write((byte)8);
            Put(o, type.GetProperty("Key")!.GetValue(x), depth + 1);
            Put(o, type.GetProperty("Value")!.GetValue(x), depth + 1);
            return;
        }
        if (x is IEnumerable sequence)
        {
            var items = sequence.Cast<object?>().ToList();
            if (items.Count > 0 && items.All(item => item is double)) { w.Write((byte)7); w.Write((ulong)items.Count); foreach (var v in items) w.Write((double)v!); return; }
            w.Write((byte)6); w.Write((ulong)items.Count);
            foreach (var item in items) Put(o, item, depth + 1);
            return;
        }
        throw new NativeException("binding", $"cannot pass a value of type {type.Name} to C++");
    }
    static void PutObject(Out o, NativeObject x)
    {
        o.W.Write((byte)10); o.W.Write(x.RosettaHolder); o.W.Write(x.RosettaHolder == 0 ? 0UL : ObjectId(x));
    }

    // ------------------------------------------------------------ decoding (C++ -> C#)
    sealed class In
    {
        readonly byte[] data;
        int at;
        internal In(byte[] bytes) { data = bytes; }
        internal bool Empty => data.Length == 0;
        internal byte Peek() => data[at];
        void Need(int n) { if (at + n > data.Length) throw new NativeException("binding", "malformed native value"); }
        internal byte U8() { Need(1); return data[at++]; }
        internal uint U32() { Need(4); var v = BitConverter.ToUInt32(data, at); at += 4; return v; }
        internal ulong U64() { Need(8); var v = BitConverter.ToUInt64(data, at); at += 8; return v; }
        internal long I64() { Need(8); var v = BitConverter.ToInt64(data, at); at += 8; return v; }
        internal double F64() { Need(8); var v = BitConverter.ToDouble(data, at); at += 8; return v; }
        internal int Count() { var n = U64(); if (n > (ulong)(data.Length - at)) throw new NativeException("binding", "malformed native value"); return (int)n; }
        internal byte[] Bytes(int n) { Need(n); var v = new byte[n]; Array.Copy(data, at, v, 0, n); at += n; return v; }
        internal string Text() { var n = Count(); var s = Encoding.UTF8.GetString(data, at, n); at += n; return s; }
    }
    static NativeObject Wrap(ulong address, uint klass, object? parent)
    {
        if (klass >= Classes.Length) throw new NativeException("binding", "C++ result type is not exported to C#");
        var o = Classes[klass]();
        Adopt(o, address, parent);
        return o;
    }
    static void Adopt(NativeObject o, ulong address, object? parent)
    {
        o.RosettaHolder = address;
        o.RosettaParent = parent;
        o.RosettaOwns = true;
    }
    static object? Get(In r, object?[]? arguments)
    {
        var tag = r.U8();
        switch (tag)
        {
            case 0: return null;
            case 1: return true;
            case 19: return false;
            case 2: return r.I64();
            case 3: { var n = BigInteger.Parse(r.Text(), CultureInfo.InvariantCulture); return n >= 0 && n <= ulong.MaxValue ? (object)(ulong)n : n; }
            case 4: return r.F64();
            case 18: return new Complex(r.F64(), r.F64());
            case 5: return r.Text();
            case 14: return new Symbol(r.Text());
            case 15: return r.Bytes(r.Count());
            case 6: { var n = r.Count(); var list = new List<object?>(n); for (var i = 0; i < n; ++i) list.Add(Get(r, arguments)); return list; }
            case 16: { var n = r.Count(); var items = new object?[n]; for (var i = 0; i < n; ++i) items[i] = Get(r, arguments); return items; }
            case 7: { var n = (int)r.U64(); var v = new double[n]; for (var i = 0; i < n; ++i) v[i] = r.F64(); return v; }
            case 9:
            {
                var n = r.Count();
                var d = new Dictionary<object, object?>(n);
                for (var i = 0; i < n; ++i) { var key = Get(r, arguments) ?? throw new NativeException("binding", "null dictionary key"); d[key] = Get(r, arguments); }
                return d;
            }
            case 8: return new KeyValuePair<object?, object?>(Get(r, arguments), Get(r, arguments));
            case 10:
            {
                var address = r.U64();
                var klass = r.U32();
                var parent = r.I64();
                return Wrap(address, klass, parent >= 0 && arguments != null && parent < arguments.Length ? arguments[parent] : null);
            }
            case 11: return Lookup(r.U64());
            case 12: { var f = new NativeFunction(default); Adopt(f, r.U64(), null); return f; }
            case 17: { var index = r.U32(); return arguments != null && index < arguments.Length ? arguments[index] : null; }
        }
        throw new NativeException("binding", $"malformed native value (tag {tag})");
    }

    // ------------------------------------------------------------ conversions to declared types
    internal static T To<T>(object? value) => (T)To(typeof(T), value)!;
    internal static object? To(Type type, object? value)
    {
        if (type == typeof(void)) return null;
        if (type == typeof(object)) return value is Symbol s ? s.Name : value;
        var underlying = Nullable.GetUnderlyingType(type);
        if (value == null)
        {
            if (type.IsValueType && underlying == null) throw new NativeException("binding", $"C++ returned null where C# expects {type.Name}");
            return null;
        }
        if (underlying != null) return To(underlying, value);
        if (type.IsEnum)
        {
            if (value is Symbol symbol) return Enum.Parse(type, symbol.Name, ignoreCase: true);
            return Enum.ToObject(type, Convert.ToInt64(value, CultureInfo.InvariantCulture));
        }
        if (type == typeof(string)) return value is Symbol named ? named.Name : value as string ?? Convert.ToString(value, CultureInfo.InvariantCulture);
        if (type == typeof(char)) return value is string text && text.Length > 0 ? text[0] : Convert.ToChar(value, CultureInfo.InvariantCulture);
        if (type == typeof(bool)) return value is bool flag ? flag : Convert.ToBoolean(value, CultureInfo.InvariantCulture);
        if (type == typeof(Complex)) return value is Complex complex ? complex : new Complex(Convert.ToDouble(value, CultureInfo.InvariantCulture), 0);
        if (type.IsPrimitive || type == typeof(decimal))
            return value is BigInteger big ? Convert.ChangeType((decimal)big, type, CultureInfo.InvariantCulture) : Convert.ChangeType(value, type, CultureInfo.InvariantCulture);
        if (type == typeof(BigInteger)) return value is BigInteger b ? b : new BigInteger(Convert.ToDecimal(value, CultureInfo.InvariantCulture));
        if (typeof(Delegate).IsAssignableFrom(type))
        {
            if (type.IsInstanceOfType(value)) return value;
            if (value is NativeFunction native) return MakeDelegate(type, native);
        }
        if (type.IsArray)
        {
            var element = type.GetElementType()!;
            if (value is double[] reals && element == typeof(double)) return reals;
            var items = Items(value);
            var array = Array.CreateInstance(element, items.Count);
            for (var i = 0; i < items.Count; ++i) array.SetValue(To(element, items[i]), i);
            return array;
        }
        if (type.IsGenericType)
        {
            var definition = type.GetGenericTypeDefinition();
            var arguments = type.GetGenericArguments();
            if (definition == typeof(List<>) || definition == typeof(IEnumerable<>) || definition == typeof(IReadOnlyList<>) ||
                definition == typeof(IList<>) || definition == typeof(ICollection<>) || definition == typeof(IReadOnlyCollection<>))
            {
                if (value is double[] reals && arguments[0] == typeof(double)) return new List<double>(reals);
                var list = (IList)Activator.CreateInstance(typeof(List<>).MakeGenericType(arguments[0]))!;
                foreach (var item in Items(value)) list.Add(To(arguments[0], item));
                return list;
            }
            if (definition == typeof(HashSet<>) || definition == typeof(ISet<>) || definition == typeof(IReadOnlySet<>))
            {
                var set = Activator.CreateInstance(typeof(HashSet<>).MakeGenericType(arguments[0]))!;
                var add = set.GetType().GetMethod("Add")!;
                foreach (var item in Items(value)) add.Invoke(set, new[] { To(arguments[0], item) });
                return set;
            }
            if (definition == typeof(Dictionary<,>) || definition == typeof(IDictionary<,>) || definition == typeof(IReadOnlyDictionary<,>))
            {
                var dictionary = (IDictionary)Activator.CreateInstance(typeof(Dictionary<,>).MakeGenericType(arguments))!;
                if (value is IDictionary source)
                    foreach (DictionaryEntry entry in source) dictionary[To(arguments[0], entry.Key)!] = To(arguments[1], entry.Value);
                else
                    foreach (var item in Items(value))
                        if (item is KeyValuePair<object?, object?> pair) dictionary[To(arguments[0], pair.Key)!] = To(arguments[1], pair.Value);
                return dictionary;
            }
            if (definition == typeof(KeyValuePair<,>) && value is KeyValuePair<object?, object?> kv)
                return Activator.CreateInstance(type, To(arguments[0], kv.Key), To(arguments[1], kv.Value));
            if (type.FullName!.StartsWith("System.ValueTuple`", StringComparison.Ordinal) || type.FullName!.StartsWith("System.Tuple`", StringComparison.Ordinal))
            {
                var items = value is KeyValuePair<object?, object?> p ? new List<object?> { p.Key, p.Value } : Items(value);
                if (items.Count != arguments.Length) throw new NativeException("binding", $"expected a tuple of {arguments.Length} elements");
                return Activator.CreateInstance(type, items.Select((item, i) => To(arguments[i], item)).ToArray());
            }
        }
        if (type.IsInstanceOfType(value)) return value;
        if (value is NativeObject from && typeof(NativeObject).IsAssignableFrom(type))
            throw new NativeException("binding", $"{from.GetType().Name} is not a {type.Name}");
        throw new NativeException("binding", $"cannot convert {value.GetType().Name} to {type.Name}");
    }
    static List<object?> Items(object value) => value switch
    {
        List<object?> list => list,
        object?[] items => items.ToList(),
        double[] reals => reals.Cast<object?>().ToList(),
        IEnumerable sequence and not string => sequence.Cast<object?>().ToList(),
        _ => throw new NativeException("binding", $"expected a sequence, got {value.GetType().Name}"),
    };
    static readonly MethodInfo toMethod = typeof(Rosetta).GetMethod(nameof(To), BindingFlags.NonPublic | BindingFlags.Static, new[] { typeof(Type), typeof(object) })!;
    static Delegate MakeDelegate(Type type, NativeFunction native)
    {
        var invoke = type.GetMethod("Invoke")!;
        var parameters = invoke.GetParameters().Select(p => Expression.Parameter(p.ParameterType, p.Name)).ToArray();
        Expression body = Expression.Call(Expression.Constant(native), typeof(NativeFunction).GetMethod(nameof(NativeFunction.Invoke))!,
            Expression.NewArrayInit(typeof(object), parameters.Select(p => Expression.Convert(p, typeof(object)))));
        if (invoke.ReturnType != typeof(void))
            body = Expression.Convert(Expression.Call(toMethod, Expression.Constant(invoke.ReturnType), body), invoke.ReturnType);
        return Expression.Lambda(type, body, parameters).Compile();
    }

    // ------------------------------------------------------------ calls
    [ThreadStatic] static Exception? callbackException;
    internal static object? Call(uint index, params object?[] arguments) => CallInto(index, arguments, null);
    internal static object? CallInto(uint index, object?[] arguments, NativeObject? into)
    {
        var o = new Out();
        try
        {
            // C# and C++ buffer standard output separately: flush before C++ writes.
            Console.Out.Flush();
            o.W.Write((uint)arguments.Length);
            foreach (var argument in arguments) Put(o, argument);
            o.W.Flush();
            var data = o.Stream.ToArray();
            var handle = GCHandle.Alloc(data, GCHandleType.Pinned);
            int status;
            IntPtr output;
            UIntPtr outputSize;
            try { status = callFn!(index, handle.AddrOfPinnedObject(), (UIntPtr)data.Length, out output, out outputSize); }
            finally { handle.Free(); }
            byte[] bytes;
            try
            {
                bytes = new byte[(int)outputSize];
                if (output != IntPtr.Zero && bytes.Length > 0) Marshal.Copy(output, bytes, 0, bytes.Length);
            }
            finally { if (output != IntPtr.Zero) freeFn!(output); }
            var reader = new In(bytes);
            switch (status)
            {
                case 0:
                {
                    object? value;
                    if (into != null && !reader.Empty && reader.Peek() == 10)
                    {
                        reader.U8();
                        var address = reader.U64();
                        reader.U32(); reader.I64();
                        Adopt(into, address, null);
                        value = into;
                    }
                    else value = Get(reader, arguments);
                    var writebacks = reader.U32();
                    for (var i = 0; i < writebacks; ++i)
                    {
                        var slot = reader.U32();
                        o.Slots[(int)slot].Value = Get(reader, arguments);
                    }
                    GC.KeepAlive(arguments);
                    return value;
                }
                case 2:
                {
                    var exception = callbackException;
                    callbackException = null;
                    Get(reader, null);
                    var message = Get(reader, null) as string ?? "error in a C# callback";
                    if (exception != null) ExceptionDispatchInfo.Capture(exception).Throw();
                    throw new NativeException("csharp", message);
                }
                case 1:
                {
                    var kind = Get(reader, null)?.ToString() ?? "native";
                    throw new NativeException(kind.Replace('-', '_'), Get(reader, null) as string ?? "native error");
                }
                default: throw new NativeException("allocation", "out of memory in a native call");
            }
        }
        finally
        {
            foreach (var id in o.Functions) Adjust(id, -1);
        }
    }
    internal static void Construct(NativeObject self, Type declared, bool director, uint index, params object?[] arguments)
    {
        var all = new object?[arguments.Length + 1];
        all[0] = director && self.GetType() != declared ? (long)ObjectId(self) : 0L;
        Array.Copy(arguments, 0, all, 1, arguments.Length);
        CallInto(index, all, self);
    }
    // A secondary base class's view of an object (C# has single inheritance): same C++ object.
    internal static T? View<T>(NativeObject? from, Func<T> make) where T : NativeObject
    {
        if (from == null) return null;
        var view = make();
        view.RosettaHolder = from.RosettaHolder;
        view.RosettaParent = from;
        view.RosettaOwns = false;
        return view;
    }
    internal static void Close(NativeObject o)
    {
        var address = o.RosettaHolder;
        if (address == 0) return;
        o.RosettaHolder = 0;
        if (o.RosettaOwns) releaseFn?.Invoke((IntPtr)(long)address, 1);
    }
    internal static void ReleaseLater(NativeObject o)
    {
        var address = o.RosettaHolder;
        if (address != 0 && o.RosettaOwns) releaseFn?.Invoke((IntPtr)(long)address, 0);
    }

    // Calls from C++: callbacks (kind 0) and overrides of virtual methods (kind 1).
    static int InvokeCallback(int kind, ulong id, uint generic, IntPtr data, UIntPtr size, IntPtr result, IntPtr resultSize)
    {
        var status = 0;
        var o = new Out();
        try
        {
            var bytes = new byte[(int)size];
            if (bytes.Length > 0) Marshal.Copy(data, bytes, 0, bytes.Length);
            var reader = new In(bytes);
            var count = reader.U32();
            var arguments = new object?[count];
            for (var i = 0; i < count; ++i) arguments[i] = Get(reader, null);
            var value = kind == 0 ? CallDelegate((Delegate)Lookup(id), arguments) : CallOverride(generic, arguments);
            try { Put(o, value); }
            catch (NativeException) { o = new Out(); Put(o, null); }
        }
        catch (Exception exception)
        {
            if (exception is TargetInvocationException { InnerException: { } inner }) exception = inner;
            callbackException = exception;
            o = new Out();
            o.W.Write((byte)5);
            o.Text(exception.Message);
            status = 2;
        }
        Console.Out.Flush();
        o.W.Flush();
        var output = o.Stream.ToArray();
        var buffer = allocFn!((UIntPtr)Math.Max(1, output.Length));
        if (output.Length > 0) Marshal.Copy(output, 0, buffer, output.Length);
        Marshal.WriteIntPtr(result, buffer);
        Marshal.WriteIntPtr(resultSize, (IntPtr)output.Length);
        return status;
    }
    static object? CallDelegate(Delegate f, object?[] arguments)
    {
        var parameters = f.GetType().GetMethod("Invoke")!.GetParameters();
        if (parameters.Length != arguments.Length)
            throw new NativeException("binding", $"callback takes {parameters.Length} arguments, C++ passed {arguments.Length}");
        var converted = new object?[arguments.Length];
        for (var i = 0; i < arguments.Length; ++i) converted[i] = To(parameters[i].ParameterType, arguments[i]);
        try { return f.DynamicInvoke(converted); }
        catch (TargetInvocationException error) when (error.InnerException != null) { ExceptionDispatchInfo.Capture(error.InnerException).Throw(); throw; }
    }
    // The C# method an override call names, on the object's own (most derived) type.
    static object? CallOverride(uint generic, object?[] arguments)
    {
        var self = arguments[0] ?? throw new NativeException("binding", "override called without an object");
        var name = Generics[generic];
        var rest = arguments.Skip(1).ToArray();
        Exception? last = null;
        foreach (var method in self.GetType().GetMethods(BindingFlags.Public | BindingFlags.Instance))
        {
            if (method.Name != name || method.GetParameters().Length != rest.Length) continue;
            object?[] converted;
            try
            {
                var parameters = method.GetParameters();
                converted = new object?[rest.Length];
                for (var i = 0; i < rest.Length; ++i)
                {
                    var type = parameters[i].ParameterType;
                    converted[i] = To(type.IsByRef ? type.GetElementType()! : type, rest[i]);
                }
            }
            catch (Exception error) { last = error; continue; }
            try { return method.Invoke(self, converted); }
            catch (TargetInvocationException error) when (error.InnerException != null) { ExceptionDispatchInfo.Capture(error.InnerException).Throw(); throw; }
        }
        throw last ?? new NativeException("binding", $"{self.GetType().Name} has no method {name} taking {rest.Length} arguments");
    }
}
)cs";
} // namespace rosetta::detail
