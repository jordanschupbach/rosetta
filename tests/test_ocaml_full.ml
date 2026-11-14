(* Full OCaml API (--ocaml-api full) on the library-shaped example shared with the Python, R6, Lua,
   Common Lisp, Julia, C# and Java backends. Argument: the generated features.mli (for the odoc
   checks). Run it with OCAMLRUNPARAM=v=0x400 or under a small minor heap to stress the GC. *)
open Features

let checks = ref 0

let check ok what =
  incr checks;
  if not ok then failwith ("check failed: " ^ what)

let contains text pattern =
  let n = String.length pattern in
  let rec go i = i + n <= String.length text && (String.sub text i n = pattern || go (i + 1)) in
  go 0

let message_of = function
  | Invalid_argument m | Failure m | Error (_, m) -> m
  | Not_found -> "Not_found"
  | e -> Printexc.to_string e

(* [f ()] raises an exception whose message contains [pattern]. *)
let fails f pattern =
  incr checks;
  match f () with
  | _ -> failwith ("expected an error containing \"" ^ pattern ^ "\", got none")
  | exception e ->
      let m = message_of e in
      if not (contains m pattern) then failwith ("expected an error containing \"" ^ pattern ^ "\", got: " ^ m)

let collect () = Gc.full_major (); Gc.compact ()
let mli = ref ""

(* The odoc comment right above the first line declaring [declaration] in features.mli. *)
let doc declaration =
  let text = !mli in
  let n = String.length declaration in
  let rec find i =
    if i + n > String.length text then failwith ("not declared: " ^ declaration)
    else if String.sub text i n = declaration && (i = 0 || text.[i - 1] = '\n') then i
    else find (i + 1)
  in
  let at = find 0 in
  let rec back i = if i < 0 then failwith ("no comment: " ^ declaration) else if String.sub text i 3 = "(**" then i else back (i - 1) in
  let start = back at in
  String.sub text start (at - start)

let read_file path =
  let ic = open_in_bin path in
  let s = really_input_string ic (in_channel_length ic) in
  close_in ic;
  s

let feq a b = Float.abs (a -. b) < 1e-9

let () =
  mli := read_file Sys.argv.(1);

  (* Classes, inheritance and virtual dispatch. *)
  let c1 = Circle.create 2. in
  check (Circle.area c1 = 12. && Circle.name c1 = "circle" && Circle.twice_area c1 = 24. && Circle.radius c1 = 2.) "circle methods";
  check (Shape.area c1 = 12. && Shape.name (c1 :> shape) = "circle" && Shape.name (Shape.upcast c1) = "circle") "inherited through the base module";
  check (c1 |> Circle.twice_area = 24.) "pipelines";
  fails (fun () -> Circle.create (-1.)) "negative radius";
  let s = Square.create () in
  check (Square.side s = 1. && Square.area s = 1.) "default constructor";
  Square.set_side s 3.;
  check (Square.area s = 9. && Square.area (Square.create_float 2.) = 4.) "field set, overloaded constructor";
  check (area_of s = 9. && describe c1 = "circle:0") "functions on classes";
  (* Enums. *)
  check (Circle.kind c1 = Kind.Circle && Square.kind s = Kind.Square) "enum results";
  check (filled Fill.Solid && not (filled Fill.None)) "enum arguments";
  check (Kind.to_int Kind.Custom = 7 && Kind.of_int 1 = Kind.Square && List.length Kind.all = 3) "enum values";
  check (Kind.to_string Kind.Custom = "custom" && Kind.of_string "circle" = Kind.Circle) "enum names";
  fails (fun () -> Kind.of_int 5) "Kind.of_int";
  check (List.mem FeaturesLegacyMode.Off FeaturesLegacyMode.all && Legacy.to_int Legacy.Legacy_b = 2) "qualified enum";
  check (Shape.sides_unknown = -1 && Float.abs (golden -. 1.618) < 1e-12) "constants";

  (* Most-derived results, ownership transfer and shared ownership. *)
  let made = make_shape Kind.Circle 1. in
  check (match Circle.cast made with Some c -> Circle.radius c = 1. | None -> false) "most-derived result";
  check (Square.cast made = None && Shape.cast made <> None) "cast checks the class";
  let canvas = Canvas.create () in
  Canvas.add canvas (make_shape Kind.Square 2.);
  Canvas.share canvas (shared_square 3.);
  let transferred = Circle.create 1. in
  Canvas.add canvas transferred;
  fails (fun () -> Circle.area transferred) "closed";
  check (Canvas.size canvas = 3 && Canvas.total canvas = 4. +. 9. +. 3.) "ownership transfer";
  check (Square.cast (Canvas.at canvas 0) <> None && Shape.area (Canvas.at canvas 0) = 4.) "borrowed element";
  (let shared = shared_square 2. in
   Canvas.share canvas shared;
   check (Shape.area shared = 4. && Canvas.total canvas = 20.) "shared ownership";
   check (total_area [ (Circle.create 1. :> shape); shared ] = 7.) "list of objects");
  check (try ignore (Canvas.at canvas 99); false with Error (`Out_of_range, _) -> true) "std::out_of_range";
  check (Printexc.to_string (Error (`Out_of_range, "m")) = "Features.Error (`Out_of_range, \"m\")") "exception printer";

  (* Overloads: one function per C++ overload, named after its parameter types. *)
  check (pick 1 = "int" && pick_float 1.5 = "double" && pick_float 2.0 = "double" && pick_string "x" = "string") "scalar overloads";
  check (pick_float_array [| 1.; 2. |] = "vector" && pick_shape s = "shape") "container and class overloads";
  (* Default arguments are optional labelled arguments. *)
  check (add 1 = 111 && add ~b:2 1 = 103 && add ~b:2 ~c:3 1 = 6) "default arguments";
  check (add ~c:3 1 = 14) "a gap takes the literal C++ default";
  check (defaults () = "x*1-1.500000") "all defaults";
  check (defaults ~kind:Kind.Custom ~loud:true () = "x*7!-1.500000") "enum and bool optional arguments";
  check (defaults ~mark:'+' ~lambda:0 () = "x+10.500000") "char and int optional arguments";
  check (Shape.label c1 = "#circle" && Shape.label ~prefix:"@" c1 = "@circle") "optional argument before the object";

  (* Containers. *)
  let counts = count_words [ "a"; "b"; "a" ] in
  check (List.assoc "a" counts = 2 && List.assoc "b" counts = 1 && List.length counts = 2) "map result";
  check (min_max [| 3.; 1.; 2. |] = (1., 3.)) "pair result";
  check (fst (min_max [| 3.; 1.; 2. |]) = 1.) "tuple";
  fails (fun () -> min_max [||]) "empty input";
  check (find [ 5; 7 ] 7 = Some 1 && find [ 5; 7 ] 9 = None) "optional result";
  let tr = transpose [ [| 1.; 2.; 3. |]; [| 4.; 5.; 6. |] ] in
  check (List.length tr = 3 && List.hd tr = [| 1.; 4. |]) "nested vectors";
  check (byte_sum (Bytes.of_string "\001\002\250") = 253 && bytes_of "AZ" = Bytes.of_string "AZ") "bytes";
  let cs = circles 3 in
  check (List.length cs = 3 && Circle.radius (List.nth cs 2) = 3.) "list of class results";
  check (repeat 'z' 3 = "zzz" && initial "hello" = 'h' && initial "" = '?') "chars";
  (* Mutable reference parameters are ref cells. *)
  let values = ref [| 1.; 2. |] in
  scale values 3.;
  check (!values = [| 3.; 6. |]) "ref argument written back";
  fails (fun () -> repeat 'z' (-1)) "negative count";
  (* Integers are range-checked on the way to C++. *)
  fails (fun () -> repeat 'z' max_int) "outside C++ type range";
  check (try ignore (repeat 'z' max_int); false with Error (`Binding, _) -> true) "int range is Error `Binding";
  (* Overloads still resolve in C++: an int too large for C++ int picks the double overload. *)
  check (pick max_int = "double") "run-time overload resolution";

  (* Callbacks both ways. *)
  check (apply (fun x -> x *. 2.) 21. = 42.) "callback";
  fails (fun () -> apply (fun _ -> failwith "inside callback") 1.) "inside callback";
  (* The OCaml exception comes back as itself. *)
  let exception Mine of int in
  check (try ignore (apply (fun _ -> raise (Mine 7)) 1.); false with Mine 7 -> true) "callback exception comes back unchanged";
  check (try ignore (apply (fun _ -> raise Not_found) 1.); false with Not_found -> true) "stdlib exception from a callback";
  let plus = adder 40 in
  check (plus 2 = 42) "native function as a closure";
  check (apply (fun x -> float_of_int (adder 1 (int_of_float x))) 3. = 4.) "native function inside a callback";
  check (apply (fun x -> apply (fun y -> y +. 1.) x) 1. = 2.) "nested callbacks";
  for i = 1 to 20 do
    check (apply (fun x -> x +. float_of_int i) 0. = float_of_int i) "many callbacks"
  done;

  (* Value type operators, chaining and borrowed references. *)
  (let v = Vec2.create_float_float 1. 2. in
   let w = Vec2.add v (Vec2.create_float_float 3. 4.) in
   check (Vec2.x w = 4. && Vec2.y w = 6. && Vec2.y (Vec2.mul v 2.) = 4. && Vec2.x (Vec2.neg v) = -1. && Vec2.x (Vec2.sub v w) = -3.) "operators";
   check (Vec2.equal v (Vec2.create_float_float 1. 2.) && not (Vec2.equal v w)) "operator==";
   check (Vec2.get v 1 = 2. && Vec2.inner v (Vec2.create_float_float 1. 1.) = 3.) "operator[] and renamed method";
   fails (fun () -> Vec2.get v 2) "Vec2 index";
   let chained = Vec2.set_y (Vec2.set_x v 10.) 20. in
   check (Vec2.x v = 10. && Vec2.y v = 20. && Vec2.x chained = 10.) "chaining (the field setter is set_x_field)";
   Vec2.set_x_field v 5.;
   check (Vec2.x v = 5.) "field setter renamed");
  (let seg = Segment.create () in
   Segment.set_end_ seg (Vec2.create_float_float 3. 4.);
   check (Segment.length2 seg = 25.) "field object set (keyword field end_)";
   let start = Segment.first seg in
   ignore (Vec2.set_x start 3.);
   check (Vec2.x (Segment.start seg) = 3. && Segment.length2 seg = 16.) "borrowed reference writes through";
   ignore (Vec2.set_y (Segment.start seg) 4.);
   check (Segment.length2 seg = 0.) "field object borrowed");
  check (Vec2.y (Vec2.clone (Vec2.create_float_float 1. 2.)) = 2.) "clone";
  check (sum [ Vec2.create_float_float 1. 2.; Vec2.create_float_float 3. 4. ] = 10.) "list of values";
  (let held = Segment.first (Segment.create ()) in
   collect ();
   check (Vec2.x held = 0.) "borrowed reference keeps its owner alive");

  (* Templates, extensions, qualified names and copy types. *)
  (let stack = IntStack.create () in
   IntStack.push stack 1;
   IntStack.push stack 2;
   check (IntStack.size stack = 2 && IntStack.items stack = [ 1; 2 ] && IntStack.pop stack = 2) "template instance";
   IntStack.close stack);
  check (clamp_int 42 = 10 && clamp_int ~high:5 3 = 3 && clamp_real 2.5 = 2.5) "function templates";
  check (Circle.label c1 = "#circle" && Circle.label ~prefix:"@" c1 = "@circle" && Circle.radius (Circle.unit ()) = 1.) "extensions";
  check (features_legacy_version () = 2 && FeaturesLegacyTag.id (FeaturesLegacyTag.create ()) = 7) "qualified names";
  check (Circle.diameter (Circle.create 2.) = 4.) "extension method";
  (let ledger = Ledger.create () in
   let copy = Ledger.latest ledger in
   Stamp.set_value copy 5;
   check (Stamp.value (Ledger.stamp ledger) = 1) "copy type");
  (let aim = Aim.create () in
   ignore (Vec2.set_x (Aim.origin aim) 2.);
   check (Vec2.x (Target.point (Aim.target aim)) = 2.) "nested borrowed fields");
  check (not (contains !mli "val set_point")) "read-only field has no setter";

  (* Errors, closing and garbage collection. *)
  check (try ignore (Circle.create (-1.)); false with Invalid_argument m -> contains m "negative radius") "std::invalid_argument is Invalid_argument";
  check (try ignore (Vec2.get (Vec2.create_float_float 1. 2.) 5); false with Error (`Out_of_range, _) -> true) "std::out_of_range is Error `Out_of_range";
  check (try ignore (min_max [||]); false with Invalid_argument _ | Failure _ | Error _ -> true) "C++ error";
  (let x = Circle.create 1. in
   check (Circle.area x = 3.) "before close";
   Circle.close x;
   check (Circle.is_closed x) "closed";
   fails (fun () -> Circle.area x) "closed";
   check (try ignore (Circle.area x); false with Error (`Binding, _) -> true) "closed object raises Error `Binding";
   Circle.close x);
  for i = 1 to 200 do
    let tmp = make_shape Kind.Square (float_of_int i) in
    check (Shape.area tmp = float_of_int (i * i)) "many objects";
    if i mod 2 = 0 then Shape.close tmp
  done;
  collect ();

  (* OCaml subclasses (directors): closures override C++ virtual methods. *)
  let triangle b h = Shape.extend ~area:(fun _ -> b *. h /. 2.) ~name:(fun _ -> "triangle") () in
  (let t = triangle 4. 3. in
   check (Shape.area t = 6. && area_of t = 6. && Shape.twice_area t = 12. && describe t = "triangle:7") "OCaml override called from C++";
   check (total_area [ t; (Circle.create 1. :> shape); (Square.create_float 2. :> shape) ] = 13. && Shape.label ~prefix:"@" t = "@triangle") "mixed overrides");
  let incomplete = Shape.extend () in
  fails (fun () -> area_of incomplete) "pure virtual";
  check (try ignore (Shape.area incomplete); false with Error (`Binding, _) -> true) "pure virtual raises Error `Binding";
  fails (fun () -> area_of (Shape.extend ~area:(fun _ -> failwith "from OCaml") ())) "from OCaml";
  let exception Override_failed in
  check (try ignore (area_of (Shape.extend ~area:(fun _ -> raise Override_failed) ())); false with Override_failed -> true) "override exception comes back as itself";
  (* Base runs the C++ method rather than dispatching back to the override. *)
  let loud r = Circle.extend ~name:(fun self -> String.uppercase_ascii (Circle.Base.name self)) r in
  check (describe (loud 1.) = "CIRCLE:0" && Circle.area (loud 2.) = 12. && Circle.name (loud 1.) = "CIRCLE") "base call runs the C++ method";
  let noisy r = Circle.extend ~name:(fun self -> String.lowercase_ascii (Circle.Base.name self) ^ "!") r in
  check (describe (noisy 1.) = "circle!:0") "override of a derived class";
  check (Kind.to_int (Shape.kind (Shape.extend ~area:(fun _ -> 1.) ~kind:(fun _ -> Kind.Square) ())) = 1) "enum result from an override";
  (let counter = ref 0 in
   let counted = Shape.extend ~area:(fun _ -> incr counter; 1.) ~name:(fun _ -> "c") () in
   ignore counted;
   check (!counter = 0) "overrides run only when called");
  let canvas = Canvas.create () in
  (let tag = ref "kept" in
   let u = Shape.extend ~area:(fun _ -> 1.) ~name:(fun _ -> !tag) () in
   Canvas.add canvas u;
   check (Shape.area u = 1.) "subclass object owned by C++");
  collect ();
  Canvas.share canvas (Square.create_float 2.);
  check (Canvas.total canvas = 5. && Canvas.size canvas = 2) "C++ keeps the OCaml object alive";
  (let back = Canvas.at canvas 0 in
   check (Shape.name back = "kept" && back == Canvas.at canvas 0) "the same OCaml object comes back");
  Canvas.clear canvas;
  collect ();
  Canvas.share canvas (Shape.extend ~area:(fun _ -> 1.) ());
  collect ();
  check (Canvas.total canvas = 1.) "shared subclass object";
  Canvas.clear canvas;
  collect ();
  (let seen = ref [||] in
   let summer = Visitor.extend ~visit:(fun _ v -> v *. 10.) ~finished:(fun _ values -> seen := values) () in
   check (run_visitor summer [| 1.; 2. |] = 30. && !seen = [| 1.; 2. |]) "callback interface");
  check (run_visitor (Visitor.create ()) [| 1.; 2. |] = 3.) "native default implementation";
  check (run_visitor (Visitor.extend ~finished:(fun _ _ -> ()) ()) [| 1.; 2. |] = 3.) "partial override keeps the C++ method";
  for i = 1 to 30 do
    let t = triangle (float_of_int i) 2. in
    check (area_of t = float_of_int i) "many subclass objects"
  done;
  collect ();

  (* odoc documentation in features.mli. *)
  (let d = doc "val add :" in
   check (contains d "Adds three integers, Socha & Dorigo style.") "summary";
   check (contains d "@param a first term") "parameter";
   check (contains d "@param b second term (optional; C++ default [10])") "optional parameter and default";
   check (contains d "@return the sum") "returns";
   check (contains d "@raise Error [(`Out_of_range, _)] never, in practice") "raises";
   check (contains d "{@cpp[\n    int total = add(1);  // 111\n    ]}") "code block";
   check (contains d "[int features::add(int a, int b = 10, int c = 100)]") "C++ signature";
   check (contains d "@see \"pick\"") "see";
   check (contains d "{- first item continued}") "list";
   check (contains d "{b no}") "bold");
  check (contains (doc "  val create : float -> t") "@raise Invalid_argument") "constructor exceptions";
  check (contains (doc "  val x :") "Horizontal component.") "field documentation";
  check (contains (doc "module Shape : sig") "Python subclasses may implement") "class documentation";
  check (contains (doc "val defaults :") "\\@ sign") "@ escaped";
  check (contains (doc "val defaults :") "\\{braces\\}") "braces escaped";
  check (contains (doc "  val area : [> `Shape ] obj -> float") "Virtual:") "virtual methods documented";

  (* Policy modules: features::data functions live in the submodule Data. *)
  (let u = Data.unit () in
   check (Vec2.x u = 1. && Vec2.y u = 0.) "submodule result");
  check (Data.scaled 3 = 6 && Data.scaled_float 2.5 = 6.25) "submodule overloads";
  check (Data.repeat "ab" = "abab" && repeat 'x' 3 = "xxx") "submodule names are separate";
  check (contains (doc "  val unit : unit -> vec2") "Mapped by the policy's modules") "submodule docs";
  collect ();
  Printf.printf "OCaml full API passed (%d checks)\n" !checks
