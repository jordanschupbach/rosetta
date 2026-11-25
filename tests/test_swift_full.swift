// Swift API (--language swift) on the library-shaped example shared with the Python, Lua, Common
// Lisp, Julia, C#, VB, Go, Rust, PHP and Perl backends. Argument: the generated
// Sources/Features/Features.swift, for the documentation checks.
import Features
#if canImport(Glibc)
import Glibc
#elseif canImport(Darwin)
import Darwin
#endif

var checks = 0

func check(_ ok: Bool, _ what: String) {
    checks += 1
    if !ok { fatalError("check failed: \(what)") }
}

// The error `body` throws, which must mention `pattern`.
@discardableResult
func fails<T>(_ pattern: String, _ body: () throws -> T) -> Error {
    checks += 1
    do {
        _ = try body()
    } catch {
        if !"\(error)".contains(pattern) { fatalError("expected an error containing \(pattern), got: \(error)") }
        return error
    }
    fatalError("expected an error containing \(pattern), got none")
}

func kind(_ error: Error) -> NativeError.Kind? { (error as? NativeError)?.kind }

// Swift subclasses override C++ virtual methods.
final class Triangle: Shape {
    let base: Double, height: Double
    init(base: Double, height: Double) throws {
        self.base = base
        self.height = height
        try super.init()
    }
    override func area() throws -> Double { base * height / 2 }
    override func name() throws -> String { "triangle" }
}

final class Incomplete: Shape {}

struct Sentinel: Error, CustomStringConvertible {
    var description: String { "from Swift" }
}

final class Failing: Shape {
    override func area() throws -> Double { throw Sentinel() }
}

// super runs the C++ implementation: an override builds on it.
final class Loud: Circle {
    override func name() throws -> String { try super.name().uppercased() }
}

class Noisy: Circle {
    override func name() throws -> String { try super.name().lowercased() + "!" }
}

final class Louder: Noisy {}

final class UnitShape: Shape {
    let tag: String
    init(tag: String) throws {
        self.tag = tag
        try super.init()
    }
    override func area() throws -> Double { 1 }
}

final class Summer: Visitor {
    var seen: [Double] = []
    override func visit(_ value: Double) throws -> Double { value * 10 }
    override func finished(_ values: [Double]) throws { seen = values }
}

// A callback from a thread C++ did not start on.
var threadResult = 0.0
func threadMain(_: UnsafeMutableRawPointer?) -> UnsafeMutableRawPointer? {
    threadResult = (try? apply({ $0 + 1 }, 1)) ?? -1
    return nil
}

let source: String = {
    guard let file = fopen(CommandLine.arguments[1], "r") else { fatalError("cannot open the generated API") }
    defer { fclose(file) }
    var bytes: [UInt8] = []
    var buffer = [UInt8](repeating: 0, count: 65536)
    while true {
        let n = fread(&buffer, 1, buffer.count, file)
        if n <= 0 { break }
        bytes.append(contentsOf: buffer[0..<n])
    }
    return String(decoding: bytes, as: UTF8.self)
}()
let lines = source.split(separator: "\n", omittingEmptySubsequences: false).map(String.init)

// The doc comment right above the first line containing `declaration`.
func doc(_ declaration: String) -> String {
    guard let at = lines.firstIndex(where: { $0.contains(declaration) && !$0.trimmingPrefix("///") }) else {
        fatalError("not declared: \(declaration)")
    }
    var block: [String] = []
    var i = at
    while i > 0, lines[i - 1].trimmingPrefix("@") { i -= 1 }
    while i > 0, lines[i - 1].trimmingPrefix("///") {
        i -= 1
        let text = lines[i].drop(while: { $0 == " " }).dropFirst(3)
        block.insert(String(text.hasPrefix(" ") ? text.dropFirst() : text), at: 0)
    }
    return block.joined(separator: "\n")
}

extension String {
    // Whether the line, without its indentation, starts with `prefix`.
    func trimmingPrefix(_ prefix: String) -> Bool { drop(while: { $0 == " " }).hasPrefix(prefix) }
}

// Classes, inheritance and virtual dispatch.
let c1 = try Circle(2)
check(try c1.area() == 12 && c1.name() == "circle" && c1.twiceArea() == 24 && c1.radius() == 2, "circle methods")
check(c1.description == "Circle" && "\(c1)" == "Circle", "description names the class")
let asShape: Shape = c1
check(asShape === c1 && asShape is Circle, "class hierarchy")
var error = fails("negative radius") { try Circle(-1) }
check(kind(error) == .invalidArgument, "exception kind")
let s = try Square()
check(try s.side == 1 && s.area() == 1, "default constructor")
s.side = 3
check(try s.area() == 9 && Square(2).area() == 4, "property set and overloaded initializer")
check(try areaOf(s) == 9 && describe(c1) == "circle:0", "functions on classes")
// Enums.
check(try c1.kind() == .circle && s.kind() == .square, "enum results")
check(try filled(.solid) && !filled(Fill.none), "enum arguments")
check(Kind.custom.description == "custom" && Kind(rawValue: 42).description == "42" && Kind.custom.rawValue == 7, "enum values")
check(Shape.sidesUnknown == -1 && abs(golden - 1.618) < 1e-12, "constants")

// Most-derived results, ownership transfer and shared ownership.
let made = try makeShape(.circle, 1)
check((try made.cast(Circle.self)?.radius()) == 1 && made is Circle, "most-derived result")
check(made.cast(Square.self) == nil, "cast to another class fails")
let canvas = try Canvas()
try canvas.add(makeShape(.square, 2))
try canvas.share(sharedSquare(3))
let transferred = try Circle(1)
try canvas.add(transferred)
fails("closed") { try transferred.area() }
check(try canvas.size() == 3 && canvas.total() == 4 + 9 + 3, "ownership transfer")
check((try canvas.at(0).cast(Square.self)?.area()) == 4, "borrowed element")
do {
    let shared = try sharedSquare(2)
    try canvas.share(shared)
    check(try shared.area() == 4 && canvas.total() == 20, "shared ownership")
    check(try totalArea([Circle(1), shared]) == 7, "array of objects")
}

// Overloads: Swift overloads, resolved again in C++.
check(try pick(Int32(1)) == "int" && pick(1.5) == "double" && pick(2.0) == "double" && pick("x") == "string", "scalar overloads")
check(try pick([1.0, 2.0]) == "vector" && pick(s) == "shape", "container and class overloads")
check(try add(1) == 111 && add(1, 2) == 103 && add(1, 2, 3) == 6, "default arguments")

// Containers.
check(try countWords(["a", "b", "a"]) == ["a": 2, "b": 1], "dictionary result")
check(try minMax([3, 1, 2]) == (1, 3), "pair result")
error = fails("empty input") { try minMax([]) }
check(kind(error) == .outOfRange, "out_of_range kind")
check(try find([5, 7], 7) == 1 && find([5, 7], 9) == nil, "optional result")
let tr = try transpose([[1, 2, 3], [4, 5, 6]])
check(tr.count == 3 && tr[0] == [1, 4] && tr[2] == [3, 6], "nested arrays")
check(try byteSum([1, 2, 250]) == 253 && bytesOf("AZ") == [65, 90], "bytes")
let cs = try circles(3)
check(try cs.count == 3 && cs[2].radius() == 3, "array of class results")
check(try `repeat`("z", 3) == "zzz" && initial("hello") == "h", "characters")
// Non-const reference parameters are inout, written back.
var values = [1.0, 2.0]
try scale(&values, 3)
check(values == [3, 6], "inout argument written back")
error = fails("negative count") { try `repeat`("z", -1) }
check(kind(error) == .invalidArgument || kind(error) == .native, "invalid_argument kind")
check(try defaults() == "x*1-1.500000", "all defaults")
check(try defaults(.circle, "y", true) == "y*0!-1.500000", "some defaults")
check(doc("public func defaults(_ kind: Kind, _ label: String, _ loud: Bool) throws").contains("`weight = 0.5`"), "omitted defaults named")

// Closures both ways.
check(try apply({ $0 * 2 }, 21) == 42, "closure")
error = fails("from Swift") { try apply({ _ in throw Sentinel() }, 1) }
check(error is Sentinel, "a closure's error comes back unchanged")
let plus = try adder(40)
check(try plus(2) == 42, "C++ std::function as a Swift closure")
check(try apply({ x in Double(try adder(1)(Int32(x))) }, 3) == 4, "C++ function inside a closure")
var calls = 0
check(try apply({ calls += 1; return $0 }, 5) == 5 && calls == 1, "capturing closure")
var thread = pthread_t()
_ = pthread_create(&thread, nil, threadMain, nil)
_ = pthread_join(thread, nil)
check(threadResult == 2, "closure from another thread")

// Value type operators, chaining and borrowed references.
do {
    let v = try Vec2(1, 2)
    let w = try v + Vec2(3, 4)
    check(try w.x == 4 && w.y == 6 && (v * 2).y == 4 && (-v).x == -1 && (v - w).x == -3, "operators")
    check(try v == Vec2(1, 2) && !(v == w), "equality operator")
    check(try v[1] == 2 && v.at(1) == 2 && v.inner(Vec2(1, 1)) == 3, "subscript, index operator and rename")
    fails("Vec2 index") { try v[2] }
    let chained = try v.setX(10).setY(20)
    check(v.x == 10 && v.y == 20 && chained === v, "chaining returns the same Swift object")
    check(try (v += w) === v && v.x == 14, "compound assignment")
    v.x = 5
    check(v.x == 5, "property setter")
}
do {
    let seg = try Segment()
    seg.end = try Vec2(3, 4)
    check(try seg.length2() == 25, "object property set")
    let start = try seg.first()
    start.x = 3
    check(try seg.start.x == 3 && seg.length2() == 16, "borrowed reference writes through")
    seg.start.y = 4
    check(try seg.length2() == 0, "object property borrowed")
}
check(try Vec2(1, 2).clone().y == 2, "C++ clone")
check(try sum([Vec2(1, 2), Vec2(3, 4)]) == 10, "array of values")
do {
    let held = try Segment().first()
    check(held.x == 0, "borrowed reference keeps its owner alive")
}

// Templates, extensions, qualified names and copy types.
do {
    let stack = try IntStack()
    try stack.push(1)
    try stack.push(2)
    check(try stack.size() == 2 && stack.items() == [1, 2] && stack.pop() == 2, "template instance")
}
check(try clampInt(42) == 10 && clampInt(3, 5) == 3 && clampReal(2.5) == 2.5, "function templates")
check(try c1.label() == "#circle" && c1.label("@") == "@circle" && Circle.unit().radius() == 1, "extensions")
check(try featuresLegacyVersion() == 2 && FeaturesLegacyTag().id == 7, "qualified names")
check(FeaturesLegacyMode.off.description == "off", "qualified enum")
check(try Circle(2).diameter() == 4, "extension method")
do {
    let ledger = try Ledger()
    let copy = try ledger.latest()
    copy.value = 5
    check(ledger.stamp.value == 1, "copy type")
}
do {
    let aim = try Aim()
    aim.origin.x = 2
    check(aim.target.point.x == 2, "nested borrowed properties")
    let at = lines.firstIndex(where: { $0.contains("public var point: Vec2") })!
    check(lines[at + 2] == "    }", "read-only property has no setter")
}

// Errors, closing and releasing.
do {
    let x = try Circle(1)
    check(try x.area() == 3 && !x.isClosed, "before close")
    x.close()
    check(x.isClosed && x.description == "Circle (closed)", "closed")
    error = fails("closed") { try x.area() }
    check(kind(error) == .binding, "binding error kind")
    x.close()
}
for i in 1...200 {
    let tmp = try makeShape(.square, Double(i))
    check(try tmp.area() == Double(i * i), "many objects")
    if i % 2 == 0 { tmp.close() }
}

// Swift subclasses override virtual methods.
do {
    let t = try Triangle(base: 4, height: 3)
    check(try t.area() == 6 && areaOf(t) == 6 && t.twiceArea() == 12 && describe(t) == "triangle:7", "override called from C++ and from Swift")
    check(try totalArea([t, Circle(1), Square(2)]) == 13 && t.label("@") == "@triangle", "mixed overrides")
}
fails("pure virtual") { try areaOf(Incomplete()) }
error = fails("from Swift") { try areaOf(Failing()) }
check(error is Sentinel, "an override's error comes back unchanged")
check(!source.contains("public init() throws {\n        super.init(rosettaHandle: Rosetta.Handle())\n        try Rosetta.construct(self, 0, director: false)"),
      "an abstract class is constructed only as a Swift subclass")
error = fails("abstract") { try Shape() }
check(kind(error) == .binding, "constructing an abstract class")
do {
    let l = try Loud(2)
    check(try describe(l) == "CIRCLE:0" && l.name() == "CIRCLE" && l.area() == 12, "super runs the C++ method")
    check(try describe(Louder(1)) == "circle!:0", "inherited override")
}
let canvas2 = try Canvas()
do {
    let u = try UnitShape(tag: "kept")
    try canvas2.add(u)
    check(try u.area() == 1, "Swift object owned by C++")
}
try canvas2.share(Square(2))
check(try canvas2.total() == 5 && canvas2.size() == 2, "C++ keeps the Swift object alive")
do {
    let back = try canvas2.at(0)
    check(try (back as? UnitShape)?.tag == "kept" && back === canvas2.at(0), "the same Swift object comes back")
}
try canvas2.clear()
try canvas2.share(UnitShape(tag: ""))
check(try canvas2.total() == 1, "shared Swift object")
try canvas2.clear()
do {
    let sm = try Summer()
    check(try runVisitor(sm, [1, 2]) == 30, "callback interface")
    check(sm.seen == [1, 2], "subclass state")
    check(try sm.visit(4) == 40, "a direct call runs the override")
}
check(try runVisitor(Visitor(), [1, 2]) == 3, "C++ default implementation")

// DocC comments carry the C++ documentation.
do {
    let d = doc("public func add(_ a: Int32) throws -> Int32")
    check(d.contains("Adds three integers, Socha & Dorigo style."), "summary")
    check(d.contains("- Parameters:\n  - a: first term"), "parameter list")
    check(d.contains("- Returns: the sum"), "returns")
    check(d.contains("## Example") && d.contains("```") && d.contains("int total = add(1);"), "heading and code block")
    check(d.contains("See also ``pick`` and ``sum``."), "doc links")
    check(d.contains("``NativeError`` of kind `.outOfRange` (C++ `std::out_of_range`)"), "errors")
    check(d.contains("`b = 10`, `c = 100`"), "omitted defaults")
}
check(doc("public init(_ radius: Double) throws").contains("`.invalidArgument` (C++ `std::invalid_argument`)"), "initializer errors")
check(doc("public var x: Double").contains("Horizontal component."), "property documentation")
check(doc("open class Shape: NativeObject").contains("Abstract in C++: subclass it in Swift"), "class remarks")
check(doc("open func area() throws -> Double").contains("Pure virtual in C++"), "pure virtual methods marked")

// Policy modules: features::data functions are static functions of the enum Data.
do {
    let u = try Data.unit()
    check(u.x == 1 && u.y == 0, "submodule result")
}
check(try Data.scaled(3) == 6 && Data.scaled(2.5) == 6.25, "submodule overloads")
check(try Data.repeat("ab") == "abab" && `repeat`("x", 3) == "xxx", "submodule names are separate")
check(doc("public static func unit() throws -> Vec2").contains("Mapped by the policy's modules"), "submodule docs")
print("Swift API passed (\(checks) checks)")
