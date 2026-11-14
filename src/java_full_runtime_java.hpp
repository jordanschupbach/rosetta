#pragma once

// Java side of the full Java backend (--java-api full), plus the JNI glue appended to the
// generated C++ library. The C++ library is the one every full C ABI backend shares
// (lisp_full_runtime.hpp): Java encodes a call's arguments into one byte[], calls the module's
// single native entry point through JNI, and decodes the result into plain values that the
// generated wrappers convert to their declared Java types (Rosetta.asDouble, asList, ...).
//
// Placeholders: @P@ the Java package, @LIB@ the library base name for System.loadLibrary, @PROP@
// the system property and @ENV@ the environment variable that may name the library file, @MOD@
// the native module, @REF@ the Ref holder class and @COMPLEX@ the complex number record.
// Every runtime file imports the JDK types it uses one by one, so that a module class named
// like a JDK type (String, List, Math, ...) does not hide them.
namespace rosetta::detail {

inline constexpr const char* java_full_native_object = R"java(package @P@;

import java.lang.AutoCloseable;
import java.lang.Object;
import java.lang.Override;
import java.lang.Runnable;
import java.lang.String;

/**
 * Base class of the classes of native module {@code @MOD@}: each instance owns (or borrows) a
 * C++ object.
 *
 * <p>{@link #close()} releases the C++ object now (use try-with-resources); otherwise a
 * {@link java.lang.ref.Cleaner} releases it after the Java object becomes unreachable. A Java
 * class extending a native class overrides its virtual methods, also when C++ calls them.
 */
public abstract class NativeObject implements AutoCloseable {
    /** Marks the constructors that wrap an existing C++ object (used by the binding). */
    public static final class Raw {
        private Raw() {}
    }
    /** The marker that generated constructors pass to their superclass. */
    protected static final Raw RAW = new Raw();

    // The C++ holder: also the Cleaner's action, so it must not refer back to the object.
    static final class Handle implements Runnable {
        volatile long address;
        boolean owns;
        @Override
        public void run() {
            final long a = address;
            address = 0;
            if (a != 0 && owns) Rosetta.releaseLater(a);
        }
    }
    final Handle rosettaHandle = new Handle();
    Object rosettaParent;
    long rosettaId;

    /**
     * An object without a C++ object yet (generated constructors create it).
     *
     * @param raw the marker {@link #RAW}
     */
    protected NativeObject(Raw raw) {}

    /**
     * Whether the C++ object is released (or was never created, or was moved to C++).
     *
     * @return true once {@link #close()} ran
     */
    public final boolean isClosed() { return rosettaHandle.address == 0; }

    /** Releases the C++ object now; later calls do nothing. */
    @Override
    public final void close() { Rosetta.close(this); }

    /**
     * The class name, as {@code Name()} or {@code Name(closed)}.
     *
     * @return a description of the object
     */
    @Override
    public String toString() { return getClass().getSimpleName() + (isClosed() ? "(closed)" : "()"); }
}
)java";

inline constexpr const char* java_full_native_error = R"java(package @P@;

import java.lang.ArithmeticException;
import java.lang.IllegalArgumentException;
import java.lang.IndexOutOfBoundsException;
import java.lang.Override;
import java.lang.String;
import java.lang.UnsupportedOperationException;

/**
 * An error raised by native module {@code @MOD@}: every exception the binding throws implements it.
 *
 * <p>C++ {@code std::invalid_argument} becomes {@link InvalidArgument} (an
 * {@link IllegalArgumentException}), {@code std::out_of_range} {@link OutOfRange} (an
 * {@link IndexOutOfBoundsException}), {@code std::domain_error}, {@code std::overflow_error},
 * {@code std::underflow_error} and {@code std::range_error} {@link Arithmetic} (an
 * {@link ArithmeticException}), a pure virtual method a Java subclass does not implement
 * {@link NotImplemented} (an {@link UnsupportedOperationException}), and anything else
 * {@link NativeException}.
 */
public interface NativeError {
    /**
     * What failed: the C++ exception's kind ({@code invalid_argument}, {@code out_of_range},
     * {@code domain_error}, {@code length_error}, {@code arithmetic_error}, {@code allocation},
     * {@code native}), or {@code binding} (conversion, overload or closed-object errors).
     *
     * @return the kind
     */
    String kind();

    /** A C++ {@code std::invalid_argument}. */
    final class InvalidArgument extends IllegalArgumentException implements NativeError {
        private static final long serialVersionUID = 1L;
        /** What failed. */
        private final String kind;
        /**
         * The error.
         *
         * @param kind the kind
         * @param message the C++ message
         */
        public InvalidArgument(String kind, String message) { super(message); this.kind = kind; }
        @Override
        public String kind() { return kind; }
    }

    /** A C++ {@code std::out_of_range}. */
    final class OutOfRange extends IndexOutOfBoundsException implements NativeError {
        private static final long serialVersionUID = 1L;
        /** What failed. */
        private final String kind;
        /**
         * The error.
         *
         * @param kind the kind
         * @param message the C++ message
         */
        public OutOfRange(String kind, String message) { super(message); this.kind = kind; }
        @Override
        public String kind() { return kind; }
    }

    /** A C++ arithmetic error ({@code std::domain_error}, {@code std::overflow_error}, ...). */
    final class Arithmetic extends ArithmeticException implements NativeError {
        private static final long serialVersionUID = 1L;
        /** What failed. */
        private final String kind;
        /**
         * The error.
         *
         * @param kind the kind
         * @param message the C++ message
         */
        public Arithmetic(String kind, String message) { super(message); this.kind = kind; }
        @Override
        public String kind() { return kind; }
    }

    /** A pure virtual C++ method that a Java subclass does not implement. */
    final class NotImplemented extends UnsupportedOperationException implements NativeError {
        private static final long serialVersionUID = 1L;
        /** What failed. */
        private final String kind;
        /**
         * The error.
         *
         * @param kind the kind
         * @param message the message
         */
        public NotImplemented(String kind, String message) { super(message); this.kind = kind; }
        @Override
        public String kind() { return kind; }
    }
}
)java";

inline constexpr const char* java_full_native_exception = R"java(package @P@;

import java.lang.Override;
import java.lang.RuntimeException;
import java.lang.String;

/**
 * A C++ exception (other than the kinds {@link NativeError} maps to standard Java exceptions), or a
 * failed conversion, in a call to native module {@code @MOD@}.
 */
public class NativeException extends RuntimeException implements NativeError {
    private static final long serialVersionUID = 1L;
    /** What failed. */
    private final String kind;

    /**
     * A native error.
     *
     * @param kind what failed (see {@link NativeError#kind()})
     * @param message the message
     */
    public NativeException(String kind, String message) {
        super(message);
        this.kind = kind;
    }

    @Override
    public String kind() { return kind; }
}
)java";

inline constexpr const char* java_full_native_function = R"java(package @P@;

import java.lang.Object;
import java.lang.System;

/**
 * A C++ {@code std::function} returned to Java. Generated methods wrap it in the declared
 * functional interface; {@link #invoke(Object...)} calls it directly.
 */
public final class NativeFunction extends NativeObject {
    NativeFunction() { super(RAW); }

    /**
     * Calls the C++ function.
     *
     * @param arguments its arguments
     * @return its result, as a plain value
     */
    public Object invoke(Object... arguments) {
        final Object[] all = new Object[arguments.length + 1];
        all[0] = new Rosetta.Callee(this);
        System.arraycopy(arguments, 0, all, 1, arguments.length);
        return Rosetta.plain(Rosetta.call(0, all));
    }
}
)java";

inline constexpr const char* java_full_native_enum = R"java(package @P@;

import java.lang.String;

/** Implemented by the enums of native module {@code @MOD@}. */
public interface NativeEnum {
    /**
     * The C++ value of the enumerator.
     *
     * @return the value
     */
    long value();

    /**
     * The C++ name of the enumerator.
     *
     * @return the name
     */
    String nativeName();
}
)java";

inline constexpr const char* java_full_ref = R"java(package @P@;

import java.lang.Object;
import java.lang.Override;
import java.lang.String;

/**
 * A mutable reference argument: C++ reads {@link #value} and writes its new value back.
 *
 * @param <T> the type of the value
 */
public final class @REF@<T> {
    /** The value, updated by the call. */
    public T value;
    Object rosettaPending;
    boolean rosettaWritten;

    /**
     * A reference holding {@code value}.
     *
     * @param value the initial value
     */
    public @REF@(T value) { this.value = value; }

    /**
     * Shows the value.
     *
     * @return the value as text
     */
    @Override
    public String toString() { return "@REF@(" + value + ")"; }
}
)java";

inline constexpr const char* java_full_complex = R"java(package @P@;

import java.lang.Math;
import java.lang.Override;
import java.lang.String;

/**
 * A C++ {@code std::complex<double>}.
 *
 * @param real the real part
 * @param imag the imaginary part
 */
public record @COMPLEX@(double real, double imag) {
    /**
     * The absolute value.
     *
     * @return {@code |z|}
     */
    public double abs() { return Math.hypot(real, imag); }

    /**
     * Shows the number.
     *
     * @return {@code (re,im)}, as C++ prints it
     */
    @Override
    public String toString() { return "(" + real + "," + imag + ")"; }
}
)java";

// The runtime class; the generator appends its tables (classes, enums, overrides) and the
// closing brace.
inline constexpr const char* java_full_rosetta = R"java(package @P@;

import java.io.File;
import java.lang.ArithmeticException;
import java.lang.Boolean;
import java.lang.Byte;
import java.lang.Character;
import java.lang.Class;
import java.lang.Double;
import java.lang.Enum;
import java.lang.Float;
import java.lang.IllegalStateException;
import java.lang.Integer;
import java.lang.Iterable;
import java.lang.Long;
import java.lang.Math;
import java.lang.Number;
import java.lang.Object;
import java.lang.Override;
import java.lang.Record;
import java.lang.ReflectiveOperationException;
import java.lang.RuntimeException;
import java.lang.Short;
import java.lang.String;
import java.lang.SuppressWarnings;
import java.lang.System;
import java.lang.ThreadLocal;
import java.lang.Throwable;
import java.lang.ref.Cleaner;
import java.lang.ref.WeakReference;
import java.lang.reflect.RecordComponent;
import java.math.BigInteger;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.AbstractMap;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.NoSuchElementException;
import java.util.Optional;
import java.util.OptionalDouble;
import java.util.OptionalInt;
import java.util.OptionalLong;
import java.util.Set;
import java.util.function.Function;
import java.util.function.IntFunction;

/**
 * The runtime of native module {@code @MOD@}: generated classes call it. It is not part of the
 * module's API.
 *
 * <p>It loads the JNI library from the file named by the system property {@code @PROP@} or the
 * environment variable {@code @ENV@}, else with {@code System.loadLibrary("@LIB@")} (so
 * {@code lib@LIB@.so} must be on {@code java.library.path}).
 */
public final class Rosetta {
    private Rosetta() {}

    /** A Java function C++ calls (the binding wraps lambdas in it). */
    @java.lang.FunctionalInterface
    public interface Callback {
        /**
         * Calls the function.
         *
         * @param arguments the arguments C++ passes, decoded
         * @return the result to encode for C++
         * @throws Throwable whatever the function throws: it propagates through C++
         */
        Object call(Object[] arguments) throws Throwable;
    }
    static final class Symbol {
        final String name;
        Symbol(String name) { this.name = name; }
        @Override
        public String toString() { return name; }
    }
    static final class Callee {
        final NativeFunction f;
        Callee(NativeFunction f) { this.f = f; }
    }
    private static final class Entry {
        final Object value;
        int count;
        Entry(Object value, int count) { this.value = value; this.count = count; }
    }

    // ------------------------------------------------------------ native library
    private static native int nativeInit(int version);
    private static native byte[] nativeCall(int index, byte[] arguments);
    private static native void nativeRelease(long holder, boolean now);
    static final Cleaner CLEANER = Cleaner.create();
    private static final Object GATE = new Object();
    private static long next;
    private static final HashMap<Long, WeakReference<Object>> WEAK = new HashMap<>();
    private static final HashMap<Long, Entry> STRONG = new HashMap<>();
    private static final ThreadLocal<Throwable> CALLBACK_ERROR = new ThreadLocal<>();

    static {
        String file = System.getProperty("@PROP@");
        if (file == null || file.isEmpty()) file = System.getenv("@ENV@");
        if (file != null && !file.isEmpty()) System.load(new File(file).getAbsolutePath());
        else System.loadLibrary("@LIB@");
        if (nativeInit(1) != 0) throw new IllegalStateException("native module @MOD@ was generated by a different Rosetta version");
    }

    // ------------------------------------------------------------ Java values C++ refers to
    // Callbacks and subclass objects: ids index these tables. C++ references keep entries strong
    // (retain/release); the rest are weak.
    private static long newId() {
        synchronized (GATE) {
            if (++next % 4096 == 0) WEAK.values().removeIf(reference -> reference.get() == null);
            return next;
        }
    }
    static long objectId(NativeObject o) {
        if (o.rosettaId != 0) return o.rosettaId;
        final long id = newId();
        synchronized (GATE) { WEAK.put(id, new WeakReference<>(o)); }
        o.rosettaId = id;
        return id;
    }
    private static Object lookup(long id) {
        synchronized (GATE) {
            final Entry entry = STRONG.get(id);
            if (entry != null) return entry.value;
            final WeakReference<Object> reference = WEAK.get(id);
            final Object target = reference == null ? null : reference.get();
            if (target != null) return target;
        }
        throw new NativeException("binding", "the Java object of a native reference is gone");
    }
    // Called by C++ (retain/release of a Java object it keeps).
    private static void retain(long id, int delta) {
        synchronized (GATE) {
            final Entry entry = STRONG.get(id);
            if (delta > 0) {
                if (entry != null) entry.count += delta;
                else {
                    final WeakReference<Object> reference = WEAK.get(id);
                    final Object target = reference == null ? null : reference.get();
                    if (target != null) STRONG.put(id, new Entry(target, delta));
                }
            } else if (entry != null) {
                entry.count += delta;
                if (entry.count <= 0) STRONG.remove(id);
            }
        }
    }

    // ------------------------------------------------------------ encoding (Java -> C++)
    static final class Out {
        ByteBuffer b = ByteBuffer.allocate(256).order(ByteOrder.nativeOrder());
        final ArrayList<Long> functions = new ArrayList<>();
        final ArrayList<@REF@<?>> slots = new ArrayList<>();
        void need(int n) {
            if (b.remaining() >= n) return;
            final ByteBuffer grown = ByteBuffer.allocate(Math.max(b.capacity() * 2, b.position() + n)).order(ByteOrder.nativeOrder());
            b.flip();
            grown.put(b);
            b = grown;
        }
        void u8(int v) { need(1); b.put((byte) v); }
        void u32(int v) { need(4); b.putInt(v); }
        void i64(long v) { need(8); b.putLong(v); }
        void f64(double v) { need(8); b.putDouble(v); }
        void raw(byte[] bytes) { i64(bytes.length); need(bytes.length); b.put(bytes); }
        void text(String s) { raw(s.getBytes(StandardCharsets.UTF_8)); }
        byte[] bytes() { return Arrays.copyOf(b.array(), b.position()); }
    }
    static void put(Out o, Object x, int depth) {
        if (depth > 10000) throw new NativeException("binding", "value nesting is too deep");
        if (x == null) { o.u8(0); return; }
        if (x instanceof Boolean) { o.u8((Boolean) x ? 1 : 19); return; }
        if (x instanceof Long || x instanceof Integer || x instanceof Short || x instanceof Byte) { o.u8(2); o.i64(((Number) x).longValue()); return; }
        if (x instanceof Double || x instanceof Float) { o.u8(4); o.f64(((Number) x).doubleValue()); return; }
        if (x instanceof String) { o.u8(5); o.text((String) x); return; }
        if (x instanceof Character) { o.u8(5); o.text(x.toString()); return; }
        if (x instanceof BigInteger) {
            final BigInteger n = (BigInteger) x;
            if (n.bitLength() < 64) { o.u8(2); o.i64(n.longValue()); } else { o.u8(3); o.text(n.toString()); }
            return;
        }
        if (x instanceof @COMPLEX@) { final @COMPLEX@ c = (@COMPLEX@) x; o.u8(18); o.f64(c.real()); o.f64(c.imag()); return; }
        if (x instanceof NativeEnum) { o.u8(14); o.text(((NativeEnum) x).nativeName()); return; }
        if (x instanceof Enum) { o.u8(14); o.text(((Enum<?>) x).name()); return; }
        if (x instanceof Symbol) { o.u8(14); o.text(((Symbol) x).name); return; }
        if (x instanceof Callee) { putObject(o, ((Callee) x).f); return; }
        if (x instanceof NativeObject) { putObject(o, (NativeObject) x); return; }
        if (x instanceof @REF@) {
            final @REF@<?> slot = (@REF@<?>) x;
            o.slots.add(slot);
            o.u8(13);
            o.u32(o.slots.size() - 1);
            put(o, slot.value, depth + 1);
            return;
        }
        if (x instanceof Callback) {
            final long id = newId();
            synchronized (GATE) { STRONG.put(id, new Entry(x, 1)); }
            o.functions.add(id);
            o.u8(12);
            o.i64(id);
            return;
        }
        if (x instanceof byte[]) { o.u8(15); o.raw((byte[]) x); return; }
        if (x instanceof double[]) { final double[] v = (double[]) x; o.u8(7); o.i64(v.length); for (final double d : v) o.f64(d); return; }
        if (x instanceof float[]) { final float[] v = (float[]) x; o.u8(7); o.i64(v.length); for (final float d : v) o.f64(d); return; }
        if (x instanceof int[]) { final int[] v = (int[]) x; o.u8(6); o.i64(v.length); for (final int i : v) { o.u8(2); o.i64(i); } return; }
        if (x instanceof long[]) { final long[] v = (long[]) x; o.u8(6); o.i64(v.length); for (final long i : v) { o.u8(2); o.i64(i); } return; }
        if (x instanceof short[]) { final short[] v = (short[]) x; o.u8(6); o.i64(v.length); for (final short i : v) { o.u8(2); o.i64(i); } return; }
        if (x instanceof boolean[]) { final boolean[] v = (boolean[]) x; o.u8(6); o.i64(v.length); for (final boolean i : v) o.u8(i ? 1 : 19); return; }
        if (x instanceof char[]) { put(o, new String((char[]) x), depth); return; }
        if (x instanceof Object[]) { final Object[] v = (Object[]) x; o.u8(6); o.i64(v.length); for (final Object i : v) put(o, i, depth + 1); return; }
        if (x instanceof Optional) { put(o, ((Optional<?>) x).orElse(null), depth); return; }
        if (x instanceof OptionalDouble) { final OptionalDouble v = (OptionalDouble) x; if (v.isPresent()) { o.u8(4); o.f64(v.getAsDouble()); } else o.u8(0); return; }
        if (x instanceof OptionalInt) { final OptionalInt v = (OptionalInt) x; if (v.isPresent()) { o.u8(2); o.i64(v.getAsInt()); } else o.u8(0); return; }
        if (x instanceof OptionalLong) { final OptionalLong v = (OptionalLong) x; if (v.isPresent()) { o.u8(2); o.i64(v.getAsLong()); } else o.u8(0); return; }
        if (x instanceof Map) {
            final Map<?, ?> m = (Map<?, ?>) x;
            o.u8(9);
            o.i64(m.size());
            for (final Map.Entry<?, ?> e : m.entrySet()) { put(o, e.getKey(), depth + 1); put(o, e.getValue(), depth + 1); }
            return;
        }
        if (x instanceof Map.Entry) { final Map.Entry<?, ?> e = (Map.Entry<?, ?>) x; o.u8(8); put(o, e.getKey(), depth + 1); put(o, e.getValue(), depth + 1); return; }
        if (x instanceof Record) {
            final RecordComponent[] components = x.getClass().getRecordComponents();
            o.u8(6);
            o.i64(components.length);
            for (final RecordComponent component : components) {
                try { put(o, component.getAccessor().invoke(x), depth + 1); }
                catch (ReflectiveOperationException error) { throw new NativeException("binding", "cannot read " + component.getName() + ": " + error); }
            }
            return;
        }
        if (x instanceof Iterable) {
            final ArrayList<Object> items = new ArrayList<>();
            for (final Object item : (Iterable<?>) x) items.add(item);
            boolean reals = !items.isEmpty();
            for (final Object item : items) if (!(item instanceof Double)) { reals = false; break; }
            if (reals) { o.u8(7); o.i64(items.size()); for (final Object item : items) o.f64((Double) item); return; }
            o.u8(6);
            o.i64(items.size());
            for (final Object item : items) put(o, item, depth + 1);
            return;
        }
        throw new NativeException("binding", "cannot pass a value of type " + x.getClass().getName() + " to C++");
    }
    private static void putObject(Out o, NativeObject x) {
        final long holder = x.rosettaHandle.address;
        o.u8(10);
        o.i64(holder);
        o.i64(holder == 0 ? 0 : objectId(x));
    }

    // ------------------------------------------------------------ decoding (C++ -> Java)
    static final class In {
        private final ByteBuffer b;
        In(byte[] bytes) { b = ByteBuffer.wrap(bytes).order(ByteOrder.nativeOrder()); }
        boolean empty() { return !b.hasRemaining(); }
        int peek() { return b.get(b.position()) & 0xff; }
        private void need(int n) { if (b.remaining() < n) throw new NativeException("binding", "malformed native value"); }
        int u8() { need(1); return b.get() & 0xff; }
        int u32() { need(4); return b.getInt(); }
        long i64() { need(8); return b.getLong(); }
        double f64() { need(8); return b.getDouble(); }
        int count() {
            final long n = i64();
            if (n < 0 || n > b.remaining()) throw new NativeException("binding", "malformed native value");
            return (int) n;
        }
        byte[] bytes(int n) { need(n); final byte[] v = new byte[n]; b.get(v); return v; }
        String text() { return new String(bytes(count()), StandardCharsets.UTF_8); }
    }
    private static NativeObject wrap(long address, int klass, Object parent) {
        if (klass < 0 || klass >= CLASSES.size()) throw new NativeException("binding", "C++ result type is not exported to Java");
        final NativeObject o = CLASSES.get(klass).apply(NativeObject.RAW);
        adopt(o, address, parent);
        return o;
    }
    private static void adopt(NativeObject o, long address, Object parent) {
        o.rosettaHandle.address = address;
        o.rosettaHandle.owns = true;
        o.rosettaParent = parent;
        CLEANER.register(o, o.rosettaHandle);
    }
    private static Object get(In r, Object[] arguments) {
        final int tag = r.u8();
        switch (tag) {
            case 0: return null;
            case 1: return Boolean.TRUE;
            case 19: return Boolean.FALSE;
            case 2: return r.i64();
            case 3: return new BigInteger(r.text());
            case 4: return r.f64();
            case 18: return new @COMPLEX@(r.f64(), r.f64());
            case 5: return r.text();
            case 14: return new Symbol(r.text());
            case 15: return r.bytes(r.count());
            case 6: {
                final int n = r.count();
                final ArrayList<Object> list = new ArrayList<>(n);
                for (int i = 0; i < n; ++i) list.add(get(r, arguments));
                return list;
            }
            case 16: {
                final int n = r.count();
                final Object[] items = new Object[n];
                for (int i = 0; i < n; ++i) items[i] = get(r, arguments);
                return items;
            }
            case 7: {
                final long n = r.i64();
                if (n < 0 || n > Integer.MAX_VALUE / 8) throw new NativeException("binding", "malformed native value");
                final double[] v = new double[(int) n];
                for (int i = 0; i < v.length; ++i) v[i] = r.f64();
                return v;
            }
            case 9: {
                final int n = r.count();
                final LinkedHashMap<Object, Object> m = new LinkedHashMap<>();
                for (int i = 0; i < n; ++i) {
                    final Object key = get(r, arguments);
                    m.put(key, get(r, arguments));
                }
                return m;
            }
            case 8: {
                final Object key = get(r, arguments);
                return new AbstractMap.SimpleEntry<Object, Object>(key, get(r, arguments));
            }
            case 10: {
                final long address = r.i64();
                final int klass = r.u32();
                final long parent = r.i64();
                return wrap(address, klass, parent >= 0 && arguments != null && parent < arguments.length ? arguments[(int) parent] : null);
            }
            case 11: return lookup(r.i64());
            case 12: {
                final NativeFunction f = new NativeFunction();
                adopt(f, r.i64(), null);
                return f;
            }
            case 17: {
                final int index = r.u32();
                return arguments != null && index >= 0 && index < arguments.length ? arguments[index] : null;
            }
            default: throw new NativeException("binding", "malformed native value (tag " + tag + ")");
        }
    }

    // ------------------------------------------------------------ conversions to declared types
    private static NativeException expected(String what, Object v) {
        return new NativeException("binding", "expected " + what + ", got " + (v == null ? "null" : v.getClass().getSimpleName() + " " + v));
    }
    private static long integral(Object v, String what) {
        if (v instanceof Long || v instanceof Integer || v instanceof Short || v instanceof Byte) return ((Number) v).longValue();
        if (v instanceof BigInteger) throw new ArithmeticException("C++ value " + v + " does not fit in a Java " + what);
        if (v instanceof Double || v instanceof Float) {
            final double d = ((Number) v).doubleValue();
            if (d == Math.rint(d) && Math.abs(d) < 9.2e18) return (long) d;
        }
        if (v instanceof NativeEnum) return ((NativeEnum) v).value();
        throw expected("an integer", v);
    }
    /**
     * A decoded value as a {@code boolean}.
     *
     * @param v the value
     * @return it
     */
    public static boolean asBoolean(Object v) {
        if (v instanceof Boolean) return (Boolean) v;
        if (v instanceof Number) return ((Number) v).doubleValue() != 0;
        throw expected("a boolean", v);
    }
    /**
     * A decoded value as a {@code byte}, range-checked.
     *
     * @param v the value
     * @return it
     */
    public static byte asByte(Object v) {
        final long n = integral(v, "byte");
        if (n < -128 || n > 255) throw new ArithmeticException("C++ value " + n + " does not fit in a Java byte");
        return (byte) n;
    }
    /**
     * A decoded value as a {@code short}, range-checked.
     *
     * @param v the value
     * @return it
     */
    public static short asShort(Object v) {
        final long n = integral(v, "short");
        if (n < Short.MIN_VALUE || n > Short.MAX_VALUE) throw new ArithmeticException("C++ value " + n + " does not fit in a Java short");
        return (short) n;
    }
    /**
     * A decoded value as an {@code int}, range-checked.
     *
     * @param v the value
     * @return it
     */
    public static int asInt(Object v) {
        final long n = integral(v, "int");
        if (n < Integer.MIN_VALUE || n > Integer.MAX_VALUE) throw new ArithmeticException("C++ value " + n + " does not fit in a Java int");
        return (int) n;
    }
    /**
     * A decoded value as a {@code long}, range-checked.
     *
     * @param v the value
     * @return it
     */
    public static long asLong(Object v) { return integral(v, "long"); }
    /**
     * A decoded value as a {@code float}.
     *
     * @param v the value
     * @return it
     */
    public static float asFloat(Object v) { return (float) asDouble(v); }
    /**
     * A decoded value as a {@code double}.
     *
     * @param v the value
     * @return it
     */
    public static double asDouble(Object v) {
        if (v instanceof Number) return ((Number) v).doubleValue();
        throw expected("a number", v);
    }
    /**
     * A decoded value as a {@code char}.
     *
     * @param v the value
     * @return it
     */
    public static char asChar(Object v) {
        if (v instanceof String && !((String) v).isEmpty()) return ((String) v).charAt(0);
        if (v instanceof Character) return (Character) v;
        if (v instanceof Number) return (char) ((Number) v).intValue();
        throw expected("a character", v);
    }
    /**
     * A decoded value as a {@code String}.
     *
     * @param v the value
     * @return it (null stays null)
     */
    public static String asString(Object v) {
        if (v == null || v instanceof String) return (String) v;
        if (v instanceof Symbol) return ((Symbol) v).name;
        if (v instanceof byte[]) return new String((byte[]) v, StandardCharsets.UTF_8);
        return v.toString();
    }
    /**
     * A decoded value as a plain Java value (enumerator names become strings).
     *
     * @param v the value
     * @return it
     */
    public static Object plain(Object v) {
        if (v instanceof Symbol) return ((Symbol) v).name;
        return v;
    }
    private static List<?> items(Object v) {
        if (v == null) return List.of();
        if (v instanceof List) return (List<?>) v;
        if (v instanceof Object[]) return Arrays.asList((Object[]) v);
        if (v instanceof double[]) { final ArrayList<Object> list = new ArrayList<>(); for (final double d : (double[]) v) list.add(d); return list; }
        if (v instanceof byte[]) { final ArrayList<Object> list = new ArrayList<>(); for (final byte d : (byte[]) v) list.add((long) (d & 0xff)); return list; }
        if (v instanceof Collection) return new ArrayList<Object>((Collection<?>) v);
        if (v instanceof Map.Entry) { final Map.Entry<?, ?> e = (Map.Entry<?, ?>) v; return Arrays.asList(e.getKey(), e.getValue()); }
        throw expected("a sequence", v);
    }
    /**
     * A decoded sequence as a list.
     *
     * @param <T> the element type
     * @param v the value
     * @param element converts each element
     * @return a new list
     */
    public static <T> List<T> asList(Object v, Function<Object, T> element) {
        final List<?> items = items(v);
        final ArrayList<T> list = new ArrayList<>(items.size());
        for (final Object item : items) list.add(element.apply(item));
        return list;
    }
    /**
     * A decoded sequence as a set.
     *
     * @param <T> the element type
     * @param v the value
     * @param element converts each element
     * @return a new set, in C++ order
     */
    public static <T> Set<T> asSet(Object v, Function<Object, T> element) {
        final LinkedHashSet<T> set = new LinkedHashSet<>();
        for (final Object item : items(v)) set.add(element.apply(item));
        return set;
    }
    /**
     * A decoded mapping as a map.
     *
     * @param <K> the key type
     * @param <V> the value type
     * @param v the value
     * @param key converts each key
     * @param value converts each value
     * @return a new map, in C++ order
     */
    public static <K, V> Map<K, V> asMap(Object v, Function<Object, K> key, Function<Object, V> value) {
        final LinkedHashMap<K, V> map = new LinkedHashMap<>();
        if (v == null) return map;
        if (v instanceof Map) {
            for (final Map.Entry<?, ?> e : ((Map<?, ?>) v).entrySet()) map.put(key.apply(e.getKey()), value.apply(e.getValue()));
            return map;
        }
        for (final Object item : items(v)) {
            final List<?> pair = items(item);
            map.put(key.apply(pair.get(0)), value.apply(pair.get(1)));
        }
        return map;
    }
    /**
     * A decoded sequence as a {@code double[]}.
     *
     * @param v the value
     * @return a new array (null stays null)
     */
    public static double[] asDoubles(Object v) {
        if (v == null) return null;
        if (v instanceof double[]) return (double[]) v;
        final List<?> items = items(v);
        final double[] result = new double[items.size()];
        for (int i = 0; i < result.length; ++i) result[i] = asDouble(items.get(i));
        return result;
    }
    /**
     * A decoded sequence as a {@code float[]}.
     *
     * @param v the value
     * @return a new array (null stays null)
     */
    public static float[] asFloats(Object v) {
        if (v == null) return null;
        final List<?> items = items(v);
        final float[] result = new float[items.size()];
        for (int i = 0; i < result.length; ++i) result[i] = asFloat(items.get(i));
        return result;
    }
    /**
     * A decoded sequence as an {@code int[]}, range-checked.
     *
     * @param v the value
     * @return a new array (null stays null)
     */
    public static int[] asInts(Object v) {
        if (v == null) return null;
        final List<?> items = items(v);
        final int[] result = new int[items.size()];
        for (int i = 0; i < result.length; ++i) result[i] = asInt(items.get(i));
        return result;
    }
    /**
     * A decoded sequence as a {@code long[]}.
     *
     * @param v the value
     * @return a new array (null stays null)
     */
    public static long[] asLongs(Object v) {
        if (v == null) return null;
        final List<?> items = items(v);
        final long[] result = new long[items.size()];
        for (int i = 0; i < result.length; ++i) result[i] = asLong(items.get(i));
        return result;
    }
    /**
     * A decoded byte sequence as a {@code byte[]}.
     *
     * @param v the value
     * @return a new array (null stays null)
     */
    public static byte[] asBytes(Object v) {
        if (v == null || v instanceof byte[]) return (byte[]) v;
        if (v instanceof String) return ((String) v).getBytes(StandardCharsets.UTF_8);
        final List<?> items = items(v);
        final byte[] result = new byte[items.size()];
        for (int i = 0; i < result.length; ++i) result[i] = asByte(items.get(i));
        return result;
    }
    /**
     * A decoded optional value.
     *
     * @param <T> the value type
     * @param v the value (null: empty)
     * @param element converts the value
     * @return the optional
     */
    public static <T> Optional<T> asOptional(Object v, Function<Object, T> element) {
        return v == null ? Optional.empty() : Optional.ofNullable(element.apply(v));
    }
    /**
     * A decoded optional {@code double}.
     *
     * @param v the value (null: empty)
     * @return the optional
     */
    public static OptionalDouble asOptionalDouble(Object v) { return v == null ? OptionalDouble.empty() : OptionalDouble.of(asDouble(v)); }
    /**
     * A decoded optional {@code int}.
     *
     * @param v the value (null: empty)
     * @return the optional
     */
    public static OptionalInt asOptionalInt(Object v) { return v == null ? OptionalInt.empty() : OptionalInt.of(asInt(v)); }
    /**
     * A decoded optional {@code long}.
     *
     * @param v the value (null: empty)
     * @return the optional
     */
    public static OptionalLong asOptionalLong(Object v) { return v == null ? OptionalLong.empty() : OptionalLong.of(asLong(v)); }
    /**
     * A decoded complex number.
     *
     * @param v the value
     * @return it
     */
    public static @COMPLEX@ asComplex(Object v) {
        if (v instanceof @COMPLEX@) return (@COMPLEX@) v;
        if (v instanceof Number) return new @COMPLEX@(((Number) v).doubleValue(), 0);
        if (v == null) return null;
        throw expected("a complex number", v);
    }
    /**
     * A decoded enumerator.
     *
     * @param <T> the enum
     * @param v the value (a C++ enumerator name or value)
     * @param type the enum class
     * @return the enumerator (null stays null)
     */
    public static <T extends Enum<T>> T asEnum(Object v, Class<T> type) {
        if (v == null || type.isInstance(v)) return type.cast(v);
        if (v instanceof Symbol || v instanceof String) {
            final String name = v.toString();
            for (final T constant : type.getEnumConstants())
                if (((NativeEnum) constant).nativeName().equalsIgnoreCase(name) || constant.name().equalsIgnoreCase(name)) return constant;
            throw new NativeException("binding", "unknown enumerator " + name + " of " + type.getSimpleName());
        }
        final long value = integral(v, type.getSimpleName());
        for (final T constant : type.getEnumConstants()) if (((NativeEnum) constant).value() == value) return constant;
        throw new NativeException("binding", "C++ value " + value + " is not an enumerator of " + type.getSimpleName());
    }
    /**
     * A decoded object, checked against its declared class.
     *
     * @param <T> the class
     * @param v the value
     * @param type the class
     * @return the object (null stays null)
     */
    public static <T> T asObject(Object v, Class<T> type) {
        if (v == null || type.isInstance(v)) return type.cast(v);
        throw expected("a " + type.getSimpleName(), v);
    }
    /**
     * A decoded tuple, built by {@code make} from its items.
     *
     * @param <T> the tuple type
     * @param v the value
     * @param size the number of items
     * @param make builds the tuple
     * @return the tuple (null stays null)
     */
    public static <T> T tuple(Object v, int size, Function<List<?>, T> make) {
        if (v == null) return null;
        final List<?> items = items(v);
        if (items.size() != size) throw new NativeException("binding", "expected a tuple of " + size + " items, got " + items.size());
        return make.apply(items);
    }
    /**
     * A decoded C++ function, wrapped by {@code make} in a functional interface.
     *
     * @param <T> the functional interface
     * @param v the value
     * @param make wraps the native function
     * @return the function (null stays null)
     */
    public static <T> T function(Object v, Function<NativeFunction, T> make) {
        if (v == null) return null;
        if (v instanceof NativeFunction) return make.apply((NativeFunction) v);
        throw expected("a function", v);
    }
    /**
     * Checks the number of arguments C++ passed to a Java function.
     *
     * @param arguments the arguments
     * @param count the expected number
     */
    public static void arity(Object[] arguments, int count) {
        if (arguments.length != count) throw new NativeException("binding", "callback takes " + count + " arguments, C++ passed " + arguments.length);
    }
    /**
     * Stores the value C++ wrote back into a {@link @REF@} argument.
     *
     * @param <T> the value type
     * @param ref the argument
     * @param convert converts the value
     */
    public static <T> void writeback(@REF@<T> ref, Function<Object, T> convert) {
        if (ref == null || !ref.rosettaWritten) return;
        ref.rosettaWritten = false;
        final Object pending = ref.rosettaPending;
        ref.rosettaPending = null;
        ref.value = convert.apply(pending);
    }
    /**
     * Compares two objects with the C++ {@code operator==} at {@code index}.
     *
     * @param index the native function
     * @param self the object
     * @param other the other object
     * @return whether C++ finds them equal (false when it cannot compare them)
     */
    public static boolean equal(int index, Object self, Object other) {
        try { return asBoolean(call(index, self, other)); }
        catch (NativeException error) {
            if ("binding".equals(error.kind())) return false;
            throw error;
        }
    }
    /**
     * Iterates over {@code size} items read with {@code get}.
     *
     * @param <T> the item type
     * @param size the number of items
     * @param get reads an item
     * @return the iterator
     */
    public static <T> Iterator<T> iterator(long size, IntFunction<T> get) {
        return new Iterator<T>() {
            int at;
            @Override
            public boolean hasNext() { return at < size; }
            @Override
            public T next() {
                if (at >= size) throw new NoSuchElementException();
                return get.apply(at++);
            }
        };
    }

    // ------------------------------------------------------------ calls
    @SuppressWarnings("unchecked")
    private static <E extends Throwable> RuntimeException rethrow(Throwable error) throws E { throw (E) error; }
    static RuntimeException error(String kind, String message) {
        switch (kind) {
            case "invalid_argument": return new NativeError.InvalidArgument(kind, message);
            case "out_of_range": return new NativeError.OutOfRange(kind, message);
            case "domain_error": case "arithmetic_error": return new NativeError.Arithmetic(kind, message);
            default:
                if (message.contains("pure virtual method")) return new NativeError.NotImplemented(kind, message);
                return new NativeException(kind, message);
        }
    }
    /**
     * Calls the generated C++ function {@code index}.
     *
     * @param index the native function
     * @param arguments its arguments
     * @return the decoded result
     */
    public static Object call(int index, Object... arguments) { return callInto(index, arguments, null); }
    static Object callInto(int index, Object[] arguments, NativeObject into) {
        final Out o = new Out();
        try {
            o.u32(arguments.length);
            for (final Object argument : arguments) put(o, argument, 0);
            // Java and C++ buffer standard output separately: flush before C++ writes.
            System.out.flush();
            final byte[] result = nativeCall(index, o.bytes());
            final int status = result[0];
            final In r = new In(Arrays.copyOfRange(result, 1, result.length));
            switch (status) {
                case 0: {
                    Object value;
                    if (into != null && !r.empty() && r.peek() == 10) {
                        r.u8();
                        final long address = r.i64();
                        r.u32();
                        r.i64();
                        adopt(into, address, null);
                        value = into;
                    } else value = get(r, arguments);
                    final int writebacks = r.u32();
                    for (int i = 0; i < writebacks; ++i) {
                        final @REF@<?> slot = o.slots.get(r.u32());
                        slot.rosettaPending = get(r, arguments);
                        slot.rosettaWritten = true;
                    }
                    return value;
                }
                case 2: {
                    final Throwable error = CALLBACK_ERROR.get();
                    CALLBACK_ERROR.remove();
                    get(r, null);
                    final Object message = get(r, null);
                    if (error != null) throw Rosetta.<RuntimeException>rethrow(error);
                    throw new NativeException("java", message instanceof String ? (String) message : "error in a Java callback");
                }
                case 1: {
                    final Object kind = get(r, null);
                    final Object message = get(r, null);
                    throw error(kind == null ? "native" : kind.toString().replace('-', '_'), message instanceof String ? (String) message : "native error");
                }
                default: throw new NativeException("allocation", "out of memory in a native call");
            }
        } finally {
            for (final long id : o.functions) retain(id, -1);
        }
    }
    /**
     * Creates the C++ object of a newly constructed Java object.
     *
     * @param self the Java object
     * @param declared the generated class whose constructor runs
     * @param director whether Java subclasses of it override C++ virtual methods
     * @param index the native constructor
     * @param arguments the constructor's arguments
     */
    public static void construct(NativeObject self, Class<?> declared, boolean director, int index, Object... arguments) {
        final Object[] all = new Object[arguments.length + 1];
        all[0] = director && self.getClass() != declared ? objectId(self) : 0L;
        System.arraycopy(arguments, 0, all, 1, arguments.length);
        callInto(index, all, self);
    }
    /**
     * A secondary base class's view of an object (Java has single inheritance): the same C++ object.
     *
     * @param <T> the base class
     * @param from the object
     * @param make creates the view
     * @return the view (null stays null)
     */
    public static <T extends NativeObject> T view(NativeObject from, Function<NativeObject.Raw, T> make) {
        if (from == null) return null;
        final T view = make.apply(NativeObject.RAW);
        view.rosettaHandle.address = from.rosettaHandle.address;
        view.rosettaHandle.owns = false;
        view.rosettaParent = from;
        return view;
    }
    static void close(NativeObject o) {
        final long address = o.rosettaHandle.address;
        if (address == 0) return;
        o.rosettaHandle.address = 0;
        if (o.rosettaHandle.owns) nativeRelease(address, true);
    }
    static void releaseLater(long address) { nativeRelease(address, false); }

    // Calls from C++: callbacks (kind 0) and overrides of virtual methods (kind 1). The first byte
    // of the result is the status (0, or 2: a Java exception, kept for the outer call to rethrow).
    private static byte[] invoke(int kind, long id, int generic, byte[] data) {
        int status = 0;
        Out o = new Out();
        try {
            final In r = new In(data);
            final int count = r.u32();
            final Object[] arguments = new Object[count];
            for (int i = 0; i < count; ++i) arguments[i] = get(r, null);
            final Object value = kind == 0 ? ((Callback) lookup(id)).call(arguments) : dispatch(generic, arguments);
            put(o, value, 0);
        } catch (Throwable error) {
            CALLBACK_ERROR.set(error);
            o = new Out();
            o.u8(5);
            o.text(error.getMessage() != null ? error.getMessage() : error.toString());
            status = 2;
        }
        System.out.flush();
        final byte[] payload = o.bytes();
        final byte[] result = new byte[payload.length + 1];
        result[0] = (byte) status;
        System.arraycopy(payload, 0, result, 1, payload.length);
        return result;
    }
    static RuntimeException noOverride(Object self, int generic) {
        return new NativeException("binding", (self == null ? "null" : self.getClass().getSimpleName()) + " has no override for virtual method " + generic);
    }
)java";

// The C++ JNI glue, appended to the generated library's main source. @C@ is the C entry points'
// prefix (rosetta_<module>), @JNI@ the mangled JNI name of the Rosetta class (Java_pkg_Rosetta).
inline constexpr const char* java_full_jni_cpp = R"cpp(
// ---------------------------------------------------------------- JNI glue (Java side: Rosetta.java)
#include <jni.h>
namespace {
JavaVM* rosetta_java_vm = nullptr;
jclass rosetta_java_class = nullptr;
jmethodID rosetta_java_invoke = nullptr;
jmethodID rosetta_java_retain = nullptr;
// A thread C++ started (not a Java thread): attached for its lifetime, detached when it ends.
struct RosettaJavaAttachment {
    JNIEnv* env = nullptr;
    ~RosettaJavaAttachment() { if (env && rosetta_java_vm) rosetta_java_vm->DetachCurrentThread(); }
};
JNIEnv* rosetta_java_env() {
    if (!rosetta_java_vm) return nullptr;
    JNIEnv* env = nullptr;
    if (rosetta_java_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_8) == JNI_OK) return env;
    thread_local RosettaJavaAttachment attachment;
    if (!attachment.env) {
        JavaVMAttachArgs arguments{JNI_VERSION_1_8, const_cast<char*>("rosetta-native"), nullptr};
        if (rosetta_java_vm->AttachCurrentThreadAsDaemon(reinterpret_cast<void**>(&attachment.env), &arguments) != JNI_OK) attachment.env = nullptr;
    }
    return attachment.env;
}
unsigned char* rosetta_java_message(const char* text, std::size_t* size) {
    // tag_string, length, bytes
    const std::size_t n = std::strlen(text);
    auto* out = static_cast<unsigned char*>(std::malloc(1 + 8 + n));
    if (!out) { *size = 0; return nullptr; }
    out[0] = 5;
    const unsigned long long length = n;
    std::memcpy(out + 1, &length, 8);
    std::memcpy(out + 9, text, n);
    *size = 9 + n;
    return out;
}
int rosetta_java_invoke_hook(int kind, unsigned long long id, unsigned generic, const unsigned char* data, std::size_t size,
                             unsigned char** out, std::size_t* out_size) {
    JNIEnv* env = rosetta_java_env();
    if (!env) { *out = rosetta_java_message("cannot attach this thread to the Java VM", out_size); return 2; }
    jbyteArray input = env->NewByteArray(static_cast<jsize>(size));
    if (!input) { env->ExceptionClear(); *out = rosetta_java_message("out of Java memory", out_size); return 2; }
    env->SetByteArrayRegion(input, 0, static_cast<jsize>(size), reinterpret_cast<const jbyte*>(data));
    auto result = static_cast<jbyteArray>(env->CallStaticObjectMethod(rosetta_java_class, rosetta_java_invoke, static_cast<jint>(kind),
                                                                      static_cast<jlong>(id), static_cast<jint>(generic), input));
    env->DeleteLocalRef(input);
    if (env->ExceptionCheck() || !result) {
        env->ExceptionClear();
        if (result) env->DeleteLocalRef(result);
        *out = rosetta_java_message("Java exception in a callback", out_size);
        return 2;
    }
    const jsize length = env->GetArrayLength(result);
    jbyte status = 0;
    if (length > 0) env->GetByteArrayRegion(result, 0, 1, &status);
    const std::size_t n = length > 0 ? static_cast<std::size_t>(length - 1) : 0;
    *out = static_cast<unsigned char*>(std::malloc(n ? n : 1));
    *out_size = n;
    if (n && *out) env->GetByteArrayRegion(result, 1, static_cast<jsize>(n), reinterpret_cast<jbyte*>(*out));
    env->DeleteLocalRef(result);
    if (!*out) { *out_size = 0; return 3; }
    return status;
}
void rosetta_java_retain_hook(unsigned long long id, int delta) {
    JNIEnv* env = rosetta_java_env();
    if (!env) return;
    // A pending exception (C++ releasing a reference while Java unwinds) is kept.
    jthrowable pending = env->ExceptionOccurred();
    if (pending) env->ExceptionClear();
    env->CallStaticVoidMethod(rosetta_java_class, rosetta_java_retain, static_cast<jlong>(id), static_cast<jint>(delta));
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (pending) { env->Throw(pending); env->DeleteLocalRef(pending); }
}
} // namespace
extern "C" JNIEXPORT jint JNICALL @JNI@_nativeInit(JNIEnv* env, jclass cls, jint version) {
    if (env->GetJavaVM(&rosetta_java_vm) != JNI_OK) return 2;
    if (!rosetta_java_class) rosetta_java_class = static_cast<jclass>(env->NewGlobalRef(cls));
    rosetta_java_invoke = env->GetStaticMethodID(cls, "invoke", "(IJI[B)[B");
    rosetta_java_retain = env->GetStaticMethodID(cls, "retain", "(JI)V");
    if (!rosetta_java_invoke || !rosetta_java_retain) return 2;
    return @C@_init(static_cast<int>(version), rosetta_java_invoke_hook, rosetta_java_retain_hook);
}
extern "C" JNIEXPORT jbyteArray JNICALL @JNI@_nativeCall(JNIEnv* env, jclass, jint index, jbyteArray arguments) {
    const jsize length = arguments ? env->GetArrayLength(arguments) : 0;
    std::vector<unsigned char> in(static_cast<std::size_t>(length));
    if (length) env->GetByteArrayRegion(arguments, 0, length, reinterpret_cast<jbyte*>(in.data()));
    unsigned char* out = nullptr;
    std::size_t out_size = 0;
    const int status = @C@_call(static_cast<unsigned>(index), in.data(), in.size(), &out, &out_size);
    jbyteArray result = env->NewByteArray(static_cast<jsize>(out_size + 1));
    if (result) {
        const jbyte head = static_cast<jbyte>(status);
        env->SetByteArrayRegion(result, 0, 1, &head);
        if (out_size) env->SetByteArrayRegion(result, 1, static_cast<jsize>(out_size), reinterpret_cast<const jbyte*>(out));
    }
    std::free(out);
    return result;
}
extern "C" JNIEXPORT void JNICALL @JNI@_nativeRelease(JNIEnv*, jclass, jlong holder, jboolean now) {
    @C@_release(reinterpret_cast<void*>(static_cast<std::uintptr_t>(holder)), now ? 1 : 0);
}
)cpp";

} // namespace rosetta::detail
