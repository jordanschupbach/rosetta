#pragma once

// Support code emitted into every generated CPython extension. It is split into
// several literals only to stay below compiler limits for one string literal.
// Everything is inline: a split extension repeats it in each source, and the
// per-module namespace ROSETTA_PY_MODULE keeps modules in one process apart.
namespace rosetta::detail {
inline constexpr const char* python_runtime_parts[] = {
R"cpp(
namespace ROSETTA_PY_MODULE { namespace rosetta_py {
struct python_error {};
[[noreturn]] inline void fail(PyObject* type, const char* message) {
    PyErr_SetString(type, message);
    throw python_error{};
}
[[noreturn]] inline void fail(PyObject* type, const std::string& message) { fail(type, message.c_str()); }

// Field setters: members whose type cannot be assigned (reference or const members)
// are exported read-only, so this branch only guards direct calls.
template <class T, class V> void assign_field(T& target, V&& value) {
    if constexpr (std::is_copy_assignable_v<T>) target = std::forward<V>(value);
    else fail(PyExc_AttributeError, "native field type cannot be assigned");
}
inline void check_error() { if (PyErr_Occurred()) throw python_error{}; }
inline PyObject* checked(PyObject* value) { if (!value) throw python_error{}; return value; }
template<class R, class F> R guard(F fn, R failure) noexcept {
    try { return fn(); }
    catch (const python_error&) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_SystemError, "missing Python exception");
    }
    catch (const std::bad_alloc&) { PyErr_NoMemory(); }
    catch (const std::overflow_error& e) { PyErr_SetString(PyExc_OverflowError, e.what()); }
    catch (const std::out_of_range& e) { PyErr_SetString(PyExc_IndexError, e.what()); }
    catch (const std::invalid_argument& e) { PyErr_SetString(PyExc_ValueError, e.what()); }
    catch (const std::domain_error& e) { PyErr_SetString(PyExc_ValueError, e.what()); }
    catch (const std::length_error& e) { PyErr_SetString(PyExc_ValueError, e.what()); }
    catch (const std::exception& e) { PyErr_SetString(PyExc_RuntimeError, e.what()); }
    catch (...) { PyErr_SetString(PyExc_RuntimeError, "unknown C++ exception"); }
    return failure;
}

// Owned strong reference.
class Ref {
public:
    Ref() = default;
    explicit Ref(PyObject* value) noexcept : value_(value) {}
    Ref(const Ref& other) noexcept : value_(Py_XNewRef(other.value_)) {}
    Ref(Ref&& other) noexcept : value_(other.value_) { other.value_ = nullptr; }
    Ref& operator=(Ref other) noexcept { std::swap(value_, other.value_); return *this; }
    ~Ref() { Py_XDECREF(value_); }
    PyObject* get() const noexcept { return value_; }
    PyObject* release() noexcept { auto* value = value_; value_ = nullptr; return value; }
    explicit operator bool() const noexcept { return value_ != nullptr; }
private:
    PyObject* value_ = nullptr;
};

// Acquire the GIL from any thread (no-op cost when already held).
struct Gil {
    PyGILState_STATE state = PyGILState_Ensure();
    Gil() = default;
    Gil(const Gil&) = delete;
    Gil& operator=(const Gil&) = delete;
    ~Gil() { PyGILState_Release(state); }
};

// A Python object kept alive by C++ (callbacks), safe to copy or destroy without the GIL held.
class SharedRef {
public:
    SharedRef() = default;
    explicit SharedRef(PyObject* value) : value_(Py_XNewRef(value)) {}
    SharedRef(const SharedRef& other) : value_(nullptr) { Gil gil; value_ = Py_XNewRef(other.value_); }
    SharedRef(SharedRef&& other) noexcept : value_(other.value_) { other.value_ = nullptr; }
    SharedRef& operator=(SharedRef other) noexcept { std::swap(value_, other.value_); return *this; }
    ~SharedRef() { if (value_ && Py_IsInitialized()) { Gil gil; Py_DECREF(value_); } }
    PyObject* get() const noexcept { return value_; }
private:
    PyObject* value_ = nullptr;
};

inline std::string type_name(PyObject* value) {
    PyTypeObject* type = Py_TYPE(value);
    return type->tp_name ? type->tp_name : "object";
}

// ---------------------------------------------------------------- classes
struct TypeInfo;
struct BaseLink { TypeInfo* base; void* (*upcast)(void*); };
struct TypeInfo {
    const char* name = nullptr;
    PyTypeObject* type = nullptr;
    std::vector<BaseLink> bases;
    void (*destroy)(void*) = nullptr;
    void* (*copy)(const void*) = nullptr;
};
template<class T> struct Info { static TypeInfo value; };
template<class T> TypeInfo Info<T>::value;
// Specialized by generated code for every exported class.
template<class T> struct Bound : std::false_type {};
template<class T> inline constexpr bool bound_v = Bound<std::remove_cv_t<T>>::value;
// std::is_copy_constructible is true for classes whose members cannot actually be copied
// (std::vector<std::unique_ptr<T>>); generated code specializes this for such classes.
template<class T> struct Copyable : std::bool_constant<std::is_copy_constructible_v<T> && !std::is_abstract_v<T>> {};
template<class T> inline constexpr bool copyable_v = Copyable<std::remove_cv_t<T>>::value;

inline std::map<std::type_index, TypeInfo*>& registry() { static std::map<std::type_index, TypeInfo*> value; return value; }
inline std::map<std::type_index, PyObject*>& enum_types() { static std::map<std::type_index, PyObject*> value; return value; }

// Every exported class derives from this layout through one shared root type.
struct Instance {
    PyObject_HEAD
    void* ptr;
    TypeInfo* info;
    std::shared_ptr<void>* holder;
    PyObject* keepalive;
    PyObject* weaklist;
    unsigned char owned;
    unsigned char director;
};
inline PyTypeObject*& root_type() { static PyTypeObject* value = nullptr; return value; }
inline bool is_instance_object(PyObject* value) {
    return root_type() && PyObject_TypeCheck(value, root_type());
}
inline Instance* instance(PyObject* value) { return reinterpret_cast<Instance*>(value); }

// Base of generated director classes: C++ subclasses forwarding virtual calls to Python.
// When C++ takes ownership of one, it keeps its Python implementation alive until deleted.
struct Director {
    PyObject* rosetta_self = nullptr;
    bool rosetta_owns_self = false;
    Director() = default;
    Director(const Director&) = delete;
    Director& operator=(const Director&) = delete;
    virtual ~Director() {
        if (rosetta_self && rosetta_owns_self && Py_IsInitialized()) {
            Gil gil;
            instance(rosetta_self)->ptr = nullptr;
            Py_DECREF(rosetta_self);
        }
    }
};

inline void* cast_path(void* ptr, const TypeInfo* from, const TypeInfo* to) {
    if (from == to) return ptr;
    for (const auto& link : from->bases)
        if (void* result = cast_path(link.upcast(ptr), link.base, to)) return result;
    return nullptr;
}
inline bool derives(const TypeInfo* from, const TypeInfo* to) {
    if (from == to) return true;
    for (const auto& link : from->bases) if (derives(link.base, to)) return true;
    return false;
}
template<class T> bool is_instance(PyObject* value) {
    if (!is_instance_object(value)) return false;
    auto* object = instance(value);
    return object->info && derives(object->info, &Info<std::remove_cv_t<T>>::value);
}
template<class T> T* native(PyObject* value) {
    using U = std::remove_cv_t<T>;
    auto* info = &Info<U>::value;
    if (!is_instance_object(value))
        fail(PyExc_TypeError, std::string("expected ") + (info->name ? info->name : "native object") + ", got " + type_name(value));
    auto* object = instance(value);
    if (!object->ptr) fail(PyExc_RuntimeError, "native object is closed, moved or uninitialized");
    void* result = cast_path(object->ptr, object->info, info);
    if (!result)
        fail(PyExc_TypeError, std::string("expected ") + (info->name ? info->name : "native object") + ", got " + type_name(value));
    return static_cast<U*>(result);
}
inline bool is_director(PyObject* value) { return is_instance_object(value) && instance(value)->director; }

inline void release(Instance* object) noexcept {
    void* ptr = object->ptr;
    auto* holder = object->holder;
    const bool owned = object->owned;
    TypeInfo* info = object->info;
    object->ptr = nullptr;
    object->holder = nullptr;
    object->owned = 0;
    if (holder) delete holder;
    else if (owned && ptr && info && info->destroy) info->destroy(ptr);
}

enum class Ownership { borrowed, owned };

template<class T> PyObject* wrap(T* value, Ownership ownership, PyObject* keepalive) {
    using U = std::remove_cv_t<T>;
    if (!value) Py_RETURN_NONE;
    TypeInfo* info = &Info<U>::value;
    void* ptr = const_cast<U*>(value);
    if constexpr (std::is_polymorphic_v<U>) {
        if (auto* director = dynamic_cast<const Director*>(value); director && director->rosetta_self) {
            if (ownership == Ownership::owned) {
                // C++ handed back ownership of a Python-implemented object.
                auto* object = instance(director->rosetta_self);
                if (object->ptr && !object->owned) object->owned = 1;
            }
            return Py_NewRef(director->rosetta_self);
        }
        const auto found = registry().find(std::type_index(typeid(*value)));
        if (found != registry().end() && found->second != info && found->second->type) {
            info = found->second;
            ptr = const_cast<void*>(dynamic_cast<const void*>(value));
        }
    }
    if (!info->type) {
        if (ownership == Ownership::owned && info->destroy) info->destroy(ptr);
        fail(PyExc_TypeError, "C++ result type is not exported to Python");
    }
    PyObject* result = info->type->tp_alloc(info->type, 0);
    if (!result) {
        if (ownership == Ownership::owned && info->destroy) info->destroy(ptr);
        throw python_error{};
    }
    auto* object = instance(result);
    object->ptr = ptr;
    object->info = info;
    object->owned = ownership == Ownership::owned;
    object->keepalive = Py_XNewRef(keepalive);
    return result;
}
template<class T> PyObject* wrap_shared(std::shared_ptr<T> value) {
    if (!value) Py_RETURN_NONE;
    if constexpr (std::is_polymorphic_v<std::remove_cv_t<T>>) {
        if (auto* director = dynamic_cast<const Director*>(value.get()); director && director->rosetta_self)
            return Py_NewRef(director->rosetta_self);
    }
    PyObject* result = wrap(value.get(), Ownership::borrowed, nullptr);
    auto* object = instance(result);
    object->holder = new std::shared_ptr<void>(std::const_pointer_cast<std::remove_cv_t<T>>(value), object->ptr);
    return result;
}
// Shares ownership with Python, converting a uniquely owned instance into a shared holder.
template<class T> std::shared_ptr<T> shared(PyObject* value) {
    if (value == Py_None) return {};
    T* ptr = native<T>(value);
    auto* object = instance(value);
    if (!object->holder) {
        if (!object->owned) {
            // Borrowed objects stay valid while their Python owner lives.
            auto keeper = std::make_shared<SharedRef>(value);
            return std::shared_ptr<T>(keeper, ptr);
        }
        auto* info = object->info;
        void* raw = object->ptr;
        object->holder = new std::shared_ptr<void>(raw, [info](void* p) { if (info->destroy) info->destroy(p); });
        object->owned = 0;
    }
    return std::shared_ptr<T>(*object->holder, ptr);
}
template<class T> std::unique_ptr<T> take(PyObject* value) {
    if (value == Py_None) return {};
    T* ptr = native<T>(value);
    auto* object = instance(value);
    if (object->holder || !object->owned) fail(PyExc_ValueError, "cannot transfer ownership of a shared or borrowed native object");
    if (object->info != &Info<std::remove_cv_t<T>>::value && !std::has_virtual_destructor_v<T>)
        fail(PyExc_TypeError, "cannot transfer a derived object without a virtual destructor");
    object->owned = 0;
    bool kept = false;
    if constexpr (std::is_polymorphic_v<T>) {
        if (auto* director = dynamic_cast<Director*>(ptr); director && object->director) {
            director->rosetta_owns_self = true; // the C++ owner now keeps the Python implementation alive
            Py_INCREF(value);
            kept = true;
        }
    }
    if (!kept) object->ptr = nullptr;
    return std::unique_ptr<T>(ptr);
}

// Python object lifecycle for the shared root type.
inline PyObject* allocate(PyTypeObject* type, PyObject*, PyObject*) {
    PyObject* self = type->tp_alloc(type, 0);
    if (self) {
        auto* object = instance(self);
        object->ptr = nullptr; object->info = nullptr; object->holder = nullptr;
        object->keepalive = nullptr; object->weaklist = nullptr; object->owned = 0; object->director = 0;
    }
    return self;
}
inline void dealloc(PyObject* self) noexcept {
    PyObject_GC_UnTrack(self);
    auto* object = instance(self);
    if (object->weaklist) PyObject_ClearWeakRefs(self);
    release(object);
    Py_CLEAR(object->keepalive);
    PyTypeObject* type = Py_TYPE(self);
    type->tp_free(self);
    Py_DECREF(type);
}
inline int traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(instance(self)->keepalive);
    Py_VISIT(Py_TYPE(self));
    return 0;
}
inline int clear(PyObject* self) {
    Py_CLEAR(instance(self)->keepalive);
    return 0;
}
inline int abstract_init(PyObject* self, PyObject*, PyObject*) {
    PyErr_Format(PyExc_TypeError, "%s cannot be constructed from Python", Py_TYPE(self)->tp_name);
    return -1;
}
inline PyObject* close_method(PyObject* self, PyObject*) {
    return guard<PyObject*>([&]() -> PyObject* {
        if (is_instance_object(self)) release(instance(self));
        Py_RETURN_NONE;
    }, nullptr);
}
inline PyObject* enter(PyObject* self, PyObject*) {
    return guard<PyObject*>([&]() -> PyObject* {
        if (!is_instance_object(self) || !instance(self)->ptr) fail(PyExc_RuntimeError, "native object is closed, moved or uninitialized");
        return Py_NewRef(self);
    }, nullptr);
}
inline PyObject* exit(PyObject* self, PyObject* args) {
    return guard<PyObject*>([&]() -> PyObject* {
        if (!PyTuple_Check(args) || PyTuple_GET_SIZE(args) != 3) fail(PyExc_TypeError, "__exit__ takes three arguments");
        if (is_instance_object(self)) release(instance(self));
        Py_RETURN_FALSE;
    }, nullptr);
}
inline PyObject* unpicklable(PyObject* self, PyObject*) {
    PyErr_Format(PyExc_TypeError, "cannot pickle native %s objects", Py_TYPE(self)->tp_name);
    return nullptr;
}
inline PyObject* copy_method(PyObject* self, PyObject*) {
    return guard<PyObject*>([&]() -> PyObject* {
        if (!is_instance_object(self) || !instance(self)->ptr) fail(PyExc_RuntimeError, "native object is closed, moved or uninitialized");
        auto* object = instance(self);
        if (!object->info->copy || object->director) fail(PyExc_TypeError, std::string(Py_TYPE(self)->tp_name) + " is not copyable");
        void* copy = object->info->copy(object->ptr);
        PyObject* result = object->info->type->tp_alloc(object->info->type, 0);
        if (!result) { object->info->destroy(copy); throw python_error{}; }
        auto* target = instance(result);
        target->ptr = copy; target->info = object->info; target->owned = 1;
        return result;
    }, nullptr);
}
inline PyObject* repr(PyObject* self) {
    auto* object = instance(self);
    if (!object->ptr) return PyUnicode_FromFormat("<%s (closed)>", Py_TYPE(self)->tp_name);
    return PyUnicode_FromFormat("<%s object at %p>", Py_TYPE(self)->tp_name, object->ptr);
}
inline PyMethodDef root_methods[] = {
    {"close", close_method, METH_NOARGS, "Release the native object now; later use raises RuntimeError."},
    {"__enter__", enter, METH_NOARGS, nullptr},
    {"__exit__", exit, METH_VARARGS, nullptr},
    {"__reduce__", unpicklable, METH_NOARGS, nullptr},
    {"__reduce_ex__", unpicklable, METH_VARARGS, nullptr},
    {"__copy__", copy_method, METH_NOARGS, nullptr},
    {"__deepcopy__", copy_method, METH_O, nullptr},
    {nullptr, nullptr, 0, nullptr}};
// SWIG's ownership flag: true while deleting the Python object deletes the C++ one.
inline PyObject* get_thisown(PyObject* self, void*) { return PyBool_FromLong(instance(self)->owned); }
inline int set_thisown(PyObject* self, PyObject* value, void*) {
    if (!value) { PyErr_SetString(PyExc_AttributeError, "cannot delete thisown"); return -1; }
    const int truth = PyObject_IsTrue(value);
    if (truth < 0) return -1;
    auto* object = instance(self);
    if (truth && object->holder) { PyErr_SetString(PyExc_ValueError, "a shared native object cannot be owned"); return -1; }
    object->owned = static_cast<unsigned char>(truth);
    return 0;
}
inline PyGetSetDef root_getset[] = {
    {"thisown", get_thisown, set_thisown, "Whether this Python object owns (and finally deletes) the native object.", nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr}};
inline PyMemberDef root_members[] = {
    {"__weaklistoffset__", Py_T_PYSSIZET, static_cast<Py_ssize_t>(offsetof(Instance, weaklist)), Py_READONLY, nullptr},
    {nullptr, 0, 0, 0, nullptr}};
inline PyType_Slot root_slots[] = {
    {Py_tp_new, reinterpret_cast<void*>(&allocate)},
    {Py_tp_dealloc, reinterpret_cast<void*>(&dealloc)},
    {Py_tp_traverse, reinterpret_cast<void*>(&traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(&clear)},
    {Py_tp_repr, reinterpret_cast<void*>(&repr)},
    {Py_tp_methods, root_methods},
    {Py_tp_members, root_members},
    {Py_tp_getset, root_getset},
    {Py_tp_doc, const_cast<char*>("Base class of native C++ objects.")},
    {0, nullptr}};
)cpp",
R"cpp(
// ---------------------------------------------------------------- values
template<class T, class = void> struct Value;   // by-value conversions

template<class T> struct is_std_function : std::false_type {};
template<class R, class... A> struct is_std_function<std::function<R(A...)>> : std::true_type {};

inline bool sequence_results_as_tuple = false;
// With the "vector" sequence policy: list and tuple subclasses with C++ method names
// (size(), push_back(), .first/.second) for sequence and pair results.
inline PyObject* vector_type = nullptr;
inline PyObject* pair_type = nullptr;
inline PyObject* as_vector(PyObject* list) {
    if (!vector_type) return list;
    Ref owned(list);
    return checked(PyObject_CallOneArg(vector_type, list));
}
inline bool is_sequence(PyObject* value) {
    return PyList_Check(value) || PyTuple_Check(value) ||
        (PySequence_Check(value) && !PyUnicode_Check(value) && !PyBytes_Check(value) && !PyByteArray_Check(value) &&
         !PyDict_Check(value) && !is_instance_object(value));
}

template<class T> struct Value<T, std::enable_if_t<std::is_same_v<T, bool>>> {
    static bool numpy_bool(PyObject* value) {
        const char* name = Py_TYPE(value)->tp_name;
        return name && (std::strcmp(name, "numpy.bool") == 0 || std::strcmp(name, "numpy.bool_") == 0);
    }
    static bool check(PyObject* value, bool strict) { return PyBool_Check(value) || (!strict && numpy_bool(value)); }
    static bool load(PyObject* value) {
        if (PyBool_Check(value)) return value == Py_True;
        if (!numpy_bool(value)) fail(PyExc_TypeError, "expected bool, got " + type_name(value));
        const int truth = PyObject_IsTrue(value);
        if (truth < 0) throw python_error{};
        return truth != 0;
    }
    static PyObject* cast(bool value) { return PyBool_FromLong(value); }
};
inline bool integer_like(PyObject* value) {
    if (PyBool_Check(value)) return false;
    if (PyLong_Check(value)) return true;
    // numpy integer scalars implement __index__ without being int subclasses.
    return !PyFloat_Check(value) && Py_TYPE(value)->tp_as_number && Py_TYPE(value)->tp_as_number->nb_index &&
        !is_instance_object(value);
}
// char is a one-character str, as in SWIG; signed/unsigned char stay integers.
template<> struct Value<char> {
    static bool check(PyObject* value, bool) { return PyUnicode_Check(value) && PyUnicode_GetLength(value) == 1; }
    static char load(PyObject* value) {
        if (!PyUnicode_Check(value)) fail(PyExc_TypeError, "expected a one-character str, got " + type_name(value));
        Py_ssize_t size = 0;
        const char* text = PyUnicode_AsUTF8AndSize(value, &size);
        if (!text) throw python_error{};
        if (size != 1) fail(PyExc_ValueError, "expected a single ASCII character");
        return text[0];
    }
    static PyObject* cast(char value) { return checked(PyUnicode_DecodeLatin1(&value, 1, nullptr)); }
};
template<class T> struct Value<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>>> {
    static bool check(PyObject* value, bool) { return integer_like(value); }
    static T load(PyObject* value) {
        if (!integer_like(value)) fail(PyExc_TypeError, "expected int, got " + type_name(value));
        Ref number(PyNumber_Index(value));
        if (!number) throw python_error{};
        if constexpr (std::is_signed_v<T>) {
            int overflow = 0;
            const auto n = PyLong_AsLongLongAndOverflow(number.get(), &overflow);
            if (overflow) fail(PyExc_OverflowError, "integer outside C++ type range");
            check_error();
            if (n < static_cast<long long>(std::numeric_limits<T>::lowest()) || n > static_cast<long long>(std::numeric_limits<T>::max()))
                fail(PyExc_OverflowError, "integer outside C++ type range");
            return static_cast<T>(n);
        } else {
            if (PyObject_RichCompareBool(number.get(), Ref(PyLong_FromLong(0)).get(), Py_LT) == 1)
                fail(PyExc_OverflowError, "negative integer for an unsigned C++ type");
            const auto n = PyLong_AsUnsignedLongLong(number.get());
            if (PyErr_Occurred()) {
                if (PyErr_ExceptionMatches(PyExc_OverflowError)) { PyErr_Clear(); fail(PyExc_OverflowError, "integer outside C++ type range"); }
                throw python_error{};
            }
            if (n > static_cast<unsigned long long>(std::numeric_limits<T>::max())) fail(PyExc_OverflowError, "integer outside C++ type range");
            return static_cast<T>(n);
        }
    }
    static PyObject* cast(T value) {
        if constexpr (std::is_signed_v<T>) return PyLong_FromLongLong(value);
        else return PyLong_FromUnsignedLongLong(value);
    }
};
template<class T> struct Value<T, std::enable_if_t<std::is_floating_point_v<T>>> {
    static bool check(PyObject* value, bool strict) {
        if (PyBool_Check(value) || is_instance_object(value)) return false;
        if (PyFloat_Check(value)) return true;
        if (strict) return false;
        return PyLong_Check(value) || (Py_TYPE(value)->tp_as_number && Py_TYPE(value)->tp_as_number->nb_float);
    }
    static T load(PyObject* value) {
        if (PyBool_Check(value)) fail(PyExc_TypeError, "expected float, got bool");
        // Read builtin storage directly: user conversion hooks could close the
        // receiver while an instance method still holds its native pointer.
        double n;
        if (PyFloat_Check(value)) n = PyFloat_AS_DOUBLE(value);
        else if (PyLong_Check(value)) n = PyLong_AsDouble(value);
        else if (Py_TYPE(value)->tp_as_number && Py_TYPE(value)->tp_as_number->nb_float) n = PyFloat_AsDouble(value);
        else fail(PyExc_TypeError, "expected float, got " + type_name(value));
        check_error();
        if constexpr (!std::is_same_v<T, double> && !std::is_same_v<T, long double>) {
            if (std::isfinite(n) && (n < std::numeric_limits<T>::lowest() || n > std::numeric_limits<T>::max()))
                fail(PyExc_OverflowError, "number outside C++ type range");
        }
        return static_cast<T>(n);
    }
    static PyObject* cast(T value) { return PyFloat_FromDouble(static_cast<double>(value)); }
};
template<> struct Value<std::string> {
    static bool check(PyObject* value, bool) { return PyUnicode_Check(value); }
    static std::string load(PyObject* value) {
        if (!PyUnicode_Check(value)) fail(PyExc_TypeError, "expected str, got " + type_name(value));
        Py_ssize_t size = 0;
        const char* text = PyUnicode_AsUTF8AndSize(value, &size);
        if (!text) throw python_error{};
        return std::string(text, static_cast<std::size_t>(size));
    }
    static PyObject* cast(const std::string& value) {
        if (value.size() > static_cast<std::size_t>(PY_SSIZE_T_MAX)) fail(PyExc_OverflowError, "string too long for Python");
        PyObject* result = PyUnicode_DecodeUTF8(value.data(), static_cast<Py_ssize_t>(value.size()), "surrogateescape");
        return checked(result);
    }
};
template<> struct Value<std::string_view> {
    static bool check(PyObject* value, bool strict) { return Value<std::string>::check(value, strict); }
    static PyObject* cast(std::string_view value) { return Value<std::string>::cast(std::string(value)); }
};
template<> struct Value<const char*> {
    static bool check(PyObject* value, bool strict) { return value == Py_None || Value<std::string>::check(value, strict); }
    static PyObject* cast(const char* value) { if (!value) Py_RETURN_NONE; return Value<std::string>::cast(std::string(value)); }
};
template<> struct Value<char*> {
    static PyObject* cast(const char* value) { return Value<const char*>::cast(value); }
};
template<> struct Value<std::filesystem::path> {
    static bool check(PyObject* value, bool) { return PyUnicode_Check(value) || PyObject_HasAttrString(value, "__fspath__"); }
    static std::filesystem::path load(PyObject* value) {
        Ref path(PyOS_FSPath(value));
        if (!path) throw python_error{};
        if (PyBytes_Check(path.get())) return std::filesystem::path(Value<std::string>::load(path.get()));
        return std::filesystem::path(Value<std::string>::load(path.get()));
    }
    static PyObject* cast(const std::filesystem::path& value) { return Value<std::string>::cast(value.string()); }
};
template<> struct Value<std::nullptr_t> {
    static bool check(PyObject* value, bool) { return value == Py_None; }
    static std::nullptr_t load(PyObject*) { return nullptr; }
    static PyObject* cast(std::nullptr_t) { Py_RETURN_NONE; }
};
template<> struct Value<std::monostate> {
    static bool check(PyObject* value, bool) { return value == Py_None; }
    static std::monostate load(PyObject*) { return {}; }
    static PyObject* cast(std::monostate) { Py_RETURN_NONE; }
};
template<class T> struct Value<std::complex<T>> {
    static bool check(PyObject* value, bool strict) { return PyComplex_Check(value) || Value<T>::check(value, strict); }
    static std::complex<T> load(PyObject* value) {
        if (PyComplex_Check(value)) return {static_cast<T>(PyComplex_RealAsDouble(value)), static_cast<T>(PyComplex_ImagAsDouble(value))};
        return {Value<T>::load(value), T{}};
    }
    static PyObject* cast(const std::complex<T>& value) { return PyComplex_FromDoubles(static_cast<double>(value.real()), static_cast<double>(value.imag())); }
};

// Enumerations convert through the generated IntEnum (or IntFlag) type.
template<class T> struct Value<T, std::enable_if_t<std::is_enum_v<T>>> {
    using U = std::underlying_type_t<T>;
    static PyObject* type() {
        const auto found = enum_types().find(std::type_index(typeid(T)));
        return found == enum_types().end() ? nullptr : found->second;
    }
    static bool check(PyObject* value, bool strict) {
        PyObject* enum_type = type();
        if (enum_type) {
            const int is = PyObject_IsInstance(value, enum_type);
            if (is < 0) { PyErr_Clear(); return false; }
            if (is) return true;
        }
        return !strict && integer_like(value);
    }
    static T load(PyObject* value) {
        if (!integer_like(value)) fail(PyExc_TypeError, "expected enum or int, got " + type_name(value));
        if constexpr (std::is_same_v<U, bool>) return static_cast<T>(Value<int>::load(value) != 0);
        else return static_cast<T>(Value<U>::load(value));
    }
    static PyObject* cast(T value) {
        Ref number(Value<std::conditional_t<std::is_same_v<U, bool>, int, U>>::cast(static_cast<std::conditional_t<std::is_same_v<U, bool>, int, U>>(value)));
        PyObject* enum_type = type();
        if (!enum_type || !number) return number.release();
        PyObject* member = PyObject_CallOneArg(enum_type, number.get());
        if (!member) { PyErr_Clear(); return number.release(); }
        return member;
    }
};

// Exported classes by value: Python receives an owned copy.
template<class T> struct Value<T, std::enable_if_t<bound_v<T>>> {
    static bool check(PyObject* value, bool) { return is_instance<T>(value); }
    static T load(PyObject* value) { return *native<T>(value); }
    static PyObject* cast(const T& value) {
        if constexpr (copyable_v<T>) return wrap(new T(value), Ownership::owned, nullptr);
        else { fail(PyExc_TypeError, "C++ result is not copyable"); }
    }
    static PyObject* cast(T&& value) {
        if constexpr (std::is_move_constructible_v<T>) return wrap(new T(std::move(value)), Ownership::owned, nullptr);
        else return cast(static_cast<const T&>(value));
    }
};
template<class T> struct Value<T*, std::enable_if_t<bound_v<T>>> {
    static bool check(PyObject* value, bool) { return value == Py_None || is_instance<T>(value); }
    static T* load(PyObject* value) { return value == Py_None ? nullptr : native<T>(value); }
    static PyObject* cast(T* value) { return wrap(value, Ownership::borrowed, nullptr); }
};
template<class T> struct Value<std::shared_ptr<T>, std::enable_if_t<bound_v<T>>> {
    static bool check(PyObject* value, bool) { return value == Py_None || is_instance<T>(value); }
    static std::shared_ptr<T> load(PyObject* value) { return shared<T>(value); }
    static PyObject* cast(const std::shared_ptr<T>& value) { return wrap_shared(value); }
};
template<class T, class D> struct Value<std::unique_ptr<T, D>, std::enable_if_t<bound_v<T>>> {
    static bool check(PyObject* value, bool) { return value == Py_None || is_instance<T>(value); }
    static std::unique_ptr<T, D> load(PyObject* value) {
        if constexpr (std::is_same_v<D, std::default_delete<T>>) return take<T>(value);
        else { fail(PyExc_TypeError, "custom deleters cannot receive Python objects"); }
    }
    static PyObject* cast(std::unique_ptr<T, D>&& value) {
        if constexpr (std::is_same_v<D, std::default_delete<T>>) return wrap(value.release(), Ownership::owned, nullptr);
        else { fail(PyExc_TypeError, "custom deleters cannot be returned to Python"); }
    }
};
template<class T> struct Value<std::optional<T>> {
    static bool check(PyObject* value, bool strict) { return value == Py_None || Value<T>::check(value, strict); }
    static std::optional<T> load(PyObject* value) {
        if (value == Py_None) return std::nullopt;
        return Value<T>::load(value);
    }
    static PyObject* cast(const std::optional<T>& value) {
        if (!value) Py_RETURN_NONE;
        return Value<T>::cast(*value);
    }
};
template<class T> struct Value<std::reference_wrapper<T>> {
    static PyObject* cast(std::reference_wrapper<T> value) {
        if constexpr (bound_v<T>) return wrap(&value.get(), Ownership::borrowed, nullptr);
        else return Value<std::remove_cv_t<T>>::cast(value.get());
    }
};
)cpp",
R"cpp(
// Sequences: lists in, lists (or tuples) out.
template<class C, class E> struct Sequence {
    static bool check(PyObject* value, bool strict) {
        if (!is_sequence(value)) return false;
        if (!PyList_Check(value) && !PyTuple_Check(value)) return !strict;
        const Py_ssize_t size = PySequence_Fast_GET_SIZE(value);
        PyObject** items = PySequence_Fast_ITEMS(value);
        for (Py_ssize_t i = 0; i < size; ++i) if (!Value<E>::check(items[i], strict)) return false;
        return true;
    }
    static C load(PyObject* value) {
        if (!is_sequence(value)) fail(PyExc_TypeError, "expected a sequence, got " + type_name(value));
        Ref fast(PySequence_Fast(value, "expected a sequence"));
        if (!fast) throw python_error{};
        const Py_ssize_t size = PySequence_Fast_GET_SIZE(fast.get());
        C result{};
        if constexpr (std::is_same_v<C, std::vector<E>>) result.reserve(static_cast<std::size_t>(size));
        for (Py_ssize_t i = 0; i < size; ++i) {
            PyObject* item = PySequence_Fast_GET_ITEM(fast.get(), i);
            if constexpr (std::is_same_v<C, std::set<E>> || std::is_same_v<C, std::unordered_set<E>>) result.insert(Value<E>::load(item));
            else result.push_back(Value<E>::load(item));
        }
        return result;
    }
    template<class Container> static PyObject* cast(Container&& value) {
        const auto size = static_cast<Py_ssize_t>(value.size());
        Ref result(sequence_results_as_tuple ? PyTuple_New(size) : PyList_New(size));
        if (!result) throw python_error{};
        Py_ssize_t i = 0;
        for (auto&& item : value) {
            PyObject* converted;
            if constexpr (std::is_rvalue_reference_v<Container&&> && !std::is_same_v<E, bool>) converted = Value<E>::cast(std::move(item));
            else converted = Value<E>::cast(item);
            if (!converted) throw python_error{};
            if (sequence_results_as_tuple) PyTuple_SET_ITEM(result.get(), i++, converted);
            else PyList_SET_ITEM(result.get(), i++, converted);
        }
        return sequence_results_as_tuple ? result.release() : as_vector(result.release());
    }
};
template<class E, class A> struct Value<std::vector<E, A>> : Sequence<std::vector<E, A>, E> {};
// Byte vectors also accept bytes and bytearray.
template<class A> struct Value<std::vector<unsigned char, A>> : Sequence<std::vector<unsigned char, A>, unsigned char> {
    using Base = Sequence<std::vector<unsigned char, A>, unsigned char>;
    static bool check(PyObject* value, bool strict) {
        return PyBytes_Check(value) || PyByteArray_Check(value) || Base::check(value, strict);
    }
    static std::vector<unsigned char, A> load(PyObject* value) {
        if (PyBytes_Check(value) || PyByteArray_Check(value)) {
            const auto* data = reinterpret_cast<const unsigned char*>(
                PyBytes_Check(value) ? PyBytes_AS_STRING(value) : PyByteArray_AS_STRING(value));
            const auto size = static_cast<std::size_t>(PyBytes_Check(value) ? PyBytes_GET_SIZE(value) : PyByteArray_GET_SIZE(value));
            return std::vector<unsigned char, A>(data, data + size);
        }
        return Base::load(value);
    }
};
template<class E, class A> struct Value<std::deque<E, A>> : Sequence<std::deque<E, A>, E> {};
template<class E, class A> struct Value<std::list<E, A>> : Sequence<std::list<E, A>, E> {};
template<class E> struct Value<std::set<E>> : Sequence<std::set<E>, E> {
    static PyObject* cast(const std::set<E>& value) {
        Ref result(PySet_New(nullptr));
        if (!result) throw python_error{};
        for (const auto& item : value) { Ref converted(Value<E>::cast(item)); if (!converted || PySet_Add(result.get(), converted.get()) < 0) throw python_error{}; }
        return result.release();
    }
    static bool check(PyObject* value, bool strict) { return PyAnySet_Check(value) || Sequence<std::set<E>, E>::check(value, strict); }
    static std::set<E> load(PyObject* value) {
        Ref iterator(PyObject_GetIter(value));
        if (!iterator) throw python_error{};
        std::set<E> result;
        while (Ref item{PyIter_Next(iterator.get())}) result.insert(Value<E>::load(item.get()));
        check_error();
        return result;
    }
};
template<class E> struct Value<std::unordered_set<E>> {
    static bool check(PyObject* value, bool strict) { return Value<std::set<E>>::check(value, strict); }
    static std::unordered_set<E> load(PyObject* value) { auto s = Value<std::set<E>>::load(value); return {s.begin(), s.end()}; }
    static PyObject* cast(const std::unordered_set<E>& value) { return Value<std::set<E>>::cast(std::set<E>(value.begin(), value.end())); }
};
template<class E> struct Value<std::valarray<E>> {
    static bool check(PyObject* value, bool strict) { return Value<std::vector<E>>::check(value, strict); }
    static std::valarray<E> load(PyObject* value) { auto v = Value<std::vector<E>>::load(value); return std::valarray<E>(v.data(), v.size()); }
    static PyObject* cast(const std::valarray<E>& value) { return Value<std::vector<E>>::cast(std::vector<E>(std::begin(value), std::end(value))); }
};
template<class E, std::size_t N> struct Value<std::array<E, N>> {
    static bool check(PyObject* value, bool strict) {
        if (!is_sequence(value)) return false;
        const Py_ssize_t size = PySequence_Size(value);
        if (size < 0) { PyErr_Clear(); return false; }
        if (static_cast<std::size_t>(size) != N) return false;
        if (!PyList_Check(value) && !PyTuple_Check(value)) return !strict;
        for (Py_ssize_t i = 0; i < size; ++i) if (!Value<E>::check(PySequence_Fast_ITEMS(value)[i], strict)) return false;
        return true;
    }
    static std::array<E, N> load(PyObject* value) {
        Ref fast(PySequence_Fast(value, "expected a sequence"));
        if (!fast) throw python_error{};
        if (static_cast<std::size_t>(PySequence_Fast_GET_SIZE(fast.get())) != N)
            fail(PyExc_ValueError, "expected a sequence of length " + std::to_string(N));
        std::array<E, N> result{};
        for (std::size_t i = 0; i < N; ++i) result[i] = Value<E>::load(PySequence_Fast_GET_ITEM(fast.get(), static_cast<Py_ssize_t>(i)));
        return result;
    }
    static PyObject* cast(const std::array<E, N>& value) {
        Ref result(sequence_results_as_tuple ? PyTuple_New(N) : PyList_New(N));
        if (!result) throw python_error{};
        for (std::size_t i = 0; i < N; ++i) {
            PyObject* item = Value<E>::cast(value[i]);
            if (!item) throw python_error{};
            if (sequence_results_as_tuple) PyTuple_SET_ITEM(result.get(), static_cast<Py_ssize_t>(i), item);
            else PyList_SET_ITEM(result.get(), static_cast<Py_ssize_t>(i), item);
        }
        return sequence_results_as_tuple ? result.release() : as_vector(result.release());
    }
};
template<class M, class K, class V> struct Mapping {
    static bool check(PyObject* value, bool strict) {
        if (!PyDict_Check(value)) return !strict && PyMapping_Check(value) && !is_sequence(value) && !PyUnicode_Check(value);
        PyObject *key, *item; Py_ssize_t position = 0;
        while (PyDict_Next(value, &position, &key, &item))
            if (!Value<K>::check(key, strict) || !Value<V>::check(item, strict)) return false;
        return true;
    }
    static M load(PyObject* value) {
        M result;
        if (PyDict_Check(value)) {
            PyObject *key, *item; Py_ssize_t position = 0;
            while (PyDict_Next(value, &position, &key, &item)) result.emplace(Value<K>::load(key), Value<V>::load(item));
            return result;
        }
        Ref items(PyMapping_Items(value));
        if (!items) throw python_error{};
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(items.get()); ++i) {
            PyObject* pair = PyList_GET_ITEM(items.get(), i);
            result.emplace(Value<K>::load(PyTuple_GET_ITEM(pair, 0)), Value<V>::load(PyTuple_GET_ITEM(pair, 1)));
        }
        return result;
    }
    static PyObject* cast(const M& value) {
        Ref result(PyDict_New());
        if (!result) throw python_error{};
        for (const auto& [key, item] : value) {
            Ref k(Value<K>::cast(key)), v(Value<V>::cast(item));
            if (!k || !v || PyDict_SetItem(result.get(), k.get(), v.get()) < 0) throw python_error{};
        }
        return result.release();
    }
};
template<class K, class V, class C, class A> struct Value<std::map<K, V, C, A>> : Mapping<std::map<K, V, C, A>, K, V> {};
template<class K, class V, class H, class E, class A> struct Value<std::unordered_map<K, V, H, E, A>> : Mapping<std::unordered_map<K, V, H, E, A>, K, V> {};
template<class A, class B> struct Value<std::pair<A, B>> {
    static bool check(PyObject* value, bool strict) {
        if (!PyTuple_Check(value) && !PyList_Check(value)) return !strict && is_sequence(value) && PySequence_Size(value) == 2;
        return PySequence_Fast_GET_SIZE(value) == 2 && Value<A>::check(PySequence_Fast_ITEMS(value)[0], strict) &&
            Value<B>::check(PySequence_Fast_ITEMS(value)[1], strict);
    }
    static std::pair<A, B> load(PyObject* value) {
        Ref fast(PySequence_Fast(value, "expected a pair"));
        if (!fast) throw python_error{};
        if (PySequence_Fast_GET_SIZE(fast.get()) != 2) fail(PyExc_ValueError, "expected a sequence of length 2");
        return {Value<A>::load(PySequence_Fast_GET_ITEM(fast.get(), 0)), Value<B>::load(PySequence_Fast_GET_ITEM(fast.get(), 1))};
    }
    static PyObject* cast(const std::pair<A, B>& value) {
        Ref first(Value<A>::cast(value.first));
        if (!first) throw python_error{};
        Ref second(Value<B>::cast(value.second));
        if (!second) throw python_error{};
        PyObject* pair = checked(PyTuple_Pack(2, first.get(), second.get()));
        if (!pair_type) return pair;
        Ref owned(pair);
        return checked(PyObject_CallOneArg(pair_type, pair));
    }
};
template<class... T> struct Value<std::tuple<T...>> {
    template<std::size_t... I> static bool check_items(PyObject* value, bool strict, std::index_sequence<I...>) {
        return (Value<T>::check(PySequence_Fast_ITEMS(value)[I], strict) && ...);
    }
    static bool check(PyObject* value, bool strict) {
        if (!PyTuple_Check(value) && !PyList_Check(value)) return !strict && is_sequence(value) && PySequence_Size(value) == sizeof...(T);
        return PySequence_Fast_GET_SIZE(value) == static_cast<Py_ssize_t>(sizeof...(T)) && check_items(value, strict, std::index_sequence_for<T...>{});
    }
    template<std::size_t... I> static std::tuple<T...> load_items(PyObject* fast, std::index_sequence<I...>) {
        return std::tuple<T...>{Value<T>::load(PySequence_Fast_GET_ITEM(fast, I))...};
    }
    static std::tuple<T...> load(PyObject* value) {
        Ref fast(PySequence_Fast(value, "expected a tuple"));
        if (!fast) throw python_error{};
        if (PySequence_Fast_GET_SIZE(fast.get()) != static_cast<Py_ssize_t>(sizeof...(T))) fail(PyExc_ValueError, "wrong tuple length");
        return load_items(fast.get(), std::index_sequence_for<T...>{});
    }
    template<std::size_t... I> static PyObject* cast_items(const std::tuple<T...>& value, std::index_sequence<I...>) {
        Ref result(PyTuple_New(sizeof...(T)));
        if (!result) throw python_error{};
        ((PyTuple_SET_ITEM(result.get(), I, checked(Value<T>::cast(std::get<I>(value))))), ...);
        return result.release();
    }
    static PyObject* cast(const std::tuple<T...>& value) { return cast_items(value, std::index_sequence_for<T...>{}); }
};
template<class... T> struct Value<std::variant<T...>> {
    static bool check(PyObject* value, bool strict) { return (Value<T>::check(value, true) || ...) || (!strict && (Value<T>::check(value, false) || ...)); }
    template<class First, class... Rest> static std::variant<T...> load_first(PyObject* value, bool strict) {
        if (Value<First>::check(value, strict)) return std::variant<T...>(std::in_place_type<First>, Value<First>::load(value));
        if constexpr (sizeof...(Rest) > 0) return load_first<Rest...>(value, strict);
        else fail(PyExc_TypeError, "no variant alternative accepts " + type_name(value));
    }
    static std::variant<T...> load(PyObject* value) {
        if ((Value<T>::check(value, true) || ...)) return load_first<T...>(value, true);
        return load_first<T...>(value, false);
    }
    static PyObject* cast(const std::variant<T...>& value) {
        return std::visit([](const auto& item) -> PyObject* { return Value<std::decay_t<decltype(item)>>::cast(item); }, value);
    }
};
template<class Rep, class Period> struct Value<std::chrono::duration<Rep, Period>> {
    using D = std::chrono::duration<Rep, Period>;
    static bool check(PyObject* value, bool strict) { return Value<double>::check(value, strict); }
    static D load(PyObject* value) {
        return std::chrono::duration_cast<D>(std::chrono::duration<double>(Value<double>::load(value)));
    }
    static PyObject* cast(const D& value) { return PyFloat_FromDouble(std::chrono::duration<double>(value).count()); }
};
)cpp",
R"cpp(
// ---------------------------------------------------------------- arguments
// Converts a Python argument for a declared C++ parameter type P and keeps any temporary alive.
template<class P, class = void> struct Param {
    using V = std::remove_cv_t<std::remove_reference_t<P>>;
    static bool check(PyObject* value, bool strict) { return Value<V>::check(value, strict); }
    explicit Param(PyObject* value) : value_(Value<V>::load(value)) {}
    decltype(auto) get() {
        if constexpr (std::is_lvalue_reference_v<P>) return static_cast<P>(value_);
        else return static_cast<V&&>(value_);
    }
    // Mutable references to converted values update the caller's list or dict in place.
    void writeback(PyObject* original) {
        if constexpr (std::is_lvalue_reference_v<P> && !std::is_const_v<std::remove_reference_t<P>>) {
            if constexpr (std::is_copy_constructible_v<V>) {
                if (PyList_Check(original)) {
                    Ref converted(Value<V>::cast(value_));
                    if (!converted) throw python_error{};
                    Ref list(PySequence_List(converted.get()));
                    if (!list || PyList_SetSlice(original, 0, PyList_GET_SIZE(original), list.get()) < 0) throw python_error{};
                } else if (PyDict_Check(original)) {
                    Ref converted(Value<V>::cast(value_));
                    if (!converted) throw python_error{};
                    PyDict_Clear(original);
                    if (PyDict_Check(converted.get()) && PyDict_Update(original, converted.get()) < 0) throw python_error{};
                }
            }
        }
    }
private:
    V value_;
};
// Exported classes are passed by reference to the Python-owned object.
template<class P> struct Param<P, std::enable_if_t<bound_v<std::remove_reference_t<P>> && !std::is_pointer_v<P>>> {
    using V = std::remove_cv_t<std::remove_reference_t<P>>;
    static bool check(PyObject* value, bool) { return is_instance<V>(value); }
    explicit Param(PyObject* value) : ptr_(native<V>(value)) {}
    decltype(auto) get() {
        if constexpr (std::is_rvalue_reference_v<P>) {
            if constexpr (copyable_v<V>) { copy_.emplace(*ptr_); return static_cast<V&&>(*copy_); }
            else return static_cast<V&&>(*ptr_);
        } else if constexpr (std::is_lvalue_reference_v<P>) return static_cast<P>(*ptr_);
        else return static_cast<V&>(*ptr_);
    }
    void writeback(PyObject*) {}
private:
    V* ptr_;
    std::conditional_t<std::is_rvalue_reference_v<P> && copyable_v<V>, std::optional<V>, char> copy_{};
};
template<class P> struct Param<P, std::enable_if_t<std::is_pointer_v<P> && bound_v<std::remove_pointer_t<P>>>> {
    using V = std::remove_cv_t<std::remove_pointer_t<P>>;
    static bool check(PyObject* value, bool) { return value == Py_None || is_instance<V>(value); }
    explicit Param(PyObject* value) : ptr_(value == Py_None ? nullptr : native<V>(value)) {}
    P get() { return ptr_; }
    void writeback(PyObject*) {}
private:
    V* ptr_;
};
template<> struct Param<const char*> {
    static bool check(PyObject* value, bool strict) { return value == Py_None || Value<std::string>::check(value, strict); }
    explicit Param(PyObject* value) { if (value != Py_None) text_ = Value<std::string>::load(value); }
    const char* get() { return text_ ? text_->c_str() : nullptr; }
    void writeback(PyObject*) {}
private:
    std::optional<std::string> text_;
};
template<class P> struct Param<P, std::enable_if_t<std::is_same_v<std::remove_cv_t<std::remove_reference_t<P>>, std::string_view>>> {
    static bool check(PyObject* value, bool strict) { return Value<std::string>::check(value, strict); }
    explicit Param(PyObject* value) : text_(Value<std::string>::load(value)) {}
    std::string_view get() { return text_; }
    void writeback(PyObject*) {}
private:
    std::string text_;
};

// Positional and keyword arguments mapped onto one overload's parameter list.
inline bool collect(PyObject* args, PyObject* kwargs, const char* const* names, Py_ssize_t count, Py_ssize_t required,
                    PyObject** out, Py_ssize_t* used) {
    const Py_ssize_t positional = PyTuple_GET_SIZE(args);
    if (positional > count) return false;
    for (Py_ssize_t i = 0; i < count; ++i) out[i] = i < positional ? PyTuple_GET_ITEM(args, i) : nullptr;
    if (kwargs && PyDict_GET_SIZE(kwargs)) {
        Py_ssize_t matched = 0;
        for (Py_ssize_t i = 0; i < count; ++i) {
            if (!names[i] || !names[i][0]) continue;
            PyObject* value = PyDict_GetItemString(kwargs, names[i]);
            if (!value) continue;
            if (out[i]) return false;
            out[i] = value;
            ++matched;
        }
        if (matched != PyDict_GET_SIZE(kwargs)) return false;
    }
    Py_ssize_t last = 0;
    for (Py_ssize_t i = 0; i < count; ++i) if (out[i]) last = i + 1;
    for (Py_ssize_t i = 0; i < last; ++i) if (!out[i]) return false; // C++ defaults cannot skip a middle argument
    if (last < required) return false;
    *used = last;
    return true;
}
[[noreturn]] inline void no_overload(const char* name, const char* signatures, PyObject* args, PyObject* kwargs) {
    std::string received;
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(args); ++i) received += (i ? ", " : "") + type_name(PyTuple_GET_ITEM(args, i));
    if (kwargs && PyDict_GET_SIZE(kwargs)) {
        PyObject *key, *value; Py_ssize_t position = 0;
        while (PyDict_Next(kwargs, &position, &key, &value)) {
            const char* text = PyUnicode_AsUTF8(key);
            received += std::string(received.empty() ? "" : ", ") + (text ? text : "?") + "=" + type_name(value);
        }
    }
    fail(PyExc_TypeError, std::string(name) + "(): incompatible arguments (" + received + "); supported signatures:\n" + signatures);
}

// ---------------------------------------------------------------- results
enum class Return { automatic, copy, owned, borrowed };
template<class R> PyObject* result(R&& value, PyObject* parent, Return policy) {
    using Q = std::remove_reference_t<R>;
    using V = std::remove_cv_t<Q>;
    if constexpr (std::is_pointer_v<V> && bound_v<std::remove_pointer_t<V>>) {
        using B = std::remove_cv_t<std::remove_pointer_t<V>>;
        B* ptr = const_cast<B*>(value);
        if (!ptr) Py_RETURN_NONE;
        if (parent && is_instance<B>(parent) && instance(parent)->ptr && native<B>(parent) == ptr) return Py_NewRef(parent);
        if (policy == Return::owned) return wrap(ptr, Ownership::owned, nullptr);
        if (policy == Return::copy) {
            if constexpr (copyable_v<B>) return wrap(new B(*ptr), Ownership::owned, nullptr);
            else fail(PyExc_TypeError, "C++ result is not copyable");
        }
        return wrap(ptr, Ownership::borrowed, parent);
    } else if constexpr (bound_v<V> && std::is_lvalue_reference_v<R>) {
        V* ptr = const_cast<V*>(&value);
        if (parent && is_instance<V>(parent) && instance(parent)->ptr && native<V>(parent) == ptr) return Py_NewRef(parent);
        if (policy == Return::copy || policy == Return::owned) {
            if constexpr (copyable_v<V>) return wrap(new V(value), Ownership::owned, nullptr);
            else fail(PyExc_TypeError, "C++ result is not copyable");
        }
        return wrap(ptr, Ownership::borrowed, parent);
    } else if constexpr (bound_v<V>) {
        return Value<V>::cast(std::move(value));
    } else if constexpr (std::is_same_v<V, char*> || std::is_same_v<V, const char*>) {
        return Value<const char*>::cast(value);
    } else if constexpr (std::is_lvalue_reference_v<R>) {
        return Value<V>::cast(static_cast<const V&>(value));
    } else {
        return Value<V>::cast(std::move(value));
    }
}
// Calls a native function (lambda returning decltype(auto)) and converts its result.
template<class F> PyObject* call(F&& function, PyObject* parent, Return policy) {
    using R = decltype(function());
    if constexpr (std::is_void_v<R>) { function(); Py_RETURN_NONE; }
    else return result<R>(function(), parent, policy);
}

// ---------------------------------------------------------------- C++ -> Python calls
// Converts arguments passed from C++ to Python callbacks; references to classes are borrowed.
template<class A> PyObject* argument(A&& value) {
    using V = std::remove_cv_t<std::remove_reference_t<A>>;
    if constexpr (bound_v<V>) {
        if constexpr (std::is_lvalue_reference_v<A>) return wrap(const_cast<V*>(&value), Ownership::borrowed, nullptr);
        else return Value<V>::cast(std::move(value));
    } else if constexpr (std::is_pointer_v<V> && bound_v<std::remove_pointer_t<V>>) {
        return wrap(const_cast<std::remove_cv_t<std::remove_pointer_t<V>>*>(value), Ownership::borrowed, nullptr);
    } else if constexpr (std::is_same_v<V, char*> || std::is_same_v<V, const char*>) {
        return Value<const char*>::cast(value);
    } else {
        return Value<V>::cast(value);
    }
}
template<class R> R returned(PyObject* value) {
    if constexpr (std::is_void_v<R>) { (void)value; }
    else {
        using V = std::remove_cv_t<std::remove_reference_t<R>>;
        if constexpr (std::is_lvalue_reference_v<R> && bound_v<V>) return *native<V>(value);
        else if constexpr (std::is_pointer_v<V> && bound_v<std::remove_pointer_t<V>>) return Value<V>::load(value);
        else {
            if (!Value<V>::check(value, false)) fail(PyExc_TypeError, "Python callback returned " + type_name(value) + ", which does not convert to the C++ result type");
            return Value<V>::load(value);
        }
    }
}
template<class R, class... A> R invoke_python(PyObject* callable, A&&... arguments) {
    Ref tuple(PyTuple_New(sizeof...(A)));
    if (!tuple) throw python_error{};
    Py_ssize_t index = 0;
    ((PyTuple_SET_ITEM(tuple.get(), index++, checked(argument<A>(std::forward<A>(arguments))))), ...);
    Ref value(PyObject_Call(callable, tuple.get(), nullptr));
    if (!value) throw python_error{};
    if constexpr (std::is_void_v<R>) return;
    else return returned<R>(value.get());
}
template<class R, class... A> struct Value<std::function<R(A...)>> {
    static bool check(PyObject* value, bool) { return value == Py_None || (PyCallable_Check(value) && !is_instance_object(value)); }
    static std::function<R(A...)> load(PyObject* value) {
        if (value == Py_None) return {};
        if (!PyCallable_Check(value)) fail(PyExc_TypeError, "expected a callable, got " + type_name(value));
        // A Python function that wraps a native std::function is unwrapped to avoid a round trip.
        SharedRef target(value);
        return [target](A... arguments) -> R {
            Gil gil;
            return invoke_python<R, A...>(target.get(), std::forward<A>(arguments)...);
        };
    }
    static PyObject* cast(const std::function<R(A...)>& value);
};
// Native callables returned to Python.
struct Callable {
    PyObject_HEAD
    std::function<PyObject*(PyObject*)>* call;
};
inline PyTypeObject*& callable_type() { static PyTypeObject* value = nullptr; return value; }
inline PyObject* callable_call(PyObject* self, PyObject* args, PyObject* kwargs) {
    return guard<PyObject*>([&]() -> PyObject* {
        if (kwargs && PyDict_GET_SIZE(kwargs)) fail(PyExc_TypeError, "native callables accept positional arguments only");
        auto* object = reinterpret_cast<Callable*>(self);
        if (!object->call || !*object->call) fail(PyExc_RuntimeError, "empty native callable");
        return (*object->call)(args);
    }, nullptr);
}
inline void callable_dealloc(PyObject* self) {
    auto* object = reinterpret_cast<Callable*>(self);
    delete object->call;
    PyTypeObject* type = Py_TYPE(self);
    type->tp_free(self);
    Py_DECREF(type);
}
inline PyType_Slot callable_slots[] = {
    {Py_tp_call, reinterpret_cast<void*>(&callable_call)},
    {Py_tp_dealloc, reinterpret_cast<void*>(&callable_dealloc)},
    {Py_tp_doc, const_cast<char*>("Native C++ callable.")},
    {0, nullptr}};
template<class R, class... A, std::size_t... I>
PyObject* call_native(const std::function<R(A...)>& function, PyObject* args, std::index_sequence<I...>) {
    std::tuple<Param<A>...> params{Param<A>(PyTuple_GET_ITEM(args, I))...};
    (void)params;
    return call([&]() -> R { return function(std::get<I>(params).get()...); }, nullptr, Return::copy);
}
template<class R, class... A>
PyObject* Value<std::function<R(A...)>>::cast(const std::function<R(A...)>& value) {
    if (!value) Py_RETURN_NONE;
    if (!callable_type()) fail(PyExc_SystemError, "native callable type is not initialized");
    PyObject* result = callable_type()->tp_alloc(callable_type(), 0);
    if (!result) throw python_error{};
    reinterpret_cast<Callable*>(result)->call = new std::function<PyObject*(PyObject*)>([value](PyObject* args) -> PyObject* {
        if (PyTuple_GET_SIZE(args) != static_cast<Py_ssize_t>(sizeof...(A))) fail(PyExc_TypeError, "incorrect number of arguments");
        return call_native<R, A...>(value, args, std::index_sequence_for<A...>{});
    });
    return result;
}

// ---------------------------------------------------------------- directors
// Returns the Python override of a virtual method, or null when the native method applies.
inline Ref override_for(PyObject* self, PyTypeObject* native_type, const char* name) {
    if (!self || !native_type || Py_TYPE(self) == native_type) return {};
    Ref attribute(PyObject_GetAttrString(reinterpret_cast<PyObject*>(Py_TYPE(self)), name));
    if (!attribute) { PyErr_Clear(); return {}; }
    Ref original(PyObject_GetAttrString(reinterpret_cast<PyObject*>(native_type), name));
    if (!original) PyErr_Clear();
    if (original && attribute.get() == original.get()) return {};
    Ref bound(PyObject_GetAttrString(self, name));
    if (!bound) throw python_error{};
    return bound;
}
[[noreturn]] inline void pure_virtual(const char* name) {
    fail(PyExc_NotImplementedError, std::string("pure virtual method ") + name + " is not implemented by the Python subclass");
}

// ---------------------------------------------------------------- module helpers
inline void add_object(PyObject* target, const char* name, PyObject* value) {
    if (!value) throw python_error{};
    const int status = PyObject_SetAttrString(target, name, value);
    Py_DECREF(value);
    if (status < 0) throw python_error{};
}
// Install a method descriptor through setattr so CPython fills the matching type slot.
inline void install_slot(PyObject* type, const char* name) {
    Ref descriptor(PyObject_GetAttrString(type, name));
    if (!descriptor) throw python_error{};
    if (PyObject_SetAttrString(type, name, descriptor.get()) < 0) throw python_error{};
}
inline PyObject* make_enum(PyObject* module, const char* kind, const char* name, const char* qualname,
                           std::initializer_list<std::pair<const char*, long long>> members,
                           std::initializer_list<std::pair<const char*, unsigned long long>> unsigned_members = {}) {
    Ref enum_module(PyImport_ImportModule("enum"));
    if (!enum_module) throw python_error{};
    Ref factory(PyObject_GetAttrString(enum_module.get(), kind));
    if (!factory) throw python_error{};
    Ref items(PyList_New(0));
    if (!items) throw python_error{};
    for (const auto& [key, value] : members) {
        Ref item(Py_BuildValue("(sL)", key, value));
        if (!item || PyList_Append(items.get(), item.get()) < 0) throw python_error{};
    }
    for (const auto& [key, value] : unsigned_members) {
        Ref item(Py_BuildValue("(sK)", key, value));
        if (!item || PyList_Append(items.get(), item.get()) < 0) throw python_error{};
    }
    Ref module_name(PyObject_GetAttrString(module, "__name__"));
    if (!module_name) throw python_error{};
    Ref args(Py_BuildValue("(sO)", name, items.get()));
    Ref kwargs(Py_BuildValue("{sOss}", "module", module_name.get(), "qualname", qualname));
    if (!args || !kwargs) throw python_error{};
    return checked(PyObject_Call(factory.get(), args.get(), kwargs.get()));
}
inline PyTypeObject* make_type(PyObject* module, PyType_Spec* spec, PyObject* bases) {
    PyObject* type = PyType_FromModuleAndSpec(module, spec, bases);
    if (!type) throw python_error{};
    return reinterpret_cast<PyTypeObject*>(type);
}
template<class T> void register_type(TypeInfo& info, const char* name) {
    info.name = name;
    if constexpr (std::is_destructible_v<T>) info.destroy = [](void* ptr) { delete static_cast<T*>(ptr); };
    if constexpr (copyable_v<T>)
        info.copy = [](const void* ptr) -> void* { return new T(*static_cast<const T*>(ptr)); };
    registry()[std::type_index(typeid(T))] = &info;
}
// Module state: strong references to the module's types; static lookups are borrowed from it.
struct ModuleState { PyObject* types; };
inline ModuleState* state(PyObject* module) { return static_cast<ModuleState*>(PyModule_GetState(module)); }
inline void keep(PyObject* module, PyObject* type) {
    if (PyList_Append(state(module)->types, type) < 0) throw python_error{};
}
// _set_doc(obj, text): `obj.__doc__ = text` that also works for this module's functions and
// methods, whose builtin __doc__ is read-only: their method definition's text is replaced.
inline PyObject* set_doc(PyObject* module, PyObject* args) {
    PyObject* target = nullptr;
    PyObject* text = nullptr;
    if (!PyArg_ParseTuple(args, "OU:_set_doc", &target, &text)) return nullptr;
    if (PyObject_SetAttrString(target, "__doc__", text) == 0) Py_RETURN_NONE;
    if (!PyErr_ExceptionMatches(PyExc_AttributeError) && !PyErr_ExceptionMatches(PyExc_TypeError)) return nullptr;
    PyErr_Clear();
    PyMethodDef* definition = nullptr;
    if (PyCFunction_Check(target)) {
        // Module functions are bound to the module; static methods to their class.
        PyObject* self = reinterpret_cast<PyCFunctionObject*>(target)->m_self;
        if (self == module || (self && PyType_Check(self) && root_type() &&
                               PyType_IsSubtype(reinterpret_cast<PyTypeObject*>(self), root_type())))
            definition = reinterpret_cast<PyCFunctionObject*>(target)->m_ml;
    }
    if (!definition && Py_IS_TYPE(target, &PyMethodDescr_Type) && root_type() &&
             PyType_IsSubtype(PyDescr_TYPE(target), root_type()))
        definition = reinterpret_cast<PyMethodDescrObject*>(target)->d_method;
    if (!definition) {
        PyErr_SetString(PyExc_TypeError, "_set_doc: the object's __doc__ is read-only and it is not a function of this module");
        return nullptr;
    }
    const char* utf8 = PyUnicode_AsUTF8(text);
    if (!utf8) return nullptr;
    // A text signature ("name(a, b)\n--\n\n") stays in front of the new text, for inspect.signature().
    Ref combined(Py_NewRef(text));
    const std::string_view old_doc = definition->ml_doc ? definition->ml_doc : "";
    const auto marker = old_doc.find(")\n--\n\n");
    const std::string_view name = definition->ml_name ? definition->ml_name : "";
    if (marker != std::string_view::npos && old_doc.substr(0, name.size()) == name && old_doc.size() > name.size() &&
        old_doc[name.size()] == '(' && old_doc.substr(0, marker).find('\n') == std::string_view::npos &&
        std::string_view(utf8).find(")\n--\n\n") == std::string_view::npos) {
        const auto signature = std::string(old_doc.substr(0, marker + 6)) + utf8;
        combined = Ref(PyUnicode_FromStringAndSize(signature.data(), static_cast<Py_ssize_t>(signature.size())));
        if (!combined) return nullptr;
        utf8 = PyUnicode_AsUTF8(combined.get());
        if (!utf8) return nullptr;
    }
    try { keep(module, combined.get()); } catch (const python_error&) { return nullptr; }  // owns the text
    definition->ml_doc = utf8;
    Py_RETURN_NONE;
}
inline void install_vector_types(PyObject* module) {
    static const char* source = R"py(
class _Vector(list):
    """A Python list with std::vector's method names."""
    __slots__ = ()
    def size(self): return len(self)
    def empty(self): return not self
    def push_back(self, value): self.append(value)
    def pop_back(self): self.pop()
    def front(self): return self[0]
    def back(self): return self[-1]
    def at(self, index):
        if not 0 <= index < len(self): raise IndexError("vector index out of range")
        return self[index]
    def reserve(self, count): pass
    def capacity(self): return len(self)
    def assign(self, count, value): self[:] = [value] * count
    def erase(self, first, last=None):
        if last is None: del self[first]
        else: del self[first:last]
    def swap(self, other): self[:], other[:] = list(other), list(self)
    def resize(self, count, value=None):
        del self[count:]
        self.extend([value] * (count - len(self)))
    # Equal to a tuple of the same items, as SWIG's tuple results were.
    def __eq__(self, other):
        return list.__eq__(self, list(other)) if isinstance(other, tuple) else list.__eq__(self, other)
    def __ne__(self, other):
        equal = self.__eq__(other)
        return equal if equal is NotImplemented else not equal
    __hash__ = None
    def __repr__(self): return type(self).__name__ + "(" + list.__repr__(self) + ")"

class _Pair(tuple):
    """A two-item tuple with std::pair's first and second."""
    __slots__ = ()
    def __new__(cls, *items):
        if len(items) == 1: items = tuple(items[0])
        if len(items) != 2: raise TypeError("a pair holds exactly two items")
        return tuple.__new__(cls, items)
    first = property(lambda self: self[0], doc="The first item.")
    second = property(lambda self: self[1], doc="The second item.")
    def __repr__(self): return type(self).__name__ + tuple.__repr__(self)
)py";
    Ref globals(checked(PyDict_New()));
    Ref module_name(checked(PyModule_GetNameObject(module)));
    if (PyDict_SetItemString(globals.get(), "__builtins__", PyEval_GetBuiltins()) < 0 ||
        PyDict_SetItemString(globals.get(), "__name__", module_name.get()) < 0) throw python_error{};
    Ref ran(PyRun_String(source, Py_file_input, globals.get(), globals.get()));
    if (!ran) throw python_error{};
    for (const char* name : {"_Vector", "_Pair"}) {
        PyObject* type = PyDict_GetItemString(globals.get(), name);
        if (!type) throw python_error{};
        keep(module, type);
        if (PyModule_AddObjectRef(module, name, type) < 0) throw python_error{};
        (name[1] == 'V' ? vector_type : pair_type) = type;
    }
}
inline int module_traverse(PyObject* module, visitproc visit, void* arg) {
    auto* current = state(module);
    if (current) Py_VISIT(current->types);
    return 0;
}
// Forget static lookups that point at this module's types before they are released.
inline int module_clear(PyObject* module) {
    auto* current = state(module);
    if (!current || !current->types) return 0;
    PyObject* types = current->types;
    const auto owned = [&](PyObject* value) {
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(types); ++i) if (PyList_GET_ITEM(types, i) == value) return true;
        return false;
    };
    for (auto& [key, info] : registry()) if (info->type && owned(reinterpret_cast<PyObject*>(info->type))) info->type = nullptr;
    if (vector_type && owned(vector_type)) vector_type = nullptr;
    if (pair_type && owned(pair_type)) pair_type = nullptr;
    for (auto it = enum_types().begin(); it != enum_types().end();) {
        if (owned(it->second)) it = enum_types().erase(it); else ++it;
    }
    Py_CLEAR(current->types);
    return 0;
}
inline void module_free(void* module) { module_clear(static_cast<PyObject*>(module)); }
// Shared by every module load in the process, so objects from earlier loads stay recognizable.
inline void initialize_runtime(PyType_Spec* root, PyType_Spec* callable) {
    if (!root_type()) root_type() = reinterpret_cast<PyTypeObject*>(checked(PyType_FromSpec(root)));
    if (!callable_type()) callable_type() = reinterpret_cast<PyTypeObject*>(checked(PyType_FromSpec(callable)));
}
template<class Derived, class Base> void add_base(TypeInfo& info) {
    info.bases.push_back({&Info<Base>::value, [](void* ptr) -> void* {
        return static_cast<Base*>(static_cast<Derived*>(ptr));
    }});
}
} } // ROSETTA_PY_MODULE::rosetta_py
)cpp"};
} // namespace rosetta::detail
