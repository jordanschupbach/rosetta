/** Compile this with the generated JavaDemo and JavaVerify classes. */
public final class JavaExample {
    private JavaExample() {}
    public static void main(String[] args) {
        System.out.println(JavaDemo.r_demo__add(20, 22));
        try (var counter = new JavaDemo.r_demo__Counter(20)) {
            System.out.println(counter.add(22));
            counter.set_count(7);
            counter.set_label(JavaDemo.utf8("Java 😀"));
            System.out.println(JavaDemo.text(counter.get_label()));
        }
        try (var series = new JavaVerify.r_verify__Series()) {
            System.out.println(series.scaled_sum(2));
        }
    }
}
