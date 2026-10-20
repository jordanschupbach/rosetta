import java.io.ByteArrayOutputStream;
import java.io.NotSerializableException;
import java.io.ObjectOutputStream;
import java.lang.ref.WeakReference;
import java.lang.reflect.InvocationTargetException;
import java.math.BigInteger;
import java.util.Arrays;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.ArrayList;

public final class TestJava {
    private TestJava() {}
    private static void check(boolean value) { if (!value) throw new AssertionError("check failed"); }
    @FunctionalInterface interface Action { void run() throws Throwable; }
    private static void fails(Action action, Class<? extends Throwable> type, String text) throws Throwable {
        try { action.run(); }
        catch (Throwable error) {
            if (error instanceof InvocationTargetException) error = error.getCause();
            if (!type.isInstance(error) || !String.valueOf(error.getMessage()).contains(text)) throw new AssertionError("unexpected failure", error);
            return;
        }
        throw new AssertionError("expected " + type.getName() + ": " + text);
    }
    private static Object call(String name, Class<?> parameter, Object value) throws Throwable {
        try { return JavaVerify.class.getMethod("r_verify__" + name, parameter).invoke(null, value); }
        catch (InvocationTargetException error) { throw error.getCause(); }
    }
    private static final class CustomUnsigned extends BigInteger {
        private static final long serialVersionUID = 1L;
        CustomUnsigned() { super("42"); }
        @Override public int signum() { throw new AssertionError("unsigned override must not enter JNI conversion"); }
    }
    private static void scalars() throws Throwable {
        for (String name : new String[]{"char","schar","short","int","long","ll"}) {
            check(call("echo_" + name, long.class, 42L).equals(42L));
        }
        int[] widths = {8,16,32,64}; String[] signed = {"schar","short","int","ll"};
        for (int i = 0; i < widths.length; ++i) {
            int bits = widths[i]; String name = "echo_" + signed[i];
            long low = bits == 64 ? Long.MIN_VALUE : -(1L << (bits-1));
            long high = bits == 64 ? Long.MAX_VALUE : (1L << (bits-1))-1;
            check(call(name,long.class,low).equals(low)); check(call(name,long.class,high).equals(high));
            if (bits < 64) {
                fails(() -> call(name,long.class,low-1),IllegalArgumentException.class,"range");
                fails(() -> call(name,long.class,high+1),IllegalArgumentException.class,"range");
            }
        }
        String[] unsigned = {"uchar","ushort","uint","ull"};
        for (int i = 0; i < widths.length; ++i) {
            String name = "echo_" + unsigned[i];
            BigInteger high = BigInteger.ONE.shiftLeft(widths[i]).subtract(BigInteger.ONE);
            check(call(name,BigInteger.class,high).equals(high));
            fails(() -> call(name,BigInteger.class,high.add(BigInteger.ONE)),IllegalArgumentException.class,"range");
            fails(() -> call(name,BigInteger.class,BigInteger.valueOf(-1)),IllegalArgumentException.class,"range");
            fails(() -> call(name,BigInteger.class,null),IllegalArgumentException.class,"null");
        }
        check(org.rosetta.bindings.Packaged_Api.r_demo__add(20,22)==42);
        fails(() -> JavaVerify.r_verify__echo_ull(new CustomUnsigned()),IllegalArgumentException.class,"unexpected Java argument type");
        BigInteger exact = BigInteger.ONE.shiftLeft(53).add(BigInteger.ONE);
        check(JavaDemo.r_demo__too_large().equals(exact));
        check(JavaVerify.r_verify__echo_ull(exact).equals(exact));
        check(JavaVerify.r_verify__echo_bool(true)); check(!JavaVerify.r_verify__echo_bool(false));
        check(JavaVerify.r_verify__echo_float(1.25f)==1.25f);
        check(JavaVerify.r_verify__echo_double(Double.MAX_VALUE)==Double.MAX_VALUE);
        fails(() -> JavaVerify.r_verify__echo_float(Float.POSITIVE_INFINITY),IllegalArgumentException.class,"finite");
        fails(() -> JavaVerify.r_verify__echo_double(Double.NaN),IllegalArgumentException.class,"finite");
        fails(() -> JavaVerify.r_verify__nonfinite_result(),IllegalArgumentException.class,"non-finite");
        byte[][] strings = {new byte[0],JavaDemo.utf8("世界😀"),new byte[]{97,0,98},new byte[]{-1,-2},new byte[100000]};
        for (byte[] bytes : strings) {
            byte[] result = JavaVerify.r_verify__echo_string(bytes);
            check(result != bytes && Arrays.equals(result,bytes));
        }
        check(Arrays.equals(JavaVerify.r_verify__embedded_nul(),new byte[]{97,0,98}));
        fails(() -> JavaVerify.r_verify__echo_string(null),IllegalArgumentException.class,"null");
        check(JavaVerify.r_verify__defaulted()==42 && JavaVerify.r_verify__defaulted(12)==12);
        check(JavaVerify.r_verify__alias_identity(42)==42);
        JavaVerify.r_verify__do_nothing();
        var invoke = JavaVerify.class.getDeclaredMethod("invoke",int.class,long.class,Object[].class);
        invoke.setAccessible(true);
        fails(() -> invoke.invoke(null,Integer.MAX_VALUE,0L,new Object[0]),IllegalArgumentException.class,"unknown native operation");
        fails(() -> invoke.invoke(null,0,0L,(Object)null),IllegalArgumentException.class,"null argument array");
        fails(() -> invoke.invoke(null,0,0L,new Object[]{true}),IllegalArgumentException.class,"unexpected Java argument type");
        for (int i=0;i<100;++i) {
            fails(() -> JavaDemo.r_demo__fail(),RuntimeException.class,"native failure");
            fails(() -> JavaVerify.r_verify__unknown_error(),RuntimeException.class,"unknown C++ exception");
            fails(() -> JavaVerify.r_verify__allocation_error(),OutOfMemoryError.class,"allocation");
            check(JavaDemo.r_demo__add(20,22)==42);
        }
        for (int i=0;i<1000;++i) check(Arrays.equals(JavaVerify.r_verify__echo_string(strings[1]),strings[1]));
    }
    private static WeakReference<JavaDemo.r_demo__Counter> abandoned() {
        return new WeakReference<>(new JavaDemo.r_demo__Counter(1));
    }
    private static void objects() throws Throwable {
        check(JavaDemo.r_demo__Counter.live()==0);
        fails(() -> new JavaDemo.r_demo__Counter(-1),RuntimeException.class,"negative initial");
        check(JavaDemo.r_demo__Counter.live()==0);
        var counter = new JavaDemo.r_demo__Counter(20); var alias = counter;
        check(counter.add(22)==42 && counter.get_count()==42 && counter.get_initial()==20);
        check(JavaDemo.r_demo__Counter.answer()==42);
        counter.set_count(7); check(alias.value()==7);
        byte[] label = JavaDemo.utf8("Java 😀\0"); counter.set_label(label);
        check(Arrays.equals(alias.get_label(),label));
        fails(() -> counter.set_count(Long.MAX_VALUE),IllegalArgumentException.class,"range");
        check(counter.value()==7);
        fails(() -> counter.set_label(null),IllegalArgumentException.class,"null");
        check(Arrays.equals(counter.get_label(),label));
        for (var method : counter.getClass().getMethods()) check(!method.getName().equals("set_initial"));
        fails(() -> { try (var stream = new ObjectOutputStream(new ByteArrayOutputStream())) { stream.writeObject(counter); } },NotSerializableException.class,"Counter");
        // Deliberately exchange opaque IDs across two record types in one library.
        try (var series = new JavaVerify.r_verify__Series(); var queue = new JavaVerify.r_verify__Worklist(2)) {
            var field = series.getClass().getDeclaredField("handle"); field.setAccessible(true);
            var other = queue.getClass().getDeclaredField("handle"); other.setAccessible(true);
            long saved = field.getLong(series); field.setLong(series,other.getLong(queue));
            fails(() -> series.get(),IllegalArgumentException.class,"foreign native owner type");
            field.setLong(series,saved);
        }
        counter.close(); counter.close(); check(JavaDemo.r_demo__Counter.live()==0);
        fails(() -> alias.value(),IllegalArgumentException.class,"closed");
        fails(() -> alias.get_count(),IllegalArgumentException.class,"closed");
        fails(() -> alias.set_count(1),IllegalArgumentException.class,"closed");
        var weak = abandoned();
        for (int i=0;i<300 && (weak.get()!=null || JavaDemo.r_demo__Counter.live()!=0);++i) { System.gc(); Thread.sleep(10); }
        check(weak.get()==null && JavaDemo.r_demo__Counter.live()==0);
        var racing = new JavaDemo.r_demo__Counter(1);
        var executor = Executors.newFixedThreadPool(4);
        try {
            var jobs = new ArrayList<Future<?>>();
            for (int i=0;i<20;++i) jobs.add(executor.submit(() -> { try { racing.add(1); } catch (IllegalArgumentException closed) { check(closed.getMessage().contains("closed")); } }));
            racing.close(); for (var job:jobs) job.get();
        } finally { racing.close(); executor.shutdown(); }
        check(JavaDemo.r_demo__Counter.live()==0);
        try (var c = new JavaDemo.r_demo__Counter(1)) { check(c.value()==1); }
        check(JavaDemo.r_demo__Counter.live()==0);
    }
    private static JavaVerify.r_verify__AsyncTask pending;
    private static void features() throws Throwable {
        check(JavaVerify.r_verify__maximum(20,42)==42 && JavaVerify.r_verify__template_box(42)==42);
        check(JavaVerify.r_verify__variadic_sum(10,20,12)==42 && JavaVerify.r_verify__constant_answer()==42);
        check(JavaVerify.r_verify__constrained_sum(20,22)==42 && JavaVerify.r_verify__hierarchy_value(42)==42);
        check(JavaVerify.r_verify__hierarchy_crosscast() && JavaVerify.r_verify__rosetta_r_c_identity(42)==42);
        fails(() -> JavaVerify.r_verify__constrained_sum(Integer.MAX_VALUE,1),ArithmeticException.class,"overflow");
        fails(() -> new JavaVerify.r_verify__Series(0,-1),IndexOutOfBoundsException.class,"series count");
        try (var series = new JavaVerify.r_verify__Series()) {
            check(series.size()==2 && series.get()==20 && series.scaled_sum(2)==82);
            check(JavaVerify.text(series.text()).equals("[20, 21]")); series.set(1,22); check(series.scaled_sum(1)==42);
            fails(() -> series.get(2),IndexOutOfBoundsException.class,"out of range");
        }
        String[][] expected = {{"m","z","a"},{"a","z","m"},{"z","m","a"},{"a","m","z"}};
        for (int order=1;order<=4;++order) {
            try (var queue = new JavaVerify.r_verify__Worklist(order)) {
                for (String text:new String[]{"a","z","m"}) queue.push(JavaVerify.utf8(text));
                check(queue.size().intValueExact()==3);
                for (String text:expected[order-1]) check(JavaVerify.text(queue.pop()).equals(text));
                fails(() -> queue.pop(),IndexOutOfBoundsException.class,"empty");
            }
        }
        fails(() -> new JavaVerify.r_verify__Worklist(0),IllegalArgumentException.class,"unknown worklist order");
        try (var task = new JavaVerify.r_verify__AsyncTask(21,0,false)) { check(task.waitFor(2000) && task.result()==42 && task.result()==42); }
        try (var task = new JavaVerify.r_verify__AsyncTask(1,0,true)) { check(task.waitFor(2000)); fails(() -> task.result(),RuntimeException.class,"async worker failure"); }
        try (var task = new JavaVerify.r_verify__AsyncTask(1,60000,false)) {
            check(!task.ready() && !task.waitFor(0) && task.cancel() && task.waitFor(2000));
            fails(() -> task.result(),RuntimeException.class,"cancelled");
        }
        fails(() -> new JavaVerify.r_verify__AsyncTask(1,-1,false),IndexOutOfBoundsException.class,"delay");
        check(JavaVerify.r_verify__AsyncTask.workers()==0);
        pending = new JavaVerify.r_verify__AsyncTask(1,60000,false);
    }
    public static void main(String[] args) throws Throwable {
        switch (args[0]) {
            case "scalars": scalars(); break;
            case "objects": objects(); break;
            case "features": features(); break;
            default: throw new AssertionError("unknown family");
        }
        System.out.println("Java " + args[0] + " checks passed");
    }
}
