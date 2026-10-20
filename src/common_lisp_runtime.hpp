#pragma once
#include "cabi_runtime.hpp"

namespace rosetta::detail {
inline constexpr auto common_lisp_runtime = cabi_runtime;

// Shared Lisp half of the C ABI. @PACKAGE@ is the public package as a symbol
// prefix (|NAME|), @LIBRARY@ the shared-library stem and @ABI@ the C prefix.
// It is read in the private NAME.IMPL package, which uses COMMON-LISP.
inline constexpr const char* common_lisp_interface_runtime = R"lisp(
(defvar *directory*
  (make-pathname :name nil :type nil :version nil :defaults (or *load-truename* *compile-file-truename*)))
(defvar *library*
  (cffi:load-foreign-library
   (merge-pathnames (make-pathname :name "@LIBRARY@" :type #+darwin "dylib" #+windows "dll" #-(or darwin windows) "so")
                    *directory*)))
(cffi:defcstruct %value
  (%tag :uint64) (%integer :int64) (%unsigned :uint64) (%real :double) (%data :pointer) (%size :size))
(cffi:defcstruct %result (%value (:struct %value)) (%error :uint64))
(cffi:defcfun ("@ABI@_abi" %abi) :uint64)
(cffi:defcfun ("@ABI@_free" %free) :void (bytes :pointer))
(cffi:defcfun ("@ABI@_shutdown" %shutdown) :void)
(cffi:defcfun ("@ABI@_call" %call) :void
  (operation :string) (id :uint64) (arguments :pointer) (count :size) (result :pointer))

(define-condition @PACKAGE@:rosetta-error (error)
  ((kind :initarg :kind :reader @PACKAGE@:rosetta-error-kind)
   (message :initarg :message :reader @PACKAGE@:rosetta-error-message))
  (:report (lambda (condition stream)
             (format stream "Rosetta ~(~A~): ~A" (@PACKAGE@:rosetta-error-kind condition)
                     (@PACKAGE@:rosetta-error-message condition)))))
(defun fail (kind message) (error '@PACKAGE@:rosetta-error :kind kind :message message))

;; SBCL traps floating-point exceptions in foreign code too; native code
;; reports non-finite values itself.
(defmacro with-native-floats (&body body)
  #+sbcl `(sb-int:with-float-traps-masked (:overflow :invalid :inexact :divide-by-zero :underflow) ,@body)
  #-sbcl `(progn ,@body))

(defun store (pointer tag &key (integer 0) (unsigned 0) (real 0d0) (data (cffi:null-pointer)) (size 0))
  (cffi:with-foreign-slots ((%tag %integer %unsigned %real %data %size) pointer (:struct %value))
    (setf %tag tag %integer integer %unsigned unsigned %real real %data data %size size)))
(defun octets (x)
  (handler-case (if (stringp x) (babel:string-to-octets x :encoding :utf-8) x)
    (error () (fail :conversion "string is not encodable as UTF-8"))))
;; Packs X into the foreign value at POINTER; returns a byte buffer to free, if any.
(defun pack (x pointer)
  (typecase x
    ((eql t) (store pointer 4 :unsigned 1) nil)
    (null (store pointer 4) nil)
    ((signed-byte 64) (store pointer 1 :integer x) nil)
    ((unsigned-byte 64) (store pointer 2 :unsigned x) nil)
    (integer (fail :conversion "integer outside C++ type range"))
    (double-float (store pointer 3 :real x) nil)
    (single-float (store pointer 6 :real (coerce x 'double-float)) nil)
    ((or string (vector (unsigned-byte 8)))
     (let* ((bytes (octets x))
            (size (length bytes))
            (data (cffi:foreign-alloc :uint8 :count (max size 1))))
       (dotimes (i size) (setf (cffi:mem-aref data :uint8 i) (aref bytes i)))
       (store pointer 5 :data data :size size)
       data))
    (t (fail :conversion "expected an integer, float, T/NIL or string"))))
(defun text (value)
  (cffi:with-foreign-slots ((%data %size) value (:struct %value))
    (let ((bytes (make-array %size :element-type '(unsigned-byte 8))))
      (dotimes (i %size) (setf (aref bytes i) (cffi:mem-aref %data :uint8 i)))
      (handler-case (babel:octets-to-string bytes :encoding :utf-8 :errorp t)
        (error () (fail :conversion "native string is not valid UTF-8"))))))
(defun decode (value)
  (cffi:with-foreign-slots ((%tag %integer %unsigned %real) value (:struct %value))
    (case %tag
      (0 nil)
      (1 %integer)
      (2 %unsigned)
      (3 %real)
      (4 (/= %unsigned 0))
      (5 (text value))
      (6 (coerce %real 'single-float))
      (t (fail :conversion "invalid native result tag")))))
(defun invoke (operation id arguments)
  (let* ((count (length arguments))
         (packed (cffi:foreign-alloc '(:struct %value) :count (max count 1)))
         (buffers '()))
    (unwind-protect
         (cffi:with-foreign-object (result '(:struct %result))
           (with-native-floats
             (loop for x in arguments
                   for i from 0
                   for buffer = (pack x (cffi:mem-aptr packed '(:struct %value) i))
                   when buffer do (push buffer buffers))
             (%call operation id packed count result))
           (let* ((value (cffi:foreign-slot-pointer result '(:struct %result) '%value))
                  (data (cffi:foreign-slot-value value '(:struct %value) '%data))
                  (code (cffi:foreign-slot-value result '(:struct %result) '%error)))
             (unwind-protect
                  (if (zerop code)
                      (decode value)
                      (fail (or (nth (1- code) '(:allocation :range :index :argument :native :conversion)) :native)
                            (if (cffi:null-pointer-p data) "native allocation failed" (text value))))
               (%free data))))
      (mapc #'cffi:foreign-free buffers)
      (cffi:foreign-free packed))))

;; Owners hold a registry ID, never a native address; 0 means closed.
(defclass @PACKAGE@:native-owner () ((id :initform 0)))
(defun owner-id (owner class)
  (if (typep owner class)
      (slot-value owner 'id)
      (fail :conversion (format nil "expected a ~A owner" (symbol-name class)))))
(defun close-id (id) (invoke "__close" id '()))
(defun adopt (class id)
  (let ((adopted nil))
    (unwind-protect
         (let ((owner (make-instance class)))
           (setf (slot-value owner 'id) id)
           (trivial-garbage:finalize owner (lambda () (ignore-errors (close-id id))))
           (setf adopted t)
           owner)
      (unless adopted (ignore-errors (close-id id))))))
(defmethod close ((owner @PACKAGE@:native-owner) &key abort)
  (declare (ignore abort))
  (let ((id (slot-value owner 'id)))
    (unless (zerop id)
      (setf (slot-value owner 'id) 0)
      (trivial-garbage:cancel-finalization owner)
      (close-id id)))
  t)
(defmethod print-object ((owner @PACKAGE@:native-owner) stream)
  (print-unreadable-object (owner stream :type t :identity t)
    (when (zerop (slot-value owner 'id)) (write-string "closed" stream))))
(defmethod make-load-form ((owner @PACKAGE@:native-owner) &optional environment)
  (declare (ignore environment))
  (fail :ownership "native owners cannot be serialized"))
(defmacro @PACKAGE@:with-owner ((variable form) &body body)
  `(let ((,variable ,form))
     (unwind-protect (progn ,@body) (close ,variable))))

(unless (= (%abi) 1) (error "Rosetta Common Lisp ABI mismatch for @LIBRARY@"))
;; Clearing the registry at exit cancels and joins remaining native workers.
(defun shutdown () (ignore-errors (%shutdown)))
#+sbcl (pushnew 'shutdown sb-ext:*exit-hooks*)
)lisp";
} // namespace rosetta::detail
