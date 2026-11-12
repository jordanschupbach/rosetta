#pragma once

// Lisp side of the full Common Lisp backend: emitted into every generated interface, in the
// internal package (which uses COMMON-LISP). @PKG@ is the public package (the module name),
// @C@ the prefix of the native entry points, @LIB@ the shared library's base name and @ENV@
// the environment variable that may name the library file.
namespace rosetta::detail {
inline constexpr const char* lisp_full_runtime_lisp = R"lisp(
;;; ------------------------------------------------------------------ runtime
;;; Values cross to C++ in a binary encoding (one buffer per call); C++ resolves overloads.

(define-condition @PKG@:native-error (error)
  ((kind :initarg :kind :initform :native :reader @PKG@:native-error-kind)
   (message :initarg :message :initform "" :reader @PKG@:native-error-message))
  (:report (lambda (condition stream)
             (write-string (@PKG@:native-error-message condition) stream)))
  (:documentation "A C++ exception or a failed conversion in a call to native module @PKG@.
NATIVE-ERROR-KIND is :binding (conversion, overload or closed-object errors), :invalid-argument,
:out-of-range, :domain-error, :length-error, :arithmetic-error, :allocation or :native."))

(defun fail (control &rest arguments)
  (error '@PKG@:native-error :kind :binding :message (apply #'format nil control arguments)))

(defstruct (@PKG@:ref (:constructor @PKG@:ref (&optional value)) (:conc-name ref-) (:copier nil) (:predicate nil))
  "A mutable cell for C++ reference parameters: a native function that takes a non-const
reference reads REF-VALUE and stores the updated value back in it."
  value)
(declaim (inline @PKG@:ref-value (setf @PKG@:ref-value)))
(defun @PKG@:ref-value (ref)
  "The value of a REF cell."
  (ref-value ref))
(defun (setf @PKG@:ref-value) (value ref)
  (setf (ref-value ref) value))

(defclass @PKG@:native-object ()
  ((holder :initarg %holder :initform nil)
   (parent :initarg %parent :initform nil)
   (id :initform nil))
  (:documentation "Base class of objects backed by a native C++ object of module @PKG@.
CLOSE releases the native object now; otherwise garbage collection releases it."))

;;; Native library.
(defparameter *source-directory*
  (let ((file (or *compile-file-truename* *load-truename*)))
    (and file (make-pathname :name nil :type nil :version nil :defaults file))))
(defvar *library* nil)
(defun library-file ()
  (let ((name (concatenate 'string "@LIB@"
                           #+(or darwin macos) ".dylib" #+(or windows win32) ".dll"
                           #-(or darwin macos windows win32) ".so"))
        (variable (uiop:getenv "@ENV@")))
    (or (and variable (plusp (length variable)) variable)
        (let ((system (asdf:find-system "@PKG@" nil)))
          (and system (probe-file (asdf:system-relative-pathname system name))))
        (and *load-truename* (probe-file (merge-pathnames name *load-truename*)))
        (and *source-directory* (probe-file (merge-pathnames name *source-directory*)))
        name)))
(defun load-library ()
  (unless *library*
    (setf *library* (cffi:load-foreign-library (let ((file (library-file))) (if (pathnamep file) (namestring file) file))))))

(cffi:defcfun ("@C@_call" foreign-call) :int
  (index :uint32) (in :pointer) (size :size) (out :pointer) (out-size :pointer))
(cffi:defcfun ("@C@_free" foreign-free) :void (pointer :pointer))
(cffi:defcfun ("@C@_alloc" foreign-allocate) :pointer (size :size))
(cffi:defcfun ("@C@_release" foreign-release) :void (holder :pointer) (now :int))
(cffi:defcfun ("@C@_init" foreign-init) :int (version :int) (invoke :pointer) (retain :pointer))

;;; Lisp values C++ refers to: functions passed as callbacks, director objects, and objects
;;; native code keeps borrowed references into. Ids index these tables.
(defvar *next-id* 0)
(defvar *weak* (trivial-garbage:make-weak-hash-table :weakness :value #+sbcl :synchronized #+sbcl t))
(defvar *strong* (make-hash-table #+sbcl :synchronized #+sbcl t))
(defvar *lock* #+sbcl (sb-thread:make-mutex :name "@PKG@ ids") #-sbcl nil)
(defmacro with-lock (&body body)
  #+sbcl `(sb-thread:with-recursive-lock (*lock*) ,@body)
  #-sbcl `(progn ,@body))
(defun new-id () (with-lock (incf *next-id*)))
(defun object-id (object)
  (or (slot-value object 'id)
      (let ((id (new-id)))
        (setf (gethash id *weak*) object
              (slot-value object 'id) id))))
(defun lookup (id)
  (with-lock
    (let ((entry (gethash id *strong*)))
      (if entry (car entry)
          (multiple-value-bind (value found) (gethash id *weak*)
            (if found value (fail "the Lisp object of a native reference is gone")))))))
(defun adjust (id delta)
  (with-lock
    (let ((entry (gethash id *strong*)))
      (cond ((plusp delta)
             (if entry (incf (cdr entry) delta)
                 (multiple-value-bind (value found) (gethash id *weak*)
                   (when found (setf (gethash id *strong*) (cons value delta))))))
            (entry
             (when (<= (incf (cdr entry) delta) 0) (remhash id *strong*)))))))

;;; Encoding (Lisp -> C++): a growable foreign buffer.
(defstruct (out (:constructor make-out ()))
  (pointer (cffi:foreign-alloc :uint8 :count 512))
  (size 0 :type fixnum)
  (capacity 512 :type fixnum)
  (functions nil)
  (refs (make-array 0 :adjustable t :fill-pointer t)))
(defun reserve (out n)
  (declare (type out out) (type fixnum n))
  (let ((need (+ (out-size out) n)))
    (when (> need (out-capacity out))
      (let* ((capacity (max need (* 2 (out-capacity out))))
             (new (cffi:foreign-alloc :uint8 :count capacity)))
        (cffi:foreign-funcall "memcpy" :pointer new :pointer (out-pointer out) :size (out-size out) :pointer)
        (cffi:foreign-free (out-pointer out))
        (setf (out-pointer out) new (out-capacity out) capacity)))))
(defmacro define-put (name type size)
  `(defun ,name (out value)
     (declare (type out out))
     (reserve out ,size)
     (setf (cffi:mem-ref (out-pointer out) ,type (out-size out)) value)
     (incf (out-size out) ,size)))
(define-put put-u8 :uint8 1)
(define-put put-u32 :uint32 4)
(define-put put-u64 :uint64 8)
(define-put put-i64 :int64 8)
(define-put put-f64 :double 8)
(defun put-text (out string)
  (let ((n (babel:string-size-in-octets string :encoding :utf-8)))
    (put-u64 out n)
    (reserve out (1+ n))
    (cffi:lisp-string-to-foreign string (cffi:inc-pointer (out-pointer out) (out-size out)) (1+ n) :encoding :utf-8)
    (incf (out-size out) n)))
(defun proper-list-p (list)
  (loop for tail = list then (cdr tail)
        for count from 0
        while (consp tail)
        finally (return (and (null tail) count))))
(defun put-value (out value)
  (typecase value
    (null (put-u8 out 0))
    ((eql t) (put-u8 out 1))
    ((signed-byte 64) (put-u8 out 2) (put-i64 out value))
    (integer (put-u8 out 3) (put-text out (let ((*print-base* 10) (*print-radix* nil)) (princ-to-string value))))
    (double-float (put-u8 out 4) (put-f64 out value))
    (real (put-u8 out 4) (put-f64 out (coerce value 'double-float)))
    (complex (put-u8 out 18)
     (put-f64 out (coerce (realpart value) 'double-float))
     (put-f64 out (coerce (imagpart value) 'double-float)))
    (string (put-u8 out 5) (put-text out value))
    (character (put-u8 out 5) (put-text out (string value)))
    (symbol (put-u8 out 14) (put-text out (string-downcase (symbol-name value))))
    (@PKG@:native-object
     (put-u8 out 10)
     (put-u64 out (or (slot-value value 'holder) 0))
     (put-u64 out (if (slot-value value 'holder) (object-id value) 0)))
    (@PKG@:ref
     (put-u8 out 13)
     (put-u32 out (vector-push-extend value (out-refs out)))
     (put-value out (@PKG@:ref-value value)))
    (function
     (let ((id (new-id)))
       (with-lock (setf (gethash id *strong*) (cons value 1)))
       (push id (out-functions out))
       (put-u8 out 12)
       (put-u64 out id)))
    (hash-table
     (put-u8 out 9)
     (put-u64 out (hash-table-count value))
     (maphash (lambda (k v) (put-value out k) (put-value out v)) value))
    ((simple-array double-float (*))
     (put-u8 out 7)
     (put-u64 out (length value))
     (reserve out (* 8 (length value)))
     (loop for x across value
           do (setf (cffi:mem-ref (out-pointer out) :double (out-size out)) x)
              (incf (out-size out) 8)))
    ((vector (unsigned-byte 8))
     (put-u8 out 15)
     (put-u64 out (length value))
     (reserve out (length value))
     (loop for x across value
           do (setf (cffi:mem-ref (out-pointer out) :uint8 (out-size out)) x)
              (incf (out-size out))))
    (vector
     (put-u8 out 6)
     (put-u64 out (length value))
     (loop for x across value do (put-value out x)))
    (cons
     (let ((count (proper-list-p value)))
       (cond (count
              (put-u8 out 6)
              (put-u64 out count)
              (dolist (x value) (put-value out x)))
             ((atom (cdr value))
              (put-u8 out 8)
              (put-value out (car value))
              (put-value out (cdr value)))
             (t (fail "cannot pass the improper list ~S to C++" value)))))
    (t (fail "cannot pass ~S to C++" value))))

;;; Decoding (C++ -> Lisp).
(defstruct (in (:constructor make-in (pointer size)))
  pointer (size 0 :type fixnum) (at 0 :type fixnum))
(defmacro define-get (name type size)
  `(defun ,name (in)
     (declare (type in in))
     (when (> (+ (in-at in) ,size) (in-size in)) (fail "malformed native value"))
     (prog1 (cffi:mem-ref (in-pointer in) ,type (in-at in)) (incf (in-at in) ,size))))
(define-get get-u8 :uint8 1)
(define-get get-u32 :uint32 4)
(define-get get-u64 :uint64 8)
(define-get get-i64 :int64 8)
(define-get get-f64 :double 8)
(defun get-text (in)
  (let ((n (get-u64 in)))
    (when (> (+ (in-at in) n) (in-size in)) (fail "malformed native value"))
    (prog1 (if (zerop n) ""
               (handler-case
                   (cffi:foreign-string-to-lisp (in-pointer in) :offset (in-at in) :count n :encoding :utf-8)
                 (error () (fail "C++ string is not valid UTF-8"))))
      (incf (in-at in) n))))
(defvar *classes* (vector))
(defun finalize-holder (object address)
  (trivial-garbage:finalize object (lambda () (foreign-release (cffi:make-pointer address) 0)))
  object)
(defun wrap-object (address class parent)
  (let ((object (make-instance (svref *classes* class) '%holder address '%parent parent)))
    (finalize-holder object address)))
(defclass native-function (@PKG@:native-object) ()
  (:documentation "A native C++ std::function returned to Lisp (inside a closure)."))
(defun native-closure (address)
  (let ((box (make-instance 'native-function '%holder address)))
    (finalize-holder box address)
    (lambda (&rest arguments) (call 0 (cons box arguments)))))
(defun get-value (in arguments)
  (let ((tag (get-u8 in)))
    (case tag
      (0 nil)
      (1 t)
      (2 (get-i64 in))
      (3 (parse-integer (get-text in)))
      (4 (get-f64 in))
      (18 (let ((re (get-f64 in))) (complex re (get-f64 in))))
      (5 (get-text in))
      (14 (intern (string-upcase (get-text in)) :keyword))
      (15 (let* ((n (get-u64 in)) (v (make-array n :element-type '(unsigned-byte 8))))
            (dotimes (i n v) (setf (aref v i) (get-u8 in)))))
      (6 (let* ((n (get-u64 in)) (v (make-array n)))
           (dotimes (i n v) (setf (svref v i) (get-value in arguments)))))
      (16 (loop repeat (get-u64 in) collect (get-value in arguments)))
      (7 (let* ((n (get-u64 in)) (v (make-array n :element-type 'double-float)))
           (when (> (+ (in-at in) (* 8 n)) (in-size in)) (fail "malformed native value"))
           (dotimes (i n v)
             (setf (aref v i) (cffi:mem-ref (in-pointer in) :double (in-at in)))
             (incf (in-at in) 8))))
      (9 (let ((table (make-hash-table :test 'equal)))
           (loop repeat (get-u64 in)
                 do (let ((key (get-value in arguments)))
                      (setf (gethash key table) (get-value in arguments))))
           table))
      (8 (let ((head (get-value in arguments))) (cons head (get-value in arguments))))
      (10 (let* ((address (get-u64 in)) (class (get-u32 in)) (parent (get-i64 in)))
            (wrap-object address class (and (>= parent 0) (nth parent arguments)))))
      (11 (lookup (get-u64 in)))
      (12 (native-closure (get-u64 in)))
      (17 (nth (get-u32 in) arguments))
      (t (fail "malformed native value (tag ~D)" tag)))))

;;; Calls.
(defvar *callback-condition* nil
  "The condition a Lisp callback signalled, signalled again when the native call returns.")
(defun call (index arguments &optional into)
  (let ((out (make-out)))
    (unwind-protect
         (progn
           ;; Lisp and C++ buffer standard output separately: flush before C++ writes.
           (finish-output *standard-output*)
           (put-u32 out (length arguments))
           (dolist (argument arguments) (put-value out argument))
           (cffi:with-foreign-objects ((result :pointer) (result-size :size))
             (let* ((status (foreign-call index (out-pointer out) (out-size out) result result-size))
                    (pointer (cffi:mem-ref result :pointer)))
               (unwind-protect
                    (let ((in (make-in pointer (cffi:mem-ref result-size :size))))
                      (case status
                        (0 (let ((value (if (and into (= (cffi:mem-ref pointer :uint8) 10))
                                            (progn (get-u8 in)
                                                   (let ((address (get-u64 in)))
                                                     (get-u32 in) (get-i64 in)
                                                     (setf (slot-value into 'holder) address)
                                                     (finalize-holder into address)))
                                            (get-value in arguments))))
                             (loop repeat (get-u32 in)
                                   do (let ((slot (get-u32 in)))
                                        (setf (@PKG@:ref-value (aref (out-refs out) slot)) (get-value in arguments))))
                             value))
                        (2 (let ((condition *callback-condition*))
                             (setf *callback-condition* nil)
                             (get-value in nil)
                             (if condition (error condition)
                                 (error '@PKG@:native-error :kind :lisp :message (get-value in nil)))))
                        (1 (let ((kind (get-value in nil)))
                             (error '@PKG@:native-error :kind kind :message (get-value in nil))))
                        (t (error '@PKG@:native-error :kind :allocation :message "out of memory in a native call"))))
                 (unless (cffi:null-pointer-p pointer) (foreign-free pointer))))))
      (cffi:foreign-free (out-pointer out))
      (dolist (id (out-functions out)) (adjust id -1)))))

;;; Calls from C++: callbacks (kind 0) and overrides of virtual methods (kind 1).
(defvar *generics* (vector))
(defun respond (out pointer size)
  (let ((buffer (foreign-allocate (max 1 (out-size out)))))
    (cffi:foreign-funcall "memcpy" :pointer buffer :pointer (out-pointer out) :size (out-size out) :pointer)
    (setf (cffi:mem-ref pointer :pointer) buffer
          (cffi:mem-ref size :size) (out-size out))))
;; SBCL notes the pointer coercions of callback arguments when it compiles these; muffle them.
(locally #+sbcl (declare (sb-ext:muffle-conditions sb-ext:compiler-note))
(cffi:defcallback invoke :int ((kind :int) (id :uint64) (generic :uint32) (in :pointer) (size :size) (result :pointer) (result-size :pointer))
  #+sbcl (declare (sb-ext:muffle-conditions sb-ext:compiler-note))
  (let ((out (make-out)))
    (unwind-protect
         (handler-case
             (let* ((reader (make-in in size))
                    (arguments (loop repeat (get-u32 reader) collect (get-value reader nil)))
                    (value (if (zerop kind)
                               (apply (lookup id) arguments)
                               (apply (svref *generics* generic) arguments))))
               (put-value out value)
               (respond out result result-size)
               0)
           (serious-condition (condition)
             (setf *callback-condition* condition)
             (setf (out-size out) 0)
             (put-u8 out 5)
             (put-text out (handler-case (princ-to-string condition)
                             (error () "error in a Lisp callback")))
             (respond out result result-size)
             2))
      (finish-output *standard-output*)
      (cffi:foreign-free (out-pointer out)))))
(cffi:defcallback retain :void ((id :uint64) (delta :int))
  #+sbcl (declare (sb-ext:muffle-conditions sb-ext:compiler-note))
  (adjust id delta)))

;;; Objects.
(defvar *constructors* (make-hash-table :test 'eq)
  "Native class name -> (function index . whether Lisp subclasses override virtual methods).")
(defun construct (object arguments)
  (let* ((name (class-name (class-of object)))
         (native (if (gethash name *constructors*) name
                     (let ((found nil))
                       (loop for class across *classes*
                             when (and (typep object class) (gethash class *constructors*))
                               do (setf found class))
                       found)))
         (entry (and native (gethash native *constructors*))))
    (unless entry (fail "~(~A~) has no public constructor" (or native name)))
    (call (car entry)
          (cons (if (and (cdr entry) (not (eq native name))) (object-id object) 0) arguments)
          object)))
(defmethod initialize-instance :after ((object @PKG@:native-object) &key (args nil) &allow-other-keys)
  (unless (slot-value object 'holder) (construct object args)))
(defmethod close ((object @PKG@:native-object) &key abort)
  (declare (ignore abort))
  (let ((address (slot-value object 'holder)))
    (when address
      (setf (slot-value object 'holder) nil)
      (trivial-garbage:cancel-finalization object)
      (foreign-release (cffi:make-pointer address) 1)))
  t)
(defmethod print-object ((object @PKG@:native-object) stream)
  (print-unreadable-object (object stream :type t :identity t)
    (unless (slot-value object 'holder) (write-string "closed" stream))))
(defmacro @PKG@:with-object ((variable form) &body body)
  "Binds VARIABLE to the native object FORM returns and closes it when BODY exits."
  `(let ((,variable ,form))
     (unwind-protect (progn ,@body)
       (when ,variable (close ,variable)))))
(defmacro @PKG@:define-override (name ((self class) &rest parameters) &body body)
  "Defines method NAME for the Lisp subclass CLASS with an ordinary parameter list:
  (define-override area ((shape triangle)) (* 1/2 (base shape) (height shape)))
expands to (defmethod area ((shape triangle) &rest args) ...), the lambda list generic
functions of module @PKG@ have. CALL-NEXT-METHOD runs the native C++ implementation."
  (let ((rest (gensym "ARGUMENTS")))
    `(defmethod ,name ((,self ,class) &rest ,rest)
       (destructuring-bind ,parameters ,rest
         ,@body))))
(defun register-constructor (class index director)
  (setf (gethash class *constructors*) (cons index director)))
(defun open-module ()
  (load-library)
  (unless (zerop (foreign-init 1 (cffi:callback invoke) (cffi:callback retain)))
    (error "native module @PKG@ was generated by a different Rosetta version")))
)lisp";
} // namespace rosetta::detail
