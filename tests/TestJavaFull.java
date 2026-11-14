// Full Java API (--java-api full) on the library-shaped example shared with the Python, R6, Lua,
// Common Lisp, Julia and C# backends. Argument: the generated source root (for the Javadoc
// checks); the JNI library is found through -Dfeatures.library (or FEATURES_JAVA_LIBRARY).
import features.*;
import features.data.Data;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;

import static features.Features.*;

public final class TestJavaFull {
    // Directors: Java subclasses implement C++ virtual methods.
    static final class Triangle extends Shape {
        final double base, height;
        Triangle(double b, double h) { base = b; height = h; }
        @Override public double area() { return base * height / 2; }
        @Override public String name() { return "triangle"; }
    }
    static final class Incomplete extends Shape {}
    static final class Failing extends Shape {
        @Override public double area() { throw new IllegalStateException("from Java"); }
    }
    // super.name() runs the native implementation rather than dispatching back to the override.
    static final class Loud extends Circle {
        Loud(double radius) { super(radius); }
        @Override public String name() { return super.name().toUpperCase(); }
    }
    static class Noisy extends Circle {
        Noisy(double radius) { super(radius); }
        @Override public String name() { return super.name().toLowerCase() + "!"; }
    }
    static final class Louder extends Noisy { Louder(double radius) { super(radius); } }
    static final class UnitShape extends Shape {
        Object tag;
        @Override public double area() { return 1; }
    }
    static final class Summer extends Visitor {
        double[] seen;
        @Override public double visit(double value) { return value * 10; }
        @Override public void finished(double[] values) { seen = values; }
    }

    static int checks;
    static void check(boolean ok, String what) {
        ++checks;
        if (!ok) throw new AssertionError("check failed: " + what);
    }
    interface Action { void run() throws Exception; }
    static void fails(Action action, String pattern) {
        ++checks;
        String message = null;
        try { action.run(); }
        catch (Throwable error) { message = String.valueOf(error.getMessage()); }
        if (message == null) throw new AssertionError("expected an error containing \"" + pattern + "\", got none");
        if (!message.contains(pattern)) throw new AssertionError("expected an error containing \"" + pattern + "\", got: " + message);
    }
    static void collect() throws InterruptedException {
        for (int i = 0; i < 3; ++i) { System.gc(); Thread.sleep(20); }
    }
    static String source;
    // The Javadoc block right above the first line containing `declaration` in `file`.
    static String doc(String file, String declaration) throws Exception {
        final String[] lines = Files.readString(Path.of(source, file)).split("\n");
        int at = -1;
        for (int i = 0; i < lines.length; ++i)
            if (lines[i].contains(declaration) && !lines[i].trim().startsWith("*")) { at = i; break; }
        if (at < 0) throw new AssertionError("not declared: " + declaration);
        final List<String> block = new ArrayList<>();
        int i = at - 1;
        while (i >= 0 && lines[i].trim().startsWith("@")) --i;  // annotations
        for (; i >= 0 && (lines[i].trim().startsWith("*") || lines[i].trim().startsWith("/**")); --i) block.add(0, lines[i].trim());
        return String.join("\n", block);
    }

    public static void main(String[] args) throws Exception {
        source = args[0];

        // Classes, inheritance and virtual dispatch.
        final Circle c1 = new Circle(2);
        check(c1.area() == 12 && c1.name().equals("circle") && c1.twiceArea() == 24 && c1.radius() == 2, "circle methods");
        check(c1.toString().contains("Circle"), "toString names the class");
        check(c1 instanceof Shape && c1 instanceof NativeObject && c1 instanceof AutoCloseable, "class hierarchy");
        fails(() -> new Circle(-1), "negative radius");
        final Square s = new Square();
        check(s.getSide() == 1 && s.area() == 1, "default constructor");
        s.setSide(3);
        check(s.area() == 9 && new Square(2).area() == 4, "field set");
        check(areaOf(s) == 9 && describe(c1).equals("circle:0"), "functions on classes");
        // Enums.
        check(c1.kind() == Kind.circle && s.kind() == Kind.square, "enum results");
        check(filled(Fill.solid) && !filled(Fill.None), "enum arguments");
        check(Arrays.stream(Kind.values()).anyMatch(k -> k.name().equals("custom")) && Kind.custom.value() == 7, "enum members");
        check(Shape.SIDES_UNKNOWN == -1 && Math.abs(GOLDEN - 1.618) < 1e-12, "constants");

        // Most-derived results, ownership transfer and shared ownership.
        final Shape made = makeShape(Kind.circle, 1);
        check(made instanceof Circle && ((Circle) made).radius() == 1, "most-derived result");
        Canvas canvas = new Canvas();
        canvas.add(makeShape(Kind.square, 2));
        canvas.share(sharedSquare(3));
        final Circle transferred = new Circle(1);
        canvas.add(transferred);
        fails(() -> transferred.area(), "closed");
        check(canvas.size() == 3 && canvas.total() == 4 + 9 + 3, "ownership transfer");
        check(canvas.at(0) instanceof Square && canvas.at(0).area() == 4, "borrowed element");
        {
            final Shape shared = sharedSquare(2);
            canvas.share(shared);
            check(shared.area() == 4 && canvas.total() == 20, "shared ownership");
            check(totalArea(List.of(new Circle(1), shared)) == 7, "list of objects");
        }
        try (Canvas owned = new Canvas()) {
            owned.add(new Square(2));
            owned.add(new Circle(1));
            double total = 0;
            int count = 0;
            for (final Shape shape : owned) { total += shape.area(); ++count; }
            check(count == 2 && total == 4 + 3, "Iterable from size() and at()");
        }

        // Overload resolution by Java type, checked again in C++.
        check(pick(1).equals("int") && pick(1.5).equals("double") && pick(2.0).equals("double") && pick("x").equals("string"), "scalar overloads");
        check(pick(new double[] {1, 2}).equals("vector") && pick(s).equals("shape"), "container and class overloads");
        check(add(1) == 111 && add(1, 2) == 103 && add(1, 2, 3) == 6, "default arguments");

        // Containers.
        final Map<String, Integer> counts = countWords(List.of("a", "b", "a"));
        check(counts.get("a") == 2 && counts.get("b") == 1, "map result");
        check(minMax(new double[] {3, 1, 2}).equals(new Pair<>(1.0, 3.0)), "pair result");
        check(minMax(new double[] {3, 1, 2}).first() == 1, "pair record");
        fails(() -> minMax(new double[0]), "empty input");
        check(find(new int[] {5, 7}, 7).getAsInt() == 1 && find(new int[] {5, 7}, 9).isEmpty(), "optional result");
        final List<double[]> tr = transpose(List.of(new double[] {1, 2, 3}, new double[] {4, 5, 6}));
        check(tr.size() == 3 && Arrays.equals(tr.get(0), new double[] {1, 4}), "nested vectors");
        check(byteSum(new byte[] {1, 2, (byte) 250}) == 253 && Arrays.equals(bytesOf("AZ"), new byte[] {65, 90}), "bytes");
        final List<Circle> cs = circles(3);
        check(cs.size() == 3 && cs.get(2).radius() == 3, "list of class results");
        check(repeat('z', 3).equals("zzz") && initial("hello") == 'h', "chars");
        // Mutable reference parameters are Ref arguments.
        final Ref<double[]> values = new Ref<>(new double[] {1, 2});
        scale(values, 3);
        check(Arrays.equals(values.value, new double[] {3, 6}), "Ref argument written back");
        fails(() -> repeat('z', -1), "negative count");
        check(defaults().equals("x*1-1.500000"), "all defaults");
        check(doc("features/Features.java", "public static String defaults(Kind kind, String label, boolean loud)").contains("{@code weight = 0.5}"), "omitted defaults named");

        // Callbacks both ways.
        check(apply(x -> x * 2, 21) == 42, "callback");
        fails(() -> apply(x -> { throw new IllegalStateException("inside callback"); }, 1), "inside callback");
        // The Java exception comes back as itself.
        try { apply(x -> { throw new ArithmeticException("divide"); }, 1); check(false, "callback exception"); }
        catch (ArithmeticException error) { check(error.getMessage().equals("divide"), "callback exception type"); }
        final java.util.function.IntUnaryOperator plus = adder(40);
        check(plus.applyAsInt(2) == 42, "native function as functional interface");
        check(apply(x -> adder(1).applyAsInt((int) x), 3) == 4, "native function inside a callback");
        check(CompletableFuture.supplyAsync(() -> apply(x -> x + 1, 1)).get() == 2, "callback from another thread");

        // Value type operators, chaining and borrowed references.
        {
            final Vec2 v = new Vec2(1, 2);
            final Vec2 w = v.plus(new Vec2(3, 4));
            check(w.getX() == 4 && w.getY() == 6 && v.times(2).getY() == 4 && v.negate().getX() == -1 && v.minus(w).getX() == -3, "operators");
            check(v.equals(new Vec2(1, 2)) && !v.equals(w) && !v.equals("text"), "equals from operator==");
            check(v.hashCode() == new Vec2(1, 2).hashCode(), "hashCode consistent with equals");
            check(v.get(1) == 2 && v.inner(new Vec2(1, 1)) == 3, "get from operator[]");
            fails(() -> v.get(2), "Vec2 index");
            final Vec2 chained = v.setX(10).setY(20);  // the C++ set_x (the field setter is setXField)
            check(v.getX() == 10 && v.getY() == 20 && chained.getX() == 10, "chaining");
            check(!v.equals(null), "null equality");
        }
        {
            final Segment seg = new Segment();
            seg.setEnd(new Vec2(3, 4));
            check(seg.length2() == 25, "field object set");
            final Vec2 start = seg.first();
            start.setX(3);
            check(seg.getStart().getX() == 3 && seg.length2() == 16, "borrowed reference writes through");
            seg.getStart().setY(4);
            check(seg.length2() == 0, "field object borrowed");
        }
        check(new Vec2(1, 2).clone().getY() == 2, "clone");
        check(sum(List.of(new Vec2(1, 2), new Vec2(3, 4))) == 10, "list of values");
        {
            final Vec2 held = new Segment().first();
            collect();
            check(held.getX() == 0, "borrowed reference keeps its owner alive");
        }

        // Templates, extensions, qualified names and copy types.
        try (IntStack stack = new IntStack()) {
            stack.push(1);
            stack.push(2);
            check(stack.size() == 2 && Arrays.equals(stack.items(), new int[] {1, 2}) && stack.pop() == 2, "template instance");
        }
        check(clampInt(42) == 10 && clampInt(3, 5) == 3 && clampReal(2.5) == 2.5, "function templates");
        check(c1.label().equals("#circle") && c1.label("@").equals("@circle") && Circle.unit().radius() == 1, "extensions");
        check(featuresLegacyVersion() == 2 && new FeaturesLegacyTag().getId() == 7, "qualified names");
        check(Arrays.stream(FeaturesLegacyMode.values()).anyMatch(m -> m.name().equals("off")), "qualified enum");
        check(new Circle(2).diameter() == 4, "extension method");
        {
            final Ledger ledger = new Ledger();
            final Stamp copy = ledger.latest();
            copy.setValue(5);
            check(ledger.getStamp().getValue() == 1, "copy type");
        }
        {
            final Aim aim = new Aim();
            aim.getOrigin().setX(2);
            check(aim.getTarget().getPoint().getX() == 2, "nested borrowed fields");
        }
        check(Arrays.stream(Target.class.getMethods()).noneMatch(m -> m.getName().equals("setPoint")), "read-only field has no setter");

        // Errors, closing and garbage collection.
        try { new Circle(-1); check(false, "no exception"); }
        catch (NativeError.InvalidArgument error) { check(error.kind().equals("invalid_argument") && error instanceof IllegalArgumentException, "exception kind"); }
        try { new Vec2(1, 2).get(5); check(false, "no exception"); }
        catch (IndexOutOfBoundsException error) { check(error instanceof NativeError && ((NativeError) error).kind().equals("out_of_range"), "out_of_range is IndexOutOfBoundsException"); }
        fails(() -> areaOf(null), "argument 'shape'");
        try { areaOf(null); } catch (NativeException error) { check(error.kind().equals("binding"), "binding error kind"); }
        {
            final Circle x = new Circle(1);
            check(x.area() == 3, "before close");
            x.close();
            check(x.isClosed(), "closed");
            fails(() -> x.area(), "closed");
            x.close();
        }
        for (int i = 1; i <= 200; ++i)
            try (Shape tmp = makeShape(Kind.square, i)) { check(tmp.area() == (double) i * i, "many objects"); }
        collect();

        // Directors.
        {
            final Triangle t = new Triangle(4, 3);
            check(t.area() == 6 && areaOf(t) == 6 && t.twiceArea() == 12 && describe(t).equals("triangle:7"), "Java override called from C++");
            check(totalArea(List.of(t, new Circle(1), new Square(2))) == 13 && t.label("@").equals("@triangle"), "mixed overrides");
        }
        fails(() -> areaOf(new Incomplete()), "pure virtual method");
        try { areaOf(new Incomplete()); } catch (UnsupportedOperationException error) { check(error instanceof NativeError, "pure virtual is UnsupportedOperationException"); }
        fails(() -> areaOf(new Failing()), "from Java");
        try { areaOf(new Failing()); } catch (IllegalStateException error) { check(error.getMessage().equals("from Java"), "override exception comes back as itself"); }
        check(describe(new Loud(1)).equals("CIRCLE:0") && new Loud(2).area() == 12, "base call runs the C++ method");
        check(describe(new Louder(1)).equals("circle!:0"), "deeper subclasses");
        canvas = new Canvas();
        {
            final UnitShape u = new UnitShape();
            u.tag = "kept";
            canvas.add(u);
            check(u.area() == 1, "subclass object owned by C++");
        }
        collect();
        canvas.share(new Square(2));
        check(canvas.total() == 5 && canvas.size() == 2, "C++ keeps the Java object alive");
        {
            final Shape back = canvas.at(0);
            check(back instanceof UnitShape && "kept".equals(((UnitShape) back).tag) && back == canvas.at(0), "the same Java object comes back");
        }
        canvas.clear();
        collect();
        canvas.share(new UnitShape());
        collect();
        check(canvas.total() == 1, "shared subclass object");
        canvas.clear();
        collect();
        {
            final Summer summer = new Summer();
            check(runVisitor(summer, new double[] {1, 2}) == 30 && Arrays.equals(summer.seen, new double[] {1, 2}), "callback interface");
        }
        check(runVisitor(new Visitor(), new double[] {1, 2}) == 3, "native default implementation");

        // Javadoc carries the C++ documentation.
        {
            final String d = doc("features/Features.java", "public static int add(int a, int b, int c)");
            check(d.contains("Adds three integers, Socha &amp; Dorigo style."), "summary");
            check(d.contains("@param a first term"), "parameter");
            check(d.contains("@return the sum"), "returns");
            check(d.contains("@throws NativeError.OutOfRange never, in practice"), "throws");
            check(d.contains("<pre>{@code\n* int total = add(1);  // 111\n* }</pre>"), "code block");
            check(d.contains("{@code int features::add(int a, int b = 10, int c = 100)}"), "C++ signature");
            check(d.contains("@see Features#pick"), "see");
            check(d.contains("<li>first item"), "list");
        }
        check(doc("features/Circle.java", "public Circle(double radius)").contains("@throws NativeError.InvalidArgument"), "constructor exceptions");
        check(doc("features/Vec2.java", "public double getX()").contains("Horizontal component."), "field documentation");
        check(doc("features/Shape.java", "public class Shape").contains("extend it in Java"), "class remarks");
        check(doc("features/Features.java", "public static String defaults()").contains("&#64; sign"), "@ escaped");

        // Policy modules: features::data functions live in the subpackage features.data.
        {
            final Vec2 u = Data.unit();
            check(u.getX() == 1 && u.getY() == 0, "submodule result");
        }
        check(Data.scaled(3) == 6 && Data.scaled(2.5) == 6.25, "submodule overloads");
        check(Data.repeat("ab").equals("abab") && repeat('x', 3).equals("xxx"), "submodule names are separate");
        check(doc("features/data/Data.java", "public static features.Vec2 unit()").contains("Mapped by the policy's modules"), "submodule docs");
        collect();
        System.out.println("Java full API passed (" + checks + " checks)");
    }
}
