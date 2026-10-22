#include "rosetta/ocaml.hpp"
#include "ocaml_runtime.hpp"
#include "type_spelling.hpp"
#include <algorithm>
#include <map>
#include <memory>
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
bool identifier(const std::string& text) {
    auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    return !text.empty() && letter(text.front()) && std::all_of(text.begin(), text.end(), [&](char c) {
        return letter(c) || (c >= '0' && c <= '9') || c == '_';
    });
}
bool keyword(const std::string& text) {
    static const std::set<std::string> words = {"and", "as", "assert", "asr", "begin", "class", "constraint", "do", "done",
        "downto", "effect", "else", "end", "exception", "external", "false", "for", "fun", "function", "functor", "if", "in",
        "include", "inherit", "initializer", "land", "lazy", "let", "lor", "lsl", "lsr", "lxor", "match", "method", "mod",
        "module", "mutable", "new", "nonrec", "object", "of", "open", "or", "private", "rec", "sig", "struct", "then", "to",
        "true", "try", "type", "val", "virtual", "when", "while", "with"};
    return words.count(text) > 0;
}
// OCaml values and labels start with a lowercase letter or underscore;
// keywords take a trailing underscore, as in `type_`.
std::string value_name(std::string text) {
    if (!text.empty() && text.front() >= 'A' && text.front() <= 'Z') text.front() = static_cast<char>(text.front() - 'A' + 'a');
    return keyword(text) ? text + "_" : text;
}
// Module names start with an uppercase letter; empty when the C++ name cannot be one.
std::string module_name(std::string text) {
    if (text.empty()) return {};
    if (text.front() >= 'a' && text.front() <= 'z') text.front() = static_cast<char>(text.front() - 'a' + 'A');
    return text.front() >= 'A' && text.front() <= 'Z' ? text : std::string{};
}
std::string scalar(const Type& type) {
    auto text = detail::trim_type(type.canonical);
    const bool reference = text.rfind("const ", 0) == 0 && !text.empty() && text.back() == '&' &&
        (text.size() < 2 || text[text.size() - 2] != '&');
    if (reference) text = detail::trim_type(text.substr(0, text.size() - 1));
    if (text.rfind("const ", 0) == 0) text = text.substr(6);
    if (text == "std::__cxx11::basic_string<char>" || text == "std::__1::basic_string<char>" || text == "std::basic_string<char>")
        text = "std::string";
    if (reference && text != "std::string") return {};
    static const std::set<std::string> supported = {"bool", "char", "signed char", "unsigned char", "short", "unsigned short",
        "int", "unsigned int", "long", "unsigned long", "long long", "unsigned long long", "float", "double", "std::string"};
    return supported.count(text) ? text : "";
}
// The OCaml type carrying a C++ scalar, which also names its conversions.
std::string representation(const std::string& cpp) {
    if (cpp == "bool") return "bool";
    if (cpp == "char") return "char";
    if (cpp == "std::string") return "string";
    if (cpp == "float" || cpp == "double") return "float";
    if (cpp == "long" || cpp == "unsigned long" || cpp == "long long" || cpp == "unsigned long long") return "int64";
    return "int";
}
std::string problem(const Function& fn) {
    if (!fn.unsupported_reason.empty()) return fn.unsupported_reason;
    if (fn.is_deleted) return "deleted function";
    if (fn.default_arguments > fn.parameters.size()) return "invalid default argument metadata";
    if (!fn.is_constructor && fn.result.canonical != "void" && scalar(fn.result).empty())
        return "unsupported OCaml result type " + fn.result.canonical;
    for (const auto& type : fn.parameters)
        if (scalar(type).empty()) return "unsupported OCaml parameter type " + type.canonical;
    return {};
}
std::string replace_all(std::string text, const std::string& from, const std::string& to) {
    for (std::size_t i = 0; (i = text.find(from, i)) != std::string::npos; i += to.size()) text.replace(i, from.size(), to);
    return text;
}
// One OCaml module: the root, a namespace or a class.
struct Node {
    std::string signature, structure;
    std::map<std::string, std::unique_ptr<Node>> children;
    std::set<std::string> values;
    bool is_class = false;
};
// Lines are unindented; indent() nests them by their module and end lines.
void emit(const Node& node, std::ostringstream& signature, std::ostringstream& structure) {
    if (node.is_class) { signature << "type t\n"; structure << "type t\n"; }
    signature << node.signature;
    structure << node.structure;
    for (const auto& child : node.children) {
        signature << "module " << child.first << " : sig\n";
        structure << "module " << child.first << " = struct\n";
        emit(*child.second, signature, structure);
        signature << "end\n";
        structure << "end\n";
    }
}
std::string indent(const std::string& text) {
    std::string lines, line;
    std::istringstream input(text);
    std::size_t depth = 1;
    while (std::getline(input, line)) {
        if (line == "end") --depth;
        lines += std::string(2 * depth, ' ') + line + '\n';
        if (line.rfind("module ", 0) == 0) ++depth;
    }
    return lines;
}
enum class Kind { function, method, constructor };
} // namespace

GeneratedOcamlSource generate_ocaml(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy) {
    // Standard library modules, whose names a library module would shadow.
    static const std::set<std::string> reserved_modules = {"stdlib", "arg", "array", "atomic", "bigarray", "bool", "buffer",
        "bytes", "callback", "char", "complex", "condition", "digest", "domain", "dynarray", "effect", "either", "ephemeron",
        "filename", "float", "format", "fun", "gc", "hashtbl", "in_channel", "int", "int32", "int64", "lazy", "lexing", "list",
        "map", "marshal", "mutex", "nativeint", "obj", "oo", "option", "out_channel", "parsing", "pervasives", "printexc",
        "printf", "queue", "random", "result", "scanf", "semaphore", "seq", "set", "stack", "std_exit", "str", "string",
        "sys", "thread", "type", "uchar", "unit", "unix", "weak", "dynlink", "threads", "topfind"};
    if (!identifier(name)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    std::string lowered = name;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    if (reserved_modules.count(lowered) || lowered.rfind("camlinternal", 0) == 0)
        throw BindingError("module name is a reserved OCaml standard library module name");
    if (!policy.callbacks.empty() || !policy.enum_flags.empty())
        throw BindingError("OCaml backend does not yet support callback or enum flag policies");
    GeneratedOcamlSource result{{}, {}, module.diagnostics};
    auto reject = [&](const std::string& context, const std::string& reason) { result.diagnostics.push_back(context + ": " + reason); };
    const auto ocaml_module = module_name(name);
    const auto error_name = "rosetta." + name + ".error";
    std::ostringstream out;
    // The library header comes before OCaml's, whose macros (Field, Val_int, ...) it may not expect.
    out << "// Generated by Rosetta: OCaml stubs for " << name << ".ml.\n"
        << "#include <cmath>\n#include <cstdint>\n#include <cstdlib>\n#include <initializer_list>\n#include <limits>\n#include <memory>\n"
        << "#include <mutex>\n#include <new>\n#include <stdexcept>\n#include <string>\n#include <type_traits>\n#include <unordered_map>\n"
        << "#include <utility>\n#include " << quote(module.header) << "\n#define CAML_NAME_SPACE\n"
        << "#include <caml/alloc.h>\n#include <caml/callback.h>\n#include <caml/custom.h>\n#include <caml/fail.h>\n"
        << "#include <caml/memory.h>\n#include <caml/mlvalues.h>\n#include <caml/signals.h>\n" << replace_all(detail::ocaml_runtime, "@ERROR@", error_name);

    Node root;
    root.values = {"string_of_error_kind", "rosetta_initialize"};
    // The module for a C++ scope, created on demand; null when a scope name
    // cannot be an OCaml module name.
    auto scope = [&](const std::vector<std::string>& namespaces) -> Node* {
        Node* node = &root;
        for (const auto& part : namespaces) {
            const auto key = module_name(part);
            if (key.empty()) return nullptr;
            auto& child = node->children[key];
            if (!child) child = std::make_unique<Node>();
            else if (child->is_class) throw BindingError("OCaml module name collision: " + key);
            node = child.get();
        }
        return node;
    };
    auto reserve = [](Node& node, const std::string& symbol) {
        if (!node.values.insert(symbol).second) throw BindingError("OCaml name collision: " + symbol);
    };
    std::size_t next = 0;
    // Emits SYMBOL into NODE: an external taking the object (for a method),
    // then the parameters, with defaulted parameters as optional labeled
    // arguments. CALL is the C++ call with @ARGS@ for the arguments, run with
    // the runtime released; a constructor's call makes the native value of a
    // new block using the custom operations OPS. CLASS is the C++ class.
    auto primitive = [&](Node& node, const Function& fn, const std::string& symbol, Kind kind, const std::string& call,
                         const std::string& klass = {}, const std::string& ops = {}) {
        const bool constructor = kind == Kind::constructor;
        const auto count = fn.parameters.size();
        const auto required = count - fn.default_arguments;
        const auto stub = "rosetta_" + name + "_" + std::to_string(next++);
        const bool self = kind == Kind::method;
        // Labels for defaulted parameters: the C++ name where it is usable.
        std::vector<std::string> labels;
        std::set<std::string> used;
        for (std::size_t i = required; i < count; ++i) {
            auto label = i < fn.parameter_names.size() ? value_name(fn.parameter_names[i]) : std::string{};
            const bool valid = !label.empty() && label != "_" && std::all_of(label.begin(), label.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
            });
            if (!valid || !used.insert(label).second) label = "arg" + std::to_string(i);
            used.insert(label);
            labels.push_back(label);
        }
        std::vector<std::string> formals;
        std::string external_type, exposed_type, result_type;
        if (constructor) result_type = "t";
        else if (fn.result.canonical == "void") result_type = "unit";
        else result_type = representation(scalar(fn.result));
        for (std::size_t i = required; i < count; ++i)
            exposed_type += "?" + labels[i - required] + ":" + representation(scalar(fn.parameters[i])) + " -> ";
        if (self) { formals.push_back("self"); external_type += "t -> "; exposed_type += "t -> "; }
        for (std::size_t i = 0; i < count; ++i) {
            formals.push_back("a" + std::to_string(i));
            const auto type = representation(scalar(fn.parameters[i]));
            external_type += type + (i < required ? "" : " option") + " -> ";
            if (i < required) exposed_type += type + " -> ";
        }
        const bool unit = formals.empty();
        if (unit) { formals.push_back("unit"); external_type += "unit -> "; }
        if (!self && required == 0) exposed_type += "unit -> ";
        external_type += result_type;
        exposed_type += result_type;

        // The C++ stub; OCaml passes more than five arguments to bytecode stubs as an array.
        std::string signature;
        for (const auto& formal : formals) signature += (signature.empty() ? "value " : ", value ") + formal;
        out << "extern \"C\" value " << stub << '(' << signature << ") {\nCAMLparam0();\n";
        for (std::size_t i = 0; i < formals.size(); i += 5) {
            const auto chunk = std::min<std::size_t>(5, formals.size() - i);
            out << "CAMLxparam" << chunk << '(';
            for (std::size_t k = 0; k < chunk; ++k) out << (k ? ", " : "") << formals[i + k];
            out << ");\n";
        }
        if (unit) out << "(void)unit;\n";
        if (constructor) out << "CAMLlocal1(object);\nobject = rosetta_ocaml::allocate<" << klass << ">(&" << ops << ");\n";
        const bool returns = !constructor && fn.result.canonical != "void";
        const auto result_scalar = returns ? scalar(fn.result) : std::string{};
        out << (returns ? "auto result = " : "") << "rosetta_ocaml::guard([&] {\n";
        // Cells are read before the runtime is released.
        if (self) out << "auto* owned = rosetta_ocaml::cell<" << klass << ">(self);\n";
        else if (constructor) out << "auto* owned = rosetta_ocaml::cell<" << klass << ">(object);\n";
        auto body = [&](std::size_t present) {
            std::string actuals;
            for (std::size_t i = 0; i < present; ++i) {
                const auto type = scalar(fn.parameters[i]);
                const auto argument = i < required ? "a" + std::to_string(i) : "Some_val(a" + std::to_string(i) + ")";
                out << "auto value" << i << " = rosetta_ocaml::from_" << representation(type) << '<' << type << ">(" << argument << ");\n";
                actuals += (i ? ", std::move(value" : "std::move(value") + std::to_string(i) + ")";
            }
            const auto invocation = replace_all(call, "@ARGS@", actuals);
            if (constructor)
                out << "rosetta_ocaml::construct(owned, [&] { return " << invocation << "; });\nreturn;\n";
            else if (!returns) out << "rosetta_ocaml::native([&] { " << invocation << "; });\nreturn;\n";
            else {
                const auto wrapped = "rosetta_ocaml::native([&] { return " + invocation + "; })";
                if (representation(result_scalar) == "float") out << "return rosetta_ocaml::finite(" << wrapped << ");\n";
                else out << "return " << wrapped << ";\n";
            }
        };
        if (fn.default_arguments == 0) body(count);
        else {
            out << "switch (rosetta_ocaml::present({";
            for (std::size_t i = required; i < count; ++i) out << (i > required ? ", a" : "a") << i;
            out << "}, " << required << ", {";
            for (std::size_t i = 0; i < labels.size(); ++i) out << (i ? ", " : "") << quote(labels[i]);
            out << "})) {\n";
            for (std::size_t present = required; present <= count; ++present) {
                out << "case " << present << ": {\n";
                body(present);
                out << "}\n";
            }
            out << "}\nrosetta_ocaml::fail(rosetta_ocaml::kind::native, \"invalid argument dispatch\");\n";
        }
        out << "});\n";
        if (constructor) out << "CAMLreturn(object);\n";
        else if (returns) out << "CAMLreturn(rosetta_ocaml::to_" << representation(result_scalar) << "(result));\n";
        else out << "CAMLreturn(Val_unit);\n";
        out << "}\n";
        std::string names = quote(stub);
        if (formals.size() > 5) {
            out << "extern \"C\" value " << stub << "_byte(value* argv, int argn) {\n(void)argn;\nreturn " << stub << '(';
            for (std::size_t i = 0; i < formals.size(); ++i) out << (i ? ", " : "") << "argv[" << i << ']';
            out << ");\n}\n";
            names = quote(stub + "_byte") + " " + quote(stub);
        }

        reserve(node, symbol);
        node.signature += "val " + symbol + " : " + exposed_type + "\n";
        if (fn.default_arguments == 0) {
            node.structure += "external " + symbol + " : " + external_type + " = " + names + "\n";
            return;
        }
        // Optional arguments come first, so applying the positional ones omits them.
        const auto hidden = "rosetta_external_" + std::to_string(next - 1);
        node.structure += "external " + hidden + " : " + external_type + " = " + names + "\nlet " + symbol;
        std::string arguments;
        for (std::size_t i = required; i < count; ++i) node.structure += " ?" + labels[i - required] + ":o" + std::to_string(i);
        if (self) { node.structure += " self"; arguments += " self"; }
        for (std::size_t i = 0; i < count; ++i) {
            if (i < required) node.structure += " a" + std::to_string(i);
            arguments += (i < required ? " a" : " o") + std::to_string(i);
        }
        if (!self && required == 0) node.structure += " ()";
        node.structure += " = " + hidden + arguments + "\n";
    };

    std::map<std::string, std::size_t> counts;
    for (const auto& fn : module.functions) ++counts[fn.qualified_name()];
    for (const auto& fn : module.functions) {
        auto why = problem(fn);
        if (counts[fn.qualified_name()] > 1) why = "overloads require an explicit facade";
        Node* node = why.empty() ? scope(fn.namespaces) : nullptr;
        if (why.empty() && !node) why = "namespace name cannot be an OCaml module name";
        if (!why.empty()) { reject(fn.qualified_name(), why); continue; }
        primitive(*node, fn, value_name(fn.name), Kind::function, "::" + fn.qualified_name() + "(@ARGS@)");
    }
    for (const auto& selection : policy.classes)
        if (std::none_of(module.records.begin(), module.records.end(), [&](const Record& r) { return r.qualified_name() == selection.first; }))
            throw BindingError("unknown class in OCaml policy: " + selection.first);
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
            if (found == rec.methods.end()) throw BindingError("unknown OCaml policy method: " + owner + "::" + requested);
            if (found->access != "public") throw BindingError("OCaml policy method is not public: " + owner + "::" + requested);
        }
        for (const auto& requested : config.fields) {
            const auto found = std::find_if(rec.fields.begin(), rec.fields.end(), [&](const Field& f) { return f.name == requested; });
            if (found == rec.fields.end()) throw BindingError("unknown OCaml policy field: " + owner + "::" + requested);
            if (found->access != "public") throw BindingError("OCaml policy field is not public: " + owner + "::" + requested);
        }
        if (!rec.complete || rec.abstract || rec.has_bases || !rec.public_destructor || !rec.unsupported_reason.empty()) {
            reject(owner, "OCaml requires a complete, concrete class without bases and a public nonthrowing destructor"); continue;
        }
        if (!config.borrow_self.empty()) { reject(owner, "borrowed OCaml results are not yet supported"); continue; }
        Node* parent = scope(rec.namespaces);
        const auto key = module_name(rec.name);
        if (!parent || key.empty()) { reject(owner, "class or namespace name cannot be an OCaml module name"); continue; }
        auto& slot = parent->children[key];
        if (slot) throw BindingError("OCaml module name collision: " + key);
        slot = std::make_unique<Node>();
        slot->is_class = true;
        Node& node = *slot;
        // Each class is a module with an abstract type t: create, close,
        // methods, field getters and set_ setters.
        const auto native = "::" + owner;
        const auto ops = "rosetta_operations_" + std::to_string(next++);
        out << "static_assert(std::is_nothrow_destructible_v<" << native << ">, \"native owners require nonthrowing destruction\");\n"
            << "static struct custom_operations " << ops << " = {" << quote("rosetta." + name + "." + owner)
            << ", &rosetta_ocaml::finalize<" << native << ">, custom_compare_default, custom_hash_default, custom_serialize_default,"
            << " custom_deserialize_default, custom_compare_ext_default, custom_fixed_length_default};\n";
        std::vector<const Function*> constructors;
        for (const auto& ctor : rec.constructors) if (!ctor.is_deleted) constructors.push_back(&ctor);
        if (config.constructors) {
            if (constructors.size() != 1) reject(owner, "constructor overloads require an explicit facade");
            else {
                const auto& ctor = *constructors.front();
                auto why = problem(ctor);
                if (ctor.access != "public") why = "constructor is not public";
                if (!why.empty()) reject(owner, why);
                else primitive(node, ctor, "create", Kind::constructor, "std::make_unique<" + native + ">(@ARGS@)", native, ops);
            }
        }
        // close works on an already closed object.
        {
            const auto stub = "rosetta_" + name + "_" + std::to_string(next++);
            out << "extern \"C\" value " << stub << "(value self) {\nCAMLparam1(self);\nrosetta_ocaml::guard([&] { rosetta_ocaml::close(rosetta_ocaml::cell<"
                << native << ">(self)); });\nCAMLreturn(Val_unit);\n}\n";
            reserve(node, "close");
            node.signature += "val close : t -> unit\n";
            node.structure += "external close : t -> unit = " + quote(stub) + "\n";
        }
        counts.clear();
        for (const auto& fn : rec.methods) ++counts[fn.name];
        for (const auto& fn : rec.methods) {
            if (fn.access != "public" || (config.restrict_methods && !config.methods.count(fn.name))) continue;
            auto why = problem(fn);
            if (counts[fn.name] > 1) why = "method overloads require an explicit facade";
            if (!why.empty()) { reject(owner + "::" + fn.name, why); continue; }
            if (fn.is_static) primitive(node, fn, value_name(fn.name), Kind::function, native + "::" + fn.name + "(@ARGS@)");
            else primitive(node, fn, value_name(fn.name), Kind::method, "rosetta_ocaml::live(owned)." + fn.name + "(@ARGS@)", native);
        }
        for (const auto& field : rec.fields) {
            if (field.access != "public" || (config.restrict_fields && !config.fields.count(field.name))) continue;
            const auto type = scalar(field.type);
            if (type.empty() || field.bitfield || field.type.is_volatile) { reject(owner + "::" + field.name, "unsupported OCaml field type"); continue; }
            const auto repr = representation(type);
            const auto getter = value_name(field.name), stub = "rosetta_" + name + "_" + std::to_string(next++);
            // The cell is read before the runtime is released.
            out << "extern \"C\" value " << stub << "_get(value self) {\nCAMLparam1(self);\nauto result = rosetta_ocaml::guard([&] {\n"
                << "auto* owned = rosetta_ocaml::cell<" << native << ">(self);\nreturn " << (repr == "float" ? "rosetta_ocaml::finite(" : "(")
                << "rosetta_ocaml::native([&] { return rosetta_ocaml::live(owned)." << field.name << "; }));\n});\n"
                << "CAMLreturn(rosetta_ocaml::to_" << repr << "(result));\n}\n";
            reserve(node, getter);
            node.signature += "val " + getter + " : t -> " + repr + "\n";
            node.structure += "external " + getter + " : t -> " + repr + " = " + quote(stub + "_get") + "\n";
            // Const fields have no setter.
            if (field.type.is_const) continue;
            const auto setter = value_name("set_" + field.name);
            out << "extern \"C\" value " << stub << "_set(value self, value a0) {\nCAMLparam2(self, a0);\nrosetta_ocaml::guard([&] {\n"
                << "auto* owned = rosetta_ocaml::cell<" << native << ">(self);\nauto converted = rosetta_ocaml::from_" << repr << '<' << type << ">(a0);\n"
                << "rosetta_ocaml::native([&] { rosetta_ocaml::live(owned)." << field.name << " = std::move(converted); });\n});\n"
                << "CAMLreturn(Val_unit);\n}\n";
            reserve(node, setter);
            node.signature += "val " + setter + " : t -> " + repr + " -> unit\n";
            node.structure += "external " + setter + " : t -> " + repr + " -> unit = " + quote(stub + "_set") + "\n";
        }
    }
    for (const auto& enumeration : module.enumerations)
        if (enumeration.selected) reject(enumeration.qualified_name(), "OCaml enum bindings are not yet supported");
    if (!skip && !result.diagnostics.empty()) {
        std::string error = "unsupported declaration for OCaml";
        for (const auto& entry : result.diagnostics) error += "\n  " + entry;
        throw BindingError(error);
    }
    // Objects still alive at exit are destroyed after OCaml's own at_exit functions.
    out << "extern \"C\" value rosetta_" << name << "_initialize(value unit) {\n(void)unit;\nstd::atexit(&rosetta_ocaml::shutdown);\nreturn Val_unit;\n}\n";
    result.source = out.str();

    std::ostringstream signature, structure;
    emit(root, signature, structure);
    std::ostringstream interface;
    interface << "(* Generated by Rosetta: the " << ocaml_module << " module. Link it with the " << name
              << "_stubs library (dll" << name << "_stubs.so for bytecode, lib" << name << "_stubs.a for native code). *)\n"
              << replace_all(replace_all(detail::ocaml_interface_runtime, "@MODULE@", ocaml_module), "@ERROR@", error_name)
              << "\ninclude (struct\n  external rosetta_initialize : unit -> unit = " << quote("rosetta_" + name + "_initialize")
              << "\n  let () = rosetta_initialize ()\n" << indent(structure.str()) << "end : sig\n"
              << indent(signature.str()) << "end)\n";
    result.interface = interface.str();
    return result;
}
} // namespace rosetta
