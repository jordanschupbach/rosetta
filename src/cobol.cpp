#include "rosetta/cobol.hpp"
#include "cobol_runtime.hpp"
#include "type_spelling.hpp"
#include <algorithm>
#include <functional>
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
bool digit(char c) { return c >= '0' && c <= '9'; }
char upper(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; }
char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
std::string replace_all(std::string text, const std::string& from, const std::string& to) {
    for (std::size_t i = 0; (i = text.find(from, i)) != std::string::npos; i += to.size()) text.replace(i, from.size(), to);
    return text;
}
std::string join(const std::vector<std::string>& parts, const std::string& separator = ", ") {
    std::string text;
    for (const auto& part : parts) text += (text.empty() ? "" : separator) + part;
    return text;
}
// A C++ identifier as a COBOL word part: upper case, with hyphens for
// underscores and between camelCase words (AsyncTask is ASYNC-TASK), none
// leading or trailing. Empty when nothing usable is left.
std::string word(const std::string& text) {
    std::string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (!letter(c) && !digit(c) && c != '_') return {};
        if (i > 0 && c >= 'A' && c <= 'Z' && text[i - 1] >= 'a' && text[i - 1] <= 'z') result += '-';
        result += c == '_' ? '-' : upper(c);
    }
    const auto first = result.find_first_not_of('-'), last = result.find_last_not_of('-');
    return first == std::string::npos ? std::string() : result.substr(first, last - first + 1);
}
std::string symbol(const std::string& cobol_word) {
    std::string result;
    for (char c : cobol_word) result += c == '-' ? '_' : lower(c);
    return result;
}
// Every generated word fits fixed format's columns 8-72 at its level.
constexpr std::size_t max_word = 56;

// A supported C++ scalar: its COBOL usage, field size, the fixed-width type
// the field holds, and a code (i signed, u unsigned, f real, b boolean,
// c character, s string).
struct Scalar { std::string cpp, usage, abi; std::size_t size; char code; };
std::optional<Scalar> scalar(const Type& type, std::size_t string_size) {
    auto text = detail::trim_type(type.canonical);
    const bool reference = text.rfind("const ", 0) == 0 && !text.empty() && text.back() == '&' &&
        (text.size() < 2 || text[text.size() - 2] != '&');
    if (reference) text = detail::trim_type(text.substr(0, text.size() - 1));
    if (text.rfind("const ", 0) == 0) text = text.substr(6);
    if (text == "std::__cxx11::basic_string<char>" || text == "std::__1::basic_string<char>" || text == "std::basic_string<char>")
        text = "std::string";
    if (reference && text != "std::string") return std::nullopt;
    struct Row { const char* usage; const char* abi; std::size_t size; char code; };
    static const std::map<std::string, Row> table = {
        {"bool", {"USAGE BINARY-CHAR UNSIGNED", "std::uint8_t", 1, 'b'}}, {"char", {"PIC X", "char", 1, 'c'}},
        {"signed char", {"USAGE BINARY-CHAR SIGNED", "std::int8_t", 1, 'i'}}, {"unsigned char", {"USAGE BINARY-CHAR UNSIGNED", "std::uint8_t", 1, 'u'}},
        {"short", {"USAGE BINARY-SHORT SIGNED", "std::int16_t", 2, 'i'}}, {"unsigned short", {"USAGE BINARY-SHORT UNSIGNED", "std::uint16_t", 2, 'u'}},
        {"int", {"USAGE BINARY-LONG SIGNED", "std::int32_t", 4, 'i'}}, {"unsigned int", {"USAGE BINARY-LONG UNSIGNED", "std::uint32_t", 4, 'u'}},
        {"long", {"USAGE BINARY-DOUBLE SIGNED", "std::int64_t", 8, 'i'}}, {"unsigned long", {"USAGE BINARY-DOUBLE UNSIGNED", "std::uint64_t", 8, 'u'}},
        {"long long", {"USAGE BINARY-DOUBLE SIGNED", "std::int64_t", 8, 'i'}},
        {"unsigned long long", {"USAGE BINARY-DOUBLE UNSIGNED", "std::uint64_t", 8, 'u'}},
        {"float", {"USAGE COMP-1", "float", 4, 'f'}}, {"double", {"USAGE COMP-2", "double", 8, 'f'}}};
    if (text == "std::string") return Scalar{text, "PIC X(" + std::to_string(string_size) + ")", "", string_size, 's'};
    const auto found = table.find(text);
    if (found == table.end()) return std::nullopt;
    return Scalar{text, found->second.usage, found->second.abi, found->second.size, found->second.code};
}
std::string problem(const Function& fn, std::size_t string_size) {
    if (!fn.unsupported_reason.empty()) return fn.unsupported_reason;
    if (fn.is_deleted) return "deleted function";
    if (fn.default_arguments > fn.parameters.size()) return "invalid default argument metadata";
    if (!fn.is_constructor && fn.result.canonical != "void" && !scalar(fn.result, string_size))
        return "unsupported COBOL result type " + fn.result.canonical;
    for (const auto& type : fn.parameters)
        if (!scalar(type, string_size)) return "unsupported COBOL parameter type " + type.canonical;
    return {};
}
// One field (slot) of a parameter record; strings also have a BINARY-LONG length after them.
struct Slot { std::string name; Scalar type; std::size_t offset = 0; };
enum class Role { function, method, constructor, close, field };
struct Entry {
    Role role = Role::function;
    std::string label, signature, record, symbol, setter; // setter: a field's set stub
    std::string call, native, type;  // the C++ expression (@ARGS@), the object's type and its name
    std::optional<std::size_t> self, count;
    std::vector<Slot> params;
    std::size_t required = 0;
    std::optional<Slot> result;     // for a field record: its VALUE
    bool writable = false;
    std::size_t size = 0;
};
// Copybook lines: level 01 starts in column 8, 05 in 11 and 88 in 14; a
// clause that would pass column 72 goes on the next line.
void line(std::ostream& out, int level, const std::string& name, const std::string& clause) {
    const std::string indent(level == 1 ? 7 : level == 5 ? 10 : 13, ' ');
    const auto head = indent + (level == 1 ? "01" : level == 5 ? "05" : "88") + " " + name;
    if (clause.empty()) out << head << ".\n";
    else if (head.size() + 1 + clause.size() + 1 <= 72) out << head << ' ' << clause << ".\n";
    else out << head << '\n' << indent << "      " << clause << ".\n";
}
// A comment, wrapped to column 72.
void comment(std::ostream& out, const std::string& text) {
    std::string current;
    std::istringstream words(text);
    std::string piece;
    while (words >> piece) {
        if (!current.empty() && 9 + current.size() + 1 + piece.size() > 72) {
            out << "      *> " << current << '\n';
            current.clear();
        }
        current += (current.empty() ? "" : " ") + piece;
    }
    out << "      *> " << current << '\n';
}
} // namespace

GeneratedCobolSource generate_cobol(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy,
                                    std::size_t string_size) {
    if (name.empty() || !letter(name.front()) || name.back() == '_' ||
        !std::all_of(name.begin(), name.end(), [](char c) { return letter(c) || digit(c) || c == '_'; }))
        throw BindingError("COBOL module name must be an ASCII identifier starting with a letter and not ending in '_'");
    const auto prefix = word(name);
    if (prefix.size() + 19 > max_word) throw BindingError("COBOL module name is too long for its record names");
    if (string_size < 1 || string_size > 65535) throw BindingError("COBOL string size must be between 1 and 65535");
    if (!policy.callbacks.empty() || !policy.enum_flags.empty())
        throw BindingError("COBOL backend does not yet support callback or enum flag policies");
    GeneratedCobolSource result{{}, {}, module.diagnostics};
    auto reject = [&](const std::string& context, const std::string& reason) { result.diagnostics.push_back(context + ": " + reason); };
    // COBOL words are case-insensitive, and every record and field shares one
    // namespace; C symbols are checked separately.
    std::set<std::string> words = {prefix + "-STATUS"}, symbols;
    for (const auto* suffix : {"-STATUS-CODE", "-STATUS-KIND", "-STATUS-LENGTH", "-STATUS-MESSAGE", "-STATUS-OK", "-STATUS-ALLOCATION",
                               "-STATUS-RANGE", "-STATUS-INDEX", "-STATUS-ARGUMENT", "-STATUS-NATIVE", "-STATUS-CLOSED"})
        words.insert(prefix + suffix);
    std::vector<Entry> entries;
    // Lays out ENTRY's record, checking its names. Returns the reason it
    // cannot be bound, or empty.
    auto layout = [&](Entry& entry, const Function* fn) -> std::string {
        std::vector<std::string> names{entry.record};
        std::size_t offset = 0;
        if (entry.role == Role::method || entry.role == Role::close || entry.role == Role::field) {
            names.push_back(entry.record + "-SELF");
            entry.self = offset;
            offset += 8;
        }
        if (fn) {
            entry.required = fn->parameters.size() - fn->default_arguments;
            if (entry.required < fn->parameters.size()) {
                names.push_back(entry.record + "-GIVEN");
                entry.count = offset;
                offset += 4;
            }
            std::vector<std::string> parameters;
            for (std::size_t i = 0; i < fn->parameters.size(); ++i) {
                const auto given = i < fn->parameter_names.size() ? fn->parameter_names[i] : std::string();
                auto part = word(given);
                const auto fallback = "ARG-" + std::to_string(i + 1);
                if (part.empty() || part == "SELF" || part == "GIVEN" || part == "RESULT" ||
                    std::find(names.begin(), names.end(), entry.record + "-" + part) != names.end())
                    part = fallback;
                Slot field{entry.record + "-" + part, *scalar(fn->parameters[i], string_size), offset};
                names.push_back(field.name);
                offset += field.type.size;
                if (field.type.code == 's') { names.push_back(field.name + "-LENGTH"); offset += 4; }
                entry.params.push_back(field);
                parameters.push_back(fn->parameters[i].spelling + (given.empty() ? "" : " " + given));
            }
            entry.signature = entry.label + "(" + join(parameters) + ")";
            if (entry.role == Role::constructor) entry.result = Slot{entry.record + "-RESULT", Scalar{"", "USAGE BINARY-DOUBLE UNSIGNED", "std::uint64_t", 8, 'h'}, 0};
            else if (fn->result.canonical != "void") entry.result = Slot{entry.record + "-RESULT", *scalar(fn->result, string_size), 0};
        }
        if (entry.result) {
            if (entry.role == Role::field) entry.result->name = entry.record + "-VALUE";
            entry.result->offset = offset;
            names.push_back(entry.result->name);
            offset += entry.result->type.size;
            if (entry.result->type.code == 's') { names.push_back(entry.result->name + "-LENGTH"); offset += 4; }
        }
        entry.size = offset;
        for (const auto& candidate : names)
            if (candidate.size() > max_word) return "COBOL name is longer than " + std::to_string(max_word) + " characters: " + candidate;
        for (const auto& candidate : names)
            if (!words.insert(candidate).second) throw BindingError("COBOL name collision: " + candidate);
        for (const auto& candidate : {entry.symbol, entry.setter})
            if (!candidate.empty() && !symbols.insert(candidate).second) throw BindingError("COBOL CALL name collision: " + candidate);
        return {};
    };
    auto add = [&](Entry entry, const Function* fn, const std::string& context) {
        const auto why = layout(entry, fn);
        if (!why.empty()) { reject(context, why); return false; }
        entries.push_back(std::move(entry));
        return true;
    };

    std::map<std::string, std::size_t> counts;
    for (const auto& fn : module.functions) ++counts[fn.qualified_name()];
    for (const auto& fn : module.functions) {
        auto why = problem(fn, string_size);
        if (counts[fn.qualified_name()] > 1) why = "overloads require an explicit facade";
        const auto part = word(fn.name);
        if (why.empty() && part.empty()) why = "name cannot be a COBOL word";
        if (!why.empty()) { reject(fn.qualified_name(), why); continue; }
        Entry entry;
        entry.label = fn.qualified_name();
        entry.record = prefix + "-" + part;
        entry.symbol = symbol(entry.record);
        entry.call = "::" + fn.qualified_name() + "(@ARGS@)";
        add(std::move(entry), &fn, fn.qualified_name());
    }
    for (const auto& selection : policy.classes)
        if (std::none_of(module.records.begin(), module.records.end(), [&](const Record& r) { return r.qualified_name() == selection.first; }))
            throw BindingError("unknown class in COBOL policy: " + selection.first);
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
            if (found == rec.methods.end()) throw BindingError("unknown COBOL policy method: " + owner + "::" + requested);
            if (found->access != "public") throw BindingError("COBOL policy method is not public: " + owner + "::" + requested);
        }
        for (const auto& requested : config.fields) {
            const auto found = std::find_if(rec.fields.begin(), rec.fields.end(), [&](const Field& f) { return f.name == requested; });
            if (found == rec.fields.end()) throw BindingError("unknown COBOL policy field: " + owner + "::" + requested);
            if (found->access != "public") throw BindingError("COBOL policy field is not public: " + owner + "::" + requested);
        }
        if (!rec.complete || rec.abstract || rec.has_bases || !rec.public_destructor || !rec.unsupported_reason.empty()) {
            reject(owner, "COBOL requires a complete, concrete class without bases and a public nonthrowing destructor"); continue;
        }
        if (!config.borrow_self.empty()) { reject(owner, "borrowed COBOL results are not yet supported"); continue; }
        const auto klass = word(rec.name);
        if (klass.empty()) { reject(owner, "name cannot be a COBOL word"); continue; }
        const auto native = "::" + owner, base = prefix + "-" + klass;
        auto member = [&](Entry entry, const Function* fn, const std::string& context) {
            entry.native = native;
            entry.type = owner;
            return add(std::move(entry), fn, context);
        };
        Entry close;
        close.role = Role::close;
        close.label = owner + "::~" + rec.name;
        close.signature = close.label + "()";
        close.record = base + "-CLOSE";
        close.symbol = symbol(close.record);
        member(std::move(close), nullptr, owner);
        std::vector<const Function*> constructors;
        for (const auto& ctor : rec.constructors) if (!ctor.is_deleted) constructors.push_back(&ctor);
        if (config.constructors) {
            if (constructors.size() != 1) reject(owner, "constructor overloads require an explicit facade");
            else {
                const auto& ctor = *constructors.front();
                auto why = problem(ctor, string_size);
                if (ctor.access != "public") why = "constructor is not public";
                if (!why.empty()) reject(owner, why);
                else {
                    Entry entry;
                    entry.role = Role::constructor;
                    entry.label = owner + "::" + rec.name;
                    entry.record = base + "-NEW";
                    entry.symbol = symbol(entry.record);
                    entry.call = "std::make_unique<" + native + ">(@ARGS@)";
                    member(std::move(entry), &ctor, owner);
                }
            }
        }
        counts.clear();
        for (const auto& fn : rec.methods) ++counts[fn.name];
        for (const auto& fn : rec.methods) {
            if (fn.access != "public" || (config.restrict_methods && !config.methods.count(fn.name))) continue;
            auto why = problem(fn, string_size);
            if (counts[fn.name] > 1) why = "method overloads require an explicit facade";
            const auto part = word(fn.name);
            if (why.empty() && part.empty()) why = "name cannot be a COBOL word";
            if (!why.empty()) { reject(owner + "::" + fn.name, why); continue; }
            Entry entry;
            entry.role = fn.is_static ? Role::function : Role::method;
            entry.label = owner + "::" + fn.name;
            entry.record = base + "-" + part;
            entry.symbol = symbol(entry.record);
            entry.call = (fn.is_static ? native + "::" : std::string("object.")) + fn.name + "(@ARGS@)";
            member(std::move(entry), &fn, owner + "::" + fn.name);
        }
        for (const auto& field : rec.fields) {
            if (field.access != "public" || (config.restrict_fields && !config.fields.count(field.name))) continue;
            const auto value = scalar(field.type, string_size);
            const auto part = word(field.name);
            if (!value || field.bitfield || field.type.is_volatile || part.empty()) { reject(owner + "::" + field.name, "unsupported COBOL field type"); continue; }
            Entry entry;
            entry.role = Role::field;
            entry.label = owner + "::" + field.name;
            entry.signature = field.type.spelling + " " + entry.label;
            entry.record = base + "-" + part;
            entry.symbol = symbol(base + "-GET-" + part);
            // Const fields have no setter.
            entry.writable = !field.type.is_const;
            if (entry.writable) entry.setter = symbol(base + "-SET-" + part);
            entry.result = Slot{"", *value, 0};
            entry.call = "object." + field.name;
            const auto context = entry.label;
            member(std::move(entry), nullptr, context);
        }
    }
    for (const auto& enumeration : module.enumerations)
        if (enumeration.selected) reject(enumeration.qualified_name(), "COBOL enum bindings are not yet supported");
    if (!skip && !result.diagnostics.empty()) {
        std::string error = "unsupported declaration for COBOL";
        for (const auto& entry : result.diagnostics) error += "\n  " + entry;
        throw BindingError(error);
    }

    // The copybook.
    std::ostringstream cpy;
    comment(cpy, "Generated by Rosetta: COBOL bindings for " + name + ". COPY this into WORKING-STORAGE, then CALL each entry USING its "
            "parameter record and " + prefix + "-STATUS, for example: CALL \"" + (entries.empty() ? name : entries.front().symbol) +
            "\" USING " + (entries.empty() ? prefix : entries.front().record) + " " + prefix + "-STATUS. RETURN-CODE receives the "
            "status code.");
    comment(cpy, "Fields have their C++ types' exact sizes. Strings are PIC X(" + std::to_string(string_size) + ") fields with a "
            "-LENGTH: -1 sends the text without trailing spaces, 0 to " + std::to_string(string_size) + " that many bytes; results set it.");
    comment(cpy, "Calls with C++ defaults have a -GIVEN count of the arguments to send (initially the required ones). Objects are "
            "handles: -NEW returns one in -RESULT, methods take it in -SELF, and -CLOSE destroys it.");
    cpy << '\n';
    line(cpy, 1, prefix + "-STATUS", "");
    line(cpy, 5, prefix + "-STATUS-CODE", "USAGE BINARY-LONG SIGNED VALUE 0");
    const char* kinds[] = {"OK", "ALLOCATION", "RANGE", "INDEX", "ARGUMENT", "NATIVE", "CLOSED"};
    for (int i = 0; i < 7; ++i) line(cpy, 88, prefix + "-STATUS-" + kinds[i], "VALUE " + std::to_string(i));
    line(cpy, 5, prefix + "-STATUS-KIND", "PIC X(10) VALUE SPACES");
    line(cpy, 5, prefix + "-STATUS-LENGTH", "USAGE BINARY-LONG SIGNED VALUE 0");
    line(cpy, 5, prefix + "-STATUS-MESSAGE", "PIC X(256) VALUE SPACES");
    auto field = [&](const Slot& f, const std::string& length_value) {
        line(cpy, 5, f.name, f.type.usage);
        if (f.type.code == 's') line(cpy, 5, f.name + "-LENGTH", "USAGE BINARY-LONG SIGNED VALUE " + length_value);
    };
    for (const auto& e : entries) {
        cpy << '\n';
        std::string calls = "CALL \"" + e.symbol + "\"";
        if (e.role == Role::field) calls = "CALL \"" + e.symbol + "\" reads it" + (e.writable ? ", CALL \"" + e.setter + "\" writes it" : "");
        comment(cpy, (e.role == Role::close ? "Destroys a " + e.type + " (closing twice is harmless)" : e.signature) + ": " + calls + ".");
        line(cpy, 1, e.record, "");
        if (e.self) line(cpy, 5, e.record + "-SELF", "USAGE BINARY-DOUBLE UNSIGNED");
        if (e.count) line(cpy, 5, e.record + "-GIVEN", "USAGE BINARY-LONG SIGNED VALUE " + std::to_string(e.required));
        for (const auto& p : e.params) field(p, "-1");
        if (e.result) field(*e.result, e.role == Role::field && e.writable ? "-1" : "0");
        if (e.size == 0) line(cpy, 5, "FILLER", "PIC X");
    }
    result.copybook = cpy.str();

    // The C++ stubs.
    std::ostringstream out;
    out << "// Generated by Rosetta: C++ stubs for the COBOL copybook " << name << ".cpy. Each stub takes its\n"
        << "// parameter record and the status record, and returns the status code.\n"
        << "#include <algorithm>\n#include <cstddef>\n#include <cstdint>\n#include <cstdlib>\n#include <cstring>\n#include <limits>\n"
        << "#include <map>\n#include <memory>\n#include <mutex>\n#include <new>\n#include <stdexcept>\n#include <string>\n"
        << "#include <type_traits>\n#include <utility>\n#include " << quote(module.header) << '\n'
        << replace_all(detail::cobol_runtime, "@MESSAGE@", "256");
    auto read = [&](const Slot& f) {
        const auto at = "record + " + std::to_string(f.offset);
        switch (f.type.code) {
        case 's': return "rosetta_cobol::text(" + at + ", " + std::to_string(f.type.size) + ", record + " + std::to_string(f.offset + f.type.size) + ")";
        case 'b': return "rosetta_cobol::boolean(rosetta_cobol::load<std::uint8_t>(" + at + "))";
        case 'c': case 'f': return "rosetta_cobol::load<" + f.type.abi + ">(" + at + ")";
        default: return "rosetta_cobol::integer<" + f.type.cpp + ">(rosetta_cobol::load<" + f.type.abi + ">(" + at + "))";
        }
    };
    auto write = [&](const Slot& f, const std::string& value) {
        const auto at = "record + " + std::to_string(f.offset);
        switch (f.type.code) {
        case 's': return "rosetta_cobol::result_text(" + at + ", " + std::to_string(f.type.size) + ", record + " + std::to_string(f.offset + f.type.size) + ", " + value + ")";
        case 'h': return "rosetta_cobol::store<std::uint64_t>(" + at + ", rosetta_cobol::adopt(" + value + "))";
        case 'b': return "rosetta_cobol::store<std::uint8_t>(" + at + ", (" + value + ") ? 1 : 0)";
        default: return "rosetta_cobol::store<" + f.type.abi + ">(" + at + ", static_cast<" + f.type.abi + ">(" + value + "))";
        }
    };
    auto stub = [&](const Entry& e, const std::string& symbol, const std::function<void()>& body) {
        out << "// " << (e.signature.empty() ? e.label : e.signature) << "\nextern \"C\" int " << symbol
            << "(unsigned char* record, unsigned char* status) {\n(void)record;\nreturn rosetta_cobol::guard(status, " << quote(e.label) << ", [&] {\n";
        body();
        out << "});\n}\n";
    };
    const auto self = [](const Entry& e) { return "rosetta_cobol::load<std::uint64_t>(record + " + std::to_string(*e.self) + ")"; };
    for (const auto& e : entries) {
        if (e.role == Role::close) {
            stub(e, e.symbol, [&] { out << "rosetta_cobol::close<" << e.native << ">(" << self(e) << ", " << quote(e.type) << ");\n"; });
            continue;
        }
        const auto object = [&] { out << "auto& object = rosetta_cobol::live<" << e.native << ">(" << self(e) << ", " << quote(e.type) << ");\n"; };
        if (e.role == Role::field) {
            stub(e, e.symbol, [&] { object(); out << write(*e.result, e.call) << ";\n"; });
            if (e.writable) stub(e, e.setter, [&] { object(); out << e.call << " = " << read(*e.result) << ";\n"; });
            continue;
        }
        stub(e, e.symbol, [&] {
            if (e.role == Role::method) object();
            auto body = [&](std::size_t present) {
                std::vector<std::string> actuals;
                for (std::size_t i = 0; i < present; ++i) {
                    out << "auto value" << i << " = " << read(e.params[i]) << ";\n";
                    actuals.push_back("std::move(value" + std::to_string(i) + ")");
                }
                const auto invocation = replace_all(e.call, "@ARGS@", join(actuals));
                if (e.result) out << write(*e.result, invocation) << ";\n";
                else out << invocation << ";\n";
            };
            if (!e.count) body(e.params.size());
            else {
                out << "switch (rosetta_cobol::count(record + " << *e.count << ", " << e.required << ", " << e.params.size() << ")) {\n";
                for (std::size_t present = e.required; present <= e.params.size(); ++present) {
                    out << "case " << present << ": {\n";
                    body(present);
                    out << "return;\n}\n";
                }
                out << "}\n";
            }
        });
    }
    result.source = out.str();
    return result;
}
} // namespace rosetta
