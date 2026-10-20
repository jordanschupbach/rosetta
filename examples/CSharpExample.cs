Console.WriteLine(CSharpDemo.r_demo__add(20, 22));
using (var counter = new CSharpDemo.r_demo__Counter(20)) {
    Console.WriteLine(counter.add(22));
    counter.count = 7;
    counter.label = System.Text.Encoding.UTF8.GetBytes("C# 😀");
    Console.WriteLine(System.Text.Encoding.UTF8.GetString(counter.label));
}
using (var series = new CSharpVerify.r_verify__Series()) {
    Console.WriteLine(series.scaled_sum(2));
}
