(* Runtime tests for the generated OCaml libraries. Run by the toplevel,
     ocaml -I DIR ocaml_demo.cma ocaml_verify.cma ocaml_names.cma test_ocaml.ml FAMILY
   and compiled to native code with the matching .cmxa libraries. *)
module D = Ocaml_demo.R_demo
module V = Ocaml_verify.R_verify
module N = Ocaml_names

let family = Sys.argv.(1)
let passes = ref 0
let failures = ref 0

let report ok description =
  if ok then incr passes
  else begin incr failures; Printf.printf "FAIL %s\n%!" description end

let check description f =
  report (try f () with e -> Printf.printf "error %s\n" (Printexc.to_string e); false) description

let contains text part =
  let n = String.length text and m = String.length part in
  let rec at i = i + m <= n && (String.sub text i m = part || at (i + 1)) in
  at 0

(* The kind and message of the error F raises, if any. *)
let outcome f =
  match f () with
  | _ -> None
  | exception Ocaml_demo.Error (kind, message) -> Some (Ocaml_demo.string_of_error_kind kind, message)
  | exception Ocaml_verify.Error (kind, message) -> Some (Ocaml_verify.string_of_error_kind kind, message)
  | exception Ocaml_names.Error (kind, message) -> Some (Ocaml_names.string_of_error_kind kind, message)
  | exception e -> Some ("other", Printexc.to_string e)

let fails ?kind description fragment f =
  match outcome f with
  | None -> report false (description ^ ": no error")
  | Some (actual, message) ->
      report (contains message fragment) (Printf.sprintf "%s: expected %S, got %S" description fragment message);
      Option.iter (fun k -> report (k = actual) (Printf.sprintf "%s: expected kind %s, got %s" description k actual)) kind

let raises_invalid_argument f = match f () with _ -> false | exception Invalid_argument _ -> true

(* Unreachable objects are destroyed when a collection finalizes them. *)
let eventually f =
  let rec loop tries = f () || (tries < 100 && (Gc.full_major (); loop (tries + 1))) in
  loop 0

let[@inline never] abandon () = ignore (Sys.opaque_identity (D.Counter.create 1))

let scalars () =
  (* Names: keywords take an underscore, values start lowercase, defaults are labels. *)
  check "type_" (fun () -> N.type_ () = 42 && N.type_ ~value:7 () = 7);
  check "upper" (fun () -> N.upper 3 = 3);
  check "list" (fun () -> N.list () = 42 && List.length [1; 2] = 2);
  check "labels" (fun () -> N.labels () = 12 && N.labels ~first:3 () = 32 && N.labels ~second:4 ~first:3 () = 34);
  fails ~kind:"argument" "label gap" "optional argument ~second requires ~first" (fun () -> N.labels ~second:4 ());
  check "keyword labels" (fun () -> N.keywords ~begin_:3 ~end_:4 () = 34);
  check "unnamed" (fun () -> N.unnamed () = 5 && N.unnamed ~arg0:1 () = 5);
  check "required then default" (fun () -> N.required_then_default 1 = 2 && N.required_then_default ~step:5 1 = 6);
  check "seven" (fun () -> N.seven 1 2 3 4 5 6 7L = 28L);
  check "eight" (fun () -> N.eight 1 2 3 4 5 6 7 = 36 && N.eight ~h:1 1 2 3 4 5 6 7 = 29);
  check "namespaces" (fun () -> N.Outer.depth () = 1 && N.Outer.Inner.depth () = 2);
  let p = N.Point.create ~x:1 ~y:2 () in
  check "point" (fun () -> N.Point.sum p = 3 && N.Point.sum ~scale:2 p = 6 && N.Point.origin () = 0);
  check "point wide method" (fun () -> N.Point.method_ p 1 2 3 4 5 = 22 && N.Point.method_ ~f:0 p 1 2 3 4 5 = 16);
  check "point fields" (fun () ->
      N.Point.set_x p 5; N.Point.set_tag p 'z';
      N.Point.x p = 5 && N.Point.y p = 2 && N.Point.tag p = 'z' && N.Point.weight p = 1.5);
  N.Point.close p;
  check "default point" (fun () -> let q = N.Point.create () in let s = N.Point.sum q in N.Point.close q; s = 0);
  (* Integers: OCaml int up to 32 bits, int64 for 64-bit types, with C++ range checks. *)
  List.iter
    (fun (name, f, low, high) ->
      check (name ^ " bounds") (fun () -> f low = low && f high = high && f 42 = 42);
      fails ~kind:"range" (name ^ " below") "integer outside C++ type range" (fun () -> f (low - 1));
      fails ~kind:"range" (name ^ " above") "integer outside C++ type range" (fun () -> f (high + 1)))
    [ ("schar", V.echo_schar, -128, 127); ("uchar", V.echo_uchar, 0, 255);
      ("short", V.echo_short, -32768, 32767); ("ushort", V.echo_ushort, 0, 65535);
      ("int", V.echo_int, Int32.to_int Int32.min_int, Int32.to_int Int32.max_int);
      ("uint", V.echo_uint, 0, 4294967295) ];
  check "char" (fun () -> V.echo_char 'A' = 'A' && V.echo_char '\255' = '\255' && V.echo_char '\000' = '\000');
  List.iter
    (fun (name, f) -> check name (fun () -> f Int64.min_int = Int64.min_int && f Int64.max_int = Int64.max_int && f 42L = 42L))
    [ ("long", V.echo_long); ("ll", V.echo_ll) ];
  (* Unsigned 64-bit values keep their bits in int64. *)
  List.iter
    (fun (name, f) ->
      check name (fun () -> f (-1L) = -1L && Int64.unsigned_to_int (f 42L) = Some 42 && f Int64.min_int = Int64.min_int))
    [ ("ulong", V.echo_ulong); ("ull", V.echo_ull) ];
  check "too_large" (fun () -> D.too_large () = 9007199254740993L);
  check "floats" (fun () -> V.echo_float 1.25 = 1.25 && V.echo_double 0.5 = 0.5 && V.echo_double max_float = max_float);
  check "half" (fun () -> D.half 3.0 = 1.5);
  fails ~kind:"range" "float range" "outside C++ type range" (fun () -> V.echo_float 1e39);
  fails ~kind:"range" "infinity" "finite" (fun () -> V.echo_double infinity);
  fails ~kind:"range" "nan" "finite" (fun () -> V.echo_double nan);
  fails ~kind:"range" "nonfinite result" "non-finite" V.nonfinite_result;
  check "bool" (fun () -> V.echo_bool true && not (V.echo_bool false) && D.negate false);
  (* Strings are bytes in both directions, including NULs and invalid UTF-8. *)
  let long = String.concat "" (List.init 10000 (fun _ -> "long")) in
  List.iter (fun text -> check (Printf.sprintf "string %S" (if String.length text > 20 then "long" else text)) (fun () -> V.echo_string text = text))
    [ ""; "world"; "世界😀"; "a\000b"; long; "\255\254" ];
  check "string bytes" (fun () -> String.length (V.echo_string "世界😀") = 10);
  check "embedded nul" (fun () -> V.embedded_nul () = "a\000b");
  check "greet" (fun () -> D.greet "OCaml" = "Hello, OCaml!");
  check "defaulted" (fun () -> V.defaulted () = 42 && V.defaulted ~value:12 () = 12);
  check "alias" (fun () -> V.alias_identity 42 = 42);
  check "void" (fun () -> V.do_nothing () = ());
  for _ = 1 to 20 do
    fails ~kind:"native" "fail" "native failure" D.fail;
    fails ~kind:"native" "unknown" "unknown C++ exception" V.unknown_error;
    fails ~kind:"allocation" "allocation" "allocation" V.allocation_error;
    check "add" (fun () -> D.add 20 22 = 42)
  done;
  check "match" (fun () ->
      match D.fail () with () -> false | exception Ocaml_demo.Error (Ocaml_demo.Native, "native failure") -> true);
  check "printer" (fun () ->
      Printexc.to_string (Ocaml_demo.Error (Ocaml_demo.Native, "native failure")) = "Ocaml_demo.Error(Native, \"native failure\")")

let objects () =
  let live = D.Counter.live in
  check "none live" (fun () -> live () = 0);
  fails ~kind:"native" "negative" "negative initial" (fun () -> D.Counter.create (-1));
  check "failed constructor" (fun () -> live () = 0);
  let c = D.Counter.create 20 in
  check "live" (fun () -> live () = 1 && D.live () = 1);
  check "add" (fun () -> D.Counter.add c 22 = 42 && D.Counter.value c = 42);
  check "fields" (fun () -> D.Counter.count c = 42 && D.Counter.initial c = 20 && D.Counter.label c = "counter");
  check "static" (fun () -> D.Counter.answer () = 42);
  D.Counter.set_count c 7;
  check "set_count" (fun () -> D.Counter.value c = 7);
  let label = "世界😀\000" in
  D.Counter.set_label c label;
  check "label" (fun () -> D.Counter.label c = label);
  fails ~kind:"range" "field range" "integer outside C++ type range" (fun () -> D.Counter.set_count c (1 lsl 40));
  check "unchanged" (fun () -> D.Counter.count c = 7);
  let other = D.Counter.create 1 in
  check "compare" (fun () -> raises_invalid_argument (fun () -> compare c other));
  check "marshal" (fun () -> raises_invalid_argument (fun () -> Marshal.to_string c []));
  check "physical equality" (fun () -> c == c && not (c == other));
  D.Counter.close other;
  D.Counter.close c;
  check "closed" (fun () -> live () = 0);
  fails ~kind:"closed" "value closed" "native object is closed" (fun () -> D.Counter.value c);
  fails ~kind:"closed" "set closed" "native object is closed" (fun () -> D.Counter.set_count c 1);
  check "close twice" (fun () -> D.Counter.close c = ());
  abandon ();
  check "finalized" (fun () -> eventually (fun () -> live () = 0));
  let s = V.Series.create () and w = V.Worklist.create 2 and t = V.AsyncTask.create 1 60000 false in
  check "verify live" (fun () -> V.Series.live () = 1 && V.Worklist.live () = 1 && V.AsyncTask.live () = 1);
  V.Series.close s; V.Worklist.close w; V.AsyncTask.close t;
  check "verify closed" (fun () -> V.Series.live () = 0 && V.AsyncTask.live () = 0 && V.AsyncTask.workers () = 0);
  (* Calls from several domains are serialized. *)
  let shared = D.Counter.create 0 in
  List.iter Domain.join (List.init 4 (fun _ -> Domain.spawn (fun () -> for _ = 1 to 1000 do ignore (D.Counter.add shared 1) done)));
  check "domains" (fun () -> D.Counter.value shared = 4000);
  D.Counter.close shared;
  (* Finalizers run in every domain's collections while others call in. *)
  let churn () =
    for i = 1 to 300 do
      ignore (D.Counter.add (D.Counter.create 1) 1);
      if i mod 50 = 0 then Gc.full_major ()
    done
  in
  List.iter Domain.join (List.init 4 (fun _ -> Domain.spawn churn));
  check "churn finalized" (fun () -> eventually (fun () -> live () = 0))

let features () =
  check "maximum" (fun () -> V.maximum 20 42 = 42);
  check "template" (fun () -> V.template_box 42 = 42);
  check "variadic" (fun () -> V.variadic_sum 10 20 12 = 42);
  check "constant" (fun () -> V.constant_answer () = 42);
  check "constrained" (fun () -> V.constrained_sum 20 22 = 42);
  check "hierarchy" (fun () -> V.hierarchy_value 42 = 42 && V.hierarchy_crosscast ());
  check "C identity" (fun () -> V.rosetta_r_c_identity 42 = 42);
  fails ~kind:"range" "overflow" "overflow" (fun () -> V.constrained_sum (Int32.to_int Int32.max_int) 1);
  fails ~kind:"index" "series count" "series count" (fun () -> V.Series.create ~first:0 ~count:(-1) ());
  let s = V.Series.create () in
  check "series" (fun () -> V.Series.size s = 2 && V.Series.get s = 20 && V.Series.scaled_sum s 2 = 82);
  check "series text" (fun () -> V.Series.text s = "[20, 21]");
  V.Series.set s 1 22;
  check "series set" (fun () -> V.Series.scaled_sum s 1 = 42 && V.Series.get ~index:1 s = 22);
  fails ~kind:"index" "series get" "out of range" (fun () -> V.Series.get ~index:2 s);
  fails "series set" "out of range" (fun () -> V.Series.set s (-1) 1);
  V.Series.close s;
  check "series labels" (fun () ->
      let s = V.Series.create ~count:3 ~first:1 () in
      let ok = V.Series.text s = "[1, 2, 3]" in
      V.Series.close s; ok);
  List.iter
    (fun (order, expected) ->
      let q = V.Worklist.create order in
      List.iter (V.Worklist.push q) [ "a"; "z"; "m" ];
      check "worklist size" (fun () -> V.Worklist.size q = 3L);
      check "worklist order" (fun () ->
          let a = V.Worklist.pop q in let b = V.Worklist.pop q in let c = V.Worklist.pop q in [ a; b; c ] = expected);
      fails ~kind:"index" "worklist empty" "empty" (fun () -> V.Worklist.pop q);
      V.Worklist.close q)
    [ (1, [ "m"; "z"; "a" ]); (2, [ "a"; "z"; "m" ]); (3, [ "z"; "m"; "a" ]); (4, [ "a"; "m"; "z" ]) ];
  fails ~kind:"argument" "worklist order" "unknown worklist order" (fun () -> V.Worklist.create 0);
  let task = V.AsyncTask.create 21 0 false in
  check "task" (fun () -> V.AsyncTask.wait task 2000 && V.AsyncTask.result task = 42 && V.AsyncTask.result task = 42);
  V.AsyncTask.close task;
  let task = V.AsyncTask.create 1 0 true in
  check "task wait" (fun () -> V.AsyncTask.wait task 2000);
  fails "task failure" "async worker failure" (fun () -> V.AsyncTask.result task);
  V.AsyncTask.close task;
  let task = V.AsyncTask.create 1 60000 false in
  check "task cancel" (fun () ->
      (not (V.AsyncTask.ready task)) && (not (V.AsyncTask.wait task 0)) && V.AsyncTask.cancel task && V.AsyncTask.wait task 2000);
  fails "task cancelled" "cancelled" (fun () -> V.AsyncTask.result task);
  V.AsyncTask.close task;
  fails ~kind:"index" "task delay" "delay" (fun () -> V.AsyncTask.create 1 (-1) false);
  check "workers" (fun () -> V.AsyncTask.workers () = 0);
  (* A domain waits on a slow task while this one keeps calling in: waits
     release the OCaml runtime, but calls are serialized, so the add calls
     finish once the wait returns. *)
  let slow = V.AsyncTask.create 2 100 false in
  let waiter = Domain.spawn (fun () -> V.AsyncTask.wait slow 5000) in
  check "parallel wait" (fun () -> Domain.join waiter && V.AsyncTask.result slow = 4);
  V.AsyncTask.close slow;
  (* Leave a 60s worker running at exit, bounded by the test timeout. *)
  ignore (V.AsyncTask.create 1 60000 false)

let () =
  (match family with
   | "scalars" -> scalars ()
   | "objects" -> objects ()
   | "features" -> features ()
   | other -> failwith ("unknown family " ^ other));
  Printf.printf "OCaml %s: %d passed, %d failed\n%!" family !passes !failures;
  exit (if !failures = 0 then 0 else 1)
