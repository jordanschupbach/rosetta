#pragma once

namespace rosetta::detail {
inline constexpr const char* python_runtime = R"cpp(
namespace { namespace rosetta_py {
struct python_error {};
[[noreturn]] inline void fail(PyObject* type, const char* message) {
    PyErr_SetString(type, message);
    throw python_error{};
}
template<class R, class F> R guard(F fn, R failure) noexcept {
    try { return fn(); }
    catch (const python_error&) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_SystemError, "missing Python exception");
    }
    catch (const std::bad_alloc&) { PyErr_NoMemory(); }
    catch (const std::overflow_error& e) { PyErr_SetString(PyExc_OverflowError, e.what()); }
    catch (const std::out_of_range& e) { PyErr_SetString(PyExc_IndexError, e.what()); }
    catch (const std::invalid_argument& e) { PyErr_SetString(PyExc_ValueError, e.what()); }
    catch (const std::exception& e) { PyErr_SetString(PyExc_RuntimeError, e.what()); }
    catch (...) { PyErr_SetString(PyExc_RuntimeError, "unknown C++ exception"); }
    return failure;
}
inline Py_ssize_t arity(PyObject* args, Py_ssize_t minimum, Py_ssize_t maximum) {
    const auto size = PyTuple_GET_SIZE(args);
    if (size < minimum || size > maximum) fail(PyExc_TypeError, "incorrect number of positional arguments");
    return size;
}
template<class T> T from(PyObject* value) {
    if constexpr (std::is_same_v<T, bool>) {
        if (!PyBool_Check(value)) fail(PyExc_TypeError, "expected bool");
        return value == Py_True;
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (!PyUnicode_Check(value)) fail(PyExc_TypeError, "expected str");
        Py_ssize_t size = 0;
        const char* text = PyUnicode_AsUTF8AndSize(value, &size);
        if (!text) throw python_error{};
        return std::string(text, static_cast<std::size_t>(size));
    } else if constexpr (std::is_integral_v<T>) {
        if (!PyLong_Check(value) || PyBool_Check(value)) fail(PyExc_TypeError, "expected int (bool is not accepted)");
        if constexpr (std::is_signed_v<T>) {
            const auto n = PyLong_AsLongLong(value);
            if (PyErr_Occurred()) throw python_error{};
            if (n < std::numeric_limits<T>::lowest() || n > std::numeric_limits<T>::max())
                fail(PyExc_OverflowError, "integer outside C++ type range");
            return static_cast<T>(n);
        } else {
            const auto n = PyLong_AsUnsignedLongLong(value);
            if (PyErr_Occurred()) throw python_error{};
            if (n > std::numeric_limits<T>::max()) fail(PyExc_OverflowError, "integer outside C++ type range");
            return static_cast<T>(n);
        }
    } else {
        if ((!PyFloat_Check(value) && !PyLong_Check(value)) || PyBool_Check(value)) fail(PyExc_TypeError, "expected float or int");
        // Read builtin storage directly: user conversion hooks could close the
        // receiver while an instance method still holds its native pointer.
        const double n = PyFloat_Check(value) ? PyFloat_AS_DOUBLE(value) : PyLong_AsDouble(value);
        if (PyErr_Occurred()) throw python_error{};
        if (!std::isfinite(n)) fail(PyExc_ValueError, "expected a finite number");
        if (n < std::numeric_limits<T>::lowest() || n > std::numeric_limits<T>::max())
            fail(PyExc_OverflowError, "number outside C++ type range");
        return static_cast<T>(n);
    }
}
template<class T> PyObject* to(const T& value) {
    if constexpr (std::is_same_v<T, bool>) return PyBool_FromLong(value);
    else if constexpr (std::is_same_v<T, std::string>) {
        if (value.size() > static_cast<std::size_t>(PY_SSIZE_T_MAX)) fail(PyExc_OverflowError, "string too long for Python");
        return PyUnicode_DecodeUTF8(value.data(), static_cast<Py_ssize_t>(value.size()), "strict");
    } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) return PyLong_FromLongLong(value);
    else if constexpr (std::is_integral_v<T>) return PyLong_FromUnsignedLongLong(value);
    else {
        if (!std::isfinite(value)) fail(PyExc_ValueError, "non-finite C++ result");
        return PyFloat_FromDouble(value);
    }
}
template<class T> struct Object { PyObject_HEAD T* ptr; bool initialized; };
template<class T> T* object(PyObject* self) {
    auto* result = reinterpret_cast<Object<T>*>(self)->ptr;
    if (!result) fail(PyExc_RuntimeError, "native object is closed or uninitialized");
    return result;
}
template<class T> PyObject* allocate(PyTypeObject* type, PyObject*, PyObject*) {
    auto* self = reinterpret_cast<Object<T>*>(type->tp_alloc(type, 0));
    if (self) { self->ptr = nullptr; self->initialized = false; }
    return reinterpret_cast<PyObject*>(self);
}
template<class T> void close(PyObject* self) noexcept {
    auto* wrapper = reinterpret_cast<Object<T>*>(self);
    auto* value = wrapper->ptr;
    wrapper->ptr = nullptr;
    delete value;
}
template<class T> void dealloc(PyObject* self) noexcept {
    PyObject_GC_UnTrack(self);
    close<T>(self);
    PyTypeObject* type = Py_TYPE(self);
    type->tp_free(self);
    Py_DECREF(type);
}
inline int traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    return 0;
}
inline PyObject* uncopyable(PyObject*, PyObject*) {
    PyErr_SetString(PyExc_TypeError, "native objects cannot be copied or pickled");
    return nullptr;
}
template<class T> PyObject* close_method(PyObject* self, PyObject* args) {
    return guard<PyObject*>([&]() -> PyObject* {
        arity(args, 0, 0); close<T>(self); Py_RETURN_NONE;
    }, nullptr);
}
template<class T> PyObject* enter(PyObject* self, PyObject* args) {
    return guard<PyObject*>([&]() -> PyObject* {
        arity(args, 0, 0); object<T>(self); return Py_NewRef(self);
    }, nullptr);
}
template<class T> PyObject* exit(PyObject* self, PyObject* args) {
    return guard<PyObject*>([&]() -> PyObject* {
        arity(args, 3, 3); close<T>(self); Py_RETURN_FALSE;
    }, nullptr);
}
} } // anonymous namespace, rosetta_py
)cpp";
} // namespace rosetta::detail
