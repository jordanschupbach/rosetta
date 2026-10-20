#include "rosetta/fortran.hpp"
#include "fortran_runtime.hpp"
#include "type_spelling.hpp"
#include <algorithm>
#include <map>
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
std::string lower(std::string text) {
    for (auto& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return text;
}
std::string exported(std::string text) {
    for (std::size_t i = 0; (i = text.find("::", i)) != std::string::npos; i += 2) text.replace(i, 2, "__");
    return text;
}
bool identifier(const std::string& text) {
    const auto small = lower(text);
    return !small.empty() && small.size() <= 63 && small.front() >= 'a' && small.front() <= 'z' &&
        std::all_of(small.begin(), small.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}
std::string scalar(const Type& type) {
    auto text = detail::trim_type(type.canonical);
    const bool reference = text.rfind("const ", 0) == 0 && !text.empty() && text.back() == '&' &&
        (text.size() < 2 || text[text.size() - 2] != '&');
    if (reference) text = detail::trim_type(text.substr(0, text.size() - 1));
    if (text.rfind("const ", 0) == 0) text = text.substr(6);
    if (text == "std::__cxx11::basic_string<char>" || text == "std::__1::basic_string<char>" || text == "std::basic_string<char>") text = "std::string";
    if (reference && text != "std::string") return {};
    static const std::set<std::string> supported = {"bool", "char", "signed char", "unsigned char", "short", "unsigned short",
        "int", "unsigned int", "long", "unsigned long", "long long", "unsigned long long", "float", "double", "std::string"};
    return supported.count(text) ? text : "";
}
std::string problem(const Function& fn) {
    if (!fn.unsupported_reason.empty()) return fn.unsupported_reason;
    if (fn.is_deleted) return "deleted function";
    if (fn.default_arguments > fn.parameters.size()) return "invalid default argument metadata";
    if (!fn.is_constructor && fn.result.canonical != "void" && scalar(fn.result).empty()) return "unsupported Fortran result type " + fn.result.canonical;
    for (const auto& type : fn.parameters) if (scalar(type).empty()) return "unsupported Fortran parameter type " + type.canonical;
    return {};
}
std::string ftype(const Type& type, bool input = false) {
    const auto t = scalar(type);
    if (t == "std::string") return input ? "character(kind=c_char,len=*)" : "character(kind=c_char,len=:), allocatable";
    if (t == "bool") return "logical(c_bool)";
    if (t == "float") return "real(c_float)";
    if (t == "double") return "real(c_double)";
    return "integer(c_int64_t)";
}
std::string initial(const Type& type) {
    const auto t = scalar(type);
    if (t == "std::string") return "''";
    if (t == "bool") return ".false._c_bool";
    if (t == "float") return "0.0_c_float";
    if (t == "double") return "0.0_c_double";
    return "0_c_int64_t";
}
std::string decode(const Type& type) {
    const auto t = scalar(type);
    if (t == "std::string") return "rs_text(rs_output%value)";
    if (t == "bool") return "rs_output%value%unsigned /= 0";
    if (t == "float") return "real(rs_output%value%real,c_float)";
    if (t == "double") return "rs_output%value%real";
    return "rs_output%value%integer";
}
// Keep generated free-form sources usable with standard 132-column limits.
std::string wrapped(const std::string& source) {
    std::istringstream lines(source);
    std::ostringstream out;
    std::string line;
    while (std::getline(lines, line)) {
        while (line.size() > 120 && line.front() != '!') {
            char quoted = 0;
            std::size_t split = std::string::npos;
            for (std::size_t i = 0; i < std::min<std::size_t>(line.size(), 116); ++i) {
                if (quoted) { if (line[i] == quoted) quoted = 0; }
                else if (line[i] == '\'' || line[i] == '"') quoted = line[i];
                else if (line[i] == ' ' || line[i] == ',') split = i + 1;
            }
            if (split == std::string::npos || split < 5) throw BindingError("cannot wrap generated Fortran source line");
            out << line.substr(0, split) << " &\n";
            line = "  & " + line.substr(split);
        }
        out << line << '\n';
    }
    return out.str();
}
} // namespace

GeneratedFortranSource generate_fortran(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy) {
    if (!identifier(name)) throw BindingError("Fortran module name must be an ASCII identifier of at most 63 characters starting with a letter");
    if (!policy.callbacks.empty() || !policy.enum_flags.empty()) throw BindingError("Fortran backend does not yet support callback or enum flag policies");
    GeneratedFortranSource result{{}, {}, module.diagnostics};
    auto reject = [&](const std::string& context, const std::string& reason) { result.diagnostics.push_back(context + ": " + reason); };
    // These imports/intrinsics are used by emitted wrappers and runtime code.
    const std::set<std::string> reserved{"rosetta_error", "rosetta_shutdown", "int", "real", "len", "huge", "present", "allocated"};
    auto invalid = [&](const std::string& key) {
        const auto small = lower(key);
        return !identifier(key) || reserved.count(small) || small.rfind("rs_", 0) == 0 || small.rfind("c_", 0) == 0;
    };
    if (invalid(name)) throw BindingError("reserved Fortran module name: " + name);
    std::set<std::string> exports{lower(name)};
    auto reserve = [&](const std::string& key) {
        if (invalid(key)) throw BindingError("invalid or reserved Fortran export: " + key);
        if (!exports.insert(lower(key)).second) throw BindingError("Fortran export name collision (case insensitive): " + key);
    };
    std::ostringstream out, dispatch, declarations, procedures;
    out << "// Generated by Rosetta: private C ABI for Fortran ISO_C_BINDING.\n"
        << "#include <cmath>\n#include <cstdint>\n#include <cstdlib>\n#include <cstring>\n#include <limits>\n#include <memory>\n#include <mutex>\n"
        << "#include <new>\n#include <stdexcept>\n#include <string>\n#include <type_traits>\n#include <unordered_map>\n#include <vector>\n"
        << "#include " << quote(module.header) << '\n' << detail::cabi_runtime
        << "// Fortran's interoperable integer kinds describe signed C integers.\n"
        << "struct rosetta_fortran_Value { std::int64_t tag, integer, unsigned_integer; double real; const char* data; std::size_t size; };\n"
        << "struct rosetta_fortran_Result { rosetta_fortran_Value value; std::int64_t error; };\n"
        << "template<class T> static T rosetta_fortran_from(rosetta_abi::Value value) {\n"
        << "if constexpr (std::is_same_v<T, std::string>) { if (!value.size) value.data = \"\"; }\n"
        << "return rosetta_abi::from<T>(value);\n}\n"
        << "template<class T> static rosetta_abi::Value rosetta_fortran_to(const T& value) {\n"
        << "if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {\n"
        << "auto raw = rosetta_abi::to(value); return rosetta_abi::to(rosetta_abi::from<std::int64_t>(raw));\n"
        << "} else return rosetta_abi::to(value);\n}\n";
    std::string runtime = detail::fortran_declarations;
    for (std::size_t p = 0; (p = runtime.find("MODULE", p)) != std::string::npos; p += name.size()) runtime.replace(p, 6, name);
    declarations << "! Generated by Rosetta. Explicit close; assignment shares a native handle.\nmodule " << name
        << "\nuse, intrinsic :: iso_c_binding\nimplicit none\nprivate\n" << runtime;
    auto entry = [&](const std::string& key, const std::string& code) { dispatch << "if (operation == " << quote(key) << ") {\n" << code << "}\n"; };
    auto body = [&](const Function& fn, const std::string& target, const std::string& owner) {
        std::ostringstream code;
        code << "rosetta_abi::arity(count, " << fn.parameters.size() - fn.default_arguments << ", " << fn.parameters.size() << ");\nswitch (count) {\n";
        for (std::size_t count = fn.parameters.size() - fn.default_arguments; count <= fn.parameters.size(); ++count) {
            code << "case " << count << ": {\n";
            std::string actuals;
            for (std::size_t i = 0; i < count; ++i) {
                code << "auto value" << i << " = rosetta_fortran_from<" << scalar(fn.parameters[i]) << ">(args[" << i << "]);\n";
                if (i) actuals += ", ";
                actuals += "value" + std::to_string(i);
            }
            if (fn.is_constructor) {
                code << "if (rosetta_abi::registry().next > static_cast<std::uint64_t>(INT64_MAX)) throw std::overflow_error(\"Fortran owner identifiers exhausted\");\n"
                    << "return rosetta_abi::to(rosetta_abi::own(std::make_unique<::" << owner << ">(" << actuals << ")));\n";
            } else if (fn.result.canonical == "void") code << target << '(' << actuals << "); return {};\n";
            else code << "auto result = " << target << '(' << actuals << "); return rosetta_fortran_to(result);\n";
            code << "}\n";
        }
        code << "}\nthrow rosetta_abi::conversion_error(\"invalid argument dispatch\");\n";
        return code.str();
    };
    auto wrapper = [&](const Function& fn, const std::string& key, const std::string& owner, bool ctor = false) {
        const bool self = !owner.empty() && (ctor || !fn.is_static);
        const bool value = !ctor && fn.result.canonical != "void";
        declarations << "public :: " << key << '\n';
        procedures << (value ? "function " : "subroutine ") << key << '(';
        if (self) procedures << "self, ";
        for (std::size_t i = 0; i < fn.parameters.size(); ++i) procedures << "arg" << i << ", ";
        procedures << "status)" << (value ? " result(output)" : "") << '\n';
        if (self) procedures << "class(" << owner << "), intent(" << (ctor ? "inout" : "in") << ") :: self\n";
        for (std::size_t i = 0; i < fn.parameters.size(); ++i) {
            procedures << ftype(fn.parameters[i], true) << ", intent(in)";
            if (scalar(fn.parameters[i]) == "std::string") procedures << ", target";
            if (i >= fn.parameters.size() - fn.default_arguments) procedures << ", optional";
            procedures << " :: arg" << i << '\n';
        }
        procedures << "type(rosetta_error), intent(out), optional :: status\n";
        if (value) procedures << ftype(fn.result) << " :: output\n";
        procedures << "type(rs_value) :: rs_args(" << std::max<std::size_t>(1, fn.parameters.size()) << ")\n"
            << "type(rs_result) :: rs_output\ninteger :: rs_count\n";
        if (value) procedures << "output = " << initial(fn.result) << '\n';
        procedures << "rs_count = " << fn.parameters.size() - fn.default_arguments << '\n';
        for (std::size_t i = 0; i < fn.parameters.size(); ++i) {
            const bool optional = i >= fn.parameters.size() - fn.default_arguments;
            if (optional) procedures << "if (present(arg" << i << ")) then\nif (rs_count /= " << i << ") then\n"
                << "call rs_error(status, 4_c_int64_t, 'optional native arguments must be consecutive')\nreturn\nend if\nrs_count = rs_count + 1\n";
            const auto t = scalar(fn.parameters[i]);
            const auto item = "rs_args(" + std::to_string(i + 1) + ")";
            if (t == "std::string") procedures << item << "%tag = 5\n" << item << "%size = len(arg" << i << ",kind=c_size_t)\n"
                << "if (len(arg" << i << ",kind=c_size_t) > 0) " << item << "%data = c_loc(arg" << i << "(1:1))\n";
            else if (t == "bool") procedures << item << "%tag = 4\nif (arg" << i << ") " << item << "%unsigned = 1\n";
            else if (t == "float" || t == "double") procedures << item << "%tag = " << (t == "float" ? 6 : 3) << '\n'
                << item << "%real = real(arg" << i << ",c_double)\n";
            else procedures << item << "%tag = 1\n" << item << "%integer = arg" << i << '\n';
            if (optional) procedures << "end if\n";
        }
        procedures << "call rs_invoke('" << key << "', " << (self && !ctor ? "self%rs_id" : "0_c_int64_t") << ", rs_args, rs_count, rs_output)\n";
        if (ctor) procedures << "if (rs_output%error == 0) then\ncall rs_release(self%rs_id)\nself%rs_id = rs_output%value%unsigned\nend if\n";
        else if (value) procedures << "if (rs_output%error == 0) output = " << decode(fn.result) << '\n';
        procedures << "call rs_finish(rs_output, status)\nend " << (value ? "function" : "subroutine") << '\n';
    };
    std::map<std::string, std::size_t> counts;
    for (const auto& fn : module.functions) ++counts[fn.qualified_name()];
    for (const auto& fn : module.functions) {
        auto why = problem(fn);
        if (counts[fn.qualified_name()] > 1) why = "overloads require an explicit facade";
        if (!why.empty()) { reject(fn.qualified_name(), why); continue; }
        const auto key = exported(fn.qualified_name()); reserve(key);
        entry(key, body(fn, "::" + fn.qualified_name(), {})); wrapper(fn, key, {});
    }
    for (const auto& selection : policy.classes)
        if (std::none_of(module.records.begin(), module.records.end(), [&](const Record& r) { return r.qualified_name() == selection.first; }))
            throw BindingError("unknown class in Fortran policy: " + selection.first);
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
            if (found == rec.methods.end()) throw BindingError("unknown Fortran policy method: " + owner + "::" + requested);
            if (found->access != "public") throw BindingError("Fortran policy method is not public: " + owner + "::" + requested);
        }
        for (const auto& requested : config.fields) {
            const auto found = std::find_if(rec.fields.begin(), rec.fields.end(), [&](const Field& f) { return f.name == requested; });
            if (found == rec.fields.end()) throw BindingError("unknown Fortran policy field: " + owner + "::" + requested);
            if (found->access != "public") throw BindingError("Fortran policy field is not public: " + owner + "::" + requested);
        }
        if (!rec.complete || rec.abstract || rec.has_bases || !rec.public_destructor || !rec.unsupported_reason.empty()) {
            reject(owner, "Fortran requires a complete, concrete class without bases and a public nonthrowing destructor"); continue;
        }
        if (!config.borrow_self.empty()) { reject(owner, "borrowed Fortran results are not yet supported"); continue; }
        const auto key = exported(owner); reserve(key);
        out << "static_assert(std::is_nothrow_destructible_v<::" << owner << ">, \"native owners require nonthrowing destruction\");\n";
        std::ostringstream members;
        const auto close = key + "__close"; reserve(close);
        members << "procedure :: close => " << close << '\n';
        procedures << "subroutine " << close << "(self)\nclass(" << key << "), intent(inout) :: self\n"
            << "call rs_release(self%rs_id)\nself%rs_id = 0\nend subroutine\n";
        declarations << "public :: " << close << '\n';
        std::vector<const Function*> ctors;
        for (const auto& ctor : rec.constructors) if (!ctor.is_deleted) ctors.push_back(&ctor);
        if (config.constructors) {
            if (ctors.size() != 1) reject(owner, "constructor overloads require an explicit facade");
            else {
                const auto& ctor = *ctors.front(); auto why = problem(ctor);
                if (ctor.access != "public") why = "constructor is not public";
                if (!why.empty()) reject(owner, why);
                else {
                    const auto symbol = key + "__init"; reserve(symbol);
                    members << "procedure :: init => " << symbol << '\n';
                    entry(symbol, body(ctor, {}, owner)); wrapper(ctor, symbol, key, true);
                }
            }
        }
        std::set<std::string> member_names{"close", "init", "rs_id"};
        auto member = [&](const std::string& text) {
            if (!identifier(text) || lower(text).rfind("rs_", 0) == 0 || !member_names.insert(lower(text)).second)
                throw BindingError("reserved or duplicate Fortran member (case insensitive): " + text);
        };
        counts.clear(); for (const auto& fn : rec.methods) ++counts[fn.name];
        for (const auto& fn : rec.methods) {
            if (fn.access != "public" || (config.restrict_methods && !config.methods.count(fn.name))) continue;
            auto why = problem(fn);
            if (counts[fn.name] > 1) why = "method overloads require an explicit facade";
            if (!why.empty()) { reject(owner + "::" + fn.name, why); continue; }
            member(fn.name);
            const auto symbol = key + "__" + fn.name; reserve(symbol);
            members << "procedure" << (fn.is_static ? ", nopass" : "") << " :: " << fn.name << " => " << symbol << '\n';
            const auto target = fn.is_static ? "::" + owner + "::" + fn.name : "rosetta_abi::object<::" + owner + ">(id)->" + fn.name;
            entry(symbol, body(fn, target, {})); wrapper(fn, symbol, key);
        }
        for (const auto& field : rec.fields) {
            if (field.access != "public" || (config.restrict_fields && !config.fields.count(field.name))) continue;
            if (scalar(field.type).empty() || field.bitfield || field.type.is_volatile) { reject(owner + "::" + field.name, "unsupported Fortran field type"); continue; }
            const auto get = "get_" + field.name, set = "set_" + field.name;
            member(get);
            const auto getter = key + "__" + get; reserve(getter);
            members << "procedure :: " << get << " => " << getter << '\n';
            entry(getter, "rosetta_abi::arity(count, 0, 0); return rosetta_fortran_to(rosetta_abi::object<::" + owner + ">(id)->" + field.name + ");\n");
            Function accessor; accessor.result = field.type; wrapper(accessor, getter, key);
            if (!field.type.is_const) {
                member(set);
                const auto setter = key + "__" + set; reserve(setter);
                members << "procedure :: " << set << " => " << setter << '\n';
                const auto conversion = "rosetta_fortran_from<" + scalar(field.type) + ">(args[0])";
                entry(setter, "rosetta_abi::arity(count, 1, 1); auto value = " + conversion + ";\nrosetta_abi::object<::" + owner + ">(id)->" + field.name + " = value; return {};\n");
                accessor.result = {"void", "void"}; accessor.parameters = {field.type}; wrapper(accessor, setter, key);
            }
        }
        declarations << "type, public :: " << key << "\nprivate\ninteger(c_int64_t) :: rs_id = 0\ncontains\n" << members.str() << "end type\n";
    }
    for (const auto& e : module.enumerations) if (e.selected) reject(e.qualified_name(), "Fortran enum bindings are not yet supported");
    if (!skip && !result.diagnostics.empty()) {
        std::string error = "unsupported declaration for Fortran";
        for (const auto& d : result.diagnostics) error += "\n  " + d;
        throw BindingError(error);
    }
    out << "extern \"C\" {\nstd::int64_t rosetta_" << name << "_abi() noexcept { return 1; }\n"
        << "void rosetta_" << name << "_free(const char* bytes) noexcept { std::free(const_cast<char*>(bytes)); }\n"
        << "void rosetta_" << name << "_shutdown() noexcept { try { std::lock_guard<std::mutex> lock(rosetta_abi::registry().mutex); rosetta_abi::registry().owners.clear(); } catch (...) {} }\n"
        << "void rosetta_" << name << "_release(std::int64_t id) noexcept { try { std::lock_guard<std::mutex> lock(rosetta_abi::registry().mutex); rosetta_abi::registry().owners.erase(static_cast<std::uint64_t>(id)); } catch (...) {} }\n"
        << "void rosetta_" << name << "_call(const char* op, std::int64_t id, const rosetta_fortran_Value* input, std::size_t count, rosetta_fortran_Result* result) noexcept {\n"
        << "rosetta_abi::Result raw{};\nrosetta_abi::guard(&raw, [&]() -> rosetta_abi::Value {\n"
        << "std::vector<rosetta_abi::Value> packed; packed.reserve(count);\n"
        << "for (std::size_t i = 0; i < count; ++i) { const auto& v = input[i];\n"
        << "packed.push_back({static_cast<std::uint64_t>(v.tag), v.integer, static_cast<std::uint64_t>(v.unsigned_integer), v.real, v.data, v.size}); }\n"
        << "const auto* args = packed.data();\n(void)id; (void)args; (void)count;\nconst std::string operation(op);\n"
        << dispatch.str() << "throw rosetta_abi::conversion_error(\"unknown native operation\");\n});\n"
        << "const auto& v = raw.value;\n*result = {{static_cast<std::int64_t>(v.tag), v.integer, static_cast<std::int64_t>(v.unsigned_integer), v.real, v.data, v.size}, static_cast<std::int64_t>(raw.error)};\n}\n}\n";
    result.source = out.str();
    result.interface = wrapped(declarations.str() + "contains\n" + detail::fortran_procedures + procedures.str() + "end module " + name + "\n");
    return result;
}
} // namespace rosetta
