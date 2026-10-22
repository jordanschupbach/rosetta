#include "rosetta/go.hpp"
#include "go_runtime.hpp"
#include "type_spelling.hpp"
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <sstream>

namespace rosetta {
namespace {
std::string quote(const std::string& text) {
    std::string result = "\"";
    for (char c : text) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else result += c;
    }
    return result + '"';
}
bool letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool identifier(const std::string& text) {
    return !text.empty() && letter(text.front()) && std::all_of(text.begin(), text.end(), [](char c) {
        return letter(c) || (c >= '0' && c <= '9') || c == '_';
    });
}
char upper(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; }
char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
// Exported Go names are CamelCase: too_large becomes TooLarge. Empty when
// the result would not start with a letter.
std::string exported(const std::string& text) {
    std::string result;
    bool start = true;
    for (char c : text) {
        if (c == '_') { start = true; continue; }
        result += start ? upper(c) : c;
        start = false;
    }
    return !result.empty() && letter(result.front()) ? result : std::string{};
}
// Go keywords, predeclared identifiers and names the generated bodies use,
// which parameters must not shadow.
bool reserved_local(const std::string& text) {
    static const std::set<std::string> words = {"break", "case", "chan", "const", "continue", "default", "defer", "else",
        "fallthrough", "for", "func", "go", "goto", "if", "import", "interface", "map", "package", "range", "return", "select",
        "struct", "switch", "type", "var", "any", "append", "bool", "byte", "cap", "clear", "close", "comparable", "complex",
        "complex64", "complex128", "copy", "delete", "error", "false", "float32", "float64", "imag", "int", "int8", "int16",
        "int32", "int64", "iota", "len", "make", "max", "min", "new", "nil", "panic", "print", "println", "real", "recover",
        "rune", "string", "true", "uint", "uint8", "uint16", "uint32", "uint64", "uintptr", "C", "unsafe", "runtime", "o",
        "r", "cell", "status", "optional", "defaults", "take", "failure", "bytes", "tooMany"};
    // The wrapper's string locals are pN and nN.
    const bool local = text.size() > 1 && (text.front() == 'p' || text.front() == 'n') &&
        std::all_of(text.begin() + 1, text.end(), [](char c) { return c >= '0' && c <= '9'; });
    return local || words.count(text) > 0;
}
struct Scalar { std::string cpp, c, go; };
// A supported C++ scalar with its fixed-width C ABI type and Go type.
std::optional<Scalar> scalar(const Type& type) {
    auto text = detail::trim_type(type.canonical);
    const bool reference = text.rfind("const ", 0) == 0 && !text.empty() && text.back() == '&' &&
        (text.size() < 2 || text[text.size() - 2] != '&');
    if (reference) text = detail::trim_type(text.substr(0, text.size() - 1));
    if (text.rfind("const ", 0) == 0) text = text.substr(6);
    if (text == "std::__cxx11::basic_string<char>" || text == "std::__1::basic_string<char>" || text == "std::basic_string<char>")
        text = "std::string";
    if (reference && text != "std::string") return std::nullopt;
    static const std::map<std::string, std::pair<std::string, std::string>> table = {
        {"bool", {"bool", "bool"}}, {"char", {"uint8_t", "byte"}}, {"signed char", {"int8_t", "int8"}},
        {"unsigned char", {"uint8_t", "uint8"}}, {"short", {"int16_t", "int16"}}, {"unsigned short", {"uint16_t", "uint16"}},
        {"int", {"int32_t", "int32"}}, {"unsigned int", {"uint32_t", "uint32"}}, {"long", {"int64_t", "int64"}},
        {"unsigned long", {"uint64_t", "uint64"}}, {"long long", {"int64_t", "int64"}}, {"unsigned long long", {"uint64_t", "uint64"}},
        {"float", {"float", "float32"}}, {"double", {"double", "float64"}}, {"std::string", {"", "string"}}};
    const auto found = table.find(text);
    if (found == table.end()) return std::nullopt;
    return Scalar{text, found->second.first, found->second.second};
}
// The C ABI type spelled for the C++ stubs.
std::string cxx(const std::string& c) { return c.find("int") != std::string::npos ? "std::" + c : c; }
std::string zero(const std::string& go) {
    if (go == "bool") return "false";
    if (go == "string") return "\"\"";
    return "0";
}
std::string problem(const Function& fn) {
    if (!fn.unsupported_reason.empty()) return fn.unsupported_reason;
    if (fn.is_deleted) return "deleted function";
    if (fn.default_arguments > fn.parameters.size()) return "invalid default argument metadata";
    if (!fn.is_constructor && fn.result.canonical != "void" && !scalar(fn.result)) return "unsupported Go result type " + fn.result.canonical;
    for (const auto& type : fn.parameters)
        if (!scalar(type)) return "unsupported Go parameter type " + type.canonical;
    return {};
}
std::string replace_all(std::string text, const std::string& from, const std::string& to) {
    for (std::size_t i = 0; (i = text.find(from, i)) != std::string::npos; i += to.size()) text.replace(i, from.size(), to);
    return text;
}
enum class Kind { function, method, constructor };
} // namespace

GeneratedGoSource generate_go(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy) {
    // Standard library import paths, which a package path of the same name
    // could not be imported over, plus Go keywords and cgo's package names.
    static const std::set<std::string> reserved_packages = {"archive", "bufio", "builtin", "bytes", "cmp", "compress",
        "container", "context", "crypto", "database", "debug", "embed", "encoding", "errors", "expvar", "flag", "fmt", "go",
        "hash", "html", "image", "index", "internal", "io", "iter", "log", "maps", "math", "mime", "net", "os", "path",
        "plugin", "reflect", "regexp", "runtime", "slices", "sort", "strconv", "strings", "structs", "sync", "syscall",
        "testing", "text", "time", "unicode", "unique", "unsafe", "weak", "main", "C", "std", "cmd", "break", "case", "chan",
        "const", "continue", "default", "defer", "else", "fallthrough", "for", "func", "goto", "if", "import", "interface",
        "map", "package", "range", "return", "select", "struct", "switch", "type", "var"};
    if (!identifier(name)) throw BindingError("package name must be a simple ASCII identifier starting with a letter");
    if (reserved_packages.count(name)) throw BindingError("package name is a Go keyword or standard library package");
    if (!policy.callbacks.empty() || !policy.enum_flags.empty())
        throw BindingError("Go backend does not yet support callback or enum flag policies");
    GeneratedGoSource result{{}, {}, module.diagnostics};
    auto reject = [&](const std::string& context, const std::string& reason) { result.diagnostics.push_back(context + ": " + reason); };
    const auto status_type = "rosetta_" + name + "_status", bytes_type = "rosetta_" + name + "_bytes";
    std::ostringstream out, declarations, go;
    out << "// Generated by Rosetta: C++ stubs for the Go package " << name << ".\n"
        << "#include <cstddef>\n#include <cstdint>\n#include <cstdlib>\n#include <cstring>\n#include <limits>\n#include <memory>\n"
        << "#include <mutex>\n#include <new>\n#include <stdexcept>\n#include <string>\n#include <type_traits>\n#include <utility>\n"
        << "#include " << quote(module.header) << '\n' << detail::go_runtime;
    // Package-level Go names; each type has its own method names.
    std::set<std::string> taken = {"Error", "ErrorKind", "KindAllocation", "KindRange", "KindIndex", "KindArgument", "KindNative", "KindClosed"};
    auto reserve = [](std::set<std::string>& names, const std::string& symbol) {
        if (!names.insert(symbol).second) throw BindingError("Go name collision: " + symbol);
    };
    bool classes = false;
    std::size_t next = 0;
    // Emits the stub for FN and its Go wrapper SYMBOL: a function, a method of
    // the Go type OWNER, or OWNER's constructor. CALL is the C++ call with
    // @ARGS@ for the arguments. Defaulted parameters become a final variadic
    // parameter when they share one Go type; otherwise every argument is required.
    auto emit = [&](const Function& fn, const std::string& symbol, Kind kind, const std::string& call, const std::string& label,
                    const std::string& owner = {}, const std::string& klass = {}) {
        const auto count = fn.parameters.size();
        std::vector<Scalar> types;
        for (const auto& type : fn.parameters) types.push_back(*scalar(type));
        bool variadic = fn.default_arguments > 0;
        for (std::size_t i = count - fn.default_arguments; variadic && i < count; ++i)
            variadic = types[i].go == types[count - fn.default_arguments].go;
        const auto required = variadic ? count - fn.default_arguments : count;
        const auto stub = "rosetta_" + name + "_" + std::to_string(next++);
        const bool self = kind == Kind::method, constructor = kind == Kind::constructor, none = fn.result.canonical == "void";
        const auto returned = constructor || none ? std::optional<Scalar>{} : scalar(fn.result);
        // Go parameter names: the C++ names in lowerCamelCase where usable.
        std::vector<std::string> names;
        std::set<std::string> used;
        for (std::size_t i = 0; i < required; ++i) {
            auto local = i < fn.parameter_names.size() ? exported(fn.parameter_names[i]) : std::string{};
            if (!local.empty()) local.front() = lower(local.front());
            if (local.empty() || reserved_local(local) || used.count(local)) local = "arg" + std::to_string(i);
            while (used.count(local)) local += "_";
            used.insert(local);
            names.push_back(local);
        }
        // The C ABI: [self], each parameter (a string as data and size),
        // [the count of given arguments], then the status.
        std::vector<std::string> cxx_params, c_params;
        if (self) { cxx_params.push_back("void* self"); c_params.push_back("void* self"); }
        for (std::size_t i = 0; i < count; ++i) {
            const auto a = "a" + std::to_string(i);
            if (types[i].go == "string") {
                cxx_params.push_back("const char* " + a); cxx_params.push_back("std::size_t " + a + "_size");
                c_params.push_back("const char* " + a); c_params.push_back("size_t " + a + "_size");
            } else {
                cxx_params.push_back(cxx(types[i].c) + " " + a);
                c_params.push_back(types[i].c + " " + a);
            }
        }
        if (variadic) { cxx_params.push_back("std::size_t count"); c_params.push_back("size_t count"); }
        cxx_params.push_back("rosetta_go::Status* status");
        c_params.push_back(status_type + "* status");
        std::string cxx_result = "void", c_result = "void";
        if (constructor) cxx_result = c_result = "void*";
        else if (returned && returned->go == "string") { cxx_result = "rosetta_go::Bytes"; c_result = bytes_type; }
        else if (returned) { cxx_result = cxx(returned->c); c_result = returned->c; }
        auto join = [](const std::vector<std::string>& parts) {
            std::string text;
            for (const auto& part : parts) text += (text.empty() ? "" : ", ") + part;
            return text;
        };
        declarations << c_result << ' ' << stub << '(' << join(c_params) << ");\n";
        out << "extern \"C\" " << cxx_result << ' ' << stub << '(' << join(cxx_params) << ") {\n"
            << "return rosetta_go::guard(status, [&]" << (none ? "" : "() -> " + cxx_result) << " {\n";
        if (self) out << "auto& object = rosetta_go::live<" << klass << ">(self);\n";
        auto body = [&](std::size_t present) {
            std::string actuals;
            for (std::size_t i = 0; i < present; ++i) {
                const auto a = "a" + std::to_string(i);
                if (types[i].go == "string") out << "auto value" << i << " = rosetta_go::text(" << a << ", " << a << "_size);\n";
                else out << "auto value" << i << " = rosetta_go::convert<" << types[i].cpp << ">(" << a << ");\n";
                actuals += (i ? ", std::move(value" : "std::move(value") + std::to_string(i) + ")";
            }
            const auto invocation = replace_all(call, "@ARGS@", actuals);
            if (constructor) out << "return rosetta_go::adopt(" << invocation << ");\n";
            else if (none) out << invocation << ";\nreturn;\n";
            else if (returned->go == "string") out << "return rosetta_go::bytes(" << invocation << ");\n";
            else out << "return rosetta_go::convert<" << cxx(returned->c) << ">(" << invocation << ");\n";
        };
        if (!variadic) body(count);
        else {
            out << "switch (count) {\n";
            for (std::size_t present = required; present <= count; ++present) {
                out << "case " << present << ": {\n";
                body(present);
                out << "}\n";
            }
            out << "}\nrosetta_go::fail(rosetta_go::argument, \"invalid optional argument count\");\n";
        }
        out << "});\n}\n";

        // The Go wrapper.
        std::string signature;
        for (std::size_t i = 0; i < count; ++i) {
            if (!signature.empty()) signature += ", ";
            signature += fn.parameters[i].spelling;
            if (i < fn.parameter_names.size() && !fn.parameter_names[i].empty()) signature += " " + fn.parameter_names[i];
        }
        go << "\n// " << symbol << " wraps " << label << '(' << signature << ").";
        if (variadic) go << " The last " << fn.default_arguments << (fn.default_arguments == 1 ? " argument is" : " arguments are")
                         << " optional, as in C++.";
        go << "\nfunc ";
        if (self) go << "(o *" << owner << ") ";
        go << symbol << '(';
        for (std::size_t i = 0; i < required; ++i) go << (i ? ", " : "") << names[i] << ' ' << types[i].go;
        if (variadic) go << (required ? ", " : "") << "defaults ..." << types[required].go;
        go << ") ";
        const auto value = constructor ? "*" + owner : returned ? returned->go : std::string{};
        const auto failed = constructor ? std::string("nil") : returned ? zero(returned->go) : std::string{};
        go << (value.empty() ? "error" : "(" + value + ", error)") << " {\n\tvar status C." << status_type << '\n';
        std::vector<std::string> actuals;
        if (self) actuals.push_back("o.handle()");
        if (variadic) {
            go << "\tvar optional [" << fn.default_arguments << ']' << types[required].go << "\n\tif len(defaults) > len(optional) {\n\t\treturn "
               << (failed.empty() ? "" : failed + ", ") << "tooMany(" << quote(label) << ")\n\t}\n\tcopy(optional[:], defaults)\n";
        }
        for (std::size_t i = 0; i < count; ++i) {
            const auto argument = i < required ? names[i] : "optional[" + std::to_string(i - required) + "]";
            if (types[i].go == "string") {
                go << "\tp" << i << ", n" << i << " := bytes(" << argument << ")\n";
                actuals.push_back("p" + std::to_string(i));
                actuals.push_back("n" + std::to_string(i));
            } else actuals.push_back("C." + types[i].c + "(" + argument + ")");
        }
        if (variadic) actuals.push_back("C.size_t(" + std::to_string(required) + "+len(defaults))");
        actuals.push_back("&status");
        go << '\t' << (none ? "" : constructor ? "cell := " : "r := ") << "C." << stub << '(' << join(actuals) << ")\n";
        if (self) go << "\truntime.KeepAlive(o)\n";
        const auto check = "failure(&status, " + quote(label) + ")";
        if (constructor) {
            go << "\tif err := " << check << "; err != nil {\n\t\treturn nil, err\n\t}\n\to := &" << owner << "{cell: cell}\n"
               << "\truntime.AddCleanup(o, func(cell unsafe.Pointer) { C.rosetta_" << name << "_release_" << owner << "(cell) }, cell)\n"
               << "\treturn o, nil\n";
        } else if (none) go << "\treturn " << check << '\n';
        else if (returned->go == "string") go << "\treturn take(r.data, r.size), " << check << '\n';
        else go << "\treturn " << returned->go << "(r), " << check << '\n';
        go << "}\n";
    };

    std::map<std::string, std::size_t> counts;
    for (const auto& fn : module.functions) ++counts[fn.qualified_name()];
    for (const auto& fn : module.functions) {
        auto why = problem(fn);
        if (counts[fn.qualified_name()] > 1) why = "overloads require an explicit facade";
        const auto symbol = exported(fn.name);
        if (why.empty() && symbol.empty()) why = "name cannot be an exported Go identifier";
        if (!why.empty()) { reject(fn.qualified_name(), why); continue; }
        reserve(taken, symbol);
        emit(fn, symbol, Kind::function, "::" + fn.qualified_name() + "(@ARGS@)", fn.qualified_name());
    }
    for (const auto& selection : policy.classes)
        if (std::none_of(module.records.begin(), module.records.end(), [&](const Record& r) { return r.qualified_name() == selection.first; }))
            throw BindingError("unknown class in Go policy: " + selection.first);
    for (const auto& rec : module.records) {
        const auto owner = rec.qualified_name();
        const auto selected = policy.classes.find(owner);
        if (selected == policy.classes.end()) {
            if (!policy.explicit_class_selection && policy.classes.empty()) reject(owner, "class requires an owned class policy");
            continue;
        }
        const auto& config = selected->second;
        for (const auto& requested : config.methods) {
            const auto found = std::find_if(rec.methods.begin(), rec.methods.end(), [&](const Function& f) { return f.name == requested; });
            if (found == rec.methods.end()) throw BindingError("unknown Go policy method: " + owner + "::" + requested);
            if (found->access != "public") throw BindingError("Go policy method is not public: " + owner + "::" + requested);
        }
        for (const auto& requested : config.fields) {
            const auto found = std::find_if(rec.fields.begin(), rec.fields.end(), [&](const Field& f) { return f.name == requested; });
            if (found == rec.fields.end()) throw BindingError("unknown Go policy field: " + owner + "::" + requested);
            if (found->access != "public") throw BindingError("Go policy field is not public: " + owner + "::" + requested);
        }
        if (!rec.complete || rec.abstract || rec.has_bases || !rec.public_destructor || !rec.unsupported_reason.empty()) {
            reject(owner, "Go requires a complete, concrete class without bases and a public nonthrowing destructor"); continue;
        }
        if (!config.borrow_self.empty()) { reject(owner, "borrowed Go results are not yet supported"); continue; }
        const auto type = exported(rec.name);
        if (type.empty()) { reject(owner, "name cannot be an exported Go identifier"); continue; }
        reserve(taken, type);
        classes = true;
        // A Go type holding the cell: NewK, methods, field getters, SetField
        // setters, Close and a cleanup; static methods are package functions KMethod.
        const auto native = "::" + owner;
        out << "static_assert(std::is_nothrow_destructible_v<" << native << ">, \"native owners require nonthrowing destruction\");\n"
            << "extern \"C\" void rosetta_" << name << "_close_" << type << "(void* cell) { rosetta_go::close<" << native << ">(cell); }\n"
            << "extern \"C\" void rosetta_" << name << "_release_" << type << "(void* cell) { rosetta_go::release<" << native << ">(cell); }\n";
        declarations << "void rosetta_" << name << "_close_" << type << "(void* cell);\nvoid rosetta_" << name << "_release_" << type << "(void* cell);\n";
        go << "\n// " << type << " owns a native " << owner << ". Close destroys it; otherwise it is destroyed\n"
           << "// after it becomes unreachable. Methods may be called from any goroutine.\ntype " << type << " struct {\n\tcell unsafe.Pointer\n}\n"
           << "\nfunc (o *" << type << ") handle() unsafe.Pointer {\n\tif o == nil {\n\t\treturn nil\n\t}\n\treturn o.cell\n}\n"
           << "\n// Close destroys the native object; later calls return a KindClosed error.\n// Closing twice is harmless.\n"
           << "func (o *" << type << ") Close() error {\n\tC.rosetta_" << name << "_close_" << type << "(o.handle())\n"
           << "\truntime.KeepAlive(o)\n\treturn nil\n}\n";
        std::set<std::string> methods = {"Close"};
        std::vector<const Function*> constructors;
        for (const auto& ctor : rec.constructors) if (!ctor.is_deleted) constructors.push_back(&ctor);
        if (config.constructors) {
            if (constructors.size() != 1) reject(owner, "constructor overloads require an explicit facade");
            else {
                const auto& ctor = *constructors.front();
                auto why = problem(ctor);
                if (ctor.access != "public") why = "constructor is not public";
                if (!why.empty()) reject(owner, why);
                else {
                    reserve(taken, "New" + type);
                    emit(ctor, "New" + type, Kind::constructor, "std::make_unique<" + native + ">(@ARGS@)", owner + "::" + rec.name, type);
                }
            }
        }
        counts.clear();
        for (const auto& fn : rec.methods) ++counts[fn.name];
        for (const auto& fn : rec.methods) {
            if (fn.access != "public" || (config.restrict_methods && !config.methods.count(fn.name))) continue;
            auto why = problem(fn);
            if (counts[fn.name] > 1) why = "method overloads require an explicit facade";
            const auto symbol = exported(fn.name);
            if (why.empty() && symbol.empty()) why = "name cannot be an exported Go identifier";
            if (!why.empty()) { reject(owner + "::" + fn.name, why); continue; }
            if (fn.is_static) {
                reserve(taken, type + symbol);
                emit(fn, type + symbol, Kind::function, native + "::" + fn.name + "(@ARGS@)", owner + "::" + fn.name);
            } else {
                reserve(methods, symbol);
                emit(fn, symbol, Kind::method, "object." + fn.name + "(@ARGS@)", owner + "::" + fn.name, type, native);
            }
        }
        for (const auto& field : rec.fields) {
            if (field.access != "public" || (config.restrict_fields && !config.fields.count(field.name))) continue;
            const auto value = scalar(field.type);
            const auto getter = exported(field.name);
            if (!value || field.bitfield || field.type.is_volatile || getter.empty()) { reject(owner + "::" + field.name, "unsupported Go field type"); continue; }
            reserve(methods, getter);
            const auto label = owner + "::" + field.name, stub = "rosetta_" + name + "_" + std::to_string(next++);
            const bool text = value->go == "string";
            const auto c_type = text ? bytes_type : value->c, cxx_type = text ? std::string("rosetta_go::Bytes") : cxx(value->c);
            declarations << c_type << ' ' << stub << "_get(void* self, " << status_type << "* status);\n";
            out << "extern \"C\" " << cxx_type << ' ' << stub << "_get(void* self, rosetta_go::Status* status) {\n"
                << "return rosetta_go::guard(status, [&]() -> " << cxx_type << " {\nauto& object = rosetta_go::live<" << native << ">(self);\n"
                << "return rosetta_go::" << (text ? "bytes(" : "convert<" + cxx_type + ">(") << "object." << field.name << ");\n});\n}\n";
            go << "\n// " << getter << " reads " << label << ".\nfunc (o *" << type << ") " << getter << "() (" << value->go << ", error) {\n"
               << "\tvar status C." << status_type << "\n\tr := C." << stub << "_get(o.handle(), &status)\n\truntime.KeepAlive(o)\n"
               << "\treturn " << (text ? "take(r.data, r.size)" : value->go + "(r)") << ", failure(&status, " << quote(label) << ")\n}\n";
            // Const fields have no setter.
            if (field.type.is_const) continue;
            const auto setter = "Set" + getter;
            reserve(methods, setter);
            declarations << "void " << stub << "_set(void* self, " << (text ? "const char* a0, size_t a0_size" : value->c + " a0")
                         << ", " << status_type << "* status);\n";
            out << "extern \"C\" void " << stub << "_set(void* self, " << (text ? "const char* a0, std::size_t a0_size" : cxx(value->c) + " a0")
                << ", rosetta_go::Status* status) {\nrosetta_go::guard(status, [&] {\nauto& object = rosetta_go::live<" << native << ">(self);\n"
                << "object." << field.name << " = " << (text ? "rosetta_go::text(a0, a0_size)" : "rosetta_go::convert<" + value->cpp + ">(a0)")
                << ";\n});\n}\n";
            go << "\n// " << setter << " writes " << label << ".\nfunc (o *" << type << ") " << setter << "(value " << value->go << ") error {\n"
               << "\tvar status C." << status_type << '\n';
            if (text) go << "\tp0, n0 := bytes(value)\n\tC." << stub << "_set(o.handle(), p0, n0, &status)\n";
            else go << "\tC." << stub << "_set(o.handle(), C." << value->c << "(value), &status)\n";
            go << "\truntime.KeepAlive(o)\n\treturn failure(&status, " << quote(label) << ")\n}\n";
        }
    }
    for (const auto& enumeration : module.enumerations)
        if (enumeration.selected) reject(enumeration.qualified_name(), "Go enum bindings are not yet supported");
    if (!skip && !result.diagnostics.empty()) {
        std::string error = "unsupported declaration for Go";
        for (const auto& entry : result.diagnostics) error += "\n  " + entry;
        throw BindingError(error);
    }
    result.source = out.str();

    std::ostringstream interface;
    interface << "// Code generated by Rosetta. DO NOT EDIT.\n\n// Package " << name << " calls C++ through cgo. Build it with lib" << name
              << "_stubs.a,\n// the compiled C++ stubs, in this directory.\npackage " << name << "\n\n/*\n#cgo LDFLAGS: -L${SRCDIR} -l" << name
              << "_stubs\n#cgo darwin LDFLAGS: -lc++\n#cgo linux LDFLAGS: -lstdc++ -lm\n#include <stdbool.h>\n#include <stddef.h>\n"
              << "#include <stdint.h>\n#include <stdlib.h>\n\ntypedef struct { uint64_t code; char* message; size_t size; } " << status_type
              << ";\ntypedef struct { char* data; size_t size; } " << bytes_type << ";\n" << declarations.str() << "*/\nimport \"C\"\n\nimport (\n"
              << (classes ? "\t\"runtime\"\n" : "") << "\t\"unsafe\"\n)\n"
              << replace_all(replace_all(detail::go_interface_runtime, "@PACKAGE@", name), "@STATUS@", status_type) << go.str();
    result.interface = interface.str();
    return result;
}
} // namespace rosetta
