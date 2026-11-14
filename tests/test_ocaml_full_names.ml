(* Names OCaml reserves or already uses (--ocaml-api full on tests/ocaml_full_names.hpp): keywords
   get a trailing _, classes named like Stdlib modules (List, Array, String, Option) shadow them
   without breaking the generated code, conversions of integers, tuples, sets, maps, options,
   closures, ref cells, policy conversions and modules, and the exception kinds. *)
open Names

let checks = ref 0

let check ok what =
  incr checks;
  if not ok then Stdlib.failwith ("check failed: " ^ what)

let raises f p =
  match f () with _ -> false | exception e -> p e

(* Run as [names --interleave] (by the check below, output to a file): OCaml and C++ output mixed. *)
let () =
  if Stdlib.Array.length Sys.argv > 1 && Stdlib.Array.get Sys.argv 1 = "--interleave" then begin
    Stdlib.print_string "a\n";
    say_buffered "b\n";
    Stdlib.print_string "c\n";
    say_around (fun () -> Stdlib.print_string "inside\n");
    say_buffered "d\n";
    Stdlib.exit 0
  end

let () =
  (* Classes named like Stdlib modules: Names.List is the C++ class, Stdlib.List still works. *)
  let l = List.create () in
  check (List.length l = 3 && List.items l = [ 1; 2; 3 ] && length l = 3) "class List";
  List.set_items l [ 4; 5 ];
  check (List.length l = 2) "field set on List";
  check (Array.get (Array.create ()) 2 = 20) "class Array";
  check (String.concat (String.create ()) "t" = "st" && String.text (String.create ()) = "s") "class String";
  check (Complex.norm (Complex.create ()) = 25.) "class Complex";
  check (Option.value (Option.create ()) = 2 && Error.code (Error.create ()) = 5) "classes Option and Error";
  check (Stdlib.List.length [ 1 ] = 1 && Stdlib.Array.length [| 1 |] = 1) "Stdlib still reachable";
  let z = twice { Stdlib.Complex.re = 1.; im = 2. } in
  check (z.Stdlib.Complex.re = 2. && z.Stdlib.Complex.im = 4.) "std::complex is Stdlib.Complex.t";

  (* Methods, fields and parameters named like keywords get a trailing _. *)
  let w = Words.create () in
  check (Words.type_ w = 1 && Words.module_ w = 2 && Words.val_ w = 3 && Words.end_ w = 4) "keyword methods 1";
  check (Words.function_ w = 5 && Words.fun_ w = 6 && Words.match_ w = 7 && Words.with_ w = 8) "keyword methods 2";
  check (Words.open_ w = 9 && Words.include_ w = 10 && Words.method_ w = 11 && Words.object_ w = 12) "keyword methods 3";
  check (Words.done_ w = 14 && Words.to_ w = 15 && Words.mod_ w 7 4 = 3 && Words.land_ w 6 3 = 2) "keyword methods 4";
  check (Words.sig_ w = 16 && Words.effect_ w = 17) "sig and effect";
  check (Words.camelCase w = 18 && Words.camel_case w = 19 && Words.http_server w = 23) "camelCase kept, PascalCase snake_cased";
  check (Words.close_ w = 20 && Words.cast_ w = 21 && Words.create_ () = 22) "names of the reserved functions get _";
  check (Words.cast (List.create ()) = None && Words.cast w <> None) "cast";
  Words.close w;
  check (Words.is_closed w) "close stays the lifetime function";
  let r = Record.create () in
  check (Record.type_ r = 1 && Record.end_ r = 2 && Record.val_ r = "v") "keyword fields";
  Record.set_type_ r 10;
  Record.set_val_ r "w";
  check (Record.type_ r = 10 && Record.val_ r = "w") "keyword field setters";
  check (Record.method_ r 1 = 4 && Record.method_ ~done_:10 r 1 = 11) "keyword parameters";

  (* Enumerators: constructors, OCaml names escaped, aliases. *)
  check (token_value Token.Type = 0 && token_value Token.True_ = 3 && token_value Token.None = 4) "keyword enumerators";
  check (next_token Token.Some = Token.Lower_case && next_token Token.Lower_case = Token.Type) "enum round trip";
  check (Token.to_string Token.True_ = "True" && Token.of_string "None" = Token.None) "enum names";
  check (Color.to_int Color.Blue = 6 && Color.of_int 0 = Color.Red && Color.to_int Color.Crimson = 0) "aliases";
  check (next_color Color.Red = Color.Green && next_color Color.Crimson = Color.Green) "alias argument";
  check (raises (fun () -> Color.of_int 3) (function Invalid_argument _ -> true | _ -> false)) "of_int raises";
  check (Stdlib.List.length Token.all = 7) "all";

  (* Functions named like Stdlib values shadow them in Names only. *)
  check (compare 1 2 = -1 && Stats.compare 2. 1. = 1 && Stdlib.compare 1 1 = 0) "compare";
  check (print_string "x" = "<x>" && ignore 3 = 3 && raise 4 = -4 && not_ 0 = 1 && to_bool 2) "Stdlib names";

  (* Conversions. *)
  check (unique [ 3; 1; 3 ] = [ 1; 3 ]) "set as a sorted list";
  check (names () = [ (1, "one"); (2, "two") ]) "map as an association list";
  check (triple () = (1, "two", true)) "tuple";
  check (nested () = ("n", [ 1; 2 ])) "nested pair";
  check (maybe (Some "x") = Some "x" && maybe None = None) "option";
  check (multiplier () 6 7 = 42) "returned closure";
  check (fold [ 1; 2; 3 ] 10 ( + ) = 16 && fold [ 2; 3 ] 1 ( * ) = 6) "two-argument callback";
  (let seen = ref [] in
   each [ 1; 2 ] (fun v -> seen := v :: !seen);
   check (!seen = [ 2; 1 ]) "unit callback");
  check (call_twice (fun () -> count ()) > 0) "zero-argument callback";
  check (echo_i64 (-5) = -5 && echo_i64 max_int = max_int && echo_u8 200 = 200) "integers";
  check (raises (fun () -> echo_u8 300) (function Error (`Binding, _) -> true | _ -> false)) "uint8 range";
  check (raises (fun () -> echo_u8 (-1)) (function Error (`Binding, _) -> true | _ -> false)) "unsigned range";
  check (raises big (function Error (`Arithmetic, _) -> true | _ -> false)) "uint64 beyond int";
  check (raises smallest (function Error (`Arithmetic, _) -> true | _ -> false)) "int64 beyond int";
  check (echo "h\195\169" = "h\195\169" && with_nul () = "a\000b") "strings are bytes";
  (let a = ref 1 and b = ref 2 in
   swap a b;
   check (!a = 2 && !b = 1) "ref cells");
  (let t = ref "a" in
   append t "b";
   check (!t = "ab") "string ref");
  check (total [| 1.; 2.; 3. |] = 6. && doubled [| 1.; 2. |] = [| 2.; 4. |]) "policy conversion";
  check (Stats.mean [| 1.; 3. |] = 2.) "policy module";
  check (Outer_Inner.depth (Outer.inner (Outer.create ())) = 2) "nested class";

  (* Exceptions. *)
  check (raises (fun () -> fail (-1)) (function Invalid_argument m -> m = "negative" | _ -> false)) "invalid_argument";
  check (raises (fun () -> fail 0) (function Failure m -> m = "zero" | _ -> false)) "runtime_error is Failure";
  check (raises (fun () -> fail 1) (function Error (`Arithmetic, "one") -> true | _ -> false)) "overflow_error";
  check (raises (fun () -> fail 2) (function Error (`Domain_error, "two") -> true | _ -> false)) "domain_error";
  check (raises (fun () -> fail 3) (function Error (`Length_error, "three") -> true | _ -> false)) "length_error";
  check (raises (fun () -> fail 4) (function Error (`Out_of_range, "four") -> true | _ -> false)) "out_of_range";
  check (raises (fun () -> fail 5) (function Error (`Native, _) | Failure _ -> true | _ -> false)) "non-standard exception";
  check (fail 6 = 6) "no exception";
  (* OCaml and C++ buffer standard output separately; redirected, they must still come out in order. *)
  let file = Filename.temp_file "names" ".out" in
  let status = Sys.command (Filename.quote_command Sys.executable_name [ "--interleave" ] ~stdout:file) in
  let ordered = Stdlib.In_channel.with_open_bin file Stdlib.In_channel.input_all in
  Sys.remove file;
  check (status = 0 && ordered = "a\nb\nc\nbefore\ninside\nafter\nd\n") ("OCaml and C++ output in order: " ^ Stdlib.String.escaped ordered);
  Stdlib.print_string "[";
  Stdlib.flush stdout;
  say "cout";
  Stdlib.print_string "]\n";
  Stdlib.Gc.full_major ();
  Printf.printf "OCaml full names passed (%d checks)\n" !checks
