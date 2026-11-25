// The Rust API for tests/rust_full_names.hpp: classes named like the prelude, std and runtime types
// generated code uses, Rust keywords, members named like generated ones, names that collide once
// in snake_case, and multiple inheritance. Argument: the generated src/lib.rs.
use names::*;
use std::collections::BTreeMap;
use std::sync::atomic::{AtomicUsize, Ordering};

static CHECKS: AtomicUsize = AtomicUsize::new(0);

fn check(ok: bool, what: &str) {
    CHECKS.fetch_add(1, Ordering::Relaxed);
    if !ok {
        panic!("check failed: {}", what);
    }
}

struct Shouting;
impl NamedImpl for Shouting {
    fn label(&self, this: &Named) -> Result<String> {
        Ok(this.label()?.to_uppercase())
    }
}

struct Deep;
impl LeafImpl for Deep {
    fn describe(&self, this: &Leaf) -> Result<String> {
        Ok(format!("deep {}", this.describe()?))
    }
}

fn main() {
    let source = std::fs::read_to_string(std::env::args().nth(1).expect("lib.rs path")).unwrap();

    // Classes named like the types generated code uses keep the std meanings apart.
    let s = String_::new().unwrap();
    let text: String = s.get().unwrap();
    check(text == "mine" && text_of(&s).unwrap() == "mine", "the module's own String");
    let words: Vec<String> = words().unwrap();
    check(words == ["a", "b"], "std Vec and String results");
    check(Vec_::new().unwrap().size().unwrap() == 3, "the module's own Vec");
    let counts: BTreeMap<String, i32> = counts(&words).unwrap();
    check(counts["a"] == 1, "std BTreeMap");
    check(Option_::new().unwrap().set() && Result_::new().unwrap().code() == 0, "the module's own Option and Result");
    let e = Error_::new().unwrap();
    check(e.code() == 7 && code_of(&e).unwrap() == 7, "the module's own Error");
    let failure: Error = fail(-1).unwrap_err();
    check(failure.kind() == ErrorKind::InvalidArgument && fail(2).unwrap() == 2, "the crate's Error");
    check(HashMap_::new().unwrap().buckets() == 16 && Value_::new().unwrap().amount() == 2.5, "the module's own HashMap and Value");
    let b = Box_::new().unwrap();
    check(b.volume().unwrap() == 8.0 && volume_of(&b).unwrap() == 8.0, "the module's own Box");
    let _ = Arc_::new().unwrap();

    // Keyword and case-colliding enumerators.
    check(mode_value(Mode::ON).unwrap() == 0 && mode_value(Mode::ON_2).unwrap() == 1 && mode_value(Mode::TYPE).unwrap() == 2, "enumerators");
    check(mode_value(Mode::MATCH).unwrap() == 3 && mode_value(Mode::SELF).unwrap() == 4, "keyword enumerators");
    check(Mode::ON_2.to_string() == "On" && Mode::SELF.to_string() == "Self", "enumerator names");

    // Keywords and generated names inside a class.
    let cursor = Cursor::new().unwrap();
    check(cursor.type_().unwrap() == 1 && cursor.match_(2, 3).unwrap() == 6, "keyword methods and parameters");
    check(cursor.yield_(2).unwrap() == 8 && cursor.yield_with_b(2, 5).unwrap() == 10, "a keyword's longer arities drop the escape");
    check(cursor.loop_(3).unwrap() == 3 && cursor.loop_x_n(1.5, 2).unwrap() == 3, "a keyword's other overloads drop the escape");
    check(cursor.move_(4).unwrap() == 4 && cursor.position() == 4, "a parameter named self");
    check(cursor.count().unwrap() == 20 && cursor.count_field() == 2, "a field and a method named alike");
    check(cursor.close_2().unwrap() && !cursor.is_closed(), "a C++ close next to the generated close");
    check(cursor.clone().unwrap() == 42 && Clone::clone(&cursor).position() == 4, "a C++ clone next to Clone");
    check(Cursor::create().unwrap() == 5, "static method");
    check(cursor.get_name().unwrap() == "camel" && cursor.get_name_2(2).unwrap() == "snakesnake", "names equal in snake_case");
    check(cursor.value(1).unwrap() == 2, "a parameter named like a generated local");
    cursor.close();
    check(cursor.is_closed(), "the generated close");

    // Constructors next to a static factory.
    check(Factory::new().unwrap().made() == 0 && Factory::new_made(3).unwrap().made() == 3 && Factory::make(4).unwrap().made() == 4,
        "constructors and a factory");

    // Multiple inheritance: the primary base by Deref, the other through as_named and From.
    let thing = Thing::new().unwrap();
    check(thing.counted().unwrap() == 3 && counted_of(&thing).unwrap() == 3, "primary base");
    check(thing.label().unwrap() == "thing" && label_of(thing.as_named()).unwrap() == "thing", "second base");
    check(label_of(&Named::from(&thing)).unwrap() == "thing" && thing.as_named().tag() == "t", "From the second base");
    check(cast::<Thing>(thing.as_named()).is_some() && cast::<Counted>(thing.as_named()).is_some(), "cast across bases");
    let shouting = Named::from_impl(Shouting).unwrap();
    check(label_of(&shouting).unwrap() == "NAMED" && shouting.label().unwrap() == "NAMED", "an implementation of a second base");

    // Three levels: Deref chains and as_base reaches every ancestor.
    let leaf = Leaf::new().unwrap();
    check(leaf.describe().unwrap() == "leaf" && leaf.name().unwrap() == "leaf" && leaf.as_base().name().unwrap() == "base", "hiding and as_base");
    check(describe_base(&leaf).unwrap() == "leaf" && describe_base(leaf.as_middle()).unwrap() == "leaf", "deref chain");
    let deep = Leaf::from_impl(Deep).unwrap();
    check(describe_base(&deep).unwrap() == "deep leaf" && deep.as_base().describe().unwrap() == "deep leaf", "override three levels deep");

    check(source.contains("pub struct String_") && source.contains("pub fn type_(&self)"), "generated names");
    println!("Rust names passed ({} checks)", CHECKS.load(Ordering::Relaxed));
}
