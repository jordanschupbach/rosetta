#include "rosetta/apl.hpp"
#include "apl_runtime.hpp"
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
// An APL character vector literal.
std::string apl_quote(const std::string& text) {
    std::string result = "'";
    for (char c : text) result += c == '\'' ? std::string("''") : std::string(1, c);
    return result + "'";
}
bool letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool digit(char c) { return c >= '0' && c <= '9'; }
bool identifier(const std::string& text, bool underscore_first) {
    return !text.empty() && (letter(text.front()) || (underscore_first && text.front() == '_')) &&
        std::all_of(text.begin(), text.end(), [](char c) { return letter(c) || digit(c) || c == '_'; });
}
std::string replace_all(std::string text, const std::string& from, const std::string& to) {
    for (std::size_t i = 0; (i = text.find(from, i)) != std::string::npos; i += to.size()) text.replace(i, from.size(), to);
    return text;
}
// A supported C++ scalar and its APL code: i signed, u unsigned, f real,
// b boolean, c character, s string.
struct Scalar { std::string cpp; char code; };
std::optional<Scalar> scalar(const Type& type) {
    auto text = detail::trim_type(type.canonical);
    const bool reference = text.rfind("const ", 0) == 0 && !text.empty() && text.back() == '&' &&
        (text.size() < 2 || text[text.size() - 2] != '&');
    if (reference) text = detail::trim_type(text.substr(0, text.size() - 1));
    if (text.rfind("const ", 0) == 0) text = text.substr(6);
    if (text == "std::__cxx11::basic_string<char>" || text == "std::__1::basic_string<char>" || text == "std::basic_string<char>")
        text = "std::string";
    if (reference && text != "std::string") return std::nullopt;
    static const std::map<std::string, char> table = {{"bool", 'b'}, {"char", 'c'}, {"signed char", 'i'}, {"unsigned char", 'u'},
        {"short", 'i'}, {"unsigned short", 'u'}, {"int", 'i'}, {"unsigned int", 'u'}, {"long", 'i'}, {"unsigned long", 'u'},
        {"long long", 'i'}, {"unsigned long long", 'u'}, {"float", 'f'}, {"double", 'f'}, {"std::string", 's'}};
    const auto found = table.find(text);
    if (found == table.end()) return std::nullopt;
    return Scalar{text, found->second};
}
// The C ABI type carrying a code: as a result, a string is its size.
std::string abi(char code) {
    if (code == 'u' || code == 'h' || code == 's') return "std::uint64_t";
    if (code == 'f') return "double";
    return "std::int64_t";
}
// The ⎕NA type of an argument and of a result.
std::string na_argument(char code) {
    if (code == 's') return "<U1[] U8";
    return code == 'u' || code == 'h' ? "U8" : code == 'f' ? "F8" : "I8";
}
std::string na_result(char code) { return code == 'u' || code == 'h' || code == 's' ? ">U8" : code == 'f' ? ">F8" : ">I8"; }
std::string problem(const Function& fn) {
    if (!fn.unsupported_reason.empty()) return fn.unsupported_reason;
    if (fn.is_deleted) return "deleted function";
    if (fn.default_arguments > fn.parameters.size()) return "invalid default argument metadata";
    if (!fn.is_constructor && fn.result.canonical != "void" && !scalar(fn.result)) return "unsupported APL result type " + fn.result.canonical;
    for (const auto& type : fn.parameters)
        if (!scalar(type)) return "unsupported APL parameter type " + type.canonical;
    return {};
}
std::string join(const std::vector<std::string>& parts, const std::string& separator = ", ") {
    std::string text;
    for (const auto& part : parts) text += (text.empty() ? "" : separator) + part;
    return text;
}
enum class Role { function, method, constructor, getter, setter, close };
// One binding: a C ABI stub, a GNU APL bridge entry and both APL wrappers.
struct Entry {
    std::size_t index = 0;
    std::string stub, label, signature;
    Role role = Role::function;
    std::vector<Scalar> params;
    std::size_t required = 0;
    char result = 0; // 0 for none, a Scalar code, or h for a new object's handle
    std::string call;   // the C++ expression, with @ARGS@ for the arguments
    std::string native; // the C++ type of a method's object
    std::string type;   // its name in messages
    std::string gnu;    // the GNU APL function
    std::string member; // the Dyalog function, method or property
    bool self() const { return role == Role::method || role == Role::getter || role == Role::setter || role == Role::close; }
};
struct Class { std::string name, label; std::vector<std::size_t> entries; };
} // namespace

GeneratedAplSource generate_apl(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy,
                                const std::string& library_dir) {
    if (!identifier(name, false)) throw BindingError("APL module name must be a simple ASCII identifier starting with a letter");
    if (library_dir.empty()) throw BindingError("APL library directory must not be empty");
    if (!policy.callbacks.empty() || !policy.enum_flags.empty())
        throw BindingError("APL backend does not yet support callback or enum flag policies");
    GeneratedAplSource result{{}, {}, {}, module.diagnostics};
    auto reject = [&](const std::string& context, const std::string& reason) { result.diagnostics.push_back(context + ": " + reason); };
    // Namespace-level names (functions and classes) and each class's members,
    // shared by both front ends.
    std::set<std::string> taken = {"last_error"};
    auto reserve = [](std::set<std::string>& names, const std::string& symbol) {
        if (!names.insert(symbol).second) throw BindingError("APL name collision: " + symbol);
    };
    std::vector<Entry> entries;
    std::vector<Class> classes;
    auto add = [&](Entry entry, const Function* fn) {
        entry.index = entries.size() + 1;
        entry.stub = "rosetta_" + name + "_" + std::to_string(entry.index);
        if (fn) {
            for (const auto& type : fn->parameters) entry.params.push_back(*scalar(type));
            entry.required = fn->parameters.size() - fn->default_arguments;
            std::vector<std::string> parameters;
            for (std::size_t i = 0; i < fn->parameters.size(); ++i) {
                auto text = fn->parameters[i].spelling;
                if (i < fn->parameter_names.size() && !fn->parameter_names[i].empty()) text += " " + fn->parameter_names[i];
                parameters.push_back(text);
            }
            entry.signature = entry.label + "(" + join(parameters) + ")";
            if (entry.role != Role::constructor && fn->result.canonical != "void") entry.result = scalar(fn->result)->code;
        }
        entries.push_back(std::move(entry));
        return entries.size() - 1;
    };

    std::map<std::string, std::size_t> counts;
    for (const auto& fn : module.functions) ++counts[fn.qualified_name()];
    for (const auto& fn : module.functions) {
        auto why = problem(fn);
        if (counts[fn.qualified_name()] > 1) why = "overloads require an explicit facade";
        if (why.empty() && !identifier(fn.name, true)) why = "name cannot be an APL identifier";
        if (!why.empty()) { reject(fn.qualified_name(), why); continue; }
        reserve(taken, fn.name);
        Entry entry;
        entry.label = fn.qualified_name();
        entry.call = "::" + fn.qualified_name() + "(@ARGS@)";
        entry.gnu = name + "∆" + fn.name;
        entry.member = fn.name;
        add(std::move(entry), &fn);
    }
    for (const auto& selection : policy.classes)
        if (std::none_of(module.records.begin(), module.records.end(), [&](const Record& r) { return r.qualified_name() == selection.first; }))
            throw BindingError("unknown class in APL policy: " + selection.first);
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
            if (found == rec.methods.end()) throw BindingError("unknown APL policy method: " + owner + "::" + requested);
            if (found->access != "public") throw BindingError("APL policy method is not public: " + owner + "::" + requested);
        }
        for (const auto& requested : config.fields) {
            const auto found = std::find_if(rec.fields.begin(), rec.fields.end(), [&](const Field& f) { return f.name == requested; });
            if (found == rec.fields.end()) throw BindingError("unknown APL policy field: " + owner + "::" + requested);
            if (found->access != "public") throw BindingError("APL policy field is not public: " + owner + "::" + requested);
        }
        if (!rec.complete || rec.abstract || rec.has_bases || !rec.public_destructor || !rec.unsupported_reason.empty()) {
            reject(owner, "APL requires a complete, concrete class without bases and a public nonthrowing destructor"); continue;
        }
        if (!config.borrow_self.empty()) { reject(owner, "borrowed APL results are not yet supported"); continue; }
        if (!identifier(rec.name, true)) { reject(owner, "name cannot be an APL identifier"); continue; }
        reserve(taken, rec.name);
        const auto native = "::" + owner;
        const auto prefix = name + "∆" + rec.name + "∆";
        Class klass{rec.name, owner, {}};
        std::set<std::string> members = {"new", "close"};
        auto member = [&](Entry entry, const Function* fn) {
            entry.native = native;
            entry.type = owner;
            klass.entries.push_back(add(std::move(entry), fn));
        };
        {
            Entry close;
            close.role = Role::close;
            close.label = owner + "::~" + rec.name;
            close.signature = close.label + "()";
            close.gnu = prefix + "close";
            close.member = "close";
            member(std::move(close), nullptr);
        }
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
                    Entry entry;
                    entry.role = Role::constructor;
                    entry.result = 'h';
                    entry.label = owner + "::" + rec.name;
                    entry.call = "std::make_unique<" + native + ">(@ARGS@)";
                    entry.gnu = prefix + "new";
                    member(std::move(entry), &ctor);
                }
            }
        }
        counts.clear();
        for (const auto& fn : rec.methods) ++counts[fn.name];
        for (const auto& fn : rec.methods) {
            if (fn.access != "public" || (config.restrict_methods && !config.methods.count(fn.name))) continue;
            auto why = problem(fn);
            if (counts[fn.name] > 1) why = "method overloads require an explicit facade";
            if (why.empty() && !identifier(fn.name, true)) why = "name cannot be an APL identifier";
            if (!why.empty()) { reject(owner + "::" + fn.name, why); continue; }
            reserve(members, fn.name);
            Entry entry;
            entry.role = fn.is_static ? Role::function : Role::method;
            entry.label = owner + "::" + fn.name;
            entry.call = (fn.is_static ? native + "::" : std::string("object.")) + fn.name + "(@ARGS@)";
            entry.gnu = prefix + fn.name;
            entry.member = fn.name;
            member(std::move(entry), &fn);
        }
        for (const auto& field : rec.fields) {
            if (field.access != "public" || (config.restrict_fields && !config.fields.count(field.name))) continue;
            const auto value = scalar(field.type);
            if (!value || field.bitfield || field.type.is_volatile || !identifier(field.name, true)) {
                reject(owner + "::" + field.name, "unsupported APL field type"); continue;
            }
            reserve(members, field.name);
            Entry getter;
            getter.role = Role::getter;
            getter.label = owner + "::" + field.name;
            getter.signature = field.type.spelling + " " + getter.label;
            getter.result = value->code;
            getter.call = "object." + field.name;
            getter.gnu = prefix + field.name;
            getter.member = field.name;
            member(getter, nullptr);
            // Const fields have no setter.
            if (field.type.is_const) continue;
            reserve(members, "set_" + field.name);
            Entry setter = getter;
            setter.role = Role::setter;
            setter.result = 0;
            setter.params = {*value};
            setter.required = 1;
            setter.call = "object." + field.name + " = @ARGS@";
            setter.gnu = prefix + "set_" + field.name;
            member(std::move(setter), nullptr);
        }
        classes.push_back(std::move(klass));
    }
    for (const auto& enumeration : module.enumerations)
        if (enumeration.selected) reject(enumeration.qualified_name(), "APL enum bindings are not yet supported");
    if (!skip && !result.diagnostics.empty()) {
        std::string error = "unsupported declaration for APL";
        for (const auto& entry : result.diagnostics) error += "\n  " + entry;
        throw BindingError(error);
    }

    // The C ABI: [object handle], each parameter (a string as bytes and a
    // size), [the number of given arguments], [the result]; returns the status.
    std::ostringstream out;
    out << "// Generated by Rosetta: C++ for the APL bindings " << name << ". Built alone, its C ABI serves\n"
        << "// Dyalog's ⎕NA; built with -DROSETTA_GNU_APL and GNU APL's configured source tree on\n"
        << "// the include path, it is also the GNU APL native function.\n"
        << "#include <cmath>\n#include <cstddef>\n#include <cstdint>\n#include <cstdlib>\n#include <cstring>\n#include <limits>\n"
        << "#include <map>\n#include <memory>\n#include <mutex>\n#include <new>\n#include <stdexcept>\n#include <string>\n"
        << "#include <type_traits>\n#include <utility>\n#include <vector>\n#include " << quote(module.header) << '\n' << detail::apl_runtime
        << "extern \"C\" std::uint64_t rosetta_" << name << "_pending_size() { return rosetta_apl::pending().size(); }\n"
        << "extern \"C\" void rosetta_" << name << "_take(std::uint8_t* out) {\n"
        << "auto& pending = rosetta_apl::pending();\nif (!pending.empty()) std::memcpy(out, pending.data(), pending.size());\npending.clear();\n}\n";
    for (const auto& e : entries) {
        const bool optional = e.required < e.params.size();
        std::vector<std::string> parameters;
        if (e.self()) parameters.push_back("std::uint64_t self");
        for (std::size_t i = 0; i < e.params.size(); ++i) {
            const auto a = "a" + std::to_string(i);
            if (e.params[i].code == 's') { parameters.push_back("const std::uint8_t* " + a); parameters.push_back("std::uint64_t " + a + "_size"); }
            else parameters.push_back(abi(e.params[i].code) + " " + a);
        }
        if (optional) parameters.push_back("std::uint64_t count");
        if (e.result) parameters.push_back(abi(e.result) + "* result");
        out << "// " << (e.signature.empty() ? e.label : e.signature) << "\nextern \"C\" std::int32_t " << e.stub << '(' << join(parameters)
            << ") {\nreturn rosetta_apl::guard([&] {\n";
        if (e.role == Role::close) { out << "rosetta_apl::close<" << e.native << ">(self, " << quote(e.type) << ");\n});\n}\n"; continue; }
        if (e.self()) out << "auto& object = rosetta_apl::live<" << e.native << ">(self, " << quote(e.type) << ");\n";
        auto body = [&](std::size_t present) {
            std::vector<std::string> actuals;
            for (std::size_t i = 0; i < present; ++i) {
                const auto& p = e.params[i];
                const auto a = "a" + std::to_string(i);
                out << "auto value" << i << " = ";
                if (p.code == 's') out << "rosetta_apl::text(" << a << ", " << a << "_size)";
                else if (p.code == 'b') out << "rosetta_apl::boolean(" << a << ')';
                else if (p.code == 'c') out << "rosetta_apl::character(" << a << ')';
                else if (p.code == 'f') out << "static_cast<" << p.cpp << ">(" << a << ')';
                else out << "rosetta_apl::integer<" << p.cpp << ">(" << a << ')';
                out << ";\n";
                actuals.push_back("std::move(value" + std::to_string(i) + ")");
            }
            const auto invocation = replace_all(e.call, "@ARGS@", join(actuals));
            switch (e.result) {
            case 0: out << invocation << ";\n"; break;
            case 'h': out << "*result = rosetta_apl::adopt(" << invocation << ");\n"; break;
            case 'u': out << "*result = static_cast<std::uint64_t>(" << invocation << ");\n"; break;
            case 'b': out << "*result = (" << invocation << ") ? 1 : 0;\n"; break;
            case 'c': out << "*result = static_cast<unsigned char>(" << invocation << ");\n"; break;
            case 'f': out << "*result = rosetta_apl::real(static_cast<double>(" << invocation << "));\n"; break;
            case 's': out << "*result = rosetta_apl::result_text(" << invocation << ");\n"; break;
            default: out << "*result = static_cast<std::int64_t>(" << invocation << ");\n";
            }
        };
        if (!optional) body(e.params.size());
        else {
            out << "switch (count) {\n";
            for (std::size_t present = e.required; present <= e.params.size(); ++present) {
                out << "case " << present << ": {\n";
                body(present);
                out << "return;\n}\n";
            }
            out << "}\nrosetta_apl::fail(rosetta_apl::argument, \"invalid optional argument count\");\n";
        }
        out << "});\n}\n";
    }

    // The GNU APL bridge: one entry per stub, then the native function.
    out << "#ifdef ROSETTA_GNU_APL\n" << replace_all(detail::gnu_apl_runtime, "@MODULE@", name);
    for (const auto& e : entries) {
        const bool optional = e.required < e.params.size();
        out << "static Token rosetta_entry_" << e.index << "(const Value& A, const Value& B) {\n"
            << "return rosetta_gnu::call(" << quote(e.label) << ", [&](Value_P& result) -> std::int32_t {\n";
        std::vector<std::string> actuals;
        if (e.self()) { out << "const auto self = rosetta_gnu::to_handle(A);\n"; actuals.push_back("self"); }
        else out << "(void)A;\n";
        if (e.params.empty()) out << "(void)B;\n";
        else out << "const auto args = rosetta_gnu::arguments(B, " << e.required << ", " << e.params.size() << ", "
                 << (e.params.front().code == 's' ? "true" : "false") << ");\n";
        for (std::size_t i = 0; i < e.params.size(); ++i) {
            const auto code = e.params[i].code;
            const auto a = "a" + std::to_string(i);
            const auto given = "args.size() > " + std::to_string(i);
            if (code == 's') {
                out << "const std::string " << a << " = " << given << " ? rosetta_gnu::to_text(args[" << i << "]) : std::string();\n";
                actuals.push_back("reinterpret_cast<const std::uint8_t*>(" + a + ".data())");
                actuals.push_back(a + ".size()");
                continue;
            }
            const auto reader = code == 'u' ? "to_unsigned" : code == 'f' ? "to_real" : code == 'b' ? "to_boolean" : code == 'c' ? "to_character" : "to_signed";
            out << "const " << abi(code) << ' ' << a << " = " << given << " ? rosetta_gnu::" << reader << "(args[" << i << "]) : "
                << abi(code) << "{};\n";
            actuals.push_back(a);
        }
        if (optional) actuals.push_back("args.size()");
        if (e.result) { out << abi(e.result) << " out{};\n"; actuals.push_back("&out"); }
        out << "if (const std::int32_t code = " << e.stub << '(' << join(actuals) << ")) return code;\n";
        switch (e.result) {
        case 0: out << "(void)result;\n"; break;
        case 'u': case 'h': out << "result = rosetta_gnu::natural(out);\n"; break;
        case 'f': out << "result = rosetta_gnu::real(out);\n"; break;
        case 'c': out << "result = rosetta_gnu::character(out);\n"; break;
        case 's': out << "result = rosetta_gnu::text(rosetta_gnu::take());\n"; break;
        default: out << "result = rosetta_gnu::integer(out);\n";
        }
        out << "return 0;\n});\n}\n";
    }
    out << "using rosetta_entry = Token (*)(const Value&, const Value&);\nstatic const rosetta_entry rosetta_entries[] = {";
    for (const auto& e : entries) out << "&rosetta_entry_" << e.index << ", ";
    out << "nullptr};\nstatic Fun_signature rosetta_signature() { return SIG_Z_A_F2_B; }\n"
        << "// A is the entry number (0 for the latest failure), followed by the object\n// handle for methods; B holds the arguments.\n"
        << "static Token rosetta_eval_AB(Value_P A, Value_P B, const NativeFunction*) {\n"
        << "const auto index = A->element_count() >= 1 && A->get_cfirst().is_integer_cell() ? A->get_cfirst().get_int_value() : -1;\n"
        << "if (index == 0) return rosetta_gnu::last_error();\n"
        << "if (index < 1 || index > " << entries.size() << ")\n"
        << "return rosetta_gnu::call(" << quote(name + "⍙native") << ", [](Value_P&) -> std::int32_t { rosetta_apl::fail(rosetta_apl::argument, \"unknown entry\"); });\n"
        << "return rosetta_entries[index - 1](*A, *B);\n}\n"
        << "// GNU APL removes the native function when it clears the workspace,\n// which drops every handle; the library stays loaded.\n"
        << "static bool rosetta_close(Cause, const NativeFunction*) { rosetta_apl::destroy_all(); return false; }\n"
        << "extern \"C\" void* get_function_mux(const char* function) {\n"
        << "if (!std::strcmp(function, \"get_signature\")) return reinterpret_cast<void*>(&rosetta_signature);\n"
        << "if (!std::strcmp(function, \"eval_AB\")) return reinterpret_cast<void*>(&rosetta_eval_AB);\n"
        << "if (!std::strcmp(function, \"close_fun\")) return reinterpret_cast<void*>(&rosetta_close);\n"
        << "return nullptr;\n}\n#endif\n";
    result.source = out.str();

    // GNU APL: prefixed functions calling the native function NAME⍙native.
    const auto directory = library_dir.back() == '/' ? library_dir.substr(0, library_dir.size() - 1) : library_dir;
    std::ostringstream gnu;
    gnu << "#!/usr/bin/env apl --script\n⍝ Generated by Rosetta: GNU APL bindings for " << name << ". Load them with )COPY.\n"
        << "⍝ A function's right argument is its argument, or a vector of its arguments\n"
        << "⍝ (nest strings; ⍬ omits optional ones). Methods take the object on the left,\n"
        << "⍝ or on the right when they have no parameters. Failures signal ⎕ET 501-506\n"
        << "⍝ (allocation range index argument native closed); " << name << "∆last_error\n"
        << "⍝ returns the kind and message of the latest one.\n\n"
        << "∇" << name << "⍙load;Z\n →(3=⎕NC '" << name << "⍙native')/0\n Z←" << apl_quote(directory + "/lib" + name + "_gnu_apl.so")
        << " ⎕FX '" << name << "⍙native'\n∇\n" << name << "⍙load\n\n"
        << "∇Z←" << name << "∆last_error;⍙M;⍙E\n ⍝ The kind and message of the latest failure\n (⍙M ⍙E Z)←0 " << name << "⍙native ⍬\n∇\n";
    for (const auto& e : entries) {
        const bool niladic = e.params.empty() && !e.self(), dyadic = e.self() && !e.params.empty();
        const bool monadic_self = e.self() && e.params.empty();
        gnu << "\n∇" << (e.result ? "Z←" : "") << (dyadic ? "A " : "") << e.gnu << (niladic ? "" : " B") << ";⍙M;⍙E" << (e.result ? "" : ";Z")
            << "\n ⍝ " << (e.role == Role::close ? "Destroys the " + e.type : e.role == Role::setter ? "Sets " + e.label : e.signature) << "\n (⍙M ⍙E Z)←"
            << (e.self() ? "(" + std::to_string(e.index) + (monadic_self ? ",B)" : ",A)") : std::to_string(e.index)) << ' ' << name
            << "⍙native " << (niladic || monadic_self ? "⍬" : "B") << "\n ⍙M ⎕ES ⍙E\n∇\n";
    }
    result.gnu = gnu.str();

    // Dyalog: a namespace of functions and classes calling ⎕NA functions ⍙fN.
    std::ostringstream dyalog;
    dyalog << ":Namespace " << name << "\n"
           << "⍝ Generated by Rosetta: Dyalog APL bindings for " << name << ", calling C++ through ⎕NA.\n"
           << "⍝ Load them with ⎕FIX 'file://" << name << ".apln'. A function's argument is its argument, or a\n"
           << "⍝ vector of its arguments (nest strings; ⍬ omits optional ones). Classes are\n"
           << "⍝ created with ⎕NEW; an object is destroyed with close or when its last\n"
           << "⍝ reference goes. Failures signal EN 501-506 (allocation range index argument\n"
           << "⍝ native closed); last_error returns the kind and message of the latest one.\n"
           << "    ⎕IO ⎕ML←1 1\n    ⍙L←" << apl_quote(directory + "/lib" + name + "_apl.so") << "\n"
           << "    ⍙Kinds←'allocation' 'range' 'index' 'argument' 'native' 'closed'\n"
           << "    ⍙Events←'ALLOCATION ERROR' 'RANGE ERROR' 'INDEX ERROR' 'ARGUMENT ERROR' 'NATIVE ERROR' 'CLOSED ERROR'\n"
           << "    ⍙last←'' ''\n"
           << "    '⍙size'⎕NA'U8 ',⍙L,'|rosetta_" << name << "_pending_size'\n"
           << "    '⍙take'⎕NA ⍙L,'|rosetta_" << name << "_take >U1[]'\n";
    for (const auto& e : entries) {
        std::vector<std::string> types;
        if (e.self()) types.push_back("U8");
        for (const auto& p : e.params) types.push_back(na_argument(p.code));
        if (e.required < e.params.size()) types.push_back("U8");
        if (e.result) types.push_back(na_result(e.result));
        dyalog << "    '⍙f" << e.index << "'⎕NA'I4 ',⍙L,'|" << e.stub << (types.empty() ? "" : " " + join(types, " ")) << "'\n";
    }
    dyalog << replace_all(R"apl(
    ∇ r←last_error
    ⍝ The kind and message of the latest failure
      r←⍙last
    ∇

    ⍝ Each wrapper passes ⍙Call a spec (C++ name, minimum and maximum argument
    ⍝ counts, parameter codes, result code, ⎕NA function, object handle) and
    ⍝ its argument; ⍙Call returns the error to signal, or ⍬, and the result.
    ∇ r←spec ⍙Call args;label;min;max;params;result;stub;self;values;abi;i;out;code
      (label min max params result stub self)←spec
      params←,params
      result←,result
      :Trap 502 504
          values←min max(⊃params,' ')⍙Args args
          abi←⍬
          :If 0≠≢self
              abi,←⊂self
          :EndIf
          :For i :In ⍳≢values
              abi,←(i⊃params)⍙Encode i⊃values
          :EndFor
          :If min≠max
              abi,←⊂≢values
          :EndIf
          :If 0≠≢result
              abi,←⊂0
          :EndIf
      :Else
          r←(⊂label ⍙Fail(⎕DMX.EN-500)⎕DMX.Message),⊂⍬
          :Return
      :EndTrap
      :Trap 11
          :If 0=≢abi
              out←⍎stub
          :ElseIf 1=≢abi
              out←⍎stub,' ⊃abi'
          :Else
              out←⍎stub,' abi'
          :EndIf
      :Else
          r←(⊂label ⍙Fail 4 ⎕DMX.Message),⊂⍬
          :Return
      :EndTrap
      code←⊃out
      :If code≠0
          r←(⊂label ⍙Fail code(⍙Text ⍙size)),⊂⍬
          :Return
      :EndIf
      r←⍬ ⍬
      :If 0≠≢result
          r←⍬((⊃result)⍙Decode 2⊃out)
      :EndIf
    ∇

    ∇ r←label ⍙Fail(code message);kind;text
      kind←code⊃⍙Kinds
      text←'@MODULE@: ',label,': ',message,' (',kind,')'
      ⍙last←kind text
      r←⊂('EN'(500+code))('EM'(code⊃⍙Events))('Message'text)
    ∇

    ⍝ A call's arguments: none, its whole argument for one parameter (⍬ omits
    ⍝ an optional one), or the items of a vector, a scalar for one, or a whole
    ⍝ string when the first parameter is a string.
    ⍙Args←{
        (min max text)←⍺
        0=max:⍬
        omitted←(0=≢⍵)∧(1=≢⍴⍵)∧~⍙IsText ⍵
        1=max:(~omitted∧0=min)/⊂⍵
        1<≢⍴⍵:⍙Bad'arguments must be a scalar or vector'
        list←{0=≢⍴⍵:,⊂⊃⍵ ⋄ ('s'=text)∧⍙IsText ⍵:,⊂⍵ ⋄ ,⍵}⍵
        (min≤≢list)∧max≥≢list:list
        min=max:⍙Bad'expected ',(⍕min),' argument',(min≠1)/'s'
        ⍙Bad'expected ',(⍕min),' to ',(⍕max),' arguments'
    }
    ⍙IsText←{(1≥≢⍴⍵)∧(⊂⎕DR ⍵)∊80 160 320}
    ⍙Bad←{⎕SIGNAL⊂('EN' 504)('Message'⍵)}
    ⍙Expected←{⍵∊'iu':'expected an integer' ⋄ ⍵='b':'expected a boolean (0 or 1)' ⋄ 'expected a real number'}
    ⍙Text←{'UTF-8'⎕UCS ⍙take ⍵}

    ⍝ The ⎕NA arguments for one APL argument of type CODE.
    ∇ r←code ⍙Encode value;x;lo;hi
      :If code='s'
          :If ~⍙IsText value
              ⍙Bad'expected a character vector'
          :EndIf
          x←'UTF-8'⎕UCS,value
          r←(⊂x),⊂≢x
          :Return
      :EndIf
      :If 1≠≢,value
          ⍙Bad ⍙Expected code
      :EndIf
      x←⊃,value
      :If code='c'
          :If ~(⊂⎕DR x)∊80 160 320
              ⍙Bad'expected a character'
          :EndIf
          r←,⊂⎕UCS x
          :Return
      :EndIf
      :If ~(⊂⎕DR x)∊11 83 163 323 645 1287
          ⍙Bad ⍙Expected code
      :EndIf
      r←,⊂x
      :If code='f'
          :Return
      :EndIf
      :If x≠⌊x
          ⍙Bad ⍙Expected code
      :EndIf
      lo hi←(-2*63)(2*63)
      :If code='u'
          lo hi←0(2*64)
      :EndIf
      :If (x<lo)∨x≥hi
          ⎕SIGNAL⊂('EN' 502)('Message' 'integer outside C++ type range')
      :EndIf
    ∇

    ∇ r←code ⍙Decode value
      r←value
      :If code='c'
          r←⎕UCS value
      :ElseIf code='s'
          r←⍙Text value
      :EndIf
    ∇
)apl", "@MODULE@", name);
    // A spec for ⍙Call; SELF is the object handle expression or ⍬.
    auto spec = [&](const Entry& e, const std::string& self) {
        std::string codes;
        for (const auto& p : e.params) codes += p.code;
        return "(" + apl_quote(e.label) + " " + std::to_string(e.required) + " " + std::to_string(e.params.size()) + " " + apl_quote(codes) + " " +
            apl_quote(e.result ? std::string(1, e.result) : std::string()) + " '⍙f" + std::to_string(e.index) + "' " + self + ")";
    };
    // A function, method or static method. Class members reach ⍙Call through ⍙N.
    auto wrapper = [&](const Entry& e, const std::string& header, const std::string& access, const std::string& self, const std::string& indent,
                       const std::string& argument) {
        const bool value = e.result != 0;
        dyalog << '\n' << indent << "∇ " << (value ? "r←" : "") << header << ";e" << (value ? "" : ";r") << '\n';
        if (!access.empty()) dyalog << indent << "  " << access << '\n';
        dyalog << indent << "⍝ " << e.signature << '\n' << indent << "  (e r)←" << spec(e, self) << ' ' << (access.empty() ? "" : "⍙N.")
               << "⍙Call " << argument << '\n' << indent << "  ⎕SIGNAL e\n" << indent << "∇\n";
    };
    for (const auto& e : entries)
        if (e.native.empty())
            wrapper(e, e.member + (e.params.empty() ? "" : " args"), "", "⍬", "    ", e.params.empty() ? "⍬" : "args");
    for (const auto& klass : classes) {
        const std::string in = "        ";
        dyalog << "\n    :Class " << klass.name << "\n    ⍝ Owns a native " << klass.label << ".\n"
               << in << ":Field Private ⍙h←0\n" << in << ":Field Private Shared ⍙N←##\n";
        for (auto index : klass.entries) {
            const auto& e = entries[index];
            const auto argument = e.params.empty() ? std::string("⍬") : std::string("args");
            switch (e.role) {
            case Role::close:
                // Destructors run during teardown too, so they never signal.
                dyalog << '\n' << in << "∇ ⍙Destroy\n" << in << "  :Implements Destructor\n" << in << "  :Trap 0\n" << in << "      {}"
                       << spec(e, "⍙h") << " ⍙N.⍙Call ⍬\n" << in << "  :EndTrap\n" << in << "∇\n";
                wrapper(e, "close", ":Access Public", "⍙h", in, "⍬");
                break;
            case Role::constructor: {
                // Dyalog picks a constructor by valence: niladic when every argument is optional.
                auto make = [&](const std::string& header, const std::string& argument) {
                    dyalog << '\n' << in << "∇ " << header << ";e\n" << in << "  :Access Public\n" << in << "  :Implements Constructor\n"
                           << in << "⍝ " << e.signature << '\n' << in << "  (e ⍙h)←" << spec(e, "⍬") << " ⍙N.⍙Call " << argument << '\n'
                           << in << "  ⎕SIGNAL e\n" << in << "∇\n";
                };
                if (e.required == 0) make("⍙New0", "⍬");
                if (!e.params.empty()) make("⍙New args", "args");
                break;
            }
            case Role::function:
                wrapper(e, e.member + (e.params.empty() ? "" : " args"), ":Access Public Shared", "⍬", in, argument);
                break;
            case Role::method:
                wrapper(e, e.member + (e.params.empty() ? "" : " args"), ":Access Public", "⍙h", in, argument);
                break;
            case Role::getter: {
                const auto setter = std::find_if(klass.entries.begin(), klass.entries.end(), [&](std::size_t other) {
                    return entries[other].role == Role::setter && entries[other].member == e.member;
                });
                dyalog << '\n' << in << ":Property " << e.member << '\n' << in << ":Access Public\n";
                const std::string inner = in + "    ";
                dyalog << inner << "∇ r←get;e\n" << inner << "  (e r)←" << spec(e, "⍙h") << " ⍙N.⍙Call ⍬\n" << inner << "  ⎕SIGNAL e\n" << inner << "∇\n";
                if (setter != klass.entries.end())
                    dyalog << inner << "∇ set value;e;r\n" << inner << "  (e r)←" << spec(entries[*setter], "⍙h") << " ⍙N.⍙Call value.NewValue\n"
                           << inner << "  ⎕SIGNAL e\n" << inner << "∇\n";
                dyalog << in << ":EndProperty\n";
                break;
            }
            case Role::setter: break;
            }
        }
        dyalog << "    :EndClass\n";
    }
    dyalog << ":EndNamespace\n";
    result.dyalog = dyalog.str();
    return result;
}
} // namespace rosetta
