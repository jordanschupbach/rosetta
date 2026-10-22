(* Loaded by `just ocaml-repl` after ocaml_demo.cma and ocaml_verify.cma. *)
open Ocaml_demo
open Ocaml_verify

let () =
  Printf.printf "Rosetta OCaml REPL: Ocaml_demo and Ocaml_verify are open; R_demo.add 20 22 = %d\n%!" (R_demo.add 20 22);
  print_endline "Try: let c = R_demo.Counter.create 20;;  R_demo.Counter.add c 22;;"
