// The Swift API for tests/swift_full_names.hpp: classes named like the runtime's types, the
// standard library's or the module; Swift keywords and standard library functions as names;
// members named like NativeObject's; a renamed virtual method; multiple inheritance; C++ hiding;
// operators; 64-bit values; booleans and inout parameters. Argument: the generated Names.swift.
import Names

var checks = 0
func check(_ ok: Bool, _ what: String) {
    checks += 1
    if !ok { fatalError("check failed: \(what)") }
}
@discardableResult
func fails<T>(_ pattern: String, _ body: () throws -> T) -> Error {
    checks += 1
    do { _ = try body() } catch {
        if !"\(error)".contains(pattern) { fatalError("expected an error containing \(pattern), got: \(error)") }
        return error
    }
    fatalError("expected an error containing \(pattern), got none")
}

final class Shouting: Named {
    override func label() throws -> String { try super.label().uppercased() }
}
final class Pipe: Resource {
    override func close2() throws -> String { "pipe " + (try super.close2()) }
}
final class Deep: Leaf {
    override func describe() throws -> String { "deep " + (try super.describe()) }
}
final class Loud: Thing {
    override func label() throws -> String { "loud " + (try super.label()) }
}

// Classes named like the runtime's types, the standard library's, the module, or alike but for case.
check(try NativeObject_().native == 2 && NativeError_().what == "not an exception" && Complex_().re == 1.5, "the runtime's names")
check(try Rosetta_().id == 3 && String_().text == "text" && Array_().size == 5 && Names_().self_ == 6, "the standard library's and the module's names")
check(try Data().value == 4 && Data_.twice(4) == 8, "a class before a policy module")
check(try pointX(Point()) == 1 && pointY(Point_()) == 2, "classes alike but for case")
check(try Names.NativeObject_().cast(NativeError_.self) == nil, "class hierarchy")

// Standard library functions as functions take an underscore; keywords take backticks.
check(try print_(4) == 4 && min_(1, 2) == 2 && `repeat`("ab") == "abab" && `in`(1, 2) == 3 && count([1, 2]) == 2, "function names")
check(Swift.min(1, 2) == 1, "the standard library's functions stay theirs")

// Enum cases.
check(try modeValue(.on) == 0 && modeValue(.on2) == 1, "cases alike but for case")
check(try modeValue(.self_) == 2 && modeValue(.default) == 3 && modeValue(.rawValue2) == 4 && modeValue(.init_) == 5 &&
      modeValue(.type) == 6 && modeValue(.off) == 7, "keywords and reserved names as cases")
check(Mode.default.description == "default" && Mode(rawValue: 9).description == "9", "case names")

// Members.
let cursor = try Cursor()
check(try cursor.count() == 20 && cursor.count2 == 2 && cursor.rosettaHandle2 == 9, "a method before a field, and the runtime's names")
check(try cursor.close2() && !cursor.isClosed && cursor.cast2() == "cast" && cursor.cast(Cursor.self) === cursor, "NativeObject members first")
check(try cursor.description2() == "described" && cursor.description == "Cursor" && !cursor.isClosed2(), "description and isClosed")
check(try cursor.init_() == 7 && cursor.subscript_(2) == 6, "Swift's reserved names")
check(try cursor.getName() == "camel" && cursor.getName2(2) == "snakesnake", "names alike in camelCase")
check(try cursor.print(3) == 6 && cursor.move(4) == 4 && cursor.position == 4, "fine method names")
check(try Cursor.create() == 5, "class method")
cursor.close()
check(cursor.isClosed, "the generated close")

// A virtual method renamed in Swift: the override has the Swift name, and C++ calls it.
check(try closeResource(Resource()) == "C++ close" && closeResource(File()) == "file close", "renamed virtual method")
check(try closeResource(Pipe()) == "pipe C++ close" && File().close2() == "file close", "Swift override of a renamed method")

// Multiple inheritance: Counted is the superclass; Named's members are declared again.
let thing = try Thing()
check(thing.asNamed.cast(Thing.self) === thing, "both bases")
check(try thing.counted() == 3 && countedOf(thing) == 3 && thing.tag == "t", "the further base's members")
check(try thing.label() == "thing" && labelOf(thing.asNamed) == "thing", "a further base argument")
let named = try asNamed(thing)!
check(try named.cast(Thing.self) != nil && labelOf(named) == "thing", "a pointer to the further base")
check(try labelOf(Shouting()) == "NAMED" && labelOf(Loud().asNamed) == "loud thing", "Swift subclasses of either base")

// C++ hiding: Swift overloads Leaf::scale with Base's; Leaf::size takes another name.
let leaf = try Leaf()
check(try leaf.scale(2, 3) == 6 && leaf.scale(2) == 2, "overloads across classes")
check(try leaf.size2() == "big" && leaf.size() == 1 && Base().size() == 1, "a method with another result takes its own name")
check(try describeBase(Deep()) == "deep leaf" && describeBase(leaf) == "leaf", "override two levels deep")

// Operators, description and callAsFunction.
let money = try Money(250)
check(money.description == "2.50" && "\(money)" == "2.50", "to_string is the description")
check(try (money + Money(5)).cents == 255 && (money * 2).cents == 500, "operators")
check(try money < Money(300) && !(money < Money(1)) && !(!money) && money(5) == 255, "comparison, prefix operator and callAsFunction")

// Values.
check(try biggest() == UInt64.max && echoU64(UInt64.max) == UInt64.max, "unsigned 64-bit values")
check(try echoI64(Int64.min) == Int64.min, "signed 64-bit minimum")
check(try !negate(true) && negate(false), "booleans")
check(try kindOf(true) == "bool" && kindOf(Int64(5)) == "int" && kindOf("5") == "string", "booleans, numbers and strings stay apart")
var n: Int32 = 1, text = "a"
try bump(&n, &text)
check(n == 2 && text == "a!", "inout parameters written back")
check(try callTwice({ $0 + 1 }, 1) == 3, "closure")
let pairs = try ranked(["a", "b"])
check(pairs.count == 2 && pairs[1].0 == "b" && pairs[1].1 == 2, "array of tuples")
fails("Swift closure takes 1 arguments") { try callTwice({ _ in throw NativeError(.binding, "Swift closure takes 1 arguments") }, 1) }

let source = CommandLine.arguments[1]
check(source.hasSuffix("Names.swift"), "generated API path")
print("Swift names passed (\(checks) checks)")
