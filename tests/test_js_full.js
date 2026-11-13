// JavaScript backend (--language javascript) on the library-shaped example shared with the
// Python, R6 and Lua backends. Run with node --expose-gc.
// Arguments: the compiled addon, then the generated TypeScript declarations.
"use strict";
const fs = require("fs");
const assert = require("assert");
const [modulePath, typesPath] = process.argv.slice(2);
const api = require(require("path").resolve(modulePath));

let checks = 0;
function check(condition, message) {
    checks++;
    if (!condition) throw new Error(message || "check failed");
}
function fails(fn, pattern, type) {
    checks++;
    let error;
    try { fn(); } catch (e) { error = e; }
    if (!error) throw new Error(`expected error containing '${pattern}', got no error`);
    const message = String(error && error.message !== undefined ? error.message : error);
    if (!message.includes(pattern)) throw new Error(`expected error containing '${pattern}', got: ${message}`);
    if (type && !(error instanceof type)) throw new Error(`expected a ${type.name} for '${pattern}', got ${error && error.constructor && error.constructor.name}`);
}
const same = (a, b) => { assert.deepStrictEqual(a, b); checks++; return true; };
const collect = () => { global.gc(); global.gc(); };

// Classes, inheritance and virtual dispatch.
const { Circle, Square } = api;
const c1 = new Circle(2);
check(c1.area() === 12 && c1.name() === "circle" && c1.twice_area() === 24 && c1.radius() === 2);
check(c1 instanceof Circle && c1 instanceof api.Shape && !(c1 instanceof Square));
check(String(c1) === "[object Circle]");
fails(() => new Circle(-1), "negative radius");
fails(() => new api.Shape(), "abstract", TypeError);
fails(() => Circle(1), "without 'new'", TypeError);
const s = new Square();
check(s.side === 1 && s.area() === 1);
s.side = 3;
check(s.area() === 9 && new Square(2).area() === 4);
fails(() => { s.side = "wide"; }, "field 'side'", TypeError);
check(api.area_of(s) === 9 && api.describe(c1) === "circle:0");
// Enums are numbers (with reverse names, like TypeScript enums); arguments also accept names.
check(c1.kind() === api.Kind.circle && api.Kind.custom === 7 && s.kind() === api.Kind.square && api.Kind[7] === "custom");
check(api.filled("solid") && !api.filled(api.Fill.None));
fails(() => api.filled("bogus"), "unknown enumerator", TypeError);
check(Object.isFrozen(api.Kind));
check(api.Shape.sides_unknown === -1 && Math.abs(api.golden - 1.618) < 1e-12);

// Most-derived results, ownership transfer and shared ownership.
const made = api.make_shape("circle", 1);
check(made instanceof Circle && made.radius() === 1);
let canvas = new api.Canvas();
canvas.add(api.make_shape(api.Kind.square, 2));
canvas.share(api.shared_square(3));
const transferred = new Circle(1);
canvas.add(transferred);
fails(() => transferred.area(), "closed");
check(canvas.size() === 3 && canvas.total() === 4 + 9 + 3);
const first = canvas.at(0);
check(first instanceof Square && first.area() === 4);
const shared = api.shared_square(2);
canvas.share(shared);
check(shared.area() === 4 && canvas.total() === 20);
check(api.total_area([new Circle(1), shared]) === 7);

// Overload resolution (JavaScript has one number type: the first matching overload wins).
check(api.pick(1) === "int" && api.pick(1.5) === "double" && api.pick("x") === "string");
check(api.pick([1, 2]) === "vector" && api.pick(new Float64Array([1, 2])) === "vector" && api.pick(s) === "shape");
check(api.add(1) === 111 && api.add(1, 2) === 103 && api.add(1, 2, 3) === 6 && api.add(1, undefined) === 111);
fails(() => api.add(), "incompatible arguments", TypeError);
fails(() => api.add(1, 2, 3, 4), "supported signatures");
fails(() => api.add("x"), "argument 'a'", TypeError);
fails(() => api.add(1.5), "fraction");
check(api.defaults() === "x*1" + (0.5 - 2).toFixed(6) && api.defaults("custom", "y", true) === "y*7!" + (0.5 - 2).toFixed(6));

// Containers.
same(api.count_words(["a", "b", "a"]), { a: 2, b: 1 });
same(api.min_max([3, 1, 2]), [1, 3]);
fails(() => api.min_max([]), "empty input", RangeError);
check(api.find([5, 7], 7) === 1 && api.find([5, 7], 9) === null && api.find(new Int32Array([5, 7]), 7) === 1);
same(api.transpose([[1, 2, 3], [4, 5, 6]]), [[1, 4], [2, 5], [3, 6]]);
check(api.byte_sum([1, 2, 250]) === 253 && api.byte_sum(Buffer.from([1, 2, 3])) === 6);
same(api.bytes_of("AZ"), [65, 90]);
fails(() => api.byte_sum([256]), "argument 'data'");
const cs = api.circles(3);
check(cs.length === 3 && cs[2].radius() === 3 && cs[2] instanceof Circle);
check(api.repeat("z", 3) === "zzz" && api.initial("hello") === "h");
// Mutable reference parameters update a Ref cell; plain arrays are passed by value.
const values = api.ref([1, 2]);
check(values instanceof api.Ref);
api.scale(values, 3);
same(values.value, [3, 6]);
const plain = [1, 2];
api.scale(plain, 3);
same(plain, [1, 2]);
fails(() => api.scale(api.ref("x"), 2), "argument 'values'");
fails(() => api.repeat("z", -1), "negative count");
fails(() => api.repeat("z", 2 ** 40), "argument 'count'", RangeError);

// Callbacks both ways; JavaScript exceptions pass through C++ unchanged.
check(api.apply((x) => x * 2, 21) === 42);
class Custom extends Error {}
const thrown = new Custom("inside callback");
let caught;
try { api.apply(() => { throw thrown; }, 1); } catch (e) { caught = e; }
check(caught === thrown, "the callback's own exception object comes back");
fails(() => api.apply(() => "text", 1), "does not convert", TypeError);
const plus = api.adder(40);
check(typeof plus === "function" && plus(2) === 42);
check(api.apply(api.adder(1), 3) === 4);
let total = 0;
for (let i = 0; i < 10000; i++) total += api.apply((x) => x + 1, i);
check(total === 10000 * 10001 / 2);

// Value type operators (as methods), chaining and borrowed references.
const { Vec2 } = api;
const v = new Vec2(1, 2);
const w = v.plus(new Vec2(3, 4));
check(w.x === 4 && w.y === 6 && v.times(2).y === 4 && v.negate().x === -1 && v.minus(w).x === -3);
check(v.equals(new Vec2(1, 2)) && !v.equals(w));
check(v.at(1) === 2 && v.inner(new Vec2(1, 1)) === 3);
fails(() => v.at(2), "Vec2 index", RangeError);
const chained = v.set_x(10).set_y(20);
check(chained === v && v.x === 10 && v.y === 20);
const seg = new api.Segment();
seg.end = new Vec2(3, 4);
check(seg.length2() === 25);
const start = seg.first();
start.x = 3;
check(seg.start.x === 3 && seg.length2() === 16);
seg.start.y = 4;
check(seg.length2() === 0);
check(new Vec2(1, 2).clone().y === 2);
check(api.sum([new Vec2(1, 2), new Vec2(3, 4)]) === 10);
const held = new api.Segment().first();
collect();
check(held.x === 0);

// Templates, extensions, qualified names and copy types.
const stack = new api.IntStack();
stack.push(1); stack.push(2);
check(stack.size() === 2 && stack.pop() === 2);
same(stack.items(), [1]);
fails(() => stack.push(2.5), "argument 'value'");
check(api.clamp_int(42) === 10 && api.clamp_int(3, 5) === 3 && api.clamp_real(2.5) === 2.5);
check(c1.label() === "#circle" && c1.label("@") === "@circle" && Circle.unit().radius() === 1);
check(api.features__legacy__version() === 2 && new api.features__legacy__Tag().id === 7);
check(api.features__legacy__Mode.off === 1);
check(api.features__legacy__Circle === Circle && api.features__legacy__Kind === api.Kind && new Circle(2).diameter() === 4);
check(api.features__legacy__area_of(new Square()) === 1);
const ledger = new api.Ledger();
const copy = ledger.latest();
copy.value = 5;
check(ledger.stamp.value === 1);
const aim = new api.Aim();
aim.origin.x = 2;
check(aim.target.point.x === 2);
fails(() => { aim.target.point = new Vec2(); }, "read-only");
fails(() => new api.Target(), "no public constructor", TypeError);

// Errors, closing and garbage collection.
fails(() => api.area_of(1), "argument 'shape': expected Shape, got number", TypeError);
fails(() => c1.area(1), "incompatible arguments");
fails(() => c1.area.call({}), "`this` is not a Circle object", TypeError);
{
    const x = new Circle(1);
    check(x.area() === 3);
    x.close();
    fails(() => x.area(), "closed");
    x.close();
}
if (typeof Symbol.dispose === "symbol") {
    const y = new Circle(1);
    y[Symbol.dispose]();
    fails(() => y.area(), "closed");
}
for (let i = 1; i <= 200; i++) { const tmp = api.make_shape("square", i); check(tmp.area() === i * i); }
collect();

// Directors: JavaScript subclasses implement C++ virtual methods.
class Triangle extends api.Shape {
    constructor(base, height) { super(); this.base = base; this.height = height; }
    area() { return this.base * this.height / 2; }
    name() { return "triangle"; }
}
const t = new Triangle(4, 3);
check(t instanceof api.Shape && t.area() === 6 && api.area_of(t) === 6 && t.twice_area() === 12 && api.describe(t) === "triangle:7");
check(api.total_area([t, new Circle(1), new Square(2)]) === 13 && t.label("@") === "@triangle");
class Incomplete extends api.Shape {}
fails(() => api.area_of(new Incomplete()), "pure virtual method");
class Failing extends api.Shape { area() { throw new Error("from JavaScript"); } }
fails(() => api.area_of(new Failing()), "from JavaScript");
class Wrong extends api.Shape { area() { return "not a number"; } }
fails(() => api.area_of(new Wrong()), "does not convert");
// super.method() calls the native implementation rather than dispatching back to the override.
class Loud extends Circle { name() { return super.name().toUpperCase(); } }
check(api.describe(new Loud(1)) === "CIRCLE:0" && new Loud(2).area() === 12);
// Subclasses of subclasses.
class Louder extends Loud {}
check(api.describe(new Louder(1)) === "CIRCLE:0");
// A JavaScript subclass of a class without virtual methods is an ordinary subclass.
class Labelled extends Square { label2() { return "sq" + this.side; } }
check(new Labelled(3).label2() === "sq3" && api.area_of(new Labelled(3)) === 9);
// Ownership transfer keeps the JavaScript implementation alive; the same object comes back.
class Unit extends api.Shape { area() { return 1; } }
canvas = new api.Canvas();
(() => {
    const unit = new Unit();
    unit.tag = "kept";
    canvas.add(unit);
    check(unit.area() === 1);
})();
collect();
canvas.share(new Square(2));
check(canvas.total() === 5 && canvas.size() === 2);
let back = canvas.at(0);
check(back.tag === "kept" && back === canvas.at(0) && back instanceof Unit);
canvas.clear();
fails(() => back.twice_area(), "closed");
back = null;
collect();
(() => { canvas.share(new Unit()); })();
collect();
check(canvas.total() === 1);
canvas.clear();
collect();
// Callback interfaces with a native default implementation.
class Summer extends api.Visitor {
    visit(value) { return value * 10; }
    finished(seen) { this.seen = seen; }
}
const summer = new Summer();
check(api.run_visitor(summer, [1, 2]) === 30);
same(summer.seen, [1, 2]);
check(api.run_visitor(new api.Visitor(), [1, 2]) === 3);

// The TypeScript declarations document the module for editors.
const types = fs.readFileSync(typesPath, "utf8");
const has = (line) => { checks++; if (!types.split("\n").includes(line)) throw new Error("declarations lack: " + line); };
has("export declare function add(a: number, b?: number, c?: number): number;");
has(" * Adds three integers, Socha & Dorigo style.");
has(" * @param a first term");
has(" * @param b second term (default: `10`)");
has(" * @returns the sum");
has(" * @throws {Error} `std::out_of_range`: never, in practice");
has("export declare class Circle extends Shape {");
has("export declare abstract class Shape {");
has("    constructor(radius: number);");
has("     * Horizontal component.");
has("    x: number;");
has("export declare enum Kind {");
has("    static unit(): Circle;");
has("export declare function scale(values: number[] | Ref<number[]>, factor: number): void;");
has("export declare function total_area(shapes: (Shape | null)[]): number;");
has(" * @param kind (default: `Kind.square`)");
has("export declare function apply(f: ((a1: number) => number), x: number): number;");
has("export declare function count_words(words: string[]): Record<string, number>;");
has("export { Circle as features__legacy__Circle };");
has("    readonly point: Vec2;");
// Every exported name is declared, and every declared name is exported.
const declared = new Set();
for (const m of types.matchAll(/^export declare (?:abstract )?(?:class|function|enum|const) (\w+)/gm)) declared.add(m[1]);
for (const m of types.matchAll(/^export \{ \w+ as (\w+) \};/gm)) declared.add(m[1]);
for (const name of Object.keys(api)) check(declared.has(name), "undeclared export " + name);
for (const name of declared) check(name in api, "declared but not exported: " + name);
console.log(`JavaScript API passed (${checks} checks)`);
