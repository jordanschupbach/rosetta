// Perl backend (--language perl): one Perl 5.14+ module (.pm) over the shared C ABI library of
// the full C ABI backends (cabi_full.hpp, lisp_full_runtime.hpp). The library's first source also
// carries a small XS glue (three XSUBs and the two callback hooks, against Perl's own headers);
// the module loads it with DynaLoader, so there are no CPAN dependencies.
//
// Classes are packages whose @ISA lists their C++ base classes (multiple inheritance included)
// over NativeObject; objects are hashes holding the C++ object. Constructors are `new`; a Perl
// subclass overrides C++ virtual methods: constructing it creates a C++ subclass whose virtual
// methods call the Perl methods (SUPER:: runs the C++ implementation). Overloads of a name are one
// sub; C++ resolves the call at run time, and only the arguments a call passes cross, so C++
// applies its default arguments. Fields are accessors ($v->x, $v->x(5)), operators `use
// overload`, enums packages of constants, containers array and hash references, std::function
// code references, non-const reference parameters written back, C++ exceptions objects of the
// Error classes. Doxygen becomes POD.
#include "rosetta/perl.hpp"
#include "cabi_full.hpp"
#include "perl_full_runtime.hpp"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>

namespace rosetta {
namespace {
using namespace cabi;

char upper(char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
bool is_upper(char c) { return std::isupper(static_cast<unsigned char>(c)) != 0; }
bool is_lower(char c) { return std::islower(static_cast<unsigned char>(c)) != 0; }
bool is_digit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
bool is_alnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

// Perl's named operators, keywords and special subs: a module function named like one could not
// be imported (builtins win), so it takes a trailing underscore.
bool perl_builtin(const std::string& name) {
    static const std::set<std::string> words = {"abs", "accept", "alarm", "and", "atan2", "bind", "binmode", "bless", "break",
        "caller", "chdir", "chmod", "chomp", "chop", "chown", "chr", "chroot", "close", "closedir", "cmp", "connect", "continue",
        "cos", "crypt", "dbmclose", "dbmopen", "default", "defined", "delete", "die", "do", "dump", "each", "else", "elsif",
        "endgrent", "endhostent", "endnetent", "endprotoent", "endpwent", "endservent", "eof", "eq", "eval", "evalbytes", "exec",
        "exists", "exit", "exp", "fc", "fcntl", "fileno", "flock", "for", "foreach", "fork", "format", "formline", "ge", "getc",
        "getgrent", "getgrgid", "getgrnam", "gethostbyaddr", "gethostbyname", "gethostent", "getlogin", "getnetbyaddr",
        "getnetbyname", "getnetent", "getpeername", "getpgrp", "getppid", "getpriority", "getprotobyname", "getprotobynumber",
        "getprotoent", "getpwent", "getpwnam", "getpwuid", "getservbyname", "getservbyport", "getservent", "getsockname",
        "getsockopt", "given", "glob", "gmtime", "goto", "grep", "gt", "hex", "if", "index", "int", "ioctl", "join", "keys", "kill",
        "last", "lc", "lcfirst", "le", "length", "link", "listen", "local", "localtime", "lock", "log", "lstat", "lt", "m", "map",
        "mkdir", "msgctl", "msgget", "msgrcv", "msgsnd", "my", "ne", "next", "no", "not", "oct", "open", "opendir", "or", "ord",
        "our", "pack", "package", "pipe", "pop", "pos", "print", "printf", "prototype", "push", "q", "qq", "qr", "quotemeta", "qw",
        "qx", "rand", "read", "readdir", "readline", "readlink", "readpipe", "recv", "redo", "ref", "rename", "require", "reset",
        "return", "reverse", "rewinddir", "rindex", "rmdir", "s", "say", "scalar", "seek", "seekdir", "select", "semctl", "semget",
        "semop", "send", "setgrent", "sethostent", "setnetent", "setpgrp", "setpriority", "setprotoent", "setpwent", "setservent",
        "setsockopt", "shift", "shmctl", "shmget", "shmread", "shmwrite", "shutdown", "sin", "sleep", "socket", "socketpair", "sort",
        "splice", "split", "sprintf", "sqrt", "srand", "stat", "state", "study", "sub", "substr", "symlink", "syscall", "sysopen",
        "sysread", "sysseek", "system", "syswrite", "tell", "telldir", "tie", "tied", "time", "times", "tr", "truncate", "uc",
        "ucfirst", "umask", "undef", "unless", "unlink", "unpack", "unshift", "untie", "until", "use", "utime", "values", "vec",
        "wait", "waitpid", "wantarray", "warn", "when", "while", "write", "x", "xor", "y", "try", "catch", "finally", "defer",
        "class", "method", "field", "ADJUST", "BEGIN", "END", "INIT", "CHECK", "UNITCHECK", "DESTROY", "AUTOLOAD", "import",
        "unimport", "can", "isa", "DOES", "VERSION", "new"};
    return words.count(name) != 0;
}

// snake_case words of a C++ name (camelCase, PascalCase, URLPath and snake_case alike).
std::string snake_words(const std::string& name) {
    std::string result;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (!is_alnum(c)) {
            if (!result.empty() && result.back() != '_') result += '_';
            continue;
        }
        if (is_upper(c) && !result.empty() && result.back() != '_') {
            const char previous = name[i - 1];
            const bool next_lower = i + 1 < name.size() && is_lower(name[i + 1]);
            if (is_lower(previous) || is_digit(previous) || (is_upper(previous) && next_lower)) result += '_';
        }
        result += lower(c);
    }
    while (!result.empty() && result.back() == '_') result.pop_back();
    while (!result.empty() && result.front() == '_') result.erase(result.begin());
    if (result.empty()) return "x";
    if (is_digit(result[0])) result = "x" + result;
    return result;
}
// PascalCase (packages).
std::string pascal(const std::string& name) {
    const auto words = snake_words(name);
    std::string result;
    bool start = true;
    for (const char c : words) {
        if (c == '_') { start = true; continue; }
        result += start ? upper(c) : c;
        start = false;
    }
    return result;
}
// UPPER_CASE (constants).
std::string shout(const std::string& name) {
    auto result = snake_words(name);
    for (auto& c : result) c = upper(c);
    return result;
}
// A single-quoted Perl string literal.
std::string perl_string(const std::string& text) {
    std::string result = "'";
    for (const char c : text) {
        if (c == '\\' || c == '\'') result += '\\';
        result += c;
    }
    return result + "'";
}
std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    const auto last = text.find_last_not_of(" \t\r");
    return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
}
bool starts(const std::string& text, const std::string& prefix) { return text.rfind(prefix, 0) == 0; }
std::string join(const std::vector<std::string>& items, const std::string& separator) {
    std::string result;
    for (std::size_t i = 0; i < items.size(); ++i) result += (i ? separator : "") + items[i];
    return result;
}

// Names in one Perl scope (case-sensitive).
struct Names {
    std::set<std::string> taken;
    bool reserve(const std::string& name) { return taken.insert(name).second; }
    bool free(const std::string& name) const { return !taken.count(name); }
    // The wanted name, else with a number (get_name_2, Point_2).
    std::string claim(const std::string& wanted) {
        if (reserve(wanted)) return wanted;
        for (int i = 2;; ++i) {
            const auto candidate = wanted + "_" + std::to_string(i);
            if (reserve(candidate)) return candidate;
        }
    }
    // A package name: the wanted name, else with a trailing underscore (Point_), else a number.
    std::string claim_type(const std::string& wanted) {
        if (reserve(wanted)) return wanted;
        return claim(wanted + "_");
    }
};

// ---------------------------------------------------------------- POD
// Inline Markdown (`code`, **bold**) as POD formatting codes, everything else escaped.
std::string pod_escape(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c == '<') out += "E<lt>";
        else if (c == '>') out += "E<gt>";
        else out += c;
    }
    return out;
}
std::string pod_inline(const std::string& text) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '`') {
            const auto end = text.find('`', i + 1);
            if (end != std::string::npos) { out += "C<" + pod_escape(text.substr(i + 1, end - i - 1)) + ">"; i = end + 1; continue; }
        }
        if (text.compare(i, 2, "**") == 0) {
            const auto end = text.find("**", i + 2);
            if (end != std::string::npos && end > i + 2) { out += "B<" + pod_escape(text.substr(i + 2, end - i - 2)) + ">"; i = end + 2; continue; }
        }
        out += pod_escape(std::string(1, text[i]));
        ++i;
    }
    // A line starting with = would be a POD command.
    if (starts(out, "=")) out = "Z<>" + out;
    return out;
}

// The paragraphs of a POD section; descriptions are Markdown-ish text.
struct Pod {
    std::vector<std::string> blocks;
    void add(const std::string& block) { if (!block.empty()) blocks.push_back(block); }
    void paragraph(const std::string& text) {
        std::istringstream in(text);
        std::vector<std::string> lines, items;
        const auto flush_lines = [&] { if (!lines.empty()) add(join(lines, "\n")); lines.clear(); };
        const auto flush_items = [&] {
            if (items.empty()) return;
            std::string block = "=over 4";
            for (const auto& item : items) block += "\n\n=item *\n\n" + item;
            add(block + "\n\n=back");
            items.clear();
        };
        for (std::string line; std::getline(in, line);) {
            const auto stripped = trim(line);
            if (stripped.empty()) { flush_lines(); flush_items(); continue; }
            if (starts(stripped, "* ") || starts(stripped, "- ")) { flush_lines(); items.push_back(pod_inline(trim(stripped.substr(2)))); continue; }
            if (!items.empty()) { items.back() += "\n" + pod_inline(stripped); continue; }
            lines.push_back(pod_inline(stripped));
        }
        flush_lines();
        flush_items();
    }
    void heading(const std::string& text) { add("B<" + pod_inline(one_line(trim(text))) + ">"); }
    // A verbatim block: every line indented.
    void code(const std::string& text) {
        std::istringstream in(text);
        std::vector<std::string> body;
        for (std::string line; std::getline(in, line);) body.push_back(line);
        while (!body.empty() && trim(body.back()).empty()) body.pop_back();
        while (!body.empty() && trim(body.front()).empty()) body.erase(body.begin());
        if (body.empty()) return;
        std::string block;
        for (const auto& line : body) block += (block.empty() ? "" : "\n") + (trim(line).empty() ? std::string() : "    " + line);
        add(block);
    }
    void list(const std::string& title, const std::vector<std::string>& items) {
        if (items.empty()) return;
        if (!title.empty()) add(title);
        std::string block = "=over 4";
        for (const auto& item : items) block += "\n\n=item *\n\n" + item;
        add(block + "\n\n=back");
    }
    std::string render() const { return join(blocks, "\n\n"); }
};

std::string exception_class(const std::string& name) {
    const auto bare = normalize(name);
    if (bare == "std::invalid_argument") return "InvalidArgument";
    if (bare == "std::out_of_range") return "OutOfRange";
    if (bare == "std::domain_error") return "Domain";
    if (bare == "std::length_error") return "Length";
    if (bare == "std::overflow_error" || bare == "std::underflow_error" || bare == "std::range_error") return "Arithmetic";
    return "Native";
}

// C++ operators: their method names ("" = not bound) and Perl's overload keys.
struct OperatorName { const char* unary; const char* binary; const char* key; };
const std::map<std::string, OperatorName>& operator_table() {
    static const std::map<std::string, OperatorName> table = {
        {"operator+", {"pos", "add", "+"}}, {"operator-", {"neg", "subtract", "-"}}, {"operator*", {nullptr, "multiply", "*"}},
        {"operator/", {nullptr, "divide", "/"}}, {"operator%", {nullptr, "modulo", "%"}}, {"operator==", {nullptr, "equals", "=="}},
        {"operator!=", {nullptr, "not_equals", "!="}}, {"operator<", {nullptr, "less", "<"}}, {"operator<=", {nullptr, "less_equal", "<="}},
        {"operator>", {nullptr, "greater", ">"}}, {"operator>=", {nullptr, "greater_equal", ">="}}, {"operator&", {nullptr, "bit_and", "&"}},
        {"operator|", {nullptr, "bit_or", "|"}}, {"operator^", {nullptr, "bit_xor", "^"}}, {"operator~", {"complement", nullptr, "~"}},
        {"operator!", {"not", nullptr, "!"}}, {"operator<<", {nullptr, "shift_left", "<<"}}, {"operator>>", {nullptr, "shift_right", ">>"}},
        {"operator[]", {nullptr, "at", ""}}, {"operator+=", {nullptr, "add_assign", "+="}}, {"operator-=", {nullptr, "subtract_assign", "-="}},
        {"operator*=", {nullptr, "multiply_assign", "*="}}, {"operator/=", {nullptr, "divide_assign", "/="}}};
    return table;
}
std::string perl_operator(const std::string& name, std::size_t arity) {
    if (name == "operator()") return "call";
    const auto found = operator_table().find(name);
    if (found == operator_table().end() || arity > 1) return {};
    const char* result = arity == 0 ? found->second.unary : found->second.binary;
    return result ? result : "";
}

// One parameter position of a sub (every overload's parameter there).
struct Param {
    std::vector<std::string> docs;
    std::string name;
    bool optional = false;
    bool byref = false;
    bool bytes = false;
    std::string description;
};
struct Sig {
    std::vector<Param> params;
    std::vector<std::string> result_docs;
    bool returns = false;
};

class Generator : public CabiGenerator {
public:
    Generator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const PerlOptions& options)
        : CabiGenerator(module, name, skip, policy, options.parts), options_(options) {}
    GeneratedPerlSource run();

private:
    PerlOptions options_;
    std::string package_, library_, runtime_;
    Names types_;                                           // packages under the main one
    std::map<std::string, Names> functions_;                // policy module ("" = main) -> sub names
    std::map<std::string, Names> members_;                  // class -> sub names (inherited ones included)
    std::map<std::string, std::map<std::string, std::string>> mapped_;  // class -> C++ member key -> Perl name
    std::map<const Group*, std::string> names_;             // group -> Perl name
    std::map<const Group*, Sig> sigs_;
    std::map<std::string, Sig> constructor_sigs_;
    std::map<std::string, std::vector<Overload>> constructor_overloads_;
    std::map<std::string, std::string> field_names_;        // class::field -> accessor
    std::map<const ConstantPlan*, std::string> constant_names_;
    std::map<std::string, std::vector<std::string>> enum_cases_;
    std::map<std::string, std::string> packages_;           // class or enum -> full package
    std::map<std::string, std::string> submodules_;         // policy module -> package
    std::map<std::string, std::string> links_;              // C++ name -> POD reference
    std::map<std::size_t, std::string> generic_methods_;    // C++ virtual method number -> Perl method

    // ------------------------------------------------------------ hooks
    std::string language() const override { return "Perl"; }
    std::string target_name(const std::string& cpp) const override { return cpp; }
    std::string method_name(const std::string& cpp) const override { return cpp; }
    std::string type_name(const std::string& cpp) const override { return pascal(cpp); }
    std::string rename_target(const std::string& name, const std::string&) const override { return name; }
    std::string field_name(const std::string& cpp) const override { return cpp; }
    std::string operator_name(const std::string& cpp, std::size_t arity) const override { return perl_operator(cpp, arity); }
    std::string static_constant_name(const ClassPlan&, const std::string& cpp) const override { return cpp; }
    std::vector<std::string> parameter_names(const Overload& overload) const override {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        std::vector<std::string> names;
        std::set<std::string> taken = {"self", "class"};
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            auto name = cpp.empty() || !identifier(cpp) ? "arg" + std::to_string(i - first + 1) : snake_words(cpp);
            while (!taken.insert(name).second) name += "_";
            names.push_back(name);
        }
        return names;
    }
    std::string signatures(const Group& group, bool) const override {
        std::string text;
        for (const auto& overload : group.overloads) text += "    " + overload_signature(overload, group.name) + "\n";
        return text;
    }
    std::string constructor_signatures(const ClassPlan& plan) const override {
        std::string text;
        for (const auto& overload : plan.constructors) text += "    " + overload_signature(overload, plan.name + "->new") + "\n";
        return text;
    }
    std::string abstract_message(const ClassPlan& plan) const override {
        return plan.name + " is abstract: subclass it in Perl and implement its pure virtual methods";
    }
    std::string protected_message(const ClassPlan& plan) const override {
        return plan.name + " constructor is protected: subclass it in Perl";
    }
    std::string symbol_prefix() const override { return "rosetta_perl_"; }
    std::string runtime_define() const override { return "ROSETTA_FULL_PERL"; }
    std::string module_prefix() const override { return "rosetta_perl_module_"; }

    // ------------------------------------------------------------ types
    std::string header_name() const {
        const auto slash = module_.header.find_last_of("/\\");
        return slash == std::string::npos ? module_.header : module_.header.substr(slash + 1);
    }
    bool writeback(const Type& type) const {
        return type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type));
    }
    bool bytes_type(const Type& type) const;
    std::string perl_type(const Type& type) const;
    std::string perl_spelled(std::string cpp) const;
    std::string perl_default(const DefaultValue& value, const Type& type) const;
    std::string overload_signature(const Overload& overload, const std::string& name) const;
    Sig signature_of(const std::vector<Overload>& overloads, std::size_t skip = std::string::npos) const;

    // ------------------------------------------------------------ naming and emission
    void name_everything(std::vector<Group>& functions, std::vector<ConstantPlan>& constants);
    std::string link(const std::string& cpp) const;
    Pod doc_of(const Documentation& doc, const std::string& lead) const;
    void add_notes(Pod& pod, const Documentation& doc) const;
    Pod group_doc(const std::vector<Overload>& overloads, const Sig& sig, const std::string& what, const std::string& call) const;
    void emit_body(std::ostream& out, const Sig& sig, std::size_t index, bool method, bool shift, std::size_t operand_at) const;
    void emit_group(std::ostream& out, const Group& group, const ClassPlan* plan, const std::string& package = {}) const;
    void emit_class(std::ostream& out, const std::string& owner) const;
    void emit_enum(std::ostream& out, const std::string& qualified) const;
    std::string file_doc() const;
    std::string xs_glue() const;
};

// ---------------------------------------------------------------- types
bool Generator::bytes_type(const Type& type) const {
    const auto& target = referent(type);
    if (target.kind != TypeKind::template_instance || target.arguments.empty()) return false;
    const auto declaration_name = normalize(target.declaration);
    if (declaration_name != "std::vector" && declaration_name != "std::deque" && declaration_name != "std::array" &&
        declaration_name != "std::span" && declaration_name != "std::list")
        return false;
    const auto& element = referent(target.arguments[0]);
    if (element.kind != TypeKind::builtin) return false;
    const auto bare = normalize(strip_cv(element.canonical));
    return bare == "unsigned char" || bare == "signed char";
}

// A type as the POD describes it.
std::string Generator::perl_type(const Type& type) const {
    const auto& target = referent(type);
    if (target.kind == TypeKind::pointer && !target.arguments.empty()) {
        const auto& pointee = target.arguments.front();
        if (strip_cv(pointee.canonical) == "char") return "string or undef";
        if (is_void(pointee)) return "any";
        return perl_type(pointee) + " or undef";
    }
    if (is_void(target)) return "nothing";
    if (!conversion(target).empty()) return perl_spelled(policy_.conversions.at(conversion(target)));
    const auto bare = normalize(strip_cv(target.canonical));
    if (target.kind == TypeKind::builtin) {
        if (bare == "bool") return "boolean";
        if (bare == "char") return "string (one character)";
        if (bare == "float" || bare == "double" || bare == "long double") return "number";
        if (bare == "void") return "nothing";
        return "integer";
    }
    if (target.kind == TypeKind::enumeration) {
        const auto found = packages_.find(target.declaration);
        return found == packages_.end() ? "integer" : found->second + " constant";
    }
    if (bound(target)) return packages_.at(target.declaration);
    if (bare == "std::filesystem::path") return "string";
    if (bare == "std::nullptr_t" || bare == "std::monostate") return "undef";
    if (target.kind == TypeKind::template_instance) {
        const auto declaration_name = normalize(target.declaration);
        const auto argument = [&](std::size_t i) { return i < target.arguments.size() ? perl_type(target.arguments[i]) : std::string("any"); };
        if (declaration_name == "std::basic_string" || declaration_name == "std::basic_string_view") return "string";
        if (bytes_type(target)) return "byte string or array reference of integers";
        if (declaration_name == "std::vector" || declaration_name == "std::deque" || declaration_name == "std::list" || declaration_name == "std::array" ||
            declaration_name == "std::valarray" || declaration_name == "std::span" || declaration_name == "std::set" ||
            declaration_name == "std::unordered_set" || declaration_name == "std::multiset" || declaration_name == "std::unordered_multiset")
            return "array reference of " + argument(0);
        if (declaration_name == "std::map" || declaration_name == "std::unordered_map" || declaration_name == "std::multimap" ||
            declaration_name == "std::unordered_multimap") {
            const auto key = argument(0);
            const bool keyed = (key == "integer" || key == "string") && declaration_name.find("multi") == std::string::npos;
            return keyed ? "hash reference of " + argument(1) : "array reference of [" + key + ", " + argument(1) + "] pairs";
        }
        if (declaration_name == "std::optional" || declaration_name == "std::shared_ptr" || declaration_name == "std::unique_ptr")
            return target.arguments.empty() ? std::string("any") : argument(0) + " or undef";
        if (declaration_name == "std::complex") return "Math::Complex";
        if (declaration_name == "std::pair" || declaration_name == "std::tuple") {
            std::vector<std::string> items;
            for (std::size_t i = 0; i < target.arguments.size(); ++i) items.push_back(argument(i));
            return "array reference [" + join(items, ", ") + "]";
        }
        if (declaration_name == "std::function" && !target.arguments.empty() && target.arguments[0].kind == TypeKind::function) {
            const auto& function = target.arguments[0];
            std::vector<std::string> items;
            for (std::size_t i = 1; i < function.arguments.size(); ++i) items.push_back(perl_type(function.arguments[i]));
            const auto result = function.arguments.empty() ? std::string("nothing") : perl_type(function.arguments[0]);
            return "code reference (" + join(items, ", ") + ") returning " + result;
        }
        if (declaration_name == "std::chrono::duration") return "number";
    }
    return "any";
}

// A type a policy conversion spells in C++.
std::string Generator::perl_spelled(std::string cpp) const {
    cpp = trim(cpp);
    while (!cpp.empty() && (cpp.back() == '&' || cpp.back() == ' ')) cpp.pop_back();
    if (starts(cpp, "const ")) cpp = trim(cpp.substr(6));
    if (starts(cpp, "::")) cpp = cpp.substr(2);
    if (cpp == "bool") return "boolean";
    if (cpp == "float" || cpp == "double" || cpp == "long double") return "number";
    if (cpp == "char" || cpp == "std::string" || cpp == "std::string_view" || cpp == "std::filesystem::path") return "string";
    static const std::set<std::string> integers = {"signed char", "unsigned char", "short", "unsigned short", "int", "unsigned int", "long",
        "unsigned long", "long long", "unsigned long long", "std::size_t", "size_t", "std::int64_t", "std::uint64_t", "std::int32_t",
        "std::uint32_t", "std::int16_t", "std::uint16_t", "std::int8_t", "std::uint8_t"};
    if (integers.count(cpp)) return "integer";
    for (const auto& [qualified, package] : packages_) if (normalize(qualified) == cpp) return package;
    if (starts(cpp, "std::vector<") || starts(cpp, "std::pair<") || starts(cpp, "std::tuple<")) return "array reference";
    if (starts(cpp, "std::map<")) return "hash reference";
    return "any";
}

std::string Generator::perl_default(const DefaultValue& value, const Type& type) const {
    if (value.kind == "int") return value.value;
    if (value.kind == "float") return shortest_double(value.value);
    if (value.kind == "bool") return value.value == "true" ? "true" : "false";
    if (value.kind == "null") return "undef";
    if (value.kind == "char") return value.value.size() == 1 ? perl_string(value.value) : std::string();
    if (value.kind == "string") {
        const auto& text = value.value;
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"' && text.find('\\') == std::string::npos)
            return perl_string(text.substr(1, text.size() - 2));
        return {};
    }
    if (value.kind == "enum") {
        const auto qualified = referent(type).declaration;
        const auto found = packages_.find(qualified);
        if (found == packages_.end() || !enum_cases_.count(qualified)) return {};
        const auto& enumeration = *enums_.at(qualified);
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i)
            if (enumeration.enumerators[i] == value.value) return found->second + "::" + enum_cases_.at(qualified)[i];
    }
    return {};
}

std::string Generator::overload_signature(const Overload& overload, const std::string& name) const {
    const auto& function = *overload.function;
    const std::size_t first = overload.receives_self ? 1 : 0;
    const auto names = parameter_names(overload);
    std::vector<std::string> parameters;
    for (std::size_t i = first; i < function.parameters.size(); ++i) {
        auto text = "$" + names[i - first] + " (" + perl_type(function.parameters[i]) + (writeback(function.parameters[i]) ? ", written back" : "") + ")";
        if (i >= function.parameters.size() - function.default_arguments) {
            const auto shown = perl_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
            text += " = " + (shown.empty() ? std::string("C++ default") : shown);
        }
        parameters.push_back(text);
    }
    auto result = name + "(" + join(parameters, ", ") + ")";
    if (!function.is_constructor && !is_void(function.result)) result += " returning " + perl_type(function.result);
    return result;
}

// The parameters of overloads called through one sub: position i takes every overload's
// parameter there, and is optional from the fewest arguments any overload needs on.
Sig Generator::signature_of(const std::vector<Overload>& overloads, std::size_t skip) const {
    Sig sig;
    std::size_t required = std::string::npos, positions = 0;
    for (const auto& overload : overloads) {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        const auto count = function.parameters.size() - first - (skip < function.parameters.size() ? 1 : 0);
        required = std::min(required, count - std::min(count, function.default_arguments));
        positions = std::max(positions, count);
    }
    if (required == std::string::npos) required = 0;
    sig.params.resize(positions);
    std::vector<bool> all_byref(positions, true);
    std::vector<std::size_t> having(positions, 0);
    for (const auto& overload : overloads) {
        const auto& function = *overload.function;
        const std::size_t first = overload.receives_self ? 1 : 0;
        const auto names = parameter_names(overload);
        for (std::size_t i = first, p = 0; i < function.parameters.size(); ++i) {
            if (i == skip) continue;
            auto& param = sig.params[p];
            const auto type = perl_type(function.parameters[i]);
            if (std::find(param.docs.begin(), param.docs.end(), type) == param.docs.end()) param.docs.push_back(type);
            if (param.name.empty()) param.name = names[i - first];
            all_byref[p] = all_byref[p] && writeback(function.parameters[i]);
            param.bytes = param.bytes || bytes_type(function.parameters[i]);
            ++having[p];
            const auto cpp = i < function.parameter_names.size() ? function.parameter_names[i] : std::string();
            if (param.description.empty())
                for (const auto& item : function.documentation.params)
                    if (item.name == cpp && !item.text.empty()) param.description = one_line(item.text);
            ++p;
        }
        if (!function.is_constructor && !is_void(function.result)) {
            sig.returns = true;
            const auto type = perl_type(function.result);
            if (std::find(sig.result_docs.begin(), sig.result_docs.end(), type) == sig.result_docs.end()) sig.result_docs.push_back(type);
        }
    }
    std::set<std::string> taken;
    for (auto& param : sig.params)
        while (!taken.insert(param.name).second) param.name += "_";
    for (std::size_t p = 0; p < positions; ++p) {
        sig.params[p].byref = all_byref[p] && having[p] == overloads.size();
        sig.params[p].optional = p >= required;
    }
    return sig;
}

// ---------------------------------------------------------------- naming
void Generator::name_everything(std::vector<Group>& functions, std::vector<ConstantPlan>& constants) {
    for (const auto& owner : class_order_) {
        auto& cls = classes_.at(owner);
        auto& members = members_[owner];
        auto& mapped = mapped_[owner];
        // A C++ name a base class binds keeps its Perl name (an override, or C++ hiding).
        for (const auto& base : cls.bases) {
            if (!members_.count(base)) continue;
            for (const auto& name : members_.at(base).taken) members.reserve(name);
            for (const auto& [key, name] : mapped_.at(base)) mapped.emplace(key, name);
        }
        for (const char* reserved : {"new", "DESTROY", "AUTOLOAD", "can", "isa", "DOES", "VERSION", "import", "unimport", "BEGIN", "END",
                                     "INIT", "CHECK", "UNITCHECK", "CLONE", "CLONE_SKIP", "close", "is_closed", "cast", "__END__",
                                     "__DATA__", "__PACKAGE__", "__FILE__", "__LINE__", "__SUB__", "ADJUST"})
            members.reserve(reserved);
        const bool constructible = !cls.record->abstract;
        for (const auto& overload : cls.constructors)
            if (cls.director || (constructible && overload.function->access == "public")) constructor_overloads_[owner].push_back(overload);
        if (constructor_overloads_.count(owner)) constructor_sigs_[owner] = signature_of(constructor_overloads_.at(owner));
        const auto name_member = [&](const std::string& key, const std::string& wanted) {
            const auto found = mapped.find(key);
            if (found != mapped.end()) return found->second;
            const auto name = members.claim(wanted);
            mapped[key] = name;
            return name;
        };
        for (auto& group : cls.methods) {
            const auto& front = group.overloads.front();
            names_[&group] = name_member("m:" + group.name, snake_words(group.name));
            sigs_[&group] = signature_of(group.overloads, !group.is_static && front.extension && !front.receives_self ? group.operand : std::string::npos);
        }
        for (auto& group : cls.operators) {
            const auto& front = group.overloads.front();
            names_[&group] = name_member("o:" + group.name + (group.operand ? "R" : ""), group.name + (group.operand ? "_r" : ""));
            sigs_[&group] = signature_of(group.overloads, front.extension && !front.receives_self ? group.operand : std::string::npos);
        }
        for (const auto& field : cls.fields) field_names_[owner + "::" + field.field->name] = name_member("f:" + field.field->name, snake_words(field.name));
        for (auto& constant : cls.constants) constant_names_[&constant] = members.claim(shout(constant.name));
    }
    for (auto& group : functions) {
        auto& scope = functions_[group.module];
        auto wanted = snake_words(group.name);
        if (perl_builtin(wanted)) wanted += "_";
        names_[&group] = scope.claim(wanted);
        sigs_[&group] = signature_of(group.overloads);
    }
    for (auto& constant : constants) constant_names_[&constant] = functions_[""].claim(shout(constant.name));
    // POD references.
    for (const auto& owner : class_order_) {
        const auto& cls = classes_.at(owner);
        const auto& package = packages_.at(owner);
        links_[owner] = "L<" + package + "|/" + package + ">";
        links_[cls.record->name] = links_[owner];
        for (const auto& group : cls.methods) {
            links_[owner + "::" + group.name] = "C<" + package + "-E<gt>" + names_.at(&group) + ">";
            links_[cls.record->name + "::" + group.name] = links_[owner + "::" + group.name];
        }
    }
    for (const auto& group : functions) {
        if (!group.module.empty()) continue;
        for (const auto& overload : group.overloads) {
            links_[overload.function->qualified_name()] = "L</" + names_.at(&group) + ">";
            links_.emplace(overload.function->name, "L</" + names_.at(&group) + ">");
        }
    }
    for (const auto& [qualified, package] : packages_) {
        if (!enums_.count(qualified)) continue;
        links_[qualified] = "L<" + package + "|/" + package + ">";
        links_.emplace(enums_.at(qualified)->name, links_[qualified]);
    }
}

// ---------------------------------------------------------------- documentation
std::string Generator::link(const std::string& cpp) const {
    const auto found = links_.find(normalize(cpp));
    return found != links_.end() ? found->second : "C<" + pod_escape(cpp) + ">";
}
void Generator::add_notes(Pod& pod, const Documentation& doc) const {
    for (const auto& item : doc.notes) {
        std::string label = item.name == "pre" ? "Precondition" : item.name == "post" ? "Postcondition" : item.name;
        if (!label.empty()) label[0] = upper(label[0]);
        pod.add("B<" + pod_inline(label) + ":> " + pod_inline(one_line(item.text)));
    }
    std::vector<std::string> see;
    for (const auto& item : doc.see) see.push_back(link(item));
    if (!see.empty()) pod.add("See also: " + join(see, ", ") + ".");
}
Pod Generator::doc_of(const Documentation& doc, const std::string& lead) const {
    Pod pod;
    auto details = doc.details;
    auto brief = doc.brief;
    if (brief.empty() && !details.empty() && details.front().kind == "text") { brief = details.front().text; details.erase(details.begin()); }
    pod.paragraph(brief.empty() ? lead : brief);
    if (!brief.empty() && !lead.empty()) pod.paragraph(lead);
    for (const auto& block : details) {
        if (block.kind == "code" || block.kind == "math") pod.code(block.text);
        else if (block.kind == "heading") pod.heading(block.text);
        else pod.paragraph(block.text);
    }
    return pod;
}

Pod Generator::group_doc(const std::vector<Overload>& overloads, const Sig& sig, const std::string& what, const std::string& call) const {
    const Function* documented = overloads.front().function;
    for (const auto& overload : overloads)
        if (!overload.function->documentation.brief.empty() || !overload.function->documentation.details.empty()) { documented = overload.function; break; }
    const auto& doc = documented->documentation;
    Pod pod;
    // The call, as a verbatim line.
    std::vector<std::string> arguments;
    for (const auto& param : sig.params) arguments.push_back((param.byref ? "\\$" : "$") + param.name);
    pod.add("    " + std::string(sig.returns ? "my $result = " : "") + call + "(" + join(arguments, ", ") + ");");
    for (const auto& block : doc_of(doc, "Calls " + what + ".").blocks) pod.add(block);
    if (overloads.size() > 1) {
        std::vector<std::string> items;
        for (const auto& overload : overloads) items.push_back("C<" + pod_escape(overload_signature(overload, call)) + ">");
        pod.list("Overloads (C++ picks one by the arguments' types):", items);
    }
    if (overloads.size() == 1) {
        const auto& function = *overloads.front().function;
        const std::size_t first = overloads.front().receives_self ? 1 : 0;
        const auto names = parameter_names(overloads.front());
        std::vector<std::string> defaults;
        for (std::size_t i = function.parameters.size() - function.default_arguments; i < function.parameters.size(); ++i) {
            if (i < first) continue;
            const auto shown = perl_default(i < function.default_values.size() ? function.default_values[i] : DefaultValue{}, function.parameters[i]);
            const auto cpp = i < function.default_values.size() ? function.default_values[i].value : std::string();
            defaults.push_back("C<$" + names[i - first] + " = " + pod_escape(shown.empty() ? cpp.empty() ? std::string("C++ default") : cpp : shown) + ">");
        }
        if (!defaults.empty()) pod.add("Arguments left out take the C++ defaults: " + join(defaults, ", ") + ".");
    }
    if (!sig.params.empty()) {
        std::string block = "Parameters:\n\n=over 4";
        for (const auto& param : sig.params) {
            block += "\n\n=item C<$" + param.name + "> (" + pod_escape(join(param.docs, " | ")) + (param.optional ? ", optional" : "") +
                     (param.byref ? ", written back: pass a variable or a reference" : "") + ")";
            if (!param.description.empty()) block += "\n\n" + pod_inline(param.description);
        }
        pod.add(block + "\n\n=back");
    }
    if (sig.returns) {
        const auto text = one_line(doc.returns);
        pod.add("Returns " + pod_escape(join(sig.result_docs, " | ")) + (text.empty() ? "." : ": " + pod_inline(text)));
    }
    std::set<std::string> thrown;
    for (const auto& overload : overloads)
        for (const auto& item : overload.function->documentation.throws) {
            const auto cls = exception_class(item.name);
            if (!thrown.insert(cls + item.text).second) continue;
            pod.add("Dies with C<" + package_ + "::Error::" + cls + ">" + (item.name.empty() ? "" : " (C++ C<" + pod_escape(item.name) + ">)") +
                    (item.text.empty() ? "." : ": " + pod_inline(one_line(item.text))));
        }
    add_notes(pod, doc);
    return pod;
}

// ---------------------------------------------------------------- emission
// The body of a sub calling native function `index`: `method` subs have the receiver in $_[0];
// `shift` subs drop $_[0] first (class methods, free operators); a free operator then puts the
// receiver at `operand_at` among the arguments.
void Generator::emit_body(std::ostream& out, const Sig& sig, std::size_t index, bool method, bool shift, std::size_t operand_at) const {
    const std::size_t base = method && !shift ? 1 : 0;
    const bool free_operator = operand_at != std::string::npos;
    std::vector<std::string> transforms, writebacks;
    std::size_t slot = 0;
    for (std::size_t p = 0; p < sig.params.size(); ++p) {
        const auto& param = sig.params[p];
        const auto at = std::to_string(base + p);
        if (param.bytes) transforms.push_back("    $args[" + at + "] = " + runtime_ + "::bytes($args[" + at + "]) if @args > " + at + ";\n");
        if (param.byref) {
            const auto number = std::to_string(slot++);
            transforms.push_back("    $args[" + at + "] = " + runtime_ + "::slot(" + number + ", $args[" + at + "]) if @args > " + at + ";\n");
            writebacks.push_back("    " + runtime_ + "::write_back(\\@_, " + at + ", $writebacks{" + number + "}) if exists $writebacks{" + number + "};\n");
        }
    }
    const auto call = runtime_ + "::call(" + std::to_string(index);
    if (shift) out << "    " << (free_operator ? "my $self = " : "") << "shift;\n";
    if (transforms.empty() && !free_operator) {
        out << "    return " << call << ", [@_]);\n";
        return;
    }
    out << "    my @args = @_;\n";
    for (const auto& line : transforms) out << line;
    if (free_operator) out << "    splice(@args, @args < " << operand_at << " ? scalar @args : " << operand_at << ", 0, $self);\n";
    if (writebacks.empty()) {
        out << "    return " << call << ", \\@args);\n";
        return;
    }
    out << "    my %writebacks;\n    my $result = " << call << ", \\@args, \\%writebacks);\n";
    for (const auto& line : writebacks) out << line;
    out << "    return $result;\n";
}

void Generator::emit_group(std::ostream& out, const Group& group, const ClassPlan* plan, const std::string& package) const {
    const auto& sig = sigs_.at(&group);
    const auto& name = names_.at(&group);
    const auto& front = group.overloads.front();
    const auto& function = *front.function;
    std::string what;
    if (front.extension) what = "C++ `" + function.qualified_name() + "`";
    else if (plan) what = "the C++ method `" + plan->record->qualified_name() + "::" + function.name + "`";
    else what = "C++ `" + function.qualified_name() + "`";
    std::string call = name;
    if (plan) call = (group.is_static ? package : "$" + snake_words(plan->name)) + "->" + name;
    const auto pod = group_doc(group.overloads, sig, what, call);
    out << "\n=head2 " << name << "\n\n" << pod.render() << "\n\n=cut\n\nsub " << name << " {\n";
    const bool method = plan != nullptr;
    const bool free_operator = method && !group.is_static && front.extension && !front.receives_self;
    emit_body(out, sig, group.index, method, group.is_static || free_operator, free_operator ? group.operand : std::string::npos);
    out << "}\n";
}

void Generator::emit_enum(std::ostream& out, const std::string& qualified) const {
    const auto& enumeration = *enums_.at(qualified);
    const auto& package = packages_.at(qualified);
    const auto& cases = enum_cases_.at(qualified);
    auto pod = doc_of(enumeration.documentation, "The C++ enumeration `" + qualified + "`.");
    pod.add("Its enumerators are constants (C<" + package + "::" + (cases.empty() ? std::string("X") : cases.front()) +
            ">) of their integer values; C++ takes and returns the integers.");
    std::vector<std::string> items;
    for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i)
        items.push_back("C<" + cases[i] + "> = " + pod_escape(enumeration.enumerator_values[i]) + " (C++ C<" + pod_escape(enumeration.enumerators[i]) + ">)");
    pod.list("", items);
    add_notes(pod, enumeration.documentation);
    out << "\npackage " << package << ";\n\n=head1 " << package << "\n\n" << pod.render() << "\n\n=cut\n\n";
    for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
        auto value = enumeration.enumerator_values[i];
        if (value == "-9223372036854775808") value = "(-9223372036854775807 - 1)";
        out << "sub " << cases[i] << " () { " << value << " }\n";
    }
}

void Generator::emit_class(std::ostream& out, const std::string& owner) const {
    const auto& plan = classes_.at(owner);
    const auto& record = *plan.record;
    const auto& package = packages_.at(owner);
    auto pod = doc_of(record.documentation, "Wraps the C++ class `" + owner + "`.");
    std::vector<std::string> bases;
    for (const auto& base : plan.bases) bases.push_back(packages_.at(base));
    pod.add("Inherits from " + (bases.empty() ? "L<" + package_ + "::NativeObject|/" + package_ + "::NativeObject>"
                                              : [&] { std::vector<std::string> l; for (const auto& b : bases) l.push_back("L<" + b + "|/" + b + ">"); return join(l, ", "); }()) + ".");
    if (plan.director)
        pod.add(std::string(record.abstract ? "Abstract in C++: subclass" : "Subclass") + " it in Perl to implement its virtual methods "
                "(C<use parent -norequire, '" + package + "'>): constructing the Perl subclass creates a C++ subclass whose virtual methods call "
                "the Perl methods, and C<SUPER::method> runs the C++ implementation. A subclass's C<new> must call C<SUPER::new>.");
    else if (record.abstract) pod.add("Abstract in C++: C++ functions return objects of derived classes.");
    add_notes(pod, record.documentation);
    out << "\npackage " << package << ";\n\n=head1 " << package << "\n\n" << pod.render() << "\n\n=cut\n\nour @ISA = ("
        << (bases.empty() ? perl_string(package_ + "::NativeObject") : [&] { std::vector<std::string> l; for (const auto& b : bases) l.push_back(perl_string(b)); return join(l, ", "); }())
        << ");\n";
    // Operators Perl overloads.
    std::map<std::string, std::pair<std::string, std::string>> binary;   // key -> (method, reflected)
    std::vector<std::string> overloads;
    for (const auto& group : plan.operators) {
        const auto& cpp = group.overloads.front().function->name;
        const auto& name = names_.at(&group);
        if (cpp == "operator()") { overloads.push_back("'&{}' => sub { my $self = shift; sub { $self->" + name + "(@_) } }"); continue; }
        const auto found = operator_table().find(cpp);
        if (found == operator_table().end() || !*found->second.key) continue;
        const std::string key = found->second.key;
        if (group.name == (found->second.unary ? found->second.unary : "")) {
            if (key == "-") overloads.push_back("'neg' => sub { $_[0]->" + name + " }");
            else if (key == "~" || key == "!") overloads.push_back("'" + key + "' => sub { $_[0]->" + name + " }");
            continue;
        }
        if (group.name == "add_assign" || group.name == "subtract_assign" || group.name == "multiply_assign" || group.name == "divide_assign") {
            overloads.push_back("'" + key + "' => sub { $_[0]->" + name + "($_[1]); $_[0] }");
            continue;
        }
        auto& entry = binary[key];
        (group.operand ? entry.second : entry.first) = name;
    }
    for (const auto& [key, methods] : binary)
        overloads.push_back("'" + key + "' => sub { " + runtime_ + "::binary(@_, " + (methods.first.empty() ? "undef" : perl_string(methods.first)) + ", " +
                            (methods.second.empty() ? "undef" : perl_string(methods.second)) + ", " + perl_string(key) + ") }");
    for (const auto& group : plan.methods)
        if (!group.is_static && group.name == "to_string" && sigs_.at(&group).returns &&
            std::none_of(sigs_.at(&group).params.begin(), sigs_.at(&group).params.end(), [](const Param& p) { return !p.optional; }))
            overloads.push_back("'\"\"' => sub { $_[0]->" + names_.at(&group) + " }");
    if (!overloads.empty()) {
        out << "use overload\n";
        for (const auto& item : overloads) out << "    " << item << ",\n";
        out << "    'bool' => sub { 1 },\n    fallback => 1;\n";
    }
    // The constructor.
    if (constructor_overloads_.count(owner)) {
        const auto& sig = constructor_sigs_.at(owner);
        const auto& overloads_ = constructor_overloads_.at(owner);
        auto about = group_doc(overloads_, sig, "the C++ constructor `" + owner + "`", package + "->new");
        if (plan.director) about.add("The object of a Perl subclass is a C++ subclass whose virtual methods call the Perl methods.");
        out << "\n=head2 new\n\n" << about.render() << "\n\n=cut\n\nsub new {\n    my $class = shift;\n    my $self = bless {}, ref($class) || $class;\n";
        std::vector<std::string> transforms;
        std::size_t slot = 0;
        for (std::size_t p = 0; p < sig.params.size(); ++p) {
            const auto at = std::to_string(p);
            if (sig.params[p].bytes) transforms.push_back("    $args[" + at + "] = " + runtime_ + "::bytes($args[" + at + "]) if @args > " + at + ";\n");
            if (sig.params[p].byref)
                transforms.push_back("    $args[" + at + "] = " + runtime_ + "::slot(" + std::to_string(slot++) + ", $args[" + at + "]) if @args > " + at + ";\n");
        }
        out << "    my @args = @_;\n";
        for (const auto& line : transforms) out << line;
        out << "    " << runtime_ << "::construct($self, " << plan.constructor_index << ", "
            << (plan.director ? "ref($self) ne __PACKAGE__" : "0") << ", \\@args);\n    return $self;\n}\n";
    } else {
        out << "\n=head2 new\n\nThere is no public C++ constructor: C++ functions return the objects of " << package << ".\n\n=cut\n\n"
            << "sub new { " << runtime_ << "::binding_error(" << perl_string(package + " has no public C++ constructor: C++ functions return its objects")
            << ") }\n";
    }
    for (const auto& group : plan.methods) emit_group(out, group, &plan, package);
    for (const auto& group : plan.operators) emit_group(out, group, &plan, package);
    // Fields are accessors.
    for (const auto& field : plan.fields) {
        const auto& f = *field.field;
        const auto& accessor = field_names_.at(owner + "::" + f.name);
        const bool writable = !f.type.is_const && !readonly(owner + "::" + f.name, owner) && f.type.kind != TypeKind::lvalue_reference;
        const auto about = one_line(f.documentation.brief.empty() ? f.doc : f.documentation.brief);
        const auto self = "$" + snake_words(plan.name);
        out << "\n=head2 " << accessor << "\n\n    my $value = " << self << "->" << accessor << ";\n" << (writable ? "    " + self + "->" + accessor + "($value);\n" : "")
            << "\n" << (about.empty() ? "The C++ field C<" + pod_escape(owner + "::" + f.name) + ">" : pod_inline(about)) << " ("
            << pod_escape(perl_type(f.type)) << (writable ? "" : ", read-only") << ").\n\n=cut\n\nsub " << accessor << " {\n    my $self = shift;\n"
            << "    return " << runtime_ << "::call(" << field.getter_index << ", [$self]) unless @_;\n";
        if (writable)
            out << "    " << runtime_ << "::call(" << field.setter_index << ", [$self, " << (bytes_type(f.type) ? runtime_ + "::bytes($_[0])" : std::string("$_[0]"))
                << "]);\n    return;\n}\n";
        else
            out << "    " << runtime_ << "::binding_error(" << perl_string(package + "::" + accessor + " is read-only") << ");\n}\n";
    }
    for (const auto& constant : plan.constants) {
        auto about = doc_of(*constant.documentation, "The C++ constant `" + constant.expression + "`.");
        about.add("A class method returning " + pod_escape(perl_type(*constant.type)) + ".");
        out << "\n=head2 " << constant_names_.at(&constant) << "\n\n" << about.render() << "\n\n=cut\n\nsub " << constant_names_.at(&constant)
            << " { return " << runtime_ << "::call(" << constant.index << ", []) }\n";
    }
}

std::string Generator::file_doc() const {
    auto environment = name_;
    for (auto& c : environment) c = upper(c);
    const auto p = package_;
    std::ostringstream pod;
    pod << "=encoding utf8\n\n=head1 NAME\n\n" << p << " - the Perl interface to the C++ library " << name_ << " (" << pod_escape(header_name()) << ")\n\n"
        << "=head1 SYNOPSIS\n\n    use " << p << " qw(:all);    # or name the functions to import\n\n"
        << "=head1 DESCRIPTION\n\nGenerated by Rosetta from C<" << pod_escape(header_name()) << ">. Each C++ class is a package under C<" << p
        << "> (C<" << p << "::Name-E<gt>new(...)>), each module function a function of C<" << p << "> that the module exports on request "
        << "(C<:all> exports every one), each C++ constant a function (C<UPPER_CASE>).\n\n"
        << "Overloaded C++ functions are one Perl function: C++ picks the overload at run time from the arguments' types, and only the "
        << "arguments a call passes cross, so C++ applies its default arguments. Numbers and strings convert into each other when no "
        << "overload takes the value as it is.\n\n"
        << "Values: integers and numbers are Perl numbers, strings Perl strings (UTF-8 in C++), C<bool> Perl booleans, vectors and sets "
        << "array references, maps hash references (or array references of C<[key, value]> pairs when keys are not strings or integers), "
        << "pairs and tuples array references, C<std::optional> and pointers C<undef> or a value, C<std::complex> L<Math::Complex>, "
        << "C<std::function> code references (a returned one is a code reference too), byte vectors byte strings or array references of "
        << "integers. A non-const reference parameter is written back to the variable passed (or through a reference: C<\\$n>, an array "
        << "reference is updated in place).\n\n"
        << "The native library C<lib" << library_ << "> is found through the C<" << environment << "_PERL_LIBRARY> environment variable (a "
        << "path), beside this file, in C<lib/> or C<../lib>, in C<auto/" << [&] { auto s = p; for (std::size_t i; (i = s.find("::")) != std::string::npos;) s.replace(i, 2, "/"); return s; }()
        << "/> under C<@INC>, else on the loader's search path. It is loaded on the first call.\n\n"
        << "=head1 " << p << "::NativeObject\n\nThe base class of the C++ classes. An object holds a C++ object, released when the last Perl "
        << "reference goes away, or at once by C<close>.\n\n=over 4\n\n"
        << "=item C<< $object->close >>\n\nReleases the C++ object now (again does nothing); later calls die with C<" << p << "::Error::Binding>.\n\n"
        << "=item C<< $object->is_closed >>\n\nWhether the C++ object is released (or was never created).\n\n"
        << "=item C<< $object->cast($package) >>\n\nThe object as C<$package>: its most-derived class (a C++ function declared to return "
        << "a base class may return a derived object) or a base class; C<undef> when it is neither.\n\n=back\n\n"
        << "=head1 " << p << "::Error\n\nCalls die with an object of a subclass of C<" << p << "::Error> for C++ exceptions and values C++ does "
        << "not accept. It stringifies as C<MESSAGE at FILE line LINE.> and has C<message>, C<kind>, C<file> and C<line> methods. An error a "
        << "Perl callback or override dies with crosses C++ unchanged.\n\n=over 4\n\n"
        << "=item C<" << p << "::Error::Binding> (C<binding>): a value that does not convert, no matching overload, or a closed object\n\n"
        << "=item C<" << p << "::Error::InvalidArgument> (C<invalid-argument>): C++ C<std::invalid_argument>\n\n"
        << "=item C<" << p << "::Error::OutOfRange> (C<out-of-range>): C++ C<std::out_of_range>\n\n"
        << "=item C<" << p << "::Error::Domain> (C<domain-error>): C++ C<std::domain_error>\n\n"
        << "=item C<" << p << "::Error::Length> (C<length-error>): C++ C<std::length_error>\n\n"
        << "=item C<" << p << "::Error::Arithmetic> (C<arithmetic-error>): C++ C<std::overflow_error>, C<std::underflow_error>, C<std::range_error>\n\n"
        << "=item C<" << p << "::Error::Native> (C<native>, C<allocation>): any other C++ exception\n\n"
        << "=item C<" << p << "::Error::Callback> (C<callback>): a Perl callback failed and its error is no longer known\n\n=back\n\n=cut\n";
    return pod.str();
}

// The XSUBs the module installs with DynaLoader, and the hooks C++ calls back through. Appended to
// the library's first source after its C ABI; Perl's headers come last, since they define macros.
std::string Generator::xs_glue() const {
    std::string glue = R"cpp(
// ---------------------------------------------------------------- Perl XS glue
// The Perl module installs @C@_perl_call, _release and _init as XSUBs (DynaLoader). Compile this
// source with Perl's headers and flags: perl -MExtUtils::Embed -e ccopts.
#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
namespace {
SV* rosetta_perl_invoke_sv = nullptr;
SV* rosetta_perl_retain_sv = nullptr;
int rosetta_perl_message(const char* text, unsigned char** result, std::size_t* result_size) {
    const unsigned long long n = std::strlen(text);
    auto* out = static_cast<unsigned char*>(@C@_alloc(1 + sizeof n + n));
    if (!out) return 3;
    out[0] = 5;
    std::memcpy(out + 1, &n, sizeof n);
    std::memcpy(out + 1 + sizeof n, text, n);
    *result = out;
    *result_size = 1 + sizeof n + n;
    return 2;
}
int rosetta_perl_invoke(int kind, unsigned long long id, unsigned generic, const unsigned char* data, std::size_t size,
                        unsigned char** result, std::size_t* result_size) {
    *result = nullptr;
    *result_size = 0;
#if defined(PERL_IMPLICIT_CONTEXT)
    if (!PERL_GET_THX) return rosetta_perl_message("C++ called back into Perl from a thread without a Perl interpreter", result, result_size);
#endif
    dTHX;
    if (!rosetta_perl_invoke_sv) return rosetta_perl_message("the Perl module is not initialized", result, result_size);
    dSP;
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);
    EXTEND(SP, 4);
    mPUSHi(kind);
    mPUSHu(static_cast<UV>(id));
    mPUSHu(generic);
    mPUSHp(reinterpret_cast<const char*>(data), size);
    PUTBACK;
    const int count = call_sv(rosetta_perl_invoke_sv, G_LIST | G_EVAL);
    SPAGAIN;
    int status;
    if (SvTRUE(ERRSV) || count != 2) {
        SP -= count;
        status = rosetta_perl_message(SvTRUE(ERRSV) ? SvPV_nolen(ERRSV) : "the Perl callback hook failed", result, result_size);
    } else {
        SV* bytes = POPs;
        status = static_cast<int>(SvIV(POPs));
        STRLEN n = 0;
        const char* buffer = SvPVbyte(bytes, n);
        auto* out = static_cast<unsigned char*>(@C@_alloc(n ? n : 1));
        if (!out) status = 3;
        else {
            if (n) std::memcpy(out, buffer, n);
            *result = out;
            *result_size = n;
        }
    }
    PUTBACK;
    FREETMPS;
    LEAVE;
    return status;
}
void rosetta_perl_retain(unsigned long long id, int delta) {
#if defined(PERL_IMPLICIT_CONTEXT)
    if (!PERL_GET_THX) return;
#endif
    dTHX;
    if (!rosetta_perl_retain_sv || PL_phase == PERL_PHASE_DESTRUCT) return;
    dSP;
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);
    EXTEND(SP, 2);
    mPUSHu(static_cast<UV>(id));
    mPUSHi(delta);
    PUTBACK;
    call_sv(rosetta_perl_retain_sv, G_DISCARD | G_EVAL);
    FREETMPS;
    LEAVE;
}
} // namespace
// (status, result bytes) = _call(index, argument bytes)
ROSETTA_CABI_EXPORT void @C@_perl_call(pTHX_ CV* cv) {
    dXSARGS;
    if (items != 2) croak_xs_usage(cv, "index, data");
    const UV index = SvUV(ST(0));
    STRLEN size = 0;
    const char* data = SvPVbyte(ST(1), size);
    unsigned char* out = nullptr;
    std::size_t out_size = 0;
    const int status = @C@_call(static_cast<unsigned>(index), reinterpret_cast<const unsigned char*>(data), size, &out, &out_size);
    SV* bytes = out ? newSVpvn(reinterpret_cast<const char*>(out), out_size) : newSVpvs("");
    if (out) @C@_free(out);
    ST(0) = sv_2mortal(newSViv(status));
    ST(1) = sv_2mortal(bytes);
    XSRETURN(2);
}
// _release(holder, now)
ROSETTA_CABI_EXPORT void @C@_perl_release(pTHX_ CV* cv) {
    dXSARGS;
    if (items != 2) croak_xs_usage(cv, "holder, now");
    @C@_release(reinterpret_cast<void*>(static_cast<std::uintptr_t>(SvUV(ST(0)))), static_cast<int>(SvIV(ST(1))));
    XSRETURN_EMPTY;
}
// status = _init(invoke hook, retain hook)
ROSETTA_CABI_EXPORT void @C@_perl_init(pTHX_ CV* cv) {
    dXSARGS;
    if (items != 2) croak_xs_usage(cv, "invoke, retain");
    if (rosetta_perl_invoke_sv) SvREFCNT_dec(rosetta_perl_invoke_sv);
    if (rosetta_perl_retain_sv) SvREFCNT_dec(rosetta_perl_retain_sv);
    rosetta_perl_invoke_sv = newSVsv(ST(0));
    rosetta_perl_retain_sv = newSVsv(ST(1));
    const int status = @C@_init(1, rosetta_perl_invoke, rosetta_perl_retain);
    ST(0) = sv_2mortal(newSViv(status));
    XSRETURN(1);
}
)cpp";
    const std::string prefix = "rosetta_" + name_;
    for (std::size_t i = 0; (i = glue.find("@C@", i)) != std::string::npos; i += prefix.size()) glue.replace(i, 3, prefix);
    return glue;
}

GeneratedPerlSource Generator::run() {
    if (!identifier(name_)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    package_ = options_.package.empty() ? pascal(name_) : options_.package;
    {
        std::size_t start = 0;
        while (true) {
            const auto colons = package_.find("::", start);
            const auto part = package_.substr(start, colons == std::string::npos ? std::string::npos : colons - start);
            if (!identifier(part) || perl_builtin(part) || part == "CORE" || part == "SUPER" || part == "UNIVERSAL" || part == "main")
                throw BindingError("Perl package must be identifiers joined by :: and not Perl keywords: " + package_);
            if (colons == std::string::npos) break;
            start = colons + 2;
        }
    }
    library_ = options_.library.empty() ? name_ : options_.library;
    for (const char c : library_)
        if (!is_alnum(c) && c != '_' && c != '-' && c != '.') throw BindingError("Perl library name must be a plain file name: " + library_);
    runtime_ = package_ + "::Rosetta";
    GeneratedPerlSource result;
    prepare();
    // Packages under the main one: the runtime's first.
    for (const char* reserved : {"NativeObject", "Error", "Rosetta", "CORE", "SUPER", "UNIVERSAL", "ISA", "EXPORT", "EXPORT_OK", "EXPORT_TAGS", "VERSION"})
        types_.reserve(reserved);
    const auto type_name = [&](const std::string& wanted) { return types_.claim_type(pascal(wanted)); };
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        plan.name = type_name(plan.name);
        packages_[owner] = package_ + "::" + plan.name;
    }
    for (const auto& enumeration : module_.enumerations) {
        const auto qualified = enumeration.qualified_name();
        if (!enums_.count(qualified)) continue;
        const bool nested = !enumeration.owner.empty();
        if (nested && !classes_.count(enumeration.owner)) continue;
        auto name = renamed(qualified, nested || flat(enumeration.namespaces) ? enumeration.name : exported(qualified));
        if (nested) name = classes_.at(enumeration.owner).name + "_" + name;
        enum_names_[qualified] = type_name(name);
        packages_[qualified] = package_ + "::" + enum_names_[qualified];
    }
    for (const auto& [qualified, name] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        Names scope;
        for (const char* reserved : {"BEGIN", "END", "INIT", "CHECK", "UNITCHECK", "DESTROY", "AUTOLOAD", "STDIN", "STDOUT", "STDERR", "ARGV",
                                     "ARGVOUT", "ENV", "INC", "SIG", "DATA", "ISA", "VERSION", "EXPORT", "EXPORT_OK", "EXPORT_TAGS", "CLONE",
                                     "CLONE_SKIP", "ADJUST", "DOES"})
            scope.reserve(reserved);
        for (const auto& enumerator : enumeration.enumerators)
            enum_cases_[qualified].push_back(scope.claim(shout(renamed(qualified + "::" + enumerator, enumerator))));
    }
    plan_members();
    std::vector<Group> functions = plan_functions();
    std::vector<ConstantPlan> constants = plan_constants([&](const Variable& variable) {
        const auto qualified = variable.qualified_name();
        return renamed(qualified, flat(variable.namespaces) ? variable.name : exported(qualified));
    });
    check_diagnostics();
    // Policy modules are packages under the main one (a.b nests).
    for (const auto& group : functions) {
        if (group.module.empty() || submodules_.count(group.module)) continue;
        std::string package = package_;
        std::size_t start = 0;
        bool top = true;
        while (true) {
            const auto dot = group.module.find('.', start);
            auto part = pascal(group.module.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
            if (top) part = types_.free(part) ? part : part + "_";
            top = false;
            package += "::" + part;
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        submodules_[group.module] = package;
        types_.reserve(package.substr(package_.size() + 2, package.find("::", package_.size() + 2) - package_.size() - 2));
    }
    std::stable_partition(functions.begin(), functions.end(), [](const Group& group) { return group.module.empty(); });
    // Module functions do not take the names of the packages beside them.
    for (const auto& name : types_.taken) functions_[""].reserve(name);
    for (const char* reserved : {"import", "unimport", "BEGIN", "END", "INIT", "CHECK", "UNITCHECK", "DESTROY", "AUTOLOAD", "VERSION", "ISA",
                                 "EXPORT", "EXPORT_OK", "EXPORT_TAGS", "can", "isa", "DOES"})
        for (auto& module : functions) functions_[module.module].reserve(reserved);
    for (const char* reserved : {"import", "BEGIN", "END", "INIT", "CHECK", "UNITCHECK", "DESTROY", "AUTOLOAD", "VERSION"}) functions_[""].reserve(reserved);
    name_everything(functions, constants);

    emit_cpp(result.source, result.parts, functions, constants, "Perl package " + package_ + " (" + name_ + ").");
    result.source += xs_glue();
    // The Perl methods C++ calls for virtual methods (emit_cpp numbers them).
    for (const auto& owner : class_order_)
        for (const auto& group : classes_.at(owner).methods) {
            if (group.is_static) continue;
            for (const auto& overload : group.overloads) {
                if (overload.extension || !overload.function->is_virtual) continue;
                const auto found = generic_index_.find(overload.function->name);
                if (found != generic_index_.end()) generic_methods_.emplace(found->second, names_.at(&group));
            }
        }

    // ------------------------------------------------------------ the Perl module
    auto runtime = std::string(detail::perl_full_runtime_parts[0]) + detail::perl_full_runtime_parts[1];
    const auto replace = [&](const std::string& from, const std::string& to) {
        for (std::size_t i = 0; (i = runtime.find(from, i)) != std::string::npos; i += to.size()) runtime.replace(i, from.size(), to);
    };
    auto environment = name_;
    for (auto& c : environment) c = upper(c);
    auto path = package_;
    for (std::size_t i; (i = path.find("::")) != std::string::npos;) path.replace(i, 2, "/");
    replace("@NS@", package_);
    replace("@C@", "rosetta_" + name_);
    replace("@LIB@", library_);
    replace("@ENV@", environment + "_PERL_LIBRARY");
    replace("@PATH@", path);
    std::ostringstream perl;
    perl << "# Code generated by Rosetta from " << header_name() << "; DO NOT EDIT.\npackage " << package_ << ";\n\nuse strict;\nuse warnings;\n"
         << "BEGIN { require 5.014 }\n\n" << file_doc() << "\n" << runtime;
    for (const auto& [qualified, name] : enum_names_) emit_enum(perl, qualified);
    for (const auto& owner : class_order_) emit_class(perl, owner);
    // Module functions and constants.
    std::vector<std::string> exports;
    perl << "\npackage " << package_ << ";\n\nuse Exporter 'import';\n\n=head1 FUNCTIONS\n\nThe functions of the module; C<use " << package_
         << " qw(:all)> imports every one.\n\n=cut\n";
    for (const auto& group : functions) {
        if (!group.module.empty()) continue;
        emit_group(perl, group, nullptr);
        exports.push_back(names_.at(&group));
    }
    if (!constants.empty()) perl << "\n=head1 CONSTANTS\n\nFunctions returning the values of the C++ constants (exported on request, and by C<:all>).\n\n=cut\n";
    for (const auto& constant : constants) {
        auto about = doc_of(*constant.documentation, "The C++ constant `" + normalize(constant.expression) + "`.");
        about.add("Returns " + pod_escape(perl_type(*constant.type)) + ".");
        const auto& name = constant_names_.at(&constant);
        perl << "\n=head2 " << name << "\n\n" << about.render() << "\n\n=cut\n\nsub " << name << " { return " << runtime_ << "::call(" << constant.index << ", []) }\n";
        exports.push_back(name);
    }
    perl << "\nour @EXPORT_OK = qw(" << join(exports, " ") << ");\nour %EXPORT_TAGS = (all => \\@EXPORT_OK);\n";
    for (const auto& [module, package] : submodules_) {
        std::string from;
        for (const auto& [ns, mapped] : policy_.modules) if (mapped == module) from = ns;
        std::vector<std::string> names;
        perl << "\npackage " << package << ";\n\nuse Exporter 'import';\n\n=head1 " << package << "\n\nThe functions of the C++ namespace C<"
             << pod_escape(from) << "> (policy module C<" << module << ">); C<use " << package << " qw(:all)> imports every one.\n\n=cut\n";
        for (const auto& group : functions)
            if (group.module == module) {
                emit_group(perl, group, nullptr);
                names.push_back(names_.at(&group));
            }
        perl << "\nour @EXPORT_OK = qw(" << join(names, " ") << ");\nour %EXPORT_TAGS = (all => \\@EXPORT_OK);\n";
    }
    // The class table and the Perl methods C++ calls for virtual methods.
    perl << "\npackage " << runtime_ << ";\n\n@CLASSES = (\n";
    for (const auto& owner : class_order_) {
        std::vector<std::string> bases;
        for (const auto& base : classes_.at(owner).bases) {
            std::size_t at = 0;
            while (class_order_[at] != base) ++at;
            bases.push_back(std::to_string(at));
        }
        perl << "    [" << perl_string(packages_.at(owner)) << ", [" << join(bases, ", ") << "]],\n";
    }
    perl << ");\n%METHODS = (";
    for (const auto& [generic, method] : generic_methods_) perl << generic << " => " << perl_string(method) << ", ";
    perl << ");\n\n1;\n";
    result.perl = perl.str();
    result.diagnostics = diagnostics_;
    return result;
}
} // namespace

GeneratedPerlSource generate_perl(const Module& module, const std::string& name, bool skip_unsupported, const BindingPolicy& policy,
                                  const PerlOptions& options) {
    if (options.parts == 0 || options.parts > 256) throw BindingError("Perl C++ source parts must be from 1 to 256");
    return Generator(module, name, skip_unsupported, policy, options).run();
}
} // namespace rosetta
