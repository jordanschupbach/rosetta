;;;; Run with: just lisp-repl
(let ((build (or (sb-ext:posix-getenv "ROSETTA_LISP_BUILD") (error "ROSETTA_LISP_BUILD is not set"))))
  (load (merge-pathnames "lisp_demo.lisp" (concatenate 'string build "/")))
  (load (merge-pathnames "lisp_verify.lisp" (concatenate 'string build "/"))))
(format t "Rosetta Common Lisp examples loaded: packages LISP_DEMO and LISP_VERIFY~%")
(format t "(lisp_demo:with-owner (c (lisp_demo:r_demo__counter 20)) (lisp_demo:r_demo__counter__add c 22))~%")
