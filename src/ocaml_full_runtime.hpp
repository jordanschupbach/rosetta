#pragma once

// Runtime of the full OCaml backend (--ocaml-api full, ocaml_full.cpp).
//
// ocaml_full_runtime_ml is the OCaml module Rosetta__ at the top of the generated NAME.ml (hidden
// by NAME.mli): the value encoding every full C ABI backend shares (lisp_full_runtime.hpp), the
// object, callback and override tables, and the typed conversions the generated wrappers compose.
// ocaml_full_stubs_cpp is the C++ glue appended to the generated library's main source: a handful
// of `external` primitives (call, object handles as custom blocks with finalizers, init) and the
// two hooks C++ calls back through (caml_callback).
//
// Placeholders: @M@ the module name (C symbols rosetta_@M@_*), @C@ the C entry points' prefix.
namespace rosetta::detail {

inline constexpr const char* ocaml_full_runtime_ml = R"ml(
(* ------------------------------------------------------------------ runtime (internal) *)
module Rosetta__ = struct
  type handle

  external native_call : int -> string -> string = "rosetta_@M@_ml_call"
  external handle_make : int -> handle = "rosetta_@M@_ml_handle"
  external handle_ptr : handle -> int = "rosetta_@M@_ml_handle_ptr" [@@noalloc]
  external handle_close : handle -> unit = "rosetta_@M@_ml_handle_close"
  external native_init : unit -> int = "rosetta_@M@_ml_init"
  external native_shutdown : unit -> unit = "rosetta_@M@_ml_shutdown"

  type error_kind =
    [ `Binding | `Out_of_range | `Domain_error | `Length_error | `Arithmetic | `Native ]

  exception Error of error_kind * string

  let kind_name = function
    | `Binding -> "`Binding"
    | `Out_of_range -> "`Out_of_range"
    | `Domain_error -> "`Domain_error"
    | `Length_error -> "`Length_error"
    | `Arithmetic -> "`Arithmetic"
    | `Native -> "`Native"

  let () =
    Printexc.register_printer (function
      | Error (kind, message) -> Some (Printf.sprintf "%s.Error (%s, %S)" "@TOP@" (kind_name kind) message)
      | _ -> None)

  (* A value crossing to or from C++ (the tags of lisp_full_runtime.hpp). *)
  type node =
    | Nil
    | True
    | False
    | Int of int
    | Big of string
    | Real of float
    | Cplx of float * float
    | Str of string
    | Sym of string
    | Byt of string
    | Lst of node list
    | Tup of node list
    | Reals of float array
    | Map of (node * node) list
    | Cons of node * node
    | Object of repr
    | Native_fn of handle
    | Fn of int
    | Ref of int * node

  (* A native object: its holder (a custom block freeing it when collected), its class index,
     the object it borrows from (kept alive), and, for OCaml subclass instances, its overrides. *)
  and repr = {
    handle : handle;
    cls : int;
    mutable id : int;
    mutable parent : node;
    mutable overrides : override list;
  }

  and override = {
    generic : int;
    closure : Stdlib.Obj.t option;
    handler : ctx -> node array -> node;
  }

  (* One call's encoding state: reference cells to write back, and the callbacks it registered. *)
  and ctx = {
    mutable slots : (int * (node -> unit)) list;
    mutable nslots : int;
    mutable fns : int list;
  }

  let malformed () = raise (Error (`Binding, "malformed native value"))

  let rec describe = function
    | Nil -> "nothing"
    | True | False -> "a bool"
    | Int _ | Big _ -> "an integer"
    | Real _ -> "a float"
    | Cplx _ -> "a complex"
    | Str _ -> "a string"
    | Sym s -> "enumerator " ^ s
    | Byt _ -> "bytes"
    | Lst _ | Reals _ -> "a sequence"
    | Tup l -> "a tuple of " ^ string_of_int (List.length l)
    | Map _ -> "a map"
    | Cons (a, b) -> "a pair of " ^ describe a ^ " and " ^ describe b
    | Object _ -> "a native object"
    | Native_fn _ | Fn _ -> "a function"
    | Ref (_, n) -> describe n

  let bad what n = raise (Error (`Binding, "expected " ^ what ^ " from C++, got " ^ describe n))

  (* ---------------------------------------------------------------- OCaml values C++ refers to *)
  (* Callbacks (strong, counted) and objects with an id (weak, strong while C++ retains them). *)
  type keep = Keep_handler of (ctx -> node array -> node) | Keep_object of repr

  let lock = Mutex.create ()

  let locked f =
    Mutex.lock lock;
    match f () with
    | v -> Mutex.unlock lock; v
    | exception e -> Mutex.unlock lock; raise e

  let strong : (int, keep * int ref) Hashtbl.t = Hashtbl.create 64
  let weak : (int, repr Weak.t) Hashtbl.t = Hashtbl.create 64
  let next = ref 0

  let fresh_id () =
    incr next;
    if !next land 4095 = 0 then
      Hashtbl.filter_map_inplace (fun _ w -> if Weak.check w 0 then Some w else None) weak;
    !next

  let remember id r =
    let w = Weak.create 1 in
    Weak.set w 0 (Some r);
    Hashtbl.replace weak id w

  let object_id r =
    if r.id <> 0 then r.id
    else
      locked (fun () ->
          let id = fresh_id () in
          remember id r;
          r.id <- id;
          id)

  let lookup_object id =
    let found =
      locked (fun () ->
          match Hashtbl.find_opt strong id with
          | Some (Keep_object r, _) -> Some r
          | _ -> ( match Hashtbl.find_opt weak id with Some w -> Weak.get w 0 | None -> None))
    in
    match found with
    | Some r -> r
    | None -> raise (Error (`Binding, "the OCaml object of a native reference is gone"))

  (* C++ keeps (delta 1) or drops (delta -1) a reference to OCaml value [id]. *)
  let retain id delta =
    locked (fun () ->
        match Hashtbl.find_opt strong id with
        | Some (_, count) ->
            count := !count + delta;
            if !count <= 0 then Hashtbl.remove strong id
        | None ->
            if delta > 0 then
              match Hashtbl.find_opt weak id with
              | Some w -> (
                  match Weak.get w 0 with
                  | Some r -> Hashtbl.replace strong id (Keep_object r, ref delta)
                  | None -> ())
              | None -> ())

  let register_handler h =
    locked (fun () ->
        let id = fresh_id () in
        Hashtbl.replace strong id (Keep_handler h, ref 1);
        id)

  let find_handler id =
    locked (fun () ->
        match Hashtbl.find_opt strong id with Some (Keep_handler h, _) -> Some h | _ -> None)

  (* ---------------------------------------------------------------- encoding (OCaml -> C++) *)
  let rec put b depth n =
    if depth > 10000 then raise (Error (`Binding, "value nesting is too deep"));
    let tag t = Buffer.add_char b (Char.unsafe_chr t) in
    let i64 v = Buffer.add_int64_ne b v in
    let int v = i64 (Int64.of_int v) in
    let real f = i64 (Int64.bits_of_float f) in
    let text s = int (String.length s); Buffer.add_string b s in
    let sub = put b (depth + 1) in
    match n with
    | Nil -> tag 0
    | True -> tag 1
    | False -> tag 19
    | Int v -> tag 2; int v
    | Big s -> tag 3; text s
    | Real f -> tag 4; real f
    | Cplx (re, im) -> tag 18; real re; real im
    | Str s -> tag 5; text s
    | Sym s -> tag 14; text s
    | Byt s -> tag 15; text s
    | Lst l -> tag 6; int (List.length l); List.iter sub l
    | Tup l -> tag 16; int (List.length l); List.iter sub l
    | Reals a -> tag 7; int (Array.length a); Array.iter real a
    | Map kvs -> tag 9; int (List.length kvs); List.iter (fun (k, v) -> sub k; sub v) kvs
    | Cons (k, v) -> tag 8; sub k; sub v
    | Object r ->
        let p = handle_ptr r.handle in
        tag 10; int p; int (if p = 0 then 0 else object_id r)
    | Native_fn h -> tag 10; int (handle_ptr h); int 0
    | Fn id -> tag 12; int id
    | Ref (slot, v) -> tag 13; Buffer.add_int32_ne b (Int32.of_int slot); sub v

  (* ---------------------------------------------------------------- decoding (C++ -> OCaml) *)
  type reader = { s : string; mutable pos : int }

  let need r n = if n < 0 || r.pos + n > String.length r.s then malformed ()

  let u8 r =
    need r 1;
    let c = Char.code (String.unsafe_get r.s r.pos) in
    r.pos <- r.pos + 1;
    c

  let u32 r =
    need r 4;
    let v = Int32.to_int (String.get_int32_ne r.s r.pos) land 0xffffffff in
    r.pos <- r.pos + 4;
    v

  let i64 r =
    need r 8;
    let v = String.get_int64_ne r.s r.pos in
    r.pos <- r.pos + 8;
    v

  let count r unit =
    let n = i64 r in
    let left = Int64.of_int ((String.length r.s - r.pos) / unit) in
    if Int64.compare n 0L < 0 || Int64.compare n left > 0 then malformed ();
    Int64.to_int n

  let text r =
    let n = count r 1 in
    let v = String.sub r.s r.pos n in
    r.pos <- r.pos + n;
    v

  let float r = Int64.float_of_bits (i64 r)

  let wrap holder cls parent = { handle = handle_make holder; cls; id = 0; parent; overrides = [] }

  let rec get r args depth =
    if depth > 10000 then malformed ();
    let items n =
      let acc = ref [] in
      for _ = 1 to n do acc := get r args (depth + 1) :: !acc done;
      List.rev !acc
    in
    match u8 r with
    | 0 -> Nil
    | 1 -> True
    | 19 -> False
    | 2 ->
        let v = i64 r in
        let i = Int64.to_int v in
        if Int64.equal (Int64.of_int i) v then Int i else Big (Int64.to_string v)
    | 3 -> Big (text r)
    | 4 -> Real (float r)
    | 18 ->
        let re = float r in
        let im = float r in
        Cplx (re, im)
    | 5 -> Str (text r)
    | 14 -> Sym (text r)
    | 15 -> Byt (text r)
    | 6 -> Lst (items (count r 1))
    | 16 -> Tup (items (count r 1))
    | 7 ->
        let n = count r 8 in
        let a = Array.make n 0. in
        for i = 0 to n - 1 do a.(i) <- float r done;
        Reals a
    | 9 ->
        let n = count r 2 in
        let acc = ref [] in
        for _ = 1 to n do
          let k = get r args (depth + 1) in
          let v = get r args (depth + 1) in
          acc := (k, v) :: !acc
        done;
        Map (List.rev !acc)
    | 8 ->
        let k = get r args (depth + 1) in
        let v = get r args (depth + 1) in
        Cons (k, v)
    | 10 ->
        let holder = Int64.to_int (i64 r) in
        let cls = u32 r in
        let parent = Int64.to_int (i64 r) in
        Object (wrap holder cls (if parent >= 0 && parent < Array.length args then args.(parent) else Nil))
    | 11 -> Object (lookup_object (Int64.to_int (i64 r)))
    | 12 -> Native_fn (handle_make (Int64.to_int (i64 r)))
    | 17 ->
        let i = u32 r in
        if i < Array.length args then args.(i) else Nil
    | _ -> malformed ()

  (* ---------------------------------------------------------------- calls *)
  (* An OCaml exception raised in a callback crosses C++ as status 2 and is raised again here. *)
  let pending : (exn * Printexc.raw_backtrace) option ref = ref None

  (* Callbacks registered while encoding callback results: released after the outermost call. *)
  let deferred : int list ref = ref []
  let depth = ref 0
  let new_ctx () = { slots = []; nslots = 0; fns = [] }

  let failure kind message =
    match kind with
    | "invalid-argument" -> Invalid_argument message
    | "native" -> Failure message
    | "allocation" -> Out_of_memory
    | "out-of-range" -> Error (`Out_of_range, message)
    | "domain-error" -> Error (`Domain_error, message)
    | "length-error" -> Error (`Length_error, message)
    | "arithmetic-error" -> Error (`Arithmetic, message)
    | "binding" -> Error (`Binding, message)
    | _ -> Error (`Native, message)

  let release ids = List.iter (fun id -> retain id (-1)) ids

  (* OCaml and C++ buffer standard output separately: flush OCaml's before C++ runs (a native call)
     and when an OCaml callback returns to C++, so piped output keeps its order. *)
  let flush_std () = try Stdlib.flush Stdlib.stdout; Stdlib.flush Stdlib.stderr with Sys_error _ -> ()

  let call_raw index (nodes : node array) (slots : (node -> unit) array) =
    let b = Buffer.create 64 in
    Buffer.add_int32_ne b (Int32.of_int (Array.length nodes));
    Array.iter (put b 0) nodes;
    let data = Buffer.contents b in
    flush_std ();
    incr depth;
    let out = match native_call index data with v -> decr depth; v | exception e -> decr depth; raise e in
    if !depth = 0 && !deferred <> [] then begin
      let ids = !deferred in
      deferred := [];
      release ids
    end;
    if String.length out = 0 then malformed ();
    let r = { s = out; pos = 1 } in
    match out.[0] with
    | '\000' ->
        let result = get r nodes 0 in
        let n = u32 r in
        for _ = 1 to n do
          let slot = u32 r in
          let v = get r nodes 0 in
          if slot < Array.length slots then slots.(slot) v
        done;
        result
    | '\002' -> (
        let _ = get r [||] 0 in
        let message = match get r [||] 0 with Str s -> s | _ -> "OCaml callback failed" in
        match !pending with
        | Some (e, bt) -> pending := None; Printexc.raise_with_backtrace e bt
        | None -> raise (Failure message))
    | _ ->
        let kind = match get r [||] 0 with Sym s -> s | _ -> "native" in
        let message = match get r [||] 0 with Str s -> s | _ -> "C++ exception" in
        raise (failure kind message)

  (* Calls native function [index] with the arguments [args] encodes; [dec] converts the result. *)
  let call index (args : ctx -> node list) (dec : node -> 'a) : 'a =
    let c = new_ctx () in
    let nodes = Array.of_list (args c) in
    let slots = Array.make c.nslots (fun (_ : node) -> ()) in
    List.iter (fun (i, f) -> slots.(i) <- f) c.slots;
    let result =
      match call_raw index nodes slots with
      | v -> release c.fns; v
      | exception e -> release c.fns; raise e
    in
    dec result

  (* Required arguments, then the optional ones up to the last one given: a gap takes the C++
     default when it is a literal, and is an error otherwise. *)
  let trailing (required : node list) (optional : (node option * node option * string) list) =
    let rec last i best = function
      | [] -> best
      | (Some _, _, _) :: rest -> last (i + 1) (i + 1) rest
      | _ :: rest -> last (i + 1) best rest
    in
    let n = last 0 0 optional in
    let rec take i acc = function
      | [] -> List.rev acc
      | _ when i >= n -> List.rev acc
      | (Some v, _, _) :: rest -> take (i + 1) (v :: acc) rest
      | (None, Some d, _) :: rest -> take (i + 1) (d :: acc) rest
      | (None, None, name) :: _ ->
          invalid_arg ("argument ~" ^ name ^ " has no OCaml default: pass it when passing a later optional argument")
    in
    required @ take 0 [] optional

  (* ---------------------------------------------------------------- objects *)
  let construct index (overrides : override list option) (args : ctx -> node list) : repr =
    let obj = function Object r -> r | n -> bad "a native object" n in
    match overrides with
    | None -> call index (fun c -> Int 0 :: args c) obj
    | Some list ->
        let id = locked fresh_id in
        let r = call index (fun c -> Int id :: args c) obj in
        r.id <- id;
        r.overrides <- list;
        locked (fun () -> remember id r);
        r

  let dispatch (r : repr) generic =
    match r.overrides with
    | [] -> None
    | l -> (
        match List.find_opt (fun o -> o.generic = generic) l with
        | Some { closure = Some f; _ } -> Some f
        | _ -> None)

  (* An override of virtual method [generic]: the OCaml closure, or the C++ implementation
     (native function [fallback], which runs it without dispatching again). *)
  let override generic (f : 'f option) (make : 'f -> ctx -> node array -> node) fallback =
    match f with
    | Some f -> { generic; closure = Some (Stdlib.Obj.repr f); handler = make f }
    | None -> { generic; closure = None; handler = (fun _ args -> call_raw fallback args [||]) }

  let close (r : repr) = handle_close r.handle
  let is_closed (r : repr) = handle_ptr r.handle = 0
  let is_a (r : repr) (classes : int array) = Array.mem r.cls classes

  (* ---------------------------------------------------------------- C++ calls into OCaml *)
  let invoke kind id generic data =
    let c = new_ctx () in
    let reply =
      match
        let r = { s = data; pos = 0 } in
        let n = u32 r in
        let args = Array.make n Nil in
        for i = 0 to n - 1 do args.(i) <- get r [||] 0 done;
        let h =
          if kind land 0xff = 0 then
            match find_handler id with
            | Some h -> h
            | None -> raise (Error (`Binding, "the OCaml callback is gone"))
          else
            let r = lookup_object id in
            match List.find_opt (fun o -> o.generic = generic) r.overrides with
            | Some o -> o.handler
            | None -> raise (Error (`Binding, "the OCaml object does not implement this virtual method"))
        in
        let result = h c args in
        let b = Buffer.create 64 in
        Buffer.add_char b '\000';
        put b 0 result;
        Buffer.contents b
      with
      | v -> v
      | exception e ->
          let bt = Printexc.get_raw_backtrace () in
          pending := Some (e, bt);
          let b = Buffer.create 64 in
          Buffer.add_char b '\002';
          put b 0 (Str (Printexc.to_string e));
          Buffer.contents b
    in
    deferred := c.fns @ !deferred;
    flush_std ();
    reply

  let () =
    Callback.register "rosetta_@M@_invoke" invoke;
    Callback.register "rosetta_@M@_retain" retain;
    if native_init () <> 0 then failwith "native module @M@ was generated by a different Rosetta version";
    at_exit native_shutdown

  (* ---------------------------------------------------------------- conversions *)
  (* Encoders (ctx -> 'a -> node) and decoders (node -> 'a), composed by the wrappers. *)
  let unit_e _ () = Nil
  let int_e _ v = Int v
  let float_e _ v = Real v
  let bool_e _ v = if v then True else False
  let string_e _ v = Str v
  let char_e _ v = Str (String.make 1 v)
  let bytes_e _ v = Byt (Bytes.to_string v)
  let complex_e _ (v : Complex.t) = Cplx (v.re, v.im)
  let reals_e _ v = Reals (Array.copy v)
  let list_e e c l = Lst (List.map (e c) l)
  let option_e e c = function None -> Nil | Some v -> e c v
  let map_e ke ve c l = Map (List.map (fun (k, v) -> (ke c k, ve c v)) l)
  let obj_e _ (r : repr) = Object r
  let tuple2_e e1 e2 c (a, b) = Tup [ e1 c a; e2 c b ]
  let tuple3_e e1 e2 e3 c (a, b, d) = Tup [ e1 c a; e2 c b; e3 c d ]
  let tuple4_e e1 e2 e3 e4 c (a, b, d, e) = Tup [ e1 c a; e2 c b; e3 c d; e4 c e ]
  let tuple5_e e1 e2 e3 e4 e5 c (a, b, d, e, f) = Tup [ e1 c a; e2 c b; e3 c d; e4 c e; e5 c f ]

  let ref_e e d c (r : 'a ref) =
    let slot = c.nslots in
    c.nslots <- slot + 1;
    c.slots <- (slot, fun n -> r := d n) :: c.slots;
    Ref (slot, e c !r)

  let fn_e (h : ctx -> node array -> node) c =
    let id = register_handler h in
    c.fns <- id :: c.fns;
    Fn id

  let unit_d (_ : node) = ()

  let int_d = function
    | Int i -> i
    | Real f when Float.is_integer f && Float.abs f < 4.6e18 -> int_of_float f
    | Big s -> raise (Error (`Arithmetic, "C++ integer " ^ s ^ " does not fit in an OCaml int"))
    | n -> bad "an integer" n

  let float_d = function
    | Real f -> f
    | Int i -> float_of_int i
    | Big s -> float_of_string s
    | n -> bad "a number" n

  let bool_d = function True -> true | False | Nil -> false | Int i -> i <> 0 | n -> bad "a bool" n
  let string_d = function Str s | Byt s | Sym s -> s | Nil -> "" | n -> bad "a string" n

  let char_d = function
    | Str s when String.length s = 1 -> s.[0]
    | Int i when i >= 0 && i < 256 -> Char.chr i
    | n -> bad "a character" n

  let items = function
    | Lst l | Tup l -> l
    | Reals a -> Array.to_list (Array.map (fun f -> Real f) a)
    | Byt s -> List.init (String.length s) (fun i -> Int (Char.code s.[i]))
    | Nil -> []
    | n -> bad "a sequence" n

  let list_d d n = List.map d (items n)

  let bytes_d = function
    | Byt s | Str s -> Bytes.of_string s
    | n -> Bytes.of_seq (List.to_seq (List.map (fun v -> Char.chr (int_d v land 255)) (items n)))

  let reals_d = function Reals a -> a | n -> Array.of_list (list_d float_d n)

  let complex_d = function
    | Cplx (re, im) -> { Complex.re; im }
    | (Real _ | Int _) as n -> { Complex.re = float_d n; im = 0. }
    | n -> bad "a complex" n

  let option_d d = function Nil -> None | n -> Some (d n)

  let map_d kd vd = function
    | Map kvs -> List.map (fun (k, v) -> (kd k, vd v)) kvs
    | Nil -> []
    | Lst l -> List.map (function Cons (k, v) | Tup [ k; v ] -> (kd k, vd v) | n -> bad "a map entry" n) l
    | n -> bad "a map" n

  let parts n count =
    match n with
    | (Tup l | Lst l) when List.length l = count -> l
    | Cons (a, b) when count = 2 -> [ a; b ]
    | n -> bad ("a tuple of " ^ string_of_int count) n

  let tuple2_d d1 d2 n = match parts n 2 with [ a; b ] -> (d1 a, d2 b) | _ -> malformed ()
  let tuple3_d d1 d2 d3 n = match parts n 3 with [ a; b; c ] -> (d1 a, d2 b, d3 c) | _ -> malformed ()

  let tuple4_d d1 d2 d3 d4 n =
    match parts n 4 with [ a; b; c; d ] -> (d1 a, d2 b, d3 c, d4 d) | _ -> malformed ()

  let tuple5_d d1 d2 d3 d4 d5 n =
    match parts n 5 with [ a; b; c; d; e ] -> (d1 a, d2 b, d3 c, d4 d, d5 e) | _ -> malformed ()

  (* A null object result raises Not_found (results that may be null are options). *)
  let obj_d = function Object r -> r | Nil -> raise Not_found | n -> bad "a native object" n
  let fn_d = function Native_fn h -> h | Nil -> raise Not_found | n -> bad "a native function" n
  let apply h (args : ctx -> node list) dec = call 0 (fun c -> Native_fn h :: args c) dec
  let symbol_d = function Sym s | Str s -> `Name s | Int i -> `Value i | n -> bad "an enumerator" n
end
)ml";

// The C++ glue, appended to the generated library's main source.
inline constexpr const char* ocaml_full_stubs_cpp = R"cpp(
// ---------------------------------------------------------------- OCaml glue (OCaml side: Rosetta__ in @M@.ml)
#define CAML_NAME_SPACE
#include <caml/mlvalues.h>
#include <caml/memory.h>
#include <caml/alloc.h>
#include <caml/custom.h>
#include <caml/callback.h>
#include <caml/fail.h>
#include <cstdio>
#include <iostream>
namespace {
// Object handles: custom blocks holding a native Holder*, released (queued: finalizers never call
// back into OCaml) when the block is collected.
void rosetta_ocaml_finalize(value handle) {
    void* holder = *static_cast<void**>(Data_custom_val(handle));
    if (holder) @C@_release(holder, 0);
}
struct custom_operations rosetta_ocaml_handle_ops = {
    "rosetta.@M@.handle", rosetta_ocaml_finalize, custom_compare_default, custom_hash_default,
    custom_serialize_default, custom_deserialize_default, custom_compare_ext_default, custom_fixed_length_default};
unsigned char* rosetta_ocaml_message(const char* text, std::size_t* size) {
    // tag_string, length, bytes
    const std::size_t n = std::strlen(text);
    auto* out = static_cast<unsigned char*>(std::malloc(1 + 8 + n));
    if (!out) { *size = 0; return nullptr; }
    out[0] = 5;
    const unsigned long long length = n;
    std::memcpy(out + 1, &length, 8);
    std::memcpy(out + 9, text, n);
    *size = 9 + n;
    return out;
}
// C++ calls OCaml (a callback, or an override of a virtual method): Rosetta__.invoke answers with
// a status byte and the encoded result. The runtime lock is held: native calls never release it.
int rosetta_ocaml_invoke_hook(int kind, unsigned long long id, unsigned generic, const unsigned char* data, std::size_t size,
                              unsigned char** out, std::size_t* out_size) {
    static const value* closure = nullptr;
    if (!closure) closure = caml_named_value("rosetta_@M@_invoke");
    if (!closure) { *out = rosetta_ocaml_message("the OCaml side of the module is not initialized", out_size); return 1; }
    CAMLparam0();
    CAMLlocal2(input, result);
    // C++ output written so far comes before the callback's own (see rosetta_@M@_ml_call).
    std::cout.flush();
    std::fflush(stdout);
    input = caml_alloc_initialized_string(size, reinterpret_cast<const char*>(data));
    value arguments[4] = {Val_int(kind), Val_long(static_cast<intnat>(id)), Val_int(static_cast<int>(generic)), input};
    result = caml_callbackN_exn(*closure, 4, arguments);
    if (Is_exception_result(result)) {
        *out = rosetta_ocaml_message("OCaml exception in a callback", out_size);
        CAMLreturnT(int, 2);
    }
    const std::size_t length = caml_string_length(result);
    const int status = length ? static_cast<int>(Byte_u(result, 0)) : 1;
    const std::size_t n = length ? length - 1 : 0;
    *out = static_cast<unsigned char*>(std::malloc(n ? n : 1));
    if (!*out) { *out_size = 0; CAMLreturnT(int, 3); }
    if (n) std::memcpy(*out, Bytes_val(result) + 1, n);
    *out_size = n;
    CAMLreturnT(int, status);
}
void rosetta_ocaml_retain_hook(unsigned long long id, int delta) {
    static const value* closure = nullptr;
    if (!closure) closure = caml_named_value("rosetta_@M@_retain");
    if (!closure) return;
    caml_callback2_exn(*closure, Val_long(static_cast<intnat>(id)), Val_int(delta));
}
} // namespace
extern "C" value rosetta_@M@_ml_call(value index, value data) {
    CAMLparam2(index, data);
    CAMLlocal1(result);
    // A copy: callbacks may run the OCaml GC (and move the argument) while C++ reads it.
    std::string input(String_val(data), caml_string_length(data));
    unsigned char* out = nullptr;
    std::size_t out_size = 0;
    const int status = @C@_call(static_cast<unsigned>(Long_val(index)), reinterpret_cast<const unsigned char*>(input.data()), input.size(), &out, &out_size);
    // C++ output (std::cout, printf) is buffered apart from OCaml's when piped: flush it before
    // OCaml prints again, as OCaml flushes its own before each call.
    std::cout.flush();
    std::fflush(stdout);
    result = caml_alloc_string(out_size + 1);
    Bytes_val(result)[0] = static_cast<unsigned char>(status);
    if (out_size) std::memcpy(Bytes_val(result) + 1, out, out_size);
    std::free(out);
    CAMLreturn(result);
}
extern "C" value rosetta_@M@_ml_handle(value address) {
    CAMLparam1(address);
    CAMLlocal1(result);
    result = caml_alloc_custom_mem(&rosetta_ocaml_handle_ops, sizeof(void*), 64);
    *static_cast<void**>(Data_custom_val(result)) = reinterpret_cast<void*>(static_cast<std::uintptr_t>(Long_val(address)));
    CAMLreturn(result);
}
extern "C" value rosetta_@M@_ml_handle_ptr(value handle) {
    return Val_long(static_cast<intnat>(reinterpret_cast<std::uintptr_t>(*static_cast<void**>(Data_custom_val(handle)))));
}
extern "C" value rosetta_@M@_ml_handle_close(value handle) {
    CAMLparam1(handle);
    void* holder = *static_cast<void**>(Data_custom_val(handle));
    *static_cast<void**>(Data_custom_val(handle)) = nullptr;
    if (holder) @C@_release(holder, 1);
    CAMLreturn(Val_unit);
}
extern "C" value rosetta_@M@_ml_init(value) {
    return Val_int(@C@_init(1, rosetta_ocaml_invoke_hook, rosetta_ocaml_retain_hook));
}
// At exit: C++ objects destroyed later (static destructors) must not call into a stopped runtime.
extern "C" value rosetta_@M@_ml_shutdown(value) {
    @C@_init(1, nullptr, nullptr);
    return Val_unit;
}
)cpp";

} // namespace rosetta::detail
