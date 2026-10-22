;;; Run with: just guile-repl
(use-modules (guile_demo) (guile_verify))
(display "Rosetta Guile examples loaded: modules (guile_demo) and (guile_verify)\n")
(display "(define c (make-r_demo__Counter 20)) (r_demo__Counter-add c 22) (r_demo__Counter-close! c)\n")
