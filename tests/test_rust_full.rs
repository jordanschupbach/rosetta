// Rust API (--language rust) on the library-shaped example shared with the Python, R6, Lua, Common
// Lisp, Julia, C#, VB and Go backends. Argument: the generated crate's src/lib.rs, for the
// documentation checks. The native library is found through FEATURES_RUST_LIB_DIR when this
// program is built, and through its rpath when it runs.
use features::*;
use std::collections::BTreeMap;
use std::fmt;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};

static CHECKS: AtomicUsize = AtomicUsize::new(0);

fn check(ok: bool, what: &str) {
    CHECKS.fetch_add(1, Ordering::Relaxed);
    if !ok {
        panic!("check failed: {}", what);
    }
}

// The error of `result`, which must contain `pattern`.
fn fails<T>(result: Result<T>, pattern: &str) -> Error {
    CHECKS.fetch_add(1, Ordering::Relaxed);
    match result {
        Ok(_) => panic!("expected an error containing {:?}, got none", pattern),
        Err(error) => {
            if !error.message().contains(pattern) {
                panic!("expected an error containing {:?}, got: {}", pattern, error);
            }
            error
        }
    }
}

// Rust implementations of C++ virtual methods.
struct Triangle {
    base: f64,
    height: f64,
}
impl ShapeImpl for Triangle {
    fn area(&self, _this: &Shape) -> Result<f64> {
        Ok(self.base * self.height / 2.0)
    }
    fn name(&self, _this: &Shape) -> Result<String> {
        Ok("triangle".into())
    }
}

struct Incomplete;
impl ShapeImpl for Incomplete {}

#[derive(Debug)]
struct Sentinel;
impl fmt::Display for Sentinel {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("from Rust")
    }
}
impl std::error::Error for Sentinel {}

struct Failing;
impl ShapeImpl for Failing {
    fn area(&self, _this: &Shape) -> Result<f64> {
        Err(Error::from_source(Sentinel))
    }
}

struct Panicking;
impl ShapeImpl for Panicking {
    fn area(&self, _this: &Shape) -> Result<f64> {
        panic!("panicked in Rust")
    }
}

// `this` runs the C++ implementation: an override builds on it.
struct Loud;
impl CircleImpl for Loud {
    fn name(&self, this: &Circle) -> Result<String> {
        Ok(this.name()?.to_uppercase())
    }
}

struct Noisy;
impl CircleImpl for Noisy {
    fn name(&self, this: &Circle) -> Result<String> {
        Ok(this.name()?.to_lowercase() + "!")
    }
}

// Composition instead of Go's embedding: delegate to another implementation.
struct Louder(Noisy);
impl CircleImpl for Louder {
    fn name(&self, this: &Circle) -> Result<String> {
        self.0.name(this)
    }
}

struct UnitShape {
    tag: String,
}
impl ShapeImpl for UnitShape {
    fn area(&self, _this: &Shape) -> Result<f64> {
        Ok(1.0)
    }
}

#[derive(Default)]
struct Summer {
    seen: Mutex<Vec<f64>>,
}
impl VisitorImpl for Summer {
    fn visit(&self, _this: &Visitor, value: f64) -> Result<f64> {
        Ok(value * 10.0)
    }
    fn finished(&self, _this: &Visitor, values: Vec<f64>) -> Result<()> {
        *self.seen.lock().unwrap() = values;
        Ok(())
    }
}

// Compile-time checks: classes and errors are thread-safe values.
fn _send_sync<T: Send + Sync>() {}
const _: fn() = || {
    _send_sync::<Circle>();
    _send_sync::<Shape>();
    _send_sync::<Error>();
};

fn main() {
    let source = std::fs::read_to_string(std::env::args().nth(1).expect("lib.rs path")).unwrap();
    // The doc comment right above the first line containing `declaration`.
    let doc = |declaration: &str| -> String {
        let lines: Vec<&str> = source.lines().collect();
        let at = lines
            .iter()
            .position(|line| line.contains(declaration) && !line.trim_start().starts_with("//"))
            .unwrap_or_else(|| panic!("not declared: {}", declaration));
        let mut block = Vec::new();
        let mut i = at;
        while i > 0 && lines[i - 1].trim_start().starts_with("#[") {
            i -= 1;
        }
        while i > 0 && lines[i - 1].trim_start().starts_with("///") {
            i -= 1;
            block.insert(0, lines[i].trim_start().trim_start_matches("///").to_string());
        }
        block.join("\n")
    };

    // Classes, inheritance and virtual dispatch.
    let c1 = Circle::new(2.0).unwrap();
    check(c1.area().unwrap() == 12.0 && c1.name().unwrap() == "circle" && c1.twice_area().unwrap() == 24.0 && c1.radius().unwrap() == 2.0, "circle methods");
    check(format!("{:?}", c1) == "Circle", "Debug names the class");
    check(same(c1.as_shape(), &c1) && same(&Shape::from(&c1), &c1), "class hierarchy");
    let error = fails(Circle::new(-1.0), "negative radius");
    check(error.kind() == ErrorKind::InvalidArgument, "exception kind");
    let s = Square::new().unwrap();
    check(s.side() == 1.0 && s.area().unwrap() == 1.0, "default constructor");
    check(s.set_side(3.0).is_ok(), "field set");
    check(s.area().unwrap() == 9.0 && Square::new_side(2.0).unwrap().area().unwrap() == 4.0, "field set and overloaded constructor");
    check(area_of(&s).unwrap() == 9.0 && describe(&c1).unwrap() == "circle:0", "functions on classes (deref to the base)");
    // Enums.
    check(c1.kind().unwrap() == Kind::CIRCLE && s.kind().unwrap() == Kind::SQUARE, "enum results");
    check(filled(Fill::SOLID).unwrap() && !filled(Fill::NONE).unwrap(), "enum arguments");
    check(Kind::CUSTOM.to_string() == "custom" && Kind(42).to_string() == "42" && Kind::CUSTOM.0 == 7, "enum values");
    check(format!("{:?}", Kind::SQUARE) == "Kind::SQUARE" && format!("{:?}", Kind(42)) == "Kind(42)", "enum Debug");
    check(Shape::sides_unknown() == -1 && (golden() - 1.618).abs() < 1e-12, "constants");

    // Most-derived results, ownership transfer and shared ownership.
    let made = make_shape(Kind::CIRCLE, 1.0).unwrap();
    let circle = cast::<Circle>(&made);
    check(circle.is_some_and(|c| c.radius().unwrap() == 1.0), "most-derived result");
    check(cast::<Square>(&made).is_none(), "cast to another class fails");
    let canvas = Canvas::new().unwrap();
    check(canvas.add(&make_shape(Kind::SQUARE, 2.0).unwrap()).is_ok(), "unique_ptr argument");
    check(canvas.share(&shared_square(3.0).unwrap()).is_ok(), "shared_ptr argument");
    let transferred = Circle::new(1.0).unwrap();
    check(canvas.add(&transferred).is_ok(), "ownership transfer");
    fails(transferred.area(), "closed");
    check(canvas.size().unwrap() == 3 && canvas.total().unwrap() == 4.0 + 9.0 + 3.0, "ownership transfer");
    let first = cast::<Square>(&canvas.at(0).unwrap());
    check(first.is_some_and(|f| f.area().unwrap() == 4.0), "borrowed element");
    {
        let shared = shared_square(2.0).unwrap();
        check(canvas.share(&shared).is_ok(), "share");
        check(shared.area().unwrap() == 4.0 && canvas.total().unwrap() == 20.0, "shared ownership");
        check(total_area(&[Circle::new(1.0).unwrap().into(), shared.clone()]).unwrap() == 7.0, "slice of objects");
    }

    // Overloads: one Rust function per C++ overload, resolved again in C++.
    check(pick(1).unwrap() == "int" && pick_f64(1.5).unwrap() == "double" && pick_f64(2.0).unwrap() == "double" && pick_str("x").unwrap() == "string",
        "scalar overloads");
    check(pick_f64s(&[1.0, 2.0]).unwrap() == "vector" && pick_shape(&s).unwrap() == "shape", "container and class overloads");
    check(add(1).unwrap() == 111 && add_with_b(1, 2).unwrap() == 103 && add_with_c(1, 2, 3).unwrap() == 6, "default arguments");

    // Containers.
    let counts = count_words(&["a", "b", "a"]).unwrap();
    check(counts == BTreeMap::from([("a".to_string(), 2), ("b".to_string(), 1)]), "map result from &str slices");
    let owned = vec!["a".to_string(), "a".to_string()];
    check(count_words(&owned).unwrap()["a"] == 2, "map result from String slices");
    check(min_max(&[3.0, 1.0, 2.0]).unwrap() == (1.0, 3.0), "pair result");
    let error = fails(min_max(&[]), "empty input");
    check(error.kind() == ErrorKind::OutOfRange, "out_of_range kind");
    check(find(&[5, 7], 7).unwrap() == Some(1) && find(&[5, 7], 9).unwrap().is_none(), "optional result");
    let tr = transpose(&[vec![1.0, 2.0, 3.0], vec![4.0, 5.0, 6.0]]).unwrap();
    check(tr.len() == 3 && tr[0] == [1.0, 4.0] && tr[2] == [3.0, 6.0], "nested vectors");
    check(byte_sum(&[1, 2, 250]).unwrap() == 253 && bytes_of("AZ").unwrap() == [65, 90], "bytes");
    let cs = circles(3).unwrap();
    check(cs.len() == 3 && cs[2].radius().unwrap() == 3.0, "vector of class results");
    check(repeat('z', 3).unwrap() == "zzz" && initial("hello").unwrap() == 'h', "chars");
    // Non-const reference parameters are &mut, written back.
    let mut values = vec![1.0, 2.0];
    check(scale(&mut values, 3.0).is_ok() && values == [3.0, 6.0], "&mut argument written back");
    let error = fails(repeat('z', -1), "negative count");
    check(error.kind() == ErrorKind::InvalidArgument || error.kind() == ErrorKind::Native, "invalid_argument kind");
    check(defaults().unwrap() == "x*1-1.500000", "all defaults");
    check(defaults_with_loud(Kind::CIRCLE, "y", true).unwrap() == "y*0!-1.500000", "some defaults");
    check(doc("pub fn defaults_with_loud(").contains("weight = 0.5"), "omitted defaults named");

    // Callbacks both ways.
    check(apply(|x| x * 2.0, 21.0).unwrap() == 42.0, "callback");
    let error = fails(apply(|_| panic!("inside callback"), 1.0), "inside callback");
    check(error.kind() == ErrorKind::Callback, "callback error kind");
    let plus = adder(40).unwrap();
    check(plus(2) == 42, "native function as a Rust closure");
    check(apply(|x| adder(1).unwrap()(x as i32) as f64, 3.0).unwrap() == 4.0, "native function inside a callback");
    let worker = std::thread::spawn(|| apply(|x| x + 1.0, 1.0).unwrap());
    check(worker.join().unwrap() == 2.0, "callback from another thread");
    let captured = Arc::new(AtomicUsize::new(0));
    let seen = captured.clone();
    check(apply(move |x| { seen.fetch_add(1, Ordering::Relaxed); x }, 5.0).unwrap() == 5.0 && captured.load(Ordering::Relaxed) == 1, "capturing closure");

    // Value type operators, chaining and borrowed references.
    {
        let v = Vec2::new_x_y(1.0, 2.0).unwrap();
        let w = v.add(&Vec2::new_x_y(3.0, 4.0).unwrap()).unwrap();
        check(w.x() == 4.0 && w.y() == 6.0 && v.mul(2.0).unwrap().y() == 4.0 && v.neg().unwrap().x() == -1.0 && v.sub(&w).unwrap().x() == -3.0, "operators");
        check(v.equal(&Vec2::new_x_y(1.0, 2.0).unwrap()).unwrap() && !v.equal(&w).unwrap(), "equality operator");
        check(v.at(1).unwrap() == 2.0 && v.inner(&Vec2::new_x_y(1.0, 1.0).unwrap()).unwrap() == 3.0, "index operator and rename");
        fails(v.at(2), "Vec2 index");
        let chained = v.set_x(10.0).unwrap().set_y(20.0).unwrap();
        check(v.x() == 10.0 && v.y() == 20.0 && same(&chained, &v), "chaining returns the same Rust object");
        check(same(&v.add_assign(&w).unwrap(), &v) && v.x() == 14.0, "compound assignment");
        check(v.set_x_field(5.0).is_ok() && v.x() == 5.0, "field setter named apart from the set_x method");
    }
    {
        let seg = Segment::new().unwrap();
        check(seg.set_end(&Vec2::new_x_y(3.0, 4.0).unwrap()).is_ok() && seg.length2().unwrap() == 25.0, "field object set");
        let start = seg.first().unwrap();
        check(start.set_x_field(3.0).is_ok(), "set through a reference");
        check(seg.start().x() == 3.0 && seg.length2().unwrap() == 16.0, "borrowed reference writes through");
        check(seg.start().set_y_field(4.0).is_ok() && seg.length2().unwrap() == 0.0, "field object borrowed");
    }
    check(Vec2::new_x_y(1.0, 2.0).unwrap().clone().unwrap().y() == 2.0, "C++ clone (a deep copy) next to Clone");
    check(sum(&[Vec2::new_x_y(1.0, 2.0).unwrap(), Vec2::new_x_y(3.0, 4.0).unwrap()]).unwrap() == 10.0, "slice of values");
    {
        let held = Segment::new().unwrap().first().unwrap();
        check(held.x() == 0.0, "borrowed reference keeps its owner alive");
    }

    // Templates, extensions, qualified names and copy types.
    {
        let stack = IntStack::new().unwrap();
        check(stack.push(1).is_ok() && stack.push(2).is_ok(), "push");
        check(stack.size().unwrap() == 2 && stack.items().unwrap() == [1, 2] && stack.pop().unwrap() == 2, "template instance");
    }
    check(clamp_int(42).unwrap() == 10 && clamp_int_with_high(3, 5).unwrap() == 3 && clamp_real(2.5).unwrap() == 2.5, "function templates");
    check(c1.label().unwrap() == "#circle" && c1.label_with_prefix("@").unwrap() == "@circle" && Circle::unit().unwrap().radius().unwrap() == 1.0, "extensions");
    check(features_legacy_version().unwrap() == 2 && FeaturesLegacyTag::new().unwrap().id() == 7, "qualified names");
    check(FeaturesLegacyMode::OFF.to_string() == "off", "qualified enum");
    check(Circle::new(2.0).unwrap().diameter().unwrap() == 4.0, "extension method");
    {
        let ledger = Ledger::new().unwrap();
        let copy = ledger.latest().unwrap();
        check(copy.set_value(5).is_ok() && ledger.stamp().value() == 1, "copy type");
    }
    {
        let aim = Aim::new().unwrap();
        check(aim.origin().set_x_field(2.0).is_ok() && aim.target().point().x() == 2.0, "nested borrowed fields");
        check(!source.contains("pub fn set_point("), "read-only field has no setter");
    }

    // Errors, closing and dropping.
    {
        let x = Circle::new(1.0).unwrap();
        check(x.area().unwrap() == 3.0 && !x.is_closed(), "before close");
        x.close();
        check(x.is_closed() && format!("{:?}", x) == "Circle (closed)", "closed");
        let error = fails(x.area(), "closed");
        check(error.kind() == ErrorKind::Binding, "binding error kind");
        x.close();
    }
    for i in 1..=200 {
        let tmp = make_shape(Kind::SQUARE, i as f64).unwrap();
        check(tmp.area().unwrap() == (i * i) as f64, "many objects");
        if i % 2 == 0 {
            tmp.close();
        }
    }

    // Rust implementations of virtual methods.
    {
        let t = Shape::from_impl(Triangle { base: 4.0, height: 3.0 }).unwrap();
        check(t.area().unwrap() == 6.0 && area_of(&t).unwrap() == 6.0 && t.twice_area().unwrap() == 12.0 && describe(&t).unwrap() == "triangle:7",
            "Rust override called from C++ and from Rust");
        check(total_area(&[t.clone(), Circle::new(1.0).unwrap().into(), Square::new_side(2.0).unwrap().into()]).unwrap() == 13.0 &&
            t.label_with_prefix("@").unwrap() == "@triangle", "mixed overrides");
        let imp = implementation::<Triangle>(&t);
        check(imp.is_some_and(|imp| imp.base == 4.0), "implementation returns the Rust value");
        check(implementation::<Loud>(&t).is_none() && implementation::<Triangle>(&c1).is_none(), "implementation of another type");
    }
    fails(area_of(&Shape::from_impl(Incomplete).unwrap()), "pure virtual method");
    let error = fails(area_of(&Shape::from_impl(Failing).unwrap()), "from Rust");
    check(error.kind() == ErrorKind::Callback, "an override's error is a callback error");
    check(std::error::Error::source(&error).is_some_and(|cause| cause.downcast_ref::<Sentinel>().is_some()), "the Rust error comes back as source()");
    let failing = Shape::from_impl(Failing).unwrap();
    check(failing.area().err().is_some_and(|e| e.message() == "from Rust"), "a direct call returns the Rust error");
    fails(area_of(&Shape::from_impl(Panicking).unwrap()), "panicked in Rust");
    check(!source.contains("    pub fn new() -> Result<Shape>"), "an abstract class has no new");
    {
        let l = Circle::from_impl(Loud, 2.0).unwrap();
        check(describe(&l).unwrap() == "CIRCLE:0" && l.name().unwrap() == "CIRCLE" && l.area().unwrap() == 12.0, "this runs the C++ method");
        let lr = Circle::from_impl(Louder(Noisy), 1.0).unwrap();
        check(describe(&lr).unwrap() == "circle!:0", "delegating implementation");
    }
    let canvas = Canvas::new().unwrap();
    {
        let u = Shape::from_impl(UnitShape { tag: "kept".into() }).unwrap();
        check(canvas.add(&u).is_ok() && u.area().unwrap() == 1.0, "Rust-implemented object owned by C++");
    }
    check(canvas.share(&Square::new_side(2.0).unwrap()).is_ok(), "share");
    check(canvas.total().unwrap() == 5.0 && canvas.size().unwrap() == 2, "C++ keeps the Rust object alive");
    {
        let back = canvas.at(0).unwrap();
        check(implementation::<UnitShape>(&back).is_some_and(|u| u.tag == "kept") && same(&back, &canvas.at(0).unwrap()), "the same Rust object comes back");
    }
    check(canvas.clear().is_ok(), "clear");
    check(canvas.share(&Shape::from_impl(UnitShape { tag: String::new() }).unwrap()).is_ok(), "share a Rust-implemented object");
    check(canvas.total().unwrap() == 1.0, "shared Rust-implemented object");
    check(canvas.clear().is_ok(), "clear");
    {
        let sm = Visitor::from_impl(Summer::default()).unwrap();
        check(run_visitor(&sm, &[1.0, 2.0]).unwrap() == 30.0, "callback interface");
        let imp = implementation::<Summer>(&sm).unwrap();
        check(*imp.seen.lock().unwrap() == [1.0, 2.0], "implementation state");
        check(sm.visit(4.0).unwrap() == 40.0, "a direct call runs the Rust implementation");
    }
    check(run_visitor(&Visitor::new().unwrap(), &[1.0, 2.0]).unwrap() == 3.0, "native default implementation");

    // Rustdoc carries the C++ documentation.
    {
        let d = doc("pub fn add(a: i32)");
        check(d.contains("Adds three integers, Socha & Dorigo style."), "summary");
        check(d.contains("- `a`: first term"), "argument list");
        check(d.contains("Returns the sum."), "returns");
        check(d.contains("# Example") && d.contains("```text") && d.contains("int total = add(1);"), "heading and code block");
        check(d.contains("See also [`pick`] and [`sum`]."), "doc links");
        check(d.contains("[`add_with_b`] and [`add_with_c`]"), "longer variants linked");
        check(d.contains("[`ErrorKind::OutOfRange`] (C++ `std::out_of_range`)"), "errors");
    }
    check(doc("pub fn new(radius: f64)").contains("[`ErrorKind::InvalidArgument`] (C++ `std::invalid_argument`)"), "constructor errors");
    check(doc("pub fn x(&self) -> f64").contains("Horizontal component."), "field documentation");
    check(doc("pub struct Shape").contains("[`Shape::from_impl`]"), "class remarks");
    check(doc("pub trait ShapeImpl").contains("`this` is the C++ object"), "trait documentation");
    check(doc("fn area(&self, this: &Shape)").contains("pure virtual"), "pure virtual methods marked");

    // Policy modules: features::data functions live in the module data.
    {
        let u = data::unit().unwrap();
        check(u.x() == 1.0 && u.y() == 0.0, "submodule result");
    }
    check(data::scaled(3).unwrap() == 6 && data::scaled_f64(2.5).unwrap() == 6.25, "submodule overloads");
    check(data::repeat("ab").unwrap() == "abab" && repeat('x', 3).unwrap() == "xxx", "submodule names are separate");
    check(doc("pub fn unit() -> Result<Vec2>").contains("Mapped by the policy's modules"), "submodule docs");
    println!("Rust API passed ({} checks)", CHECKS.load(Ordering::Relaxed));
}
