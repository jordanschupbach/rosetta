// R6 backend: .Call entries with runtime overload dispatch, and R6 classes calling them.
#include "rosetta/r.hpp"
#include "r6_runtime.hpp"
#include "type_spelling.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
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
        else if (c == '?') result += "\\?"; // no trigraphs
        else result += c;
    }
    return result + '"';
}
// An R string literal.
std::string rstring(const std::string& text) {
    std::string result = "\"";
    for (char c : text) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n') result += "\\n";
        else result += c;
    }
    return result + '"';
}
// A backquoted R name (any text is a valid name inside backquotes).
std::string bq(const std::string& name) {
    std::string result = "`";
    for (char c : name) { if (c == '`' || c == '\\') result += '\\'; result += c; }
    return result + "`";
}
std::string exported(std::string text) {
    for (std::size_t i = 0; (i = text.find("::", i)) != std::string::npos; i += 2) text.replace(i, 2, "__");
    return text;
}
bool identifier(const std::string& text) {
    auto start = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '.'; };
    return !text.empty() && start(text.front()) && std::all_of(text.begin(), text.end(), [&](char c) {
        return start(c) || (c >= '0' && c <= '9');
    });
}
// R reserved words cannot follow $; they get a trailing underscore.
std::string r_safe(const std::string& name) {
    static const std::set<std::string> reserved = {"if", "else", "repeat", "while", "function", "for", "next", "break", "TRUE",
        "FALSE", "NULL", "Inf", "NaN", "NA", "NA_integer_", "NA_real_", "NA_character_", "NA_complex_", "in"};
    return reserved.count(name) ? name + "_" : name;
}
std::string normalize(std::string text) {
    for (const char* inline_namespace : {"__cxx11::", "__1::", "__debug::", "__ndk1::", "__fs::"}) {
        for (std::size_t i = 0; (i = text.find(inline_namespace, i)) != std::string::npos;) text.erase(i, std::string(inline_namespace).size());
    }
    if (text.rfind("::", 0) == 0) text.erase(0, 2);
    return text;
}
std::string strip_cv(std::string text) {
    text = detail::trim_type(text);
    for (bool changed = true; changed;) {
        changed = false;
        for (const std::string prefix : {"const ", "volatile "})
            if (text.rfind(prefix, 0) == 0) { text = detail::trim_type(text.substr(prefix.size())); changed = true; }
    }
    return text;
}
const Type& referent(const Type& type) {
    return (type.kind == TypeKind::lvalue_reference || type.kind == TypeKind::rvalue_reference) && !type.arguments.empty()
        ? type.arguments.front() : type;
}
bool is_void(const Type& type) { return strip_cv(type.canonical) == "void"; }

// Operator methods are exported under Python-style names that the R Ops method dispatches to.
std::string operator_name(const std::string& name, std::size_t arity) {
    static const std::map<std::string, std::pair<const char*, const char*>> names = {
        {"operator+", {"__pos__", "__add__"}}, {"operator-", {"__neg__", "__sub__"}}, {"operator*", {nullptr, "__mul__"}},
        {"operator/", {nullptr, "__truediv__"}}, {"operator%", {nullptr, "__mod__"}}, {"operator==", {nullptr, "__eq__"}},
        {"operator!=", {nullptr, "__ne__"}}, {"operator<", {nullptr, "__lt__"}}, {"operator<=", {nullptr, "__le__"}},
        {"operator>", {nullptr, "__gt__"}}, {"operator>=", {nullptr, "__ge__"}}, {"operator&", {nullptr, "__and__"}},
        {"operator|", {nullptr, "__or__"}}, {"operator!", {"__not__", nullptr}}, {"operator[]", {nullptr, "__getitem__"}}};
    if (name == "operator()") return "call";
    const auto found = names.find(name);
    if (found == names.end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.first : found->second.second;
    return result ? result : "";
}

enum class Use { parameter, result, element, callback_argument, callback_result };

struct Overload {
    const Function* function;
    std::string owner;
    bool extension = false;
    bool receives_self = false;
};
struct Group {
    std::string name;
    std::vector<Overload> overloads;
    bool is_static = false;
};
struct FieldPlan { const Field* field; std::string name; };
struct ClassPlan {
    const Record* record;
    std::string r;           // R class (and variable) name
    std::string member;      // name inside the outer class (nested classes)
    std::vector<std::string> bases;
    std::vector<Overload> constructors;
    std::vector<Group> methods;
    std::vector<FieldPlan> fields;
    std::string parent;
    bool director = false;   // R6 subclasses can implement its virtual methods
};
const std::set<std::string>& r6_reserved() {
    static const std::set<std::string> names = {"initialize", "finalize", "clone", "self", "private", "super", ".ptr",
        ".__enclos_env__", ".__active__"};
    return names;
}
// R6Class() rejects these member names, but R6's $set() accepts them, so they keep their C++ name.
bool settable_reserved(const std::string& name) { return name == "clone"; }
std::string member_name(std::string rname) {
    if (r6_reserved().count(rname) && !settable_reserved(rname)) rname += "_";
    return rname;
}
const std::set<std::string>& generator_reserved() {
    static const std::set<std::string> names = {"new", "set", "classname", "public_fields", "public_methods", "active",
        "private_fields", "private_methods", "inherit", "parent_env", "lock_objects", "portable", "cloneable", "lock_class",
        "is_locked", "lock", "unlock", "debug", "undebug", "get_inherit", "has_private", "self", "debug_names", ".__enclos_env__"};
    return names;
}

// --- Roxygen ---------------------------------------------------------------
//
// Doxygen comments become roxygen2 blocks (markdown) above the generated R objects, so a package
// build that runs roxygen2 gets man/ pages that R's help() (and editors) show. Functions take
// `...` at the R level, so each block carries an explicit @usage with the C++ parameters.

// Text for roxygen markdown: outside `code` spans, backslashes, braces and square brackets are
// escaped (Rd groups and roxygen links), as is > or # opening a line; roxygen escapes % itself.
std::string rox_escape(const std::string& text) {
    std::string out;
    bool code = false;
    bool line_start = true;
    for (const char c : text) {
        // A line opening with > or # would be a markdown block quote or heading.
        if (line_start && !code && (c == '>' || c == '#')) out += '\\';
        if (c != ' ') line_start = c == '\n';
        if (c == '`') code = !code;
        if (!code && (c == '\\' || c == '{' || c == '}' || c == '[' || c == ']')) out += '\\';
        if (c == '@') out += '@'; // @@ is a literal @ for roxygen, inside code too
        out += c;
    }
    return out;
}

// Prefixes every line with #'.
std::string rox_lines(const std::string& text) {
    std::string out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        auto line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        out += line.empty() ? "#'\n" : "#' " + line + "\n";
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return out;
}

std::string at_escape(std::string text) {
    for (std::size_t at = 0; (at = text.find('@', at)) != std::string::npos; at += 2) text.insert(at, "@");
    return text;
}

// Prose (escaped), or a fenced code block.
std::string rox_block(const DocBlock& block) {
    if (block.kind == "code") {
        std::string language = block.language;
        if (language.empty() || language == "c++" || language == "cxx" || language == "cc") language = "cpp";
        if (language == "py") language = "python";
        return "```" + language + "\n" + at_escape(block.text) + "\n```";
    }
    if (block.kind == "math") return "```\n" + at_escape(block.text) + "\n```";
    return rox_escape(block.text);
}

std::string indent_continuation(const std::string& text, const std::string& indent) {
    std::string out;
    for (const char c : text) {
        out += c;
        if (c == '\n') out += indent;
    }
    return out;
}

// Title (first sentence of the brief) and the rest of the brief.
std::pair<std::string, std::string> split_title(const std::string& brief) {
    std::string flat = brief;
    for (auto& c : flat) if (c == '\n') c = ' ';
    std::size_t at = 0;
    while ((at = flat.find(". ", at)) != std::string::npos) {
        // Not after a one-letter abbreviation or a common one ("e.g. ", "et al. ").
        const auto word_start = flat.rfind(' ', at);
        const auto word = flat.substr(word_start == std::string::npos ? 0 : word_start + 1, at - (word_start == std::string::npos ? 0 : word_start + 1));
        if (word.size() > 1 && word != "e.g" && word != "i.e" && word != "al" && word != "vs" && word != "etc") break;
        ++at;
    }
    if (at == std::string::npos || at + 2 >= flat.size()) return {flat, {}};
    return {flat.substr(0, at + 1), flat.substr(at + 2)};
}

// Sections shared by functions and classes: description, notes, errors, see also, @par sections.
struct RoxygenDoc {
    std::string title, description;
    std::vector<std::pair<std::string, std::string>> sections; // @section title, body
    std::string note, seealso, references;

    void add_section(const std::string& title_text, const std::string& body) {
        if (body.empty()) return;
        for (auto& section : sections)
            if (section.first == title_text) { section.second += "\n\n" + body; return; }
        sections.emplace_back(title_text, body);
    }
    void read(const Documentation& doc) {
        auto [first, rest] = split_title(doc.brief);
        if (title.empty()) title = rox_escape(first);
        else if (!first.empty()) rest = first + (rest.empty() ? "" : " " + rest);
        const auto paragraph = [&](const std::string& text) {
            if (!text.empty()) description += (description.empty() ? "" : "\n\n") + text;
        };
        paragraph(rox_escape(rest));
        std::string heading;
        for (const auto& block : doc.details) {
            if (block.kind == "heading") { heading = block.text; continue; }
            if (heading.empty()) { paragraph(rox_block(block)); continue; }
            std::string lower;
            for (const char c : heading) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower == "reference" || lower == "references")
                references += (references.empty() ? "" : "\n\n") + rox_block(block);
            else if (lower == "example" || lower == "examples")
                add_section("Examples (C++)", rox_block(block));
            else add_section(rox_escape(heading), rox_block(block));
        }
        for (const auto& item : doc.notes) {
            const auto text = rox_escape(item.text);
            if (item.name == "deprecated") add_section("Deprecated", text);
            else if (item.name == "warning") add_section("Warning", text);
            else if (item.name == "note" || item.name == "remark") note += (note.empty() ? "" : "\n\n") + text;
            else {
                std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
                label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
                note += (note.empty() ? "" : "\n\n") + label + ": " + text;
            }
        }
        std::string errors;
        for (const auto& item : doc.throws)
            errors += (errors.empty() ? "" : "\n") + std::string("* ") + (item.name.empty() ? "" : "`" + item.name + "`" +
                (item.text.empty() ? "" : ": ")) + indent_continuation(rox_escape(item.text), "  ");
        add_section("Errors", errors.empty() ? "" : "Signals an R error (from the C++ exception):\n\n" + errors);
        for (const auto& target : doc.see) seealso += (seealso.empty() ? "" : ", ") + rox_escape(target);
    }
    std::string render(const std::string& middle) const {
        std::string out = title + "\n";
        if (!description.empty()) out += "\n" + description + "\n";
        out += middle;
        for (const auto& [name, body] : sections) out += "@section " + name + ":\n" + body + "\n";
        if (!note.empty()) out += "@note " + note + "\n";
        if (!references.empty()) out += "@references " + references + "\n";
        if (!seealso.empty()) out += "@seealso " + seealso + "\n";
        out += "@md\n";
        return rox_lines(out);
    }
};

class Generator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, bool package, std::size_t parts)
        : module_(module), name_(name), skip_(skip), policy_(resolve_reexports(policy, module)), flat_(policy.names == "flat"), package_(package), parts_(parts) {}
    GeneratedRSource run();

private:
    const Module& module_;
    std::string name_;
    bool skip_;
    const BindingPolicy policy_;
    bool flat_;
    bool package_;
    std::size_t parts_ = 1;
    std::vector<std::string> diagnostics_;
    std::map<std::string, const Record*> records_;
    std::map<std::string, ClassPlan> classes_;
    std::vector<std::string> class_order_;
    std::map<std::string, std::string> template_names_;
    std::map<std::string, std::string> function_template_names_;
    std::set<std::string> extension_functions_;
    std::map<std::string, const Enumeration*> enums_;
    std::size_t next_ = 0;
    // Registered .Call routines: (R name, C++ symbol, argument count).
    std::vector<std::tuple<std::string, std::string, int>> routines_;

    void reject(const std::string& context, const std::string& reason) { diagnostics_.push_back(context + ": " + reason); }
    bool excluded(const std::string& qualified, const std::string& identity = {}) const {
        for (const auto& pattern : policy_.exclude)
            if (policy_match(pattern, qualified) || (!identity.empty() && policy_match(pattern, identity))) return true;
        return false;
    }
    bool readonly(const std::string& qualified, const std::string& owner) const {
        for (const auto& pattern : policy_.readonly)
            if (policy_match(pattern, qualified) || (!owner.empty() && policy_match(pattern, owner))) return true;
        return false;
    }
    std::string renamed(const std::string& qualified, const std::string& fallback) const {
        const auto found = policy_.rename.find(qualified);
        if (found == policy_.rename.end()) return fallback;
        if (!identifier(found->second)) throw BindingError("R rename target is not an identifier: " + found->second);
        return found->second;
    }
    // A registered routine: returns its R name.
    std::string routine(const std::string& symbol, int arguments) {
        const auto r = ".rs_" + symbol.substr(symbol.find_last_of('_') + 1);
        routines_.emplace_back(r, symbol, arguments);
        return r;
    }
    std::string symbol(const std::string& prefix) { return prefix + std::to_string(next_++); }
    bool bound(const Type& type) const { return type.kind == TypeKind::record && classes_.count(type.declaration); }
    // Whether a native object crosses through this type (directly, by pointer or reference, or in a container).
    bool carries_bound(const Type& type) const {
        if (bound(type)) return true;
        return std::any_of(type.arguments.begin(), type.arguments.end(), [&](const Type& t) { return carries_bound(t); });
    }
    std::string conversion(const Type& type) const {
        const auto spelling = normalize(strip_cv(type.canonical));
        for (const auto& item : policy_.conversions)
            if (normalize(item.first) == spelling) return item.first;
        if (type.kind == TypeKind::template_instance && spelling.size() > 1 && spelling.back() == '>') {
            const auto stem = spelling.substr(0, spelling.size() - 1);
            for (const auto& item : policy_.conversions) {
                const auto key = normalize(item.first);
                if (key.size() <= stem.size() || key.compare(0, stem.size(), stem) != 0) continue;
                const char next = key[stem.size()];
                if (stem.back() == '<' || next == ',') return item.first;
            }
        }
        return {};
    }
    std::map<std::string, bool> copyable_cache_;
    // as_base: copied as a base subobject, where abstractness does not matter.
    bool copyable_record(const std::string& name, unsigned depth = 0, bool as_base = false);
    bool copyable_type(const Type& type, unsigned depth);
    std::string value_problem(const Type& type, Use use) const;
    std::string type_problem(const Type& type, Use use) const;
    std::string function_problem(const Function& function, bool self_parameter) const;
    std::string display(const Type& type) const;
    std::string signature(const Overload& overload, const std::string& name) const;
    std::string r_default(const DefaultValue& value, const Type& type) const;
    std::string usage(const Overload& overload, const std::string& name) const;
    std::string roxygen_function(const Group& group, const std::string& title) const;
    std::string roxygen_class(const ClassPlan& plan) const;
    std::string roxygen_enum(const Enumeration& enumeration) const;
    std::string return_policy(const Function& function) const;
    std::string class_r_name(const Record& record) const;
    bool copy_type(const Type& type) const {
        if (type.kind == TypeKind::pointer || type.kind == TypeKind::rvalue_reference) return false;
        const auto& target = referent(type);
        return target.kind == TypeKind::record && policy_.copy_types.count(target.declaration) != 0;
    }
    bool flat(const std::vector<std::string>& namespaces) const {
        if (!flat_) return false;
        std::string scope;
        for (const auto& part : namespaces) scope += (scope.empty() ? "" : "::") + part;
        for (const auto& pattern : policy_.qualify) if (policy_match(pattern, scope)) return false;
        return true;
    }
    void plan_classes();
    std::vector<const Function*> virtuals(const Record& record, std::map<std::string, bool>& pure) const;
    bool callback_supported(const Function& method) const;
    void plan_directors();
    void emit_director(std::ostream& out, const ClassPlan& plan);
    // checks = false (a sole overload): convert directly, so conversion errors name the problem.
    void emit_overload(std::ostream& out, const Overload& overload, const std::string& kind, const std::string& indent, bool checks);
    std::string emit_group(std::ostream& out, const Group& group, const std::string& kind);
    std::string emit_constructor(std::ostream& out, const ClassPlan& plan);
    static std::string director_name(const ClassPlan& plan) {
        std::string name = "rosetta_director_";
        for (const char c : plan.record->qualified_name()) name += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
        return name;
    }
    // The .Call expression of a group; results of void functions are invisible.
    static std::string call(const Group& group, const std::string& arguments) {
        const bool silent = std::all_of(group.overloads.begin(), group.overloads.end(), [](const Overload& o) { return is_void(o.function->result); });
        return std::string(silent ? "invisible(" : "") + ".Call(" + arguments + ")" + (silent ? ")" : "");
    }
};

bool Generator::copyable_type(const Type& type, unsigned depth) {
    if (depth > 32) return true;
    const auto bare = normalize(strip_cv(type.canonical));
    static const std::set<std::string> fixed = {"std::mutex", "std::recursive_mutex", "std::timed_mutex", "std::shared_mutex",
        "std::condition_variable", "std::condition_variable_any", "std::thread", "std::jthread", "std::stop_source"};
    if (fixed.count(bare)) return false;
    if (type.kind == TypeKind::rvalue_reference) return false;
    if (type.kind == TypeKind::record) return copyable_record(type.declaration, depth + 1);
    if (type.kind == TypeKind::template_instance) {
        const auto declaration = normalize(type.declaration);
        static const std::set<std::string> movable_only = {"std::unique_ptr", "std::atomic", "std::promise", "std::future",
            "std::packaged_task", "std::basic_ifstream", "std::basic_ofstream", "std::basic_fstream", "std::basic_stringstream",
            "std::basic_istringstream", "std::basic_ostringstream", "std::basic_filebuf", "std::unique_lock", "std::lock_guard",
            "std::scoped_lock", "std::move_only_function"};
        if (movable_only.count(declaration)) return false;
        if (declaration == "std::shared_ptr" || declaration == "std::weak_ptr" || declaration == "std::function") return true;
        for (const auto& argument : type.arguments) if (!copyable_type(argument, depth + 1)) return false;
    }
    return true;
}
bool Generator::copyable_record(const std::string& name, unsigned depth, bool as_base) {
    const auto found = records_.find(name);
    if (as_base && found != records_.end() && found->second->abstract) {
        // An abstract base copies like a concrete one: check its members alone.
        const auto& record = *found->second;
        for (const auto& constructor : record.constructors)
            if (constructor.parameters.size() == 1 && constructor.parameters[0].kind == TypeKind::lvalue_reference &&
                referent(constructor.parameters[0]).declaration == name && (constructor.is_deleted || constructor.access == "private"))
                return false;
        for (const auto& field : record.fields) if (!copyable_type(field.type, depth + 1)) return false;
        for (const auto& base : record.bases)
            if (!copyable_record(base.type.declaration.empty() ? base.type.canonical : base.type.declaration, depth + 1, true)) return false;
        return true;
    }
    const auto cached = copyable_cache_.find(name);
    if (cached != copyable_cache_.end()) return cached->second;
    if (found == records_.end() || depth > 32) return true;
    copyable_cache_[name] = true; // provisional for recursive types
    const auto& record = *found->second;
    bool result = !record.abstract;
    bool copy_declared = false, move_declared = false;
    for (const auto& constructor : record.constructors) {
        if (constructor.parameters.size() != 1) continue;
        const auto& parameter = constructor.parameters[0];
        if (referent(parameter).declaration != name) continue;
        if (parameter.kind == TypeKind::lvalue_reference) {
            copy_declared = true;
            if (constructor.is_deleted || constructor.access != "public") result = false;
        } else if (parameter.kind == TypeKind::rvalue_reference) move_declared = true;
    }
    if (move_declared && !copy_declared) result = false;
    for (const auto& field : record.fields) if (result && !copyable_type(field.type, depth + 1)) result = false;
    for (const auto& base : record.bases)
        if (result && !copyable_record(base.type.declaration.empty() ? base.type.canonical : base.type.declaration, depth + 1, true)) result = false;
    copyable_cache_[name] = result;
    return result;
}

std::string Generator::value_problem(const Type& type, Use) const {
    if (!conversion(type).empty()) return {};
    const auto bare = normalize(strip_cv(type.canonical));
    switch (type.kind) {
    case TypeKind::builtin: {
        static const std::set<std::string> supported = {"bool", "char", "signed char", "unsigned char", "short", "unsigned short",
            "int", "unsigned int", "long", "unsigned long", "long long", "unsigned long long", "float", "double"};
        if (supported.count(bare)) return {};
        return "unsupported R value type " + type.canonical;
    }
    case TypeKind::enumeration: return {};
    case TypeKind::record:
        if (bound(type)) return {};
        if (bare == "std::filesystem::path" || bare == "std::nullptr_t" || bare == "std::monostate") return {};
        return "class " + strip_cv(type.canonical) + " is not exported to R";
    case TypeKind::template_instance: {
        const auto declaration = normalize(type.declaration);
        const auto all = [&](std::size_t count, Use element) -> std::string {
            if (type.arguments.size() < count) return "incomplete template arguments in " + type.canonical;
            for (std::size_t i = 0; i < count; ++i) {
                auto why = type_problem(type.arguments[i], element);
                if (!why.empty()) return why;
            }
            return {};
        };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view")
            return !type.arguments.empty() && strip_cv(type.arguments[0].canonical) == "char" ? "" : "only char strings are supported";
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::set" ||
            declaration == "std::unordered_set" || declaration == "std::valarray" || declaration == "std::optional" ||
            declaration == "std::array") {
            if (!type.arguments.empty() && type.arguments[0].kind == TypeKind::template_instance &&
                normalize(type.arguments[0].declaration) == "std::unique_ptr" && declaration != "std::optional")
                return "containers of unique_ptr are unsupported";
            return all(1, Use::element);
        }
        if (declaration == "std::map" || declaration == "std::unordered_map") return all(2, Use::element);
        if (declaration == "std::pair" || declaration == "std::tuple" || declaration == "std::variant")
            return all(type.arguments.size(), Use::element);
        if (declaration == "std::complex")
            return !type.arguments.empty() && type.arguments[0].kind == TypeKind::builtin ? "" : "unsupported complex type";
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") {
            if (type.arguments.empty() || !bound(type.arguments[0]))
                return "smart pointers require an exported class: " + type.canonical;
            if (declaration == "std::unique_ptr" && type.arguments.size() > 1 && normalize(type.arguments[1].declaration) != "std::default_delete")
                return "unique_ptr with a custom deleter is unsupported";
            return {};
        }
        if (declaration == "std::function") {
            if (type.arguments.empty() || type.arguments[0].kind != TypeKind::function) return "unsupported std::function signature";
            const auto& signature = type.arguments[0];
            auto why = type_problem(signature.arguments[0], Use::callback_result);
            if (!why.empty()) return "callback result: " + why;
            for (std::size_t i = 1; i < signature.arguments.size(); ++i) {
                why = type_problem(signature.arguments[i], Use::callback_argument);
                if (!why.empty()) return "callback argument: " + why;
            }
            return {};
        }
        if (declaration == "std::chrono::duration") return {};
        return "unsupported R template type " + type.canonical;
    }
    default:
        return "unsupported R value type " + type.canonical;
    }
}

std::string Generator::type_problem(const Type& type, Use use) const {
    if (use == Use::result && is_void(type)) return {};
    if (use == Use::callback_result && is_void(type)) return {};
    if (type.is_volatile) return "volatile types are unsupported";
    if (type.kind == TypeKind::lvalue_reference || type.kind == TypeKind::rvalue_reference) {
        const auto& target = referent(type);
        if (use == Use::element || use == Use::callback_result) return "references are unsupported here: " + type.canonical;
        if (type.kind == TypeKind::rvalue_reference && use != Use::parameter) return "rvalue reference results are unsupported";
        if (target.is_volatile) return "volatile types are unsupported";
        if (bound(target)) return {};
        if (target.kind == TypeKind::pointer) return "references to pointers are unsupported";
        return value_problem(target, use);
    }
    if (type.kind == TypeKind::pointer) {
        if (type.arguments.empty()) return "unsupported pointer";
        const auto& pointee = type.arguments.front();
        if (bound(pointee)) return {};
        if (pointee.kind == TypeKind::builtin && strip_cv(pointee.canonical) == "char" && (pointee.is_const || use == Use::result))
            return {};
        return "unsupported R pointer type " + type.canonical;
    }
    if (type.kind == TypeKind::function) return "function types are unsupported";
    return value_problem(type, use);
}

std::string Generator::function_problem(const Function& function, bool self_parameter) const {
    if (!function.unsupported_reason.empty() && function.unsupported_reason.rfind("default calls with ", 0) != 0)
        return function.unsupported_reason;
    if (function.is_deleted) return "deleted function";
    if (function.default_arguments > function.parameters.size()) return "invalid default argument metadata";
    if (!function.is_constructor) {
        auto why = type_problem(function.result, Use::result);
        if (!why.empty()) return "unsupported R result type: " + why;
    }
    for (std::size_t i = self_parameter ? 1 : 0; i < function.parameters.size(); ++i) {
        auto why = type_problem(function.parameters[i], Use::parameter);
        if (!why.empty()) return "unsupported R parameter type: " + why;
    }
    return {};
}

// R-side spelling of a C++ type, for error messages listing signatures.
std::string Generator::display(const Type& type) const {
    const auto& target = referent(type);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return "character";
        return display(pointee) + " or NULL";
    }
    if (is_void(target)) return "NULL";
    if (!conversion(target).empty()) return policy_.conversions.at(conversion(target));
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        if (bare == "bool") return "logical";
        if (bare == "char") return "character";
        if (bare == "float" || bare == "double" || bare == "long double") return "numeric";
        return "integer";
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = enums_.find(target.declaration);
        return found != enums_.end() ? "character (" + found->second->name + ")" : "integer";
    }
    if (bound(target)) return classes_.at(target.declaration).r;
    if (bare == "std::filesystem::path") return "character";
    if (target.kind == TypeKind::template_instance) {
        const auto declaration = normalize(target.declaration);
        const auto arg = [&](std::size_t i) { return i < target.arguments.size() ? display(target.arguments[i]) : std::string("ANY"); };
        if (declaration == "std::basic_string" || declaration == "std::basic_string_view") return "character";
        if (declaration == "std::vector" || declaration == "std::deque" || declaration == "std::list" || declaration == "std::array" ||
            declaration == "std::valarray" || declaration == "std::set" || declaration == "std::unordered_set") {
            // Atomic elements come back as an R vector, anything else as a list.
            const auto element = arg(0);
            static const std::set<std::string> atomic = {"logical", "integer", "numeric", "character", "complex"};
            return atomic.count(element) ? element + " vector" : "list of " + element;
        }
        if (declaration == "std::map" || declaration == "std::unordered_map") return "map<" + arg(0) + ", " + arg(1) + ">";
        if (declaration == "std::optional") return arg(0) + " or NULL";
        if (declaration == "std::complex") return "complex";
        if (declaration == "std::shared_ptr" || declaration == "std::unique_ptr") return arg(0);
        if (declaration == "std::pair" || declaration == "std::tuple") {
            std::string result = "list(";
            for (std::size_t i = 0; i < target.arguments.size(); ++i) result += (i ? ", " : "") + display(target.arguments[i]);
            return result + ")";
        }
        if (declaration == "std::variant") {
            std::string result;
            for (std::size_t i = 0; i < target.arguments.size(); ++i) result += (i ? " | " : "") + display(target.arguments[i]);
            return result;
        }
        if (declaration == "std::function") return "function";
        if (declaration == "std::chrono::duration") return "numeric";
    }
    return strip_cv(target.canonical);
}

std::string Generator::signature(const Overload& overload, const std::string& name) const {
    const auto& function = *overload.function;
    std::string result = name + "(";
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t required = function.parameters.size() - function.default_arguments;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        if (i > first) result += ", ";
        const auto& parameter = i < function.parameter_names.size() && !function.parameter_names[i].empty()
            ? function.parameter_names[i] : "arg" + std::to_string(i - first);
        result += parameter + ": " + display(function.parameters[i]);
        if (i >= required) result += " = ...";
    }
    result += ")";
    if (!function.is_constructor) result += " -> " + display(function.result);
    return result;
}

std::string Generator::r_default(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int" || value.kind == "float") return value.value;
    if (value.kind == "bool") return value.value == "true" ? "TRUE" : "FALSE";
    if (value.kind == "null") return "NULL";
    if (value.kind == "string") return value.value;
    if (value.kind == "char") return rstring(value.value);
    if (value.kind == "enum") {
        const auto& target = referent(type);
        return rstring(renamed(target.declaration + "::" + value.value, value.value));
    }
    if (value.kind == "constant") return value.value;
    if (value.kind == "default") {
        const auto& target = referent(type);
        if (bound(target)) return classes_.at(target.declaration).r + "$new()";
        const auto shown = display(target);
        if (shown.size() > 7 && shown.compare(shown.size() - 7, 7, " vector") == 0) return "c()";
        if (shown.rfind("map<", 0) == 0 || shown.rfind("list", 0) == 0) return "list()";
        if (shown == "character") return "\"\"";
        if (shown == "numeric" || shown == "integer") return "0";
        if (shown == "logical") return "FALSE";
    }
    return "<default>";
}

std::string Generator::usage(const Overload& overload, const std::string& name) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t required = function.parameters.size() - function.default_arguments;
    std::string result = name + "(";
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        auto parameter = i < function.parameter_names.size() && !function.parameter_names[i].empty()
            ? function.parameter_names[i] : "arg" + std::to_string(i - first);
        if (!identifier(parameter) || r_safe(parameter) != parameter) parameter = bq(parameter);
        result += (i > first ? ", " : "") + parameter;
        if (i >= required)
            result += " = " + r_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
    }
    return result + ")";
}

namespace {
// Parameters and results of a set of overloads, merged by name.
struct RoxygenParameters {
    struct Entry { std::string name, type, text; };
    std::vector<Entry> entries;
    std::vector<std::string> returns;
};
}

std::string Generator::roxygen_function(const Group& group, const std::string& title) const {
    RoxygenDoc doc;
    std::set<std::string> seen;
    for (const auto& overload : group.overloads) {
        const auto& documentation = overload.function->documentation;
        if (!documentation.empty() && seen.insert(plain_documentation(documentation)).second) doc.read(documentation);
    }
    if (doc.title.empty()) doc.title = title;
    std::string middle = "@usage\n";
    std::set<std::string> usages;
    RoxygenParameters merged;
    for (const auto& overload : group.overloads) {
        const auto line = usage(overload, group.name);
        if (usages.insert(line).second) middle += line + "\n";
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            const auto name = cpp.empty() ? "arg" + std::to_string(i - first) : cpp;
            const auto type = "`" + display(function.parameters[i]) + "`";
            std::string text;
            for (const auto& item : function.documentation.params) if (item.name == cpp) text = rox_escape(item.text);
            auto existing = std::find_if(merged.entries.begin(), merged.entries.end(), [&](const auto& e) { return e.name == name; });
            if (existing == merged.entries.end()) merged.entries.push_back({name, type, text});
            else {
                if (existing->type.find(type) == std::string::npos) existing->type += " or " + type;
                if (existing->text.empty()) existing->text = text;
            }
        }
        if (!function.is_constructor) {
            const auto type = display(function.result);
            std::string text = rox_escape(function.documentation.returns);
            for (const auto& value : function.documentation.retvals)
                text += (text.empty() ? "" : "\n") + std::string("* `") + value.name + "`" + (value.text.empty() ? "" : ": " + rox_escape(value.text));
            if (type != "NULL" || !text.empty()) {
                const auto entry = "`" + type + "`" + (text.empty() ? "" : (text.front() == '*' ? ":\n\n" : ": ") + text);
                if (std::find(merged.returns.begin(), merged.returns.end(), entry) == merged.returns.end()) merged.returns.push_back(entry);
            }
        }
    }
    for (const auto& entry : merged.entries)
        middle += "@param " + (identifier(entry.name) ? entry.name : bq(entry.name)) + " " + entry.type +
            (entry.text.empty() ? "" : ". " + indent_continuation(entry.text, "  ")) + "\n";
    if (!merged.returns.empty()) {
        middle += "@return ";
        for (std::size_t i = 0; i < merged.returns.size(); ++i) middle += (i ? "\n\nOr " : "") + merged.returns[i];
        middle += "\n";
    }
    return doc.render(middle);
}

std::string Generator::roxygen_class(const ClassPlan& plan) const {
    RoxygenDoc doc;
    doc.read(plan.record->documentation);
    if (doc.title.empty()) doc.title = "C++ class `" + plan.record->qualified_name() + "`";
    // Constructor usage and arguments.
    std::string constructor;
    if (!plan.constructors.empty()) {
        std::set<std::string> usages;
        for (const auto& overload : plan.constructors) {
            const auto line = "`" + usage(overload, plan.r + "$new") + "`";
            if (usages.insert(line).second) constructor += (constructor.empty() ? "" : "\n\n") + line;
            if (!overload.function->documentation.empty()) {
                const auto& ctor = overload.function->documentation;
                auto summary = rox_escape(ctor.brief);
                if (!summary.empty()) constructor += "\n\n" + summary;
            }
        }
        std::string arguments;
        std::set<std::string> listed;
        for (const auto& overload : plan.constructors) {
            const auto& function = *overload.function;
            for (std::size_t i = 0; i < function.parameters.size(); ++i) {
                const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
                const auto name = cpp.empty() ? "arg" + std::to_string(i) : cpp;
                if (!listed.insert(name).second) continue;
                std::string text;
                for (const auto& item : function.documentation.params) if (item.name == cpp) text = rox_escape(item.text);
                arguments += "\n* `" + name + "` (`" + display(function.parameters[i]) + "`)" + (text.empty() ? "" : ": " + indent_continuation(text, "  "));
            }
            for (const auto& item : function.documentation.throws)
                doc.add_section("Errors", "* `" + item.name + "`: " + rox_escape(item.text));
        }
        if (!arguments.empty()) constructor += "\n" + arguments;
    } else {
        constructor = plan.record->abstract ? "Abstract: create objects of a derived class (or an R6 subclass)." : "No public constructor.";
    }
    doc.sections.insert(doc.sections.begin(), {"Constructor", constructor});
    std::string fields;
    for (const auto& field : plan.fields) {
        auto text = rox_escape(field.field->documentation.brief);
        fields += (fields.empty() ? "" : "\n") + std::string("* `$") + field.name + "` (`" + display(field.field->type) + "`)" +
            (text.empty() ? "" : ": " + indent_continuation(text, "  "));
    }
    std::string methods, statics;
    for (const auto& group : plan.methods) {
        std::string entry;
        std::set<std::string> usages;
        for (const auto& overload : group.overloads) {
            const auto line = usage(overload, (group.is_static ? plan.r + "$" : std::string("$")) + group.name);
            if (!usages.insert(line).second) continue;
            const auto result = overload.function->is_constructor ? std::string() : display(overload.function->result);
            entry += (entry.empty() ? "* " : "\n  ") + std::string("`") + line + "`" + (result.empty() || result == "NULL" ? "" : " -> `" + result + "`");
        }
        std::set<std::string> seen;
        for (const auto& overload : group.overloads) {
            const auto& documentation = overload.function->documentation;
            if (documentation.empty() || !seen.insert(plain_documentation(documentation)).second) continue;
            if (!documentation.brief.empty()) entry += "\n\n  " + indent_continuation(rox_escape(documentation.brief), "  ");
            for (const auto& item : documentation.params)
                entry += "\n  - `" + item.name + "`: " + indent_continuation(rox_escape(item.text), "    ");
            if (!documentation.returns.empty()) entry += "\n  - Returns: " + indent_continuation(rox_escape(documentation.returns), "    ");
        }
        (group.is_static ? statics : methods) += (group.is_static ? statics : methods).empty() ? entry : "\n\n" + entry;
    }
    std::vector<std::pair<std::string, std::string>> members;
    if (!fields.empty()) members.emplace_back("Fields", fields);
    if (!methods.empty()) members.emplace_back("Methods", methods);
    if (!statics.empty()) members.emplace_back("Static methods", statics);
    doc.sections.insert(doc.sections.begin() + 1, members.begin(), members.end());
    if (!plan.bases.empty()) {
        std::string bases;
        for (const auto& base : plan.bases) bases += (bases.empty() ? "" : ", ") + std::string("`") + classes_.at(base).r + "`";
        doc.add_section("Inherits", bases);
    }
    return doc.render({});
}

std::string Generator::roxygen_enum(const Enumeration& enumeration) const {
    RoxygenDoc doc;
    doc.read(enumeration.documentation);
    if (doc.title.empty()) doc.title = "C++ enumeration `" + enumeration.qualified_name() + "`";
    std::string values;
    for (const auto& enumerator : enumeration.enumerators)
        values += (values.empty() ? "" : ", ") + std::string("`\"") + renamed(enumeration.qualified_name() + "::" + enumerator, enumerator) + "\"`";
    return doc.render("@format A list of enumerator names, which arguments of this type accept: " + values + ".\n");
}

std::string Generator::return_policy(const Function& function) const {
    auto found = policy_.returns.find(function.identity());
    if (found == policy_.returns.end()) found = policy_.returns.find(function.qualified_name());
    if (found == policy_.returns.end())
        return copy_type(function.result) ? "rosetta_r::Return::copy" : "rosetta_r::Return::automatic";
    return "rosetta_r::Return::" + found->second;
}

std::string Generator::class_r_name(const Record& record) const {
    const auto qualified = record.qualified_name();
    const auto instantiated = template_names_.find(qualified);
    if (instantiated != template_names_.end()) return renamed(qualified, instantiated->second);
    if (!record.owner.empty()) return renamed(qualified, record.name);
    return renamed(qualified, flat(record.namespaces) ? record.name : exported(qualified));
}

void Generator::plan_classes() {
    for (const auto& requested : policy_.templates) {
        const auto found = std::find_if(module_.records.begin(), module_.records.end(), [&](const Record& record) {
            return !record.template_name.empty() && normalize(record.qualified_name()) == normalize(requested.second);
        });
        if (!identifier(requested.first)) throw BindingError("template binding name is not an identifier: " + requested.first);
        if (found == module_.records.end()) {
            const auto function = std::find_if(module_.functions.begin(), module_.functions.end(), [&](const Function& f) {
                return !f.template_name.empty() && normalize(f.qualified_name()) == normalize(requested.second);
            });
            if (function == module_.functions.end()) { reject(requested.second, "template instantiation was not exported by the frontend"); continue; }
            function_template_names_[function->qualified_name()] = requested.first;
            continue;
        }
        template_names_[found->qualified_name()] = requested.first;
    }
    for (const auto& selection : policy_.classes)
        if (std::none_of(module_.records.begin(), module_.records.end(), [&](const Record& r) { return r.qualified_name() == selection.first; }))
            throw BindingError("unknown class in R policy: " + selection.first);
    for (const auto& record : module_.records) records_[record.qualified_name()] = &record;
    for (const auto& record : module_.records) {
        const auto owner = record.qualified_name();
        const bool listed = policy_.classes.count(owner) != 0;
        const bool instantiated = template_names_.count(owner) != 0;
        if (!record.template_name.empty() && !instantiated) continue;
        if (!listed && !instantiated && !policy_.select_all) {
            if (!policy_.explicit_class_selection && policy_.classes.empty()) reject(owner, "class requires an owned class policy");
            continue;
        }
        if (excluded(owner)) continue;
        if (!record.complete) { if (listed) reject(owner, "R6 requires a complete class definition"); continue; }
        if (!record.unsupported_reason.empty()) { reject(owner, record.unsupported_reason); continue; }
        if (!record.owner.empty() && !records_.count(record.owner)) continue;
        const auto config = policy_.classes.find(owner);
        if (config != policy_.classes.end() && !config->second.borrow_self.empty()) {
            reject(owner, "borrowed R6 results are not yet supported"); continue;
        }
        ClassPlan plan;
        plan.record = &record;
        classes_.emplace(owner, std::move(plan));
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (auto it = classes_.begin(); it != classes_.end();) {
            const auto& outer = it->second.record->owner;
            if (!outer.empty() && !classes_.count(outer)) { it = classes_.erase(it); changed = true; }
            else ++it;
        }
    }
    for (auto& [owner, plan] : classes_) {
        plan.member = class_r_name(*plan.record);
        plan.parent = plan.record->owner;
    }
    // Nested classes are also top-level variables, named Outer_Inner.
    for (auto& [owner, plan] : classes_) {
        std::string name = plan.member;
        for (auto parent = plan.parent; !parent.empty(); parent = classes_.at(parent).parent)
            name = classes_.at(parent).member + "_" + name;
        plan.r = name;
    }
    for (auto& [owner, plan] : classes_) {
        std::vector<std::string> found;
        const auto visit = [&](const auto& self, const Record& record, unsigned depth) -> void {
            if (depth > 64) return;
            for (const auto& base : record.bases) {
                if (base.access != "public") continue;
                const auto name = base.type.declaration.empty() ? base.type.canonical : base.type.declaration;
                if (classes_.count(name)) { if (std::find(found.begin(), found.end(), name) == found.end()) found.push_back(name); continue; }
                const auto record_found = records_.find(name);
                if (record_found != records_.end()) self(self, *record_found->second, depth + 1);
            }
        };
        visit(visit, *plan.record, 0);
        plan.bases = found;
    }
    const std::function<bool(const std::string&, const std::string&)> derives = [&](const std::string& from, const std::string& to) {
        if (from == to) return true;
        for (const auto& base : classes_.at(from).bases) if (derives(base, to)) return true;
        return false;
    };
    for (auto& [owner, plan] : classes_) {
        std::vector<std::string> minimal;
        for (const auto& base : plan.bases) {
            bool redundant = false;
            for (const auto& other : plan.bases) if (other != base && derives(other, base)) redundant = true;
            if (!redundant) minimal.push_back(base);
        }
        plan.bases = minimal;
    }
    std::set<std::string> placed;
    const std::function<void(const std::string&, unsigned)> place = [&](const std::string& owner, unsigned depth) {
        if (placed.count(owner) || depth > 256) return;
        const auto& plan = classes_.at(owner);
        for (const auto& base : plan.bases) place(base, depth + 1);
        if (!plan.parent.empty()) place(plan.parent, depth + 1);
        if (placed.insert(owner).second) class_order_.push_back(owner);
    };
    for (const auto& record : module_.records) if (classes_.count(record.qualified_name())) place(record.qualified_name(), 0);
}

// Virtual methods visible in a class (most-derived declaration first), with pure status.
std::vector<const Function*> Generator::virtuals(const Record& record, std::map<std::string, bool>& pure) const {
    std::vector<const Function*> result;
    std::set<std::string> seen;
    const auto key = [](const Function& function) {
        std::string text = function.name + "(";
        for (const auto& parameter : function.parameters) text += parameter.canonical + ",";
        return text + (function.is_const ? ")const" : ")");
    };
    const std::function<bool(const Record&, unsigned)> visit = [&](const Record& current, unsigned depth) {
        if (depth > 64) return false;
        for (const auto& method : current.methods) {
            if (!method.is_virtual || method.is_static || method.name.rfind("~", 0) == 0) continue;
            if (seen.insert(key(method)).second) { result.push_back(&method); pure[key(method)] = method.is_pure; }
        }
        for (const auto& base : current.bases) {
            const auto found = records_.find(base.type.declaration.empty() ? base.type.canonical : base.type.declaration);
            if (found == records_.end()) return false; // unknown base: virtuals cannot be enumerated
            if (!visit(*found->second, depth + 1)) return false;
        }
        return true;
    };
    if (!visit(record, 0)) { pure.clear(); return {}; }
    return result;
}

bool Generator::callback_supported(const Function& method) const {
    if (!method.unsupported_reason.empty() || !type_problem(method.result, Use::callback_result).empty()) return false;
    for (const auto& parameter : method.parameters)
        if (!type_problem(parameter, Use::callback_argument).empty()) return false;
    // Operators have no R6 method name to override.
    return !(method.name.rfind("operator", 0) == 0 && (method.name.size() == 8 || !identifier(method.name.substr(8))));
}

// A polymorphic class gets a director when every pure virtual can be implemented in R.
void Generator::plan_directors() {
    if (policy_.directors == "none") return;
    for (auto& [owner, plan] : classes_) {
        const auto& record = *plan.record;
        if (!record.polymorphic || !record.virtual_destructor || record.is_final || !record.public_destructor) continue;
        std::map<std::string, bool> pure;
        const auto methods = virtuals(record, pure);
        if (methods.empty()) continue;
        bool possible = true;
        for (const auto* method : methods)
            if (method->is_pure && !callback_supported(*method)) possible = false;
        const bool constructible = std::any_of(record.constructors.begin(), record.constructors.end(), [](const Function& constructor) {
            return !constructor.is_deleted && constructor.access != "private";
        });
        plan.director = possible && constructible;
    }
}

// A C++ subclass whose virtual methods call the R6 object's overrides, or the native implementation.
void Generator::emit_director(std::ostream& out, const ClassPlan& plan) {
    const auto owner = plan.record->qualified_name();
    std::map<std::string, bool> pure;
    const auto methods = virtuals(*plan.record, pure);
    out << "struct " << director_name(plan) << " final : " << owner << ", rosetta_r::Director {\n"
        << "    template<class... A> explicit " << director_name(plan) << "(A&&... arguments) : " << owner
        << "(std::forward<A>(arguments)...) {}\n";
    for (const auto* method : methods) {
        if (!callback_supported(*method)) continue;
        std::string key = method->name + "(";
        for (const auto& parameter : method->parameters) key += parameter.canonical + ",";
        key += method->is_const ? ")const" : ")";
        const bool is_pure = pure[key];
        if (method->access == "private" && !is_pure) continue; // a private base implementation cannot be called
        auto rname = renamed(method->qualified_name(), r_safe(method->name));
        rname = member_name(rname);
        std::string parameters, arguments, types, call_arguments;
        for (std::size_t i = 0; i < method->parameters.size(); ++i) {
            const auto& type = method->parameters[i];
            const auto name = "a" + std::to_string(i);
            parameters += (i ? ", " : "") + type.canonical + " " + name;
            const bool forward = type.kind == TypeKind::rvalue_reference || (type.kind != TypeKind::lvalue_reference && type.kind != TypeKind::pointer);
            const auto passed = forward ? "std::move(" + name + ")" : name;
            arguments += ", " + passed;
            call_arguments += (i ? ", " : "") + passed;
            types += ", " + type.canonical;
        }
        const auto result = method->result.canonical;
        out << "    " << result << " " << method->name << "(" << parameters << ")" << (method->is_const ? " const" : "")
            << (method->is_noexcept ? " noexcept" : "") << " override {\n";
        if (method->is_noexcept) out << "        try {\n";
        out << "        if (SEXP method = const_cast<" << director_name(plan) << "*>(this)->rosetta_override(" << quote(rname) << "))\n"
            << "            return rosetta_r::invoke_r<" << result << types << ">(method" << arguments << ");\n";
        if (is_pure) out << "        rosetta_r::pure_virtual(" << quote(owner + "::" + method->name) << ");\n";
        else out << "        return " << owner << "::" << method->name << "(" << call_arguments << ");\n";
        if (method->is_noexcept)
            out << "        } catch (const std::exception& error) { REprintf(\"error in noexcept R override: %s\\n\", error.what()); }\n"
                << "        catch (...) {}\n        std::terminate();\n";
        out << "    }\n";
    }
    out << "};\n";
}

void Generator::emit_overload(std::ostream& out, const Overload& overload, const std::string& kind, const std::string& indent, bool checks) {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const std::size_t count = function.parameters.size() - first;
    const std::size_t required = function.parameters.size() - function.default_arguments - first;
    out << indent << "{\n" << indent << "    static const char* const names[] = {";
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        const auto& name = i < function.parameter_names.size() ? function.parameter_names[i] : std::string{};
        out << (i > first ? ", " : "") << quote(name);
    }
    if (count == 0) out << "nullptr";
    out << "};\n" << indent << "    SEXP a[" << std::max<std::size_t>(count, 1) << "]; R_xlen_t n = 0;\n"
        << indent << "    if (rosetta_r::collect(args, names, " << count << ", " << required << ", a, &n)";
    if (overload.receives_self && checks) out << " && rosetta_r::Param<" << function.parameters[0].canonical << ">::check(self, false)";
    for (std::size_t i = 0; i < count && checks; ++i)
        out << "\n" << indent << "        && (n <= " << i << " || rosetta_r::Param<" << function.parameters[i + first].canonical
            << ">::check(a[" << i << "], strict))";
    out << ") {\n";
    const auto owner = overload.owner;
    if (kind == "method") out << indent << "        auto* native = rosetta_r::native<" << owner << ">(self);\n";
    if (overload.receives_self) out << indent << "        rosetta_r::Param<" << function.parameters[0].canonical << "> self_param(self);\n";
    const auto policy = function.is_constructor ? std::string() : return_policy(function);
    const bool parent = kind == "method" || overload.receives_self;
    for (std::size_t arity = required; arity <= count; ++arity) {
        out << indent << "        if (n == " << arity << ") {\n";
        std::string arguments;
        if (overload.receives_self) arguments = "self_param.get()";
        for (std::size_t i = 0; i < arity; ++i) {
            const auto& name = i + first < function.parameter_names.size() ? function.parameter_names[i + first] : std::string{};
            out << indent << "            rosetta_r::converting() = " << quote(name.empty() ? "argument " + std::to_string(i + 1) : "argument '" + name + "'") << ";\n"
                << indent << "            rosetta_r::Param<" << function.parameters[i + first].canonical << "> p" << i << "(a[" << i << "]);\n";
            arguments += std::string(arguments.empty() ? "" : ", ") + "p" + std::to_string(i) + ".get()";
        }
        if (arity > 0) out << indent << "            rosetta_r::converting() = nullptr;\n";
        if (kind == "constructor") {
            const auto& plan = classes_.at(owner);
            if (plan.director)
                out << indent << "            if (director) { auto* created = new " << director_name(plan) << "(" << arguments << ");\n"
                    << indent << "                return rosetta_r::adopt_director<" << owner << ">(created, created, self); }\n";
            if (plan.record->abstract) out << indent << "            rosetta_r::fail(" << quote(plan.r + " is abstract; implement it with an R6 subclass") << ");\n";
            else if (function.access != "public") out << indent << "            rosetta_r::fail(" << quote(plan.r + " constructor is protected") << ");\n";
            else out << indent << "            return rosetta_r::adopt(new " << owner << "(" << arguments << "));\n";
        } else {
            std::string body = kind == "method" ? "return native->" + function.name + "(" + arguments + ");"
                                                : "return ::" + function.qualified_name() + "(" + arguments + ");";
            // Called on an R subclass instance, a virtual method runs the native implementation, so that
            // an override calling super$method() does not dispatch back to itself.
            if (kind == "method" && function.is_virtual && policy_.directors != "none")
                body = "if (rosetta_r::is_director(self)) { " + (function.is_pure
                    ? "rosetta_r::pure_virtual(" + quote(owner + "::" + function.name) + ");"
                    : "return native->" + owner + "::" + function.name + "(" + arguments + ");") + " } " + body;
            // Mutable references to converted values update a rosetta_ref() argument after the call.
            std::string writebacks;
            for (std::size_t i = 0; i < arity; ++i) {
                const auto& type = function.parameters[i + first];
                if (type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type)))
                    writebacks += indent + "            p" + std::to_string(i) + ".writeback(a[" + std::to_string(i) + "]);\n";
            }
            const auto invocation = "rosetta_r::call([&]() -> decltype(auto) { " + body + " }, " + (parent ? "self" : "nullptr") + ", " + policy + ")";
            if (writebacks.empty()) out << indent << "            return " << invocation << ";\n";
            else out << indent << "            rosetta_r::Protect result(" << invocation << ");\n"
                     << writebacks << indent << "            return result.get();\n";
        }
        out << indent << "        }\n";
    }
    out << indent << "    }\n" << indent << "}\n";
}

std::string Generator::emit_group(std::ostream& out, const Group& group, const std::string& kind) {
    const auto name = symbol("rosetta_r_");
    const bool single = group.overloads.size() == 1;
    out << "SEXP " << name << "(SEXP self, SEXP args) {\n"
        << "    return rosetta_r::boundary([&]() -> SEXP {\n";
    if (kind != "method" && std::none_of(group.overloads.begin(), group.overloads.end(), [](const Overload& o) { return o.receives_self; }))
        out << "        (void)self;\n";
    out << "        for (int pass = " << (single ? 1 : 0) << "; pass < 2; ++pass) {\n"
        << "            const bool strict = pass == 0; (void)strict;\n";
    for (const auto& overload : group.overloads) emit_overload(out, overload, overload.extension ? "function" : kind, "            ", !single);
    std::string signatures;
    for (const auto& overload : group.overloads) signatures += "    " + signature(overload, group.name) + "\n";
    out << "        }\n        rosetta_r::no_overload(" << quote(group.name) << ", " << quote(signatures) << ", args);\n"
        << "    });\n}\n";
    routine(name, 2);
    return name;
}

std::string Generator::emit_constructor(std::ostream& out, const ClassPlan& plan) {
    const auto name = symbol("rosetta_r_");
    const bool single = plan.constructors.size() == 1;
    if (plan.director) emit_director(out, plan);
    out << "SEXP " << name << (plan.director ? "(SEXP args, SEXP self) {\n" : "(SEXP args) {\n")
        << "    return rosetta_r::boundary([&]() -> SEXP {\n";
    if (plan.director) out << "        const bool director = rosetta_r::is_subclass(self, " << quote(plan.r) << ");\n";
    out << ""
        << "        for (int pass = " << (single ? 1 : 0) << "; pass < 2; ++pass) {\n"
        << "            const bool strict = pass == 0; (void)strict;\n";
    for (const auto& overload : plan.constructors) emit_overload(out, overload, "constructor", "            ", !single);
    std::string signatures;
    for (const auto& overload : plan.constructors) signatures += "    " + signature(overload, plan.r + "$new") + "\n";
    out << "        }\n        rosetta_r::no_overload(" << quote(plan.r + "$new") << ", " << quote(signatures) << ", args);\n"
        << "    });\n}\n";
    routine(name, plan.director ? 2 : 1);
    return name;
}

GeneratedRSource Generator::run() {
    if (!identifier(name_) || name_.find('.') != std::string::npos)
        throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    for (const auto& rule : policy_.enum_flags)
        if (std::none_of(module_.enumerations.begin(), module_.enumerations.end(), [&](const Enumeration& e) { return e.qualified_name() == rule.first; }))
            throw BindingError("enum flag policy refers to an unknown enum: " + rule.first);
    GeneratedRSource result;
    std::set<std::string> variables;
    for (const auto& variable : module_.variables) variables.insert(variable.qualified_name() + ": unsupported declaration VarDecl");
    for (const auto& record : module_.records)
        if (!record.template_name.empty()) variables.insert(record.template_name + ": unsupported declaration ClassTemplateDecl");
    for (const auto& diagnostic : module_.diagnostics) {
        if (variables.count(diagnostic)) continue;
        const auto colon = diagnostic.find(": ");
        if (colon != std::string::npos && excluded(diagnostic.substr(0, colon))) continue;
        diagnostics_.push_back(diagnostic);
    }
    for (const auto& enumeration : module_.enumerations)
        if (enumeration.selected && !enumeration.enumerators.empty() && !excluded(enumeration.qualified_name()))
            enums_[enumeration.qualified_name()] = &enumeration;
    plan_classes();
    plan_directors();
    for (auto it = enums_.begin(); it != enums_.end();) {
        if (!it->second->owner.empty() && !classes_.count(it->second->owner)) it = enums_.erase(it);
        else ++it;
    }
    // Free functions attached to classes as methods.
    std::map<std::string, std::vector<Group>> extensions;
    for (const auto& [owner, methods] : policy_.extend) {
        if (!classes_.count(owner)) {
            if (records_.count(owner) && excluded(owner)) continue;
            throw BindingError("policy extend names a class that is not exported: " + owner);
        }
        for (const auto& [rname, target] : methods) {
            Group group;
            group.name = r_safe(rname);
            bool any = false, any_static = false;
            for (const auto& function : module_.functions) {
                if (function.qualified_name() != target && normalize(function.qualified_name()) != normalize(target)) continue;
                any = true;
                extension_functions_.insert(function.qualified_name());
                const auto& owner_bases = classes_.at(owner).bases;
                const bool self = !function.parameters.empty() && [&, &owner = owner] {
                    const auto& first = function.parameters[0];
                    const auto& base = first.kind == TypeKind::pointer && !first.arguments.empty() ? first.arguments[0] : referent(first);
                    const auto& bases = owner_bases;
                    return base.kind == TypeKind::record && (base.declaration == owner ||
                        std::find(bases.begin(), bases.end(), base.declaration) != bases.end());
                }();
                auto why = function_problem(function, self);
                if (!why.empty()) { reject(owner + "." + rname, why); continue; }
                if (!self) any_static = true;
                group.overloads.push_back({&function, owner, true, self});
            }
            if (!any) throw BindingError("policy extend function not found: " + target);
            group.is_static = any_static;
            if (!group.overloads.empty()) extensions[owner].push_back(std::move(group));
        }
    }

    // Methods, constructors and fields.
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        const auto& record = *plan.record;
        const auto config = policy_.classes.find(owner);
        const bool constructors = config == policy_.classes.end() || config->second.constructors;
        if (config != policy_.classes.end()) {
            for (const auto& requested : config->second.methods) {
                const auto found = std::find_if(record.methods.begin(), record.methods.end(), [&](const Function& f) { return f.name == requested; });
                if (found == record.methods.end()) throw BindingError("unknown R6 policy method: " + owner + "::" + requested);
                if (found->access != "public") throw BindingError("R6 policy method is not public: " + owner + "::" + requested);
            }
            for (const auto& requested : config->second.fields) {
                const auto found = std::find_if(record.fields.begin(), record.fields.end(), [&](const Field& f) { return f.name == requested; });
                if (found == record.fields.end()) throw BindingError("unknown R6 policy field: " + owner + "::" + requested);
                if (found->access != "public") throw BindingError("R6 policy field is not public: " + owner + "::" + requested);
            }
        }
        if (constructors && record.public_destructor && (!record.abstract || plan.director)) {
            for (const auto& constructor : record.constructors) {
                if (constructor.is_deleted || (constructor.access != "public" && !(plan.director && constructor.access == "protected"))) continue;
                if (excluded(owner + "::" + record.name, constructor.identity())) continue;
                if (constructor.parameters.size() == 1 && constructor.parameters[0].kind == TypeKind::rvalue_reference &&
                    referent(constructor.parameters[0]).declaration == owner) continue;
                auto why = function_problem(constructor, false);
                if (!why.empty()) { reject(owner + "::" + record.name, why); continue; }
                plan.constructors.push_back({&constructor, owner});
            }
        }
        std::map<std::string, std::size_t> group_index;
        std::set<std::string> names;
        for (const auto& name : r6_reserved()) if (!settable_reserved(name)) names.insert(name);
        const auto add = [&](const std::string& rname, const Overload& overload, bool is_static) {
            const auto found = group_index.find(rname);
            if (found == group_index.end()) {
                group_index[rname] = plan.methods.size();
                plan.methods.push_back({rname, {overload}, is_static});
            } else {
                auto& group = plan.methods[found->second];
                if (group.is_static != is_static) {
                    reject(overload.function->qualified_name(), "static and instance overloads cannot share an R name");
                    return;
                }
                group.overloads.push_back(overload);
            }
        };
        for (const auto& method : record.methods) {
            if (method.access != "public") continue;
            if (config != policy_.classes.end() && config->second.restrict_methods && !config->second.methods.count(method.name)) continue;
            if (excluded(method.qualified_name(), method.identity())) continue;
            std::string rname;
            if (method.name.rfind("operator", 0) == 0 && (method.name.size() == 8 || !identifier(method.name.substr(8)) || method.name == "operator bool")) {
                rname = operator_name(method.name, method.parameters.size());
                if (rname.empty()) continue; // assignment, conversion and other operators stay C++-only
            } else {
                rname = r_safe(method.name);
            }
            rname = renamed(method.qualified_name(), rname);
            rname = member_name(rname);
            auto why = function_problem(method, false);
            if (!why.empty()) { reject(method.qualified_name(), why); continue; }
            add(rname, {&method, owner}, method.is_static);
        }
        for (auto& group : extensions[owner]) for (const auto& overload : group.overloads) add(group.name, overload, group.is_static);
        for (const auto& group : plan.methods) names.insert(group.name);
        for (const auto& field : record.fields) {
            if (field.access != "public") continue;
            if (config != policy_.classes.end() && config->second.restrict_fields && !config->second.fields.count(field.name)) continue;
            const auto qualified = owner + "::" + field.name;
            if (excluded(qualified)) continue;
            auto why = type_problem(field.type, Use::result);
            if (field.type.kind == TypeKind::rvalue_reference) why = "rvalue reference fields are unsupported";
            if (field.bitfield || field.type.is_volatile) why = "unsupported R field type";
            if (field.type.canonical.find('[') != std::string::npos) why = "array fields are unsupported";
            if (!why.empty()) { reject(qualified, why.rfind("unsupported R field type", 0) == 0 ? why : "unsupported R field type: " + why); continue; }
            auto rname = renamed(qualified, r_safe(field.name));
            rname = member_name(rname);
            if (!names.insert(rname).second) throw BindingError("reserved or duplicate R6 member: " + rname);
            plan.fields.push_back({&field, rname});
        }
    }

    // Module functions grouped by R name.
    std::vector<Group> functions;
    std::map<std::string, std::size_t> function_index;
    for (const auto& function : module_.functions) {
        if (extension_functions_.count(function.qualified_name())) continue;
        if (excluded(function.qualified_name(), function.identity())) continue;
        auto why = function_problem(function, false);
        if (!why.empty()) { reject(function.qualified_name(), why); continue; }
        const auto specialization = function_template_names_.find(function.qualified_name());
        if (!function.template_name.empty() && specialization == function_template_names_.end()) continue;
        const auto rname = specialization != function_template_names_.end() ? specialization->second
            : renamed(function.qualified_name(), r_safe(flat(function.namespaces) ? function.name : exported(function.qualified_name())));
        const auto found = function_index.find(rname);
        if (found == function_index.end()) {
            function_index[rname] = functions.size();
            functions.push_back({rname, {{&function, {}}}, false});
        } else functions[found->second].overloads.push_back({&function, {}});
    }

    // Top-level names must be unique.
    std::map<std::string, std::string> names;
    const auto reserve = [&](const std::string& rname, const std::string& what) {
        const auto inserted = names.emplace(rname, what);
        if (!inserted.second) throw BindingError("R export name collision: " + rname + " (" + inserted.first->second + " and " + what + ")");
    };
    for (const auto* internal : {".rosetta_object", ".rosetta_ops", ".rosetta_callable", ".rosetta_override", ".rosetta_onload", ".rosetta_exports", "rosetta_ref",
                                  ".rosetta_guarded", ".rosetta_has_native", ".rosetta_guard"})
        reserve(internal, "Rosetta runtime");
    for (const auto& group : functions) reserve(group.name, "function " + group.overloads.front().function->qualified_name());
    for (const auto& owner : class_order_) reserve(classes_.at(owner).r, "class " + owner);

    std::map<std::string, std::string> enum_rnames; // qualified enum -> R name

    // C++ sources.
    struct Unit { std::string code, declaration; };
    std::vector<Unit> units;
    std::ostringstream out, rcode, onload;
    std::string module_namespace = "rosetta_r_module_" + name_;
    out << "// Generated by Rosetta: R native bridge for R6 classes.\n#define R_NO_REMAP\n#include <R.h>\n#include <Rinternals.h>\n"
        << "#include <Rversion.h>\n#include <R_ext/Rdynload.h>\n#include <R_ext/Visibility.h>\n";
    for (const auto* header : {"array", "chrono", "climits", "cmath", "complex", "csetjmp", "cstring", "deque", "exception", "filesystem", "functional",
                               "limits", "list", "map", "memory", "optional", "set", "stdexcept", "string", "string_view", "tuple",
                               "typeindex", "type_traits", "unordered_map", "unordered_set", "utility", "valarray", "variant", "vector"})
        out << "#include <" << header << ">\n";
    out << "#include " << quote(module_.header) << '\n'
        << "#define ROSETTA_R_MODULE " << module_namespace << '\n'
        << "#define ROSETTA_R_TAG " << quote("rosetta:" + name_) << '\n';
    for (const auto* part : detail::r6_runtime_parts) out << part;
    out << "namespace ROSETTA_R_MODULE { namespace rosetta_r {\n";
    for (const auto& owner : class_order_) {
        out << "template<> struct Bound<" << owner << "> : std::true_type {};\n";
        if (!copyable_record(owner)) out << "template<> struct Copyable<" << owner << "> : std::false_type {};\n";
    }
    for (const auto& [from, to] : policy_.conversions) {
        out << "template<> struct Value<" << from << "> {\n"
            << "    static bool check(SEXP value, bool strict) { return Value<" << to << ">::check(value, strict); }\n"
            << "    static " << from << " load(SEXP value) { return " << from << "(Value<" << to << ">::load(value)); }\n"
            << "    static SEXP cast(const " << from << "& value) { return Value<" << to << ">::cast(" << to << "(value)); }\n"
            << "};\n";
    }
    out << "} }\n";

    // R code: module functions.
    std::vector<std::string> guardable;
    for (const auto& group : functions) {
        std::ostringstream unit;
        const auto symbol_name = emit_group(unit, group, "function");
        units.push_back({unit.str(), "SEXP " + symbol_name + "(SEXP, SEXP);\n"});
        rcode << roxygen_function(group, "C++ function `" + group.overloads.front().function->qualified_name() + "`")
              << bq(group.name) << " <- function(...) " << call(group, bq(std::get<0>(routines_.back())) + ", NULL, list(...)") << "\n";
        // Every overload takes a native object, so calls without one can go to a same-named R function.
        const bool native_only = std::all_of(group.overloads.begin(), group.overloads.end(), [&](const Overload& o) {
            const auto& ps = o.function->parameters;
            return std::any_of(ps.begin(), ps.end(), [&](const Type& t) { return carries_bound(t); });
        });
        if (native_only) guardable.push_back(group.name);
    }

    // Classes. R code of each class's own members, by name, for copying into classes that
    // reach a class through a second base (R6 inherits from one class).
    std::ostringstream registration;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> public_code, active_code;
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        const auto& record = *plan.record;
        std::ostringstream unit;
        std::vector<std::string> public_members, active_members, statics;
        for (const auto& group : plan.methods) {
            const auto symbol_name = emit_group(unit, group, group.is_static ? "function" : "method");
            const auto r = bq(std::get<0>(routines_.back()));
            public_members.push_back(bq(group.name) + " = function(...) " + call(group, r + (group.is_static ? ", NULL" : ", self") + ", list(...)"));
            public_code[owner].emplace_back(group.name, public_members.back());
            if (group.is_static) {
                auto name = group.name;
                if (generator_reserved().count(name)) name += "_";
                statics.push_back(bq(plan.r) + "$" + bq(name) + " <- function(...) " + call(group, r + ", NULL, list(...)"));
            }
        }
        for (auto& field : plan.fields) {
            const auto& f = *field.field;
            const auto qualified = owner + "::" + f.name;
            const auto get = symbol("rosetta_r_"), set = symbol("rosetta_r_");
            unit << "SEXP " << get << "(SEXP self) {\n    return rosetta_r::boundary([&]() -> SEXP {\n"
                 << "        auto* native = rosetta_r::native<" << owner << ">(self);\n"
                 << "        return rosetta_r::result<decltype((native->" << f.name << "))>(native->" << f.name
                 << ", self, rosetta_r::Return::" << (copy_type(f.type) ? "copy" : "automatic") << ");\n    });\n}\n";
            const bool writable = !f.type.is_const && !readonly(qualified, owner) && f.type.kind != TypeKind::lvalue_reference;
            unit << "SEXP " << set << "(SEXP self, SEXP value) {\n    return rosetta_r::boundary([&]() -> SEXP {\n"
                 << "        auto* native = rosetta_r::native<" << owner << ">(self);\n"
                 << "        using F = std::remove_cv_t<std::remove_reference_t<decltype(native->" << f.name << ")>>;\n"
                 << "        if constexpr (rosetta_r::bound_v<F>) {\n"
                 << "            // obj$field$x <- v assigns the (modified) field object back to itself.\n"
                 << "            if (rosetta_r::is_instance<F>(value) && rosetta_r::native<F>(value) == &native->" << f.name << ") return R_NilValue;\n"
                 << "        }\n";
            if (writable)
                unit << "        rosetta_r::Param<const F&> converted(value);\n"
                     << "        rosetta_r::assign_field(native->" << f.name << ", converted.get());\n";
            else
                unit << "        (void)value;\n        rosetta_r::fail(" << quote("Readonly field " + field.name) << ");\n";
            unit << "        return R_NilValue;\n    });\n}\n";
            routine(get, 1);
            const auto rget = bq(std::get<0>(routines_.back()));
            routine(set, 2);
            const auto rset = bq(std::get<0>(routines_.back()));
            active_members.push_back(bq(field.name) + " = function(value) if (missing(value)) .Call(" + rget + ", self) else invisible(.Call(" + rset + ", self, value))");
            active_code[owner].emplace_back(field.name, active_members.back());
            units.push_back({{}, "SEXP " + get + "(SEXP);\nSEXP " + set + "(SEXP, SEXP);\n"});
        }
        std::string constructor = "stop(" + rstring(plan.r + (record.abstract ? " is abstract" : " has no public constructor")) + ")";
        if (!plan.constructors.empty()) {
            const auto symbol_name = emit_constructor(unit, plan);
            units.push_back({{}, "SEXP " + symbol_name + (plan.director ? "(SEXP, SEXP);\n" : "(SEXP);\n")});
            constructor = "{ if (!is.null(self$.ptr)) stop(\"native object already initialized\"); self$.ptr <- .Call(" +
                bq(std::get<0>(routines_.back())) + ", list(...)" + (plan.director ? ", self" : "") + ") }";
        }
        units.push_back({unit.str(), {}});
        // Members reached through second bases are copied; their routines cast self natively.
        {
            std::set<std::string> taken;
            for (const auto& group : plan.methods) taken.insert(group.name);
            for (const auto& field : plan.fields) taken.insert(field.name);
            const std::function<void(const std::string&, bool)> walk = [&](const std::string& cls, bool copy) {
                for (const auto& [name, code] : public_code[cls])
                    if (taken.insert(name).second && copy) public_members.push_back(code);
                for (const auto& [name, code] : active_code[cls])
                    if (taken.insert(name).second && copy) active_members.push_back(code);
                const auto& bases = classes_.at(cls).bases;
                for (std::size_t i = 0; i < bases.size(); ++i) walk(bases[i], copy || i > 0);
            };
            for (std::size_t i = 0; i < plan.bases.size(); ++i) walk(plan.bases[i], i > 0);
        }
        // R class definition.
        const auto inherit = plan.bases.empty() ? std::string(".rosetta_object") : classes_.at(plan.bases.front()).r;
        rcode << roxygen_class(plan) << bq(plan.r) << " <- R6::R6Class(" << rstring(plan.r) << ", inherit = " << bq(inherit) << ", cloneable = FALSE,\n"
              << "  public = list(\n    initialize = function(...) {\n"
              << "      if (...length() == 1L && inherits(..1, \"rosetta_ptr\")) self$.ptr <- ..1\n"
              << "      else " << constructor << "\n      invisible(self)\n    }";
        // Members named like an R6Class() reserved name are added with $set() afterwards.
        struct Later { const char* kind; std::string name, body; };
        std::vector<Later> later;
        const auto keep = [&](const std::vector<std::string>& members, const char* kind) {
            std::vector<std::string> kept;
            for (const auto& member : members) {
                const auto stop = member.find("` = ");
                const auto name = member.front() == '`' && stop != std::string::npos ? member.substr(1, stop - 1) : std::string();
                if (settable_reserved(name)) later.push_back({kind, name, member.substr(stop + 4)});
                else kept.push_back(member);
            }
            return kept;
        };
        const auto kept_public = keep(public_members, "public"), kept_active = keep(active_members, "active");
        for (const auto& member : kept_public) rcode << ",\n    " << member;
        rcode << "\n  )";
        if (!kept_active.empty()) {
            rcode << ",\n  active = list(";
            for (std::size_t i = 0; i < kept_active.size(); ++i) rcode << (i ? ",\n    " : "\n    ") << kept_active[i];
            rcode << "\n  )";
        }
        rcode << "\n)\n";
        for (const auto& member : later)
            rcode << bq(plan.r) << "$set(\"" << member.kind << "\", " << rstring(member.name) << ", " << member.body << ")\n";
        for (const auto& line : statics) rcode << line << '\n';
        if (!plan.parent.empty()) rcode << bq(classes_.at(plan.parent).r) << "$" << bq(plan.member) << " <- " << bq(plan.r) << '\n';
        registration << "    rosetta_r::register_type<" << owner << ">(" << quote(plan.r) << ");\n";
        for (const auto& base : plan.bases) registration << "    rosetta_r::add_base<" << owner << ", " << base << ">();\n";
        // Constant static members are set on the generator at load time.
        for (const auto& variable : record.static_fields) {
            const auto qualified = owner + "::" + variable.name;
            if (variable.access != "public" || excluded(qualified)) continue;
            if (!variable.type.is_const && !variable.is_constexpr) { reject(qualified, "mutable static members are not exported to R"); continue; }
            auto why = type_problem(variable.type, Use::result);
            if (!why.empty() || variable.type.kind == TypeKind::pointer) { reject(qualified, why.empty() ? "pointer constants are unsupported" : why); continue; }
            auto name = renamed(qualified, r_safe(variable.name));
            if (generator_reserved().count(name)) name += "_";
            onload << "    {" << quote(plan.r) << ", " << quote(name) << ", [] { return rosetta_r::Value<std::remove_cv_t<decltype(" << qualified
                   << ")>>::cast(static_cast<std::remove_cv_t<decltype(" << qualified << ")>>(" << qualified << ")); }},\n";
        }
    }
    // Method-group declarations for split sources.
    {
        std::string declarations;
        for (const auto& routine : routines_) {
            const auto& [rname, symbol_name, count] = routine;
            (void)rname;
            declarations += "SEXP " + symbol_name + "(SEXP" + (count == 2 ? ", SEXP" : "") + ");\n";
        }
        units.push_back({{}, declarations});
    }

    // Enumerations: lists of enumerator names; nested ones are also members of their class.
    std::ostringstream enum_tables;
    for (const auto& enumeration : module_.enumerations) {
        const auto qualified = enumeration.qualified_name();
        if (!enums_.count(qualified)) continue;
        const bool nested = !enumeration.owner.empty();
        auto rname = renamed(qualified, nested || flat(enumeration.namespaces) ? enumeration.name : exported(qualified));
        const auto member = rname;
        if (nested) rname = classes_.at(enumeration.owner).r + "_" + rname;
        if (names.count(rname)) {
            if (!flat_) throw BindingError("R export name collision: " + rname);
            reject(qualified, "enum name collides with another export");
            continue;
        }
        reserve(rname, "enum " + qualified);
        enum_rnames[qualified] = rname;
        std::string items, entries;
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
            const auto& enumerator = enumeration.enumerators[i];
            const auto label = renamed(qualified + "::" + enumerator, enumerator);
            const auto& value = enumeration.enumerator_values[i];
            items += std::string(items.empty() ? "" : ", ") + bq(label) + " = " + rstring(label);
            const bool negative = !value.empty() && value.front() == '-';
            const bool big = !negative && (value.size() > 19 || (value.size() == 19 && value > "9223372036854775807"));
            entries += "{" + quote(label) + ", " + (big ? "static_cast<long long>(" + value + "ULL)"
                : value == "-9223372036854775808" ? std::string("(-9223372036854775807LL - 1)") : value + "LL") + "}, ";
        }
        rcode << roxygen_enum(enumeration) << bq(rname) << " <- list(" << items << ")\n";
        if (nested) rcode << bq(classes_.at(enumeration.owner).r) << "$" << bq(member) << " <- " << bq(rname) << '\n';
        enum_tables << "    rosetta_r::EnumTable<" << qualified << ">::entries = {" << entries << "};\n";
    }
    // Constant namespace-scope variables, computed at load time.
    std::ostringstream constants;
    for (const auto& variable : module_.variables) {
        const auto qualified = variable.qualified_name();
        if (excluded(qualified)) continue;
        if (!variable.type.is_const && !variable.is_constexpr) { reject(qualified, "mutable global variables are not exported to R"); continue; }
        auto why = type_problem(variable.type, Use::result);
        if (!why.empty() || variable.type.kind == TypeKind::pointer || variable.type.kind == TypeKind::lvalue_reference) {
            reject(qualified, why.empty() ? "pointer and reference constants are unsupported" : why); continue;
        }
        const auto rname = renamed(qualified, r_safe(flat(variable.namespaces) ? variable.name : exported(qualified)));
        if (names.count(rname)) { reject(qualified, "constant name collides with another export"); continue; }
        reserve(rname, "constant " + qualified);
        constants << "    {\"\", " << quote(rname) << ", [] { return rosetta_r::Value<std::remove_cv_t<decltype(::" << qualified
                  << ")>>::cast(static_cast<std::remove_cv_t<decltype(::" << qualified << ")>>(::" << qualified << ")); }},\n";
    }
    if (!skip_ && !diagnostics_.empty()) {
        std::string error = "unsupported declaration for R6";
        for (const auto& entry : diagnostics_) error += "\n  " + entry;
        throw BindingError(error);
    }

    // Using-declarations re-export bound entities under another namespace's name.
    std::vector<std::pair<std::string, std::string>> aliases; // R alias -> R target
    {
        std::map<std::string, std::string> function_rnames;
        for (const auto& group : functions)
            for (const auto& overload : group.overloads) function_rnames.emplace(overload.function->qualified_name(), group.name);
        for (const auto& reexport : module_.reexports) {
            std::string target;
            if (reexport.kind == "record" || reexport.kind == "type") { const auto c = classes_.find(reexport.target); if (c != classes_.end()) target = c->second.r; }
            if (reexport.kind == "enum" || reexport.kind == "type") { const auto e = enum_rnames.find(reexport.target); if (e != enum_rnames.end()) target = e->second; }
            else if (reexport.kind == "function") { const auto f = function_rnames.find(reexport.target); if (f != function_rnames.end()) target = f->second; }
            if (target.empty()) continue;
            std::string scope;
            for (const auto& part : reexport.namespaces) scope += (scope.empty() ? "" : "::") + part;
            const auto qualified = scope.empty() ? reexport.name : scope + "::" + reexport.name;
            const auto rname = renamed(qualified, r_safe(flat(reexport.namespaces) ? reexport.name : exported(qualified)));
            if (rname == target || names.count(rname)) continue; // already that name, or taken by a declaration
            reserve(rname, "alias of " + reexport.target);
            aliases.emplace_back(rname, target);
        }
    }

    // Split sources by size, as the Python backend does.
    std::vector<std::ostringstream> extra(parts_ > 1 ? parts_ - 1 : 0);
    if (!extra.empty()) {
        std::vector<std::size_t> order(units.size()), sizes(extra.size(), 0), bin(units.size(), 0);
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return units[a].code.size() > units[b].code.size(); });
        for (const auto index : order) {
            if (units[index].code.empty()) continue;
            const auto smallest = static_cast<std::size_t>(std::min_element(sizes.begin(), sizes.end()) - sizes.begin());
            bin[index] = smallest;
            sizes[smallest] += units[index].code.size();
        }
        const auto prelude = out.str();
        for (auto& part : extra) part << prelude << "namespace ROSETTA_R_MODULE {\n";
        out << "namespace ROSETTA_R_MODULE {\n";
        for (std::size_t i = 0; i < units.size(); ++i) {
            if (!units[i].code.empty()) extra[bin[i]] << units[i].code;
            out << units[i].declaration;
        }
        for (auto& part : extra) part << "} // namespace ROSETTA_R_MODULE\n";
    } else {
        out << "namespace ROSETTA_R_MODULE {\n";
        for (const auto& unit : units) out << unit.code;
    }
    // Module initialization: types, enums, and the .Call table.
    out << "struct Constant { const char* owner; const char* name; SEXP (*make)(); };\n"
        << "static const Constant rosetta_constants[] = {\n" << onload.str() << constants.str() << "    {nullptr, nullptr, nullptr}};\n"
        << "// Records the environment holding the R classes; returns the constants to define there.\n"
        << "SEXP rosetta_r_init(SEXP env) {\n    return rosetta_r::boundary([&]() -> SEXP {\n"
        << "        if (TYPEOF(env) != ENVSXP) rosetta_r::fail(\"expected an environment\");\n"
        << "        if (rosetta_r::module_env()) R_ReleaseObject(rosetta_r::module_env());\n"
        << "        R_PreserveObject(env);\n        rosetta_r::module_env() = env;\n"
        << "        R_xlen_t count = 0;\n        for (const auto* c = rosetta_constants; c->name; ++c) ++count;\n"
        << "        rosetta_r::Protect result(Rf_allocVector(VECSXP, count));\n"
        << "        rosetta_r::Protect owners(Rf_allocVector(STRSXP, count));\n"
        << "        rosetta_r::Protect labels(Rf_allocVector(STRSXP, count));\n"
        << "        for (R_xlen_t i = 0; i < count; ++i) {\n"
        << "            SET_VECTOR_ELT(result.get(), i, rosetta_constants[i].make());\n"
        << "            SET_STRING_ELT(owners.get(), i, Rf_mkChar(rosetta_constants[i].owner));\n"
        << "            SET_STRING_ELT(labels.get(), i, Rf_mkChar(rosetta_constants[i].name));\n"
        << "        }\n"
        << "        Rf_setAttrib(result.get(), R_NamesSymbol, labels.get());\n"
        << "        Rf_setAttrib(result.get(), Rf_install(\"owners\"), owners.get());\n"
        << "        return result.get();\n    });\n}\n"
        << "SEXP rosetta_r_release(SEXP self) { return rosetta_r::boundary([&]() -> SEXP { return rosetta_r::release_object(self); }); }\n"
        << "SEXP rosetta_r_invoke(SEXP callable, SEXP args) { return rosetta_r::boundary([&]() -> SEXP { return rosetta_r::invoke_callable(callable, args); }); }\n"
        << "} // namespace ROSETTA_R_MODULE\n"
        << "static const R_CallMethodDef rosetta_calls[] = {\n"
        << "    {\".rs_init\", reinterpret_cast<DL_FUNC>(&ROSETTA_R_MODULE::rosetta_r_init), 1},\n"
        << "    {\".rs_release\", reinterpret_cast<DL_FUNC>(&ROSETTA_R_MODULE::rosetta_r_release), 1},\n"
        << "    {\".rs_invoke\", reinterpret_cast<DL_FUNC>(&ROSETTA_R_MODULE::rosetta_r_invoke), 2},\n";
    for (const auto& [rname, symbol_name, count] : routines_)
        out << "    {" << quote(rname) << ", reinterpret_cast<DL_FUNC>(&ROSETTA_R_MODULE::" << symbol_name << "), " << count << "},\n";
    out << "    {nullptr, nullptr, 0}};\n"
        << "extern \"C\" void attribute_visible R_init_" << name_ << "(DllInfo* dll) {\n"
        << "    using namespace ROSETTA_R_MODULE;\n";
    if (package_) out << "    rosetta_r::package_name() = " << quote(name_) << ";\n";
    out << registration.str() << enum_tables.str()
        << "    R_registerRoutines(dll, nullptr, rosetta_calls, nullptr, nullptr);\n"
        << "    R_useDynamicSymbols(dll, FALSE);\n    R_forceSymbols(dll, TRUE);\n}\n";
    result.source = out.str();
    for (auto& part : extra) result.parts.push_back(part.str());

    // R interface.
    std::ostringstream r;
    r << "# Generated by Rosetta: R6 classes for the " << name_ << " native library.\n";
    if (package_)
        r << "# Package code: NAMESPACE needs useDynLib(" << name_ << ", .registration = TRUE), importFrom(R6, R6Class),\n"
          << "# S3method(Ops, RosettaObject, .rosetta_ops); .onLoad() must call .rosetta_onload().\n";
    else
        r << "# Load the shared library with dyn.load() before sourcing; the result is a list of exports.\n"
          << bq(name_) << " <- local({\n"
          << "if (!requireNamespace(\"R6\", quietly = TRUE)) stop(\"Install the R6 package\")\n"
          << ".rosetta_dll <- getLoadedDLLs()[[" << rstring(name_) << "]]\n"
          << "if (is.null(.rosetta_dll)) stop(\"Load the " << name_ << " shared library with dyn.load() first\")\n"
          << ".rosetta_routines <- getDLLRegisteredRoutines(.rosetta_dll)$.Call\n"
          << "for (.rosetta_name in names(.rosetta_routines)) assign(.rosetta_name, .rosetta_routines[[.rosetta_name]])\n";
    r << ".rosetta_object <- R6::R6Class(\"RosettaObject\", cloneable = FALSE, public = list(\n"
      << "  .ptr = NULL,\n"
      << "  close = function() { .Call(`.rs_release`, self); invisible(NULL) },\n"
      << "  print = function(...) { cat(\"<\", class(self)[[1L]], \">\\n\", sep = \"\"); invisible(self) }\n))\n"
      << ".rosetta_ops <- function(e1, e2) {\n"
      << "  unary <- missing(e2)\n"
      << "  name <- switch(.Generic, \"+\" = if (unary) \"__pos__\" else \"__add__\", \"-\" = if (unary) \"__neg__\" else \"__sub__\",\n"
      << "    \"*\" = \"__mul__\", \"/\" = \"__truediv__\", \"%%\" = \"__mod__\", \"==\" = \"__eq__\", \"!=\" = \"__ne__\", \"<\" = \"__lt__\",\n"
      << "    \"<=\" = \"__le__\", \">\" = \"__gt__\", \">=\" = \"__ge__\", \"&\" = \"__and__\", \"|\" = \"__or__\", \"!\" = \"__not__\", NULL)\n"
      << "  method <- if (!is.null(name) && inherits(e1, \"RosettaObject\")) e1[[name]] else NULL\n"
      << "  if (!is.function(method)) {\n"
      << "    if (.Generic %in% c(\"==\", \"!=\") && !unary && inherits(e1, \"RosettaObject\") && inherits(e2, \"RosettaObject\"))\n"
      << "      return((.Generic == \"==\") == identical(e1, e2))\n"
      << "    stop(\"operator \", .Generic, \" is not defined for \", class(e1)[[1L]])\n"
      << "  }\n"
      << "  if (unary) method() else method(e2)\n}\n"
      << ".rosetta_callable <- function(ptr) { force(ptr); function(...) .Call(`.rs_invoke`, ptr, list(...)) }\n"
      << "#' A mutable cell for C++ reference parameters\n#'\n"
      << "#' A native function that takes a non-const C++ reference updates `x$value` when called with\n"
      << "#' `x <- rosetta_ref(value)`; plain R values are passed by copy.\n#'\n"
      << "#' @param value The initial value.\n#' @return An environment of class `rosetta_ref` holding `value`.\n#' @md\n"
      << "rosetta_ref <- function(value = NULL) {\n"
      << "  ref <- new.env(parent = emptyenv())\n  ref$value <- value\n  class(ref) <- \"rosetta_ref\"\n  ref\n}\n"
      << "# The R6 subclass override of a virtual method, or NULL where the generated native method applies.\n"
      << ".rosetta_override <- function(self, name) {\n"
      << "  method <- get0(name, envir = self, inherits = FALSE)\n"
      << "  if (!is.function(method)) return(NULL)\n"
      << "  code <- body(method)\n"
      << "  if (is.call(code) && identical(code[[1L]], quote(invisible))) code <- code[[2L]]\n"
      << "  native <- is.call(code) && identical(code[[1L]], quote(.Call)) && is.name(code[[2L]]) && startsWith(as.character(code[[2L]]), \".rs_\")\n"
      << "  if (native) NULL else method\n}\n";
    r << rcode.str();
    // Functions named like an R function from a default package (sum, log, all, ...) would hide it
    // wherever this code is evaluated; calls passing no native object go to the R function instead.
    if (!guardable.empty()) {
        r << ".rosetta_guarded <- c(";
        for (std::size_t i = 0; i < guardable.size(); ++i) r << (i ? ", " : "") << rstring(guardable[i]);
        r << ")\n"
          << ".rosetta_has_native <- function(x) base::inherits(x, \"RosettaObject\") ||\n"
          << "  (base::is.list(x) && !base::is.object(x) && base::any(base::vapply(x, base::inherits, base::logical(1L), \"RosettaObject\")))\n"
          << ".rosetta_guard <- function(native, fallback) {\n"
          << "  base::force(native); base::force(fallback)\n"
          << "  function(...) if (base::any(base::vapply(base::list(...), .rosetta_has_native, base::logical(1L)))) native(...) else fallback(...)\n}\n"
          << "for (.rosetta_name in .rosetta_guarded) {\n"
          << "  for (.rosetta_pkg in base::c(\"base\", \"stats\", \"utils\", \"methods\", \"graphics\", \"grDevices\")) {\n"
          << "    .rosetta_fallback <- base::get0(.rosetta_name, envir = base::asNamespace(.rosetta_pkg), inherits = FALSE)\n"
          << "    if (base::is.function(.rosetta_fallback)) {\n"
          << "      base::assign(.rosetta_name, .rosetta_guard(base::get(.rosetta_name), .rosetta_fallback))\n"
          << "      break\n    }\n  }\n}\n"
          << "base::rm(list = base::intersect(base::c(\".rosetta_name\", \".rosetta_pkg\", \".rosetta_fallback\"), base::ls(all.names = TRUE)))\n";
    }
    for (const auto& [alias, target] : aliases) r << "#' @rdname " << target << "\n" << bq(alias) << " <- " << bq(target) << "\n";
    r << ".rosetta_onload <- function(env = parent.frame()) {\n"
      << "  values <- .Call(`.rs_init`, env)\n"
      << "  owners <- attr(values, \"owners\")\n"
      << "  for (i in seq_along(values)) {\n"
      << "    if (nzchar(owners[[i]])) assign(names(values)[[i]], values[[i]], envir = get(owners[[i]], envir = env))\n"
      << "    else assign(names(values)[[i]], values[[i]], envir = env)\n"
      << "  }\n";
    if (!package_) r << "  registerS3method(\"Ops\", \"RosettaObject\", .rosetta_ops, envir = env)\n";
    r << "  invisible(NULL)\n}\n";
    std::vector<std::string> exports;
    for (const auto& [rname, what] : names) if (rname.front() != '.') exports.push_back(rname);
    r << ".rosetta_exports <- c(";
    for (std::size_t i = 0; i < exports.size(); ++i) r << (i ? ", " : "") << rstring(exports[i]);
    r << ")\n";
    if (!package_) r << ".rosetta_onload(environment())\nmget(.rosetta_exports, envir = environment())\n})\n";
    result.interface = r.str();
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

namespace detail {
GeneratedRSource generate_r6(const Module& module, const std::string& name, bool skip_unsupported,
                             const BindingPolicy& policy, bool package, std::size_t parts) {
    if (parts == 0 || parts > 256) throw BindingError("R source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, package, parts).run();
}
} // namespace detail
} // namespace rosetta
