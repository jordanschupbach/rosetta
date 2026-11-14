// The full Java API for tests/java_full_names.hpp: Java keywords and contextual names, the methods
// of java.lang.Object, and classes named like the java.lang/java.util types and the binding's
// helper types. The module's classes are spelled jnames.X: an on-demand import would make
// String and the others ambiguous here.
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import jnames.Jnames;
import jnames.Keywords;
import jnames.NativeComplex;
import jnames.NativeError;
import jnames.NativePair;
import jnames.NativeRef;
import jnames.Word;

public final class TestJavaNames {
    static int checks;
    static void check(boolean ok, String what) {
        ++checks;
        if (!ok) throw new AssertionError("check failed: " + what);
    }

    public static void main(String[] args) throws Exception {
        if (args.length > 0 && args[0].equals("--interleave")) {
            System.out.println("a");
            Jnames.say("b\n");
            System.out.println("c");
            Jnames.sayAround(() -> System.out.println("inside"));
            Jnames.say("d\n");
            return;
        }
        // Classes named like java.lang and java.util types.
        try (jnames.String s = new jnames.String()) {
            check(s.length() == 104 && s.getText().equals("mine") && Jnames.greet(s).equals("hello mine"), "the module's own String");
        }
        check(new jnames.Integer().getValue() == 5, "the module's own Integer");
        check(jnames.Math.twice(4) == 8, "the module's own Math (a static method)");
        check(new jnames.System().getExitCode() == 3, "the module's own System");
        check(new jnames.Thread().id() == 7, "the module's own Thread");
        check(new jnames.Object().getMarker() == 9, "the module's own Object");
        check(new jnames.List().size() == 3, "the module's own List");
        check(new jnames.Complex().norm() == 25 && Jnames.normOf(new jnames.Complex()) == 25, "the module's own Complex");
        check(Jnames.words().equals(List.of("a", "b")), "std::vector<std::string> is a java.util.List");
        final Map<String, Integer> counts = Jnames.counts(List.of("a", "b", "a"));
        check(counts.get("a") == 2 && counts.get("b") == 1, "std::map is a java.util.Map");
        check(Jnames.maybe(true).getAsDouble() == 2.5 && Jnames.maybe(false).isEmpty(), "std::optional is an OptionalDouble");
        check(Jnames.twice(new NativeComplex(1, 2)).equals(new NativeComplex(2, 4)), "std::complex is the NativeComplex record");
        final NativePair<Integer, String> pair = Jnames.pair();
        check(pair.first() == 1 && pair.second().equals("one"), "std::pair is the NativePair record");
        check(Jnames.apply(x -> x * 10, 1.5) == 15, "std::function is a DoubleUnaryOperator");
        check(Jnames.fold3((a, b, c) -> a * 100 + b * 10 + c) == 123, "a three-argument std::function is a generated Function3");
        final NativeRef<Integer> ref = new NativeRef<>(41);
        Jnames.bump(ref);
        check(ref.value == 42, "int& is a NativeRef");
        try { Jnames.fail(-1); check(false, "no exception"); }
        catch (NativeError.InvalidArgument error) { check(error.kind().equals("invalid_argument"), "NativeError.InvalidArgument"); }
        check(Jnames.toString_().equals("module") && Jnames.wait_() == 20, "module functions named like Object methods");

        // Keywords and Object's methods.
        try (Keywords k = new Keywords()) {
            check(k.class_() == 1 && k.new_() == 2 && k.default_() == 3 && k.int_() == 4, "renamed to C++ keywords");
            check(k.package_() == 5 && k.synchronized_() == 6 && k.instanceof_(1, 2) == 12, "Java keywords");
            check(k.var() == 7 && k.record() == 8 && k.yield() == 9, "contextual names stay");
            check(k.getClass_().equals("C++ class") && k.getClass() == Keywords.class, "getClass is final");
            check(k.wait_() == 10 && k.notify_() == 11 && k.finalize_() == 13 && k.close_() == 14, "final and lifetime methods");
            check(k.hashCode_() == 12 && k.equals_(k), "hashCode and equals of other signatures");
            check(k.toString().equals("Keywords!"), "to_string is toString");
            check(k.clone().int_() == 4, "clone");
            check(k.getClassField() == 15, "field named class");
            k.setClass(16);
            check(k.getClassField() == 16, "its setter");
            check(k.getWord() == Word.record, "enum field");
        }
        check(Arrays.stream(Word.values()).map(Enum::name).toList().equals(List.of("null_", "var", "record", "package_", "yield")), "enumerators");
        check(Jnames.wordOf(3) == Word.package_ && Word.null_.nativeName().equals("null"), "keyword enumerators");

        // Java and C++ buffer standard output separately; piped, they must still come out in order.
        final List<String> command = new java.util.ArrayList<>(List.of(
            java.nio.file.Path.of(System.getProperty("java.home"), "bin", "java").toString(),
            "-cp", System.getProperty("java.class.path")));
        if (System.getProperty("jnames.library") != null) command.add("-Djnames.library=" + System.getProperty("jnames.library"));
        command.addAll(List.of("TestJavaNames", "--interleave"));
        final Process child = new ProcessBuilder(command).redirectErrorStream(true).start();
        final String ordered = new String(child.getInputStream().readAllBytes(), java.nio.charset.StandardCharsets.UTF_8).strip();
        check(child.waitFor() == 0 && ordered.equals("a\nb\nc\nbefore\ninside\nafter\nd"),
              "System.out and std::cout output interleave in order (" + ordered.replace("\n", " ") + ")");
        System.out.println("Java names passed (" + checks + " checks)");
    }
}
