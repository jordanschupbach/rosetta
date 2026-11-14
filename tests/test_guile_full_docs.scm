;;; Documentation of the full Guile API (--guile-api full): the generated module file reads back
;;; with (read), every definition carries its docstring where editors look for it (the string
;;; first in a define/define* body, as mep's Scheme server reads it), and the same text is what
;;; procedure-documentation returns at run time. Argument: the generated features.scm.
(use-modules (oop goops)
             (features)
             (ice-9 documentation)
             ((srfi srfi-1) #:select (every filter-map last))
             (srfi srfi-13))

(define checks 0)
(define (check ok what)
  (set! checks (+ checks 1))
  (unless ok (error "check failed:" what)))

(define (read-forms file)
  (call-with-input-file file
    (lambda (port)
      (let loop ((forms '()))
        (let ((form (read port)))
          (if (eof-object? form) (reverse forms) (loop (cons form forms))))))))

(define module-file (cadr (command-line)))
(define forms (read-forms module-file))
(check (pair? forms) "the module file reads back")
(check (equal? (list-head (car forms) 2) '(define-module (features))) "first form defines (features)")

;; (define (name . formals) "doc" body ...) and define*: name, formals and docstring.
(define (definition form)
  (and (pair? form) (memq (car form) '(define define*))
       (pair? (cdr form)) (pair? (cadr form))
       (symbol? (car (cadr form)))
       (let ((body (cddr form)))
         (and (pair? body) (string? (car body)) (pair? (cdr body))
              (list (car (cadr form)) (cdr (cadr form)) (car body))))))
(define documented (filter-map definition forms))
;; Editors use the first definition of a name.
(define first-docs
  (let loop ((items documented) (seen '()) (out '()))
    (cond ((null? items) (reverse out))
          ((memq (caar items) seen) (loop (cdr items) seen out))
          (else (loop (cdr items) (cons (caar items) seen) (cons (car items) out))))))
(check (> (length first-docs) 60) "most definitions carry a docstring")

(define interface (resolve-interface '(features)))
(define (exported name) (module-ref interface name))
(define public-docs
  (filter (lambda (item) (module-defined? interface (car item))) first-docs))
(check (> (length public-docs) 60) "documented definitions are exported")
;; The docstring in the source is what procedure-documentation returns.
(for-each
 (lambda (item)
   (let* ((name (car item)) (text (caddr item)) (value (exported name)))
     (check (procedure? value) (list 'procedure name))
     (check (equal? (procedure-documentation value) text) (list 'procedure-documentation name))
     (check (equal? (object-documentation value) text) (list 'object-documentation name))))
 public-docs)

;; Guile's docstring convention: one summary line, a blank line, then the details.
(for-each
 (lambda (item)
   (let* ((text (caddr item)) (lines (string-split text #\newline)))
     (check (not (string-null? (car lines))) (list 'summary (car item)))
     (check (or (null? (cdr lines)) (string-null? (cadr lines))) (list 'blank-line (car item)))))
 public-docs)

;; Every exported procedure (generic, accessor, enum helper) is documented.
(module-for-each
 (lambda (name variable)
   (let ((value (variable-ref variable)))
     (when (and (procedure? value) (not (is-a? value <class>)))
       (check (string? (procedure-documentation value)) (list 'documented name)))))
 interface)

(define (doc-of name) (caddr (assq name first-docs)))
(define (formals-of name) (cadr (assq name first-docs)))
;; Signatures as editors show them.
(check (equal? (formals-of 'add) '(. args)) "add is a generic over a procedure (rest formals)")
(check (equal? (formals-of 'defaults)
               '(#:optional (kind 'square) (label "x") (loud #f) (weight 0.5) (mark #\*) (lambda_ -2)))
       "literal defaults as #:optional formals")
(check (equal? (formals-of 'repeat) '(c count)) "plain formals")
(check (equal? (formals-of 'radius) '(self . args)) "method formals")
(check (string-contains (doc-of 'defaults) "WEIGHT: Real; default 0.5.") "default documented")
(check (string-contains (doc-of 'defaults) "MARK: Character; default #\\*.") "char default documented")
(check (string-contains (doc-of 'add) "Example:\n    int total = add(1);") "example block")
(check (string-contains (doc-of 'add) "Note: Defaults keep the result small.") "note")
(check (string-contains (doc-of 'add) "Warning: Signed overflow is undefined.") "warning")
(check (string-contains (doc-of 'add) "See also: pick") "see also")
(check (string-contains (doc-of 'make-circle) "Raises: features-invalid-argument (std::invalid_argument)") "constructor raises")
(check (string-contains (doc-of 'area) "(area SHAPE)") "method usage line")
(check (string-contains (doc-of 'x) "Set with (set-x! VEC2 VALUE) or (set! (x VEC2) VALUE).") "field setter documented")
(check (string-contains (doc-of 'kind-values) "(circle square custom)") "enum values documented")
(check (string-contains (doc-of 'apply) "call Guile's apply") "core fallback documented")
(check (string-contains (doc-of 'close!) "Destroy the C++ object") "runtime documented")

;; Classes: ;; comments above define-class and the documentation object property.
(check (string-contains (object-documentation <circle>) "Construct with:\n  (make <circle> RADIUS)") "class constructors")
(check (string-contains (object-documentation <circle>) "Base classes: <shape>.") "class bases")
(check (string-contains (object-documentation <vec2>) "Operators (core generics): + - * equal?.") "class operators")
(check (string-contains (object-documentation <visitor>) "define-method") "director classes")
(check (string-contains (object-documentation <native-object>) "close!") "runtime class")
(define class-forms
  (filter (lambda (form) (and (pair? form) (eq? (car form) 'goops:define-class))) forms))
(check (>= (length class-forms) 14) "classes are defined in the module file")

;; Submodules read back too.
(define sub-forms (read-forms (string-append (dirname module-file) "/features/data.scm")))
(check (equal? (list-head (car sub-forms) 2) '(define-module (features data))) "submodule file")
(check (every (lambda (item) (string? (caddr item))) (filter-map definition sub-forms)) "submodule docstrings")

(format #t "Guile full docs passed (~a checks)\n" checks)
