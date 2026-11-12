#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace rosetta {

class BindingError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class TypeKind { unresolved, builtin, enumeration, record, function, pointer, lvalue_reference, rvalue_reference, template_instance };

const char* type_kind_name(TypeKind kind);

struct Type {
    std::string spelling;
    std::string canonical;
    TypeKind kind = TypeKind::unresolved;
    std::string declaration{};
    std::vector<Type> arguments{};
    bool is_const = false;
    bool is_volatile = false;
};

struct SourceLocation {
    std::string file;
    std::size_t line = 0;
    std::size_t column = 0;
    std::size_t offset = 0;
};

// A documentation comment parsed from its Doxygen/Javadoc commands, for backends to render in
// their language's convention. Text uses markup every target reads: `code`, **bold**, *emphasis*;
// line breaks are kept so lists survive, and list continuation lines are indented two spaces.
struct DocBlock {
    std::string kind;     // "text", "code" (verbatim lines), "math" or "heading" (@par title)
    std::string text;
    std::string language; // code only: the language named by @code{.py}, empty otherwise
};

struct DocItem {
    std::string name;      // parameter, exception type, return value or command name
    std::string text;
    std::string direction; // @param[in], [out] or [in,out]; empty unless written
};

struct Documentation {
    std::string brief;                 // @brief, or the first paragraph without one
    std::vector<DocBlock> details;     // the rest of the description, in source order
    std::vector<DocItem> params, tparams, throws, retvals;
    std::string returns;
    std::vector<DocItem> notes;        // name: note, warning, deprecated, pre, post, since, todo, ...
    std::vector<std::string> see;      // @see/@sa targets as written

    bool empty() const {
        return brief.empty() && details.empty() && params.empty() && tparams.empty() && throws.empty() &&
               retvals.empty() && returns.empty() && notes.empty() && see.empty();
    }
};

// Plain text rendering of a documentation comment (the `doc` strings below).
std::string plain_documentation(const Documentation& documentation);

// A default argument, when its value is a literal a binding can show or reuse.
struct DefaultValue {
    // "" (none or not representable), "int", "float", "bool", "null", "string" (value is a C++
    // string literal with quotes), "char" (value is the character), "enum" (value is the
    // enumerator name), "constant" (value is the variable's name) or "default" (`{}`, `T()`).
    std::string kind;
    std::string value;
};

struct Function {
    std::string name;
    std::vector<std::string> namespaces;
    Type result;
    std::vector<Type> parameters;
    SourceLocation location{};
    std::string owner{};
    std::string access = "public";
    std::string unsupported_reason{};
    bool is_constructor = false;
    bool is_static = false;
    bool is_const = false;
    bool is_deleted = false;
    bool is_noexcept = false;
    // Virtual by declaration, override/final attribute, or inheritance from a virtual base method.
    bool is_virtual = false;
    bool is_pure = false;
    // Plain text of the attached documentation comment, empty without one.
    std::string doc{};
    Documentation documentation{};
    // Number of trailing parameters whose defaults are visible at the header entry point.
    std::size_t default_arguments = 0;
    // Declared parameter names, empty where unnamed; parallel to parameters.
    std::vector<std::string> parameter_names{};
    // Default argument values, parallel to parameters (kind empty where there is none or it is not a literal).
    std::vector<DefaultValue> default_values{};
    // Primary template of a requested function template specialization (name is then f<args>).
    std::string template_name{};

    std::string qualified_name() const;
    std::string identity() const;
};

struct Field {
    std::string name;
    Type type;
    std::string access = "public";
    bool bitfield = false;
    SourceLocation location{};
    std::string doc{};
    Documentation documentation{};
};

// Namespace-scope or static member variables. Constant initializers stay in C++:
// generated code reads the variable itself.
struct Variable {
    std::string name;
    std::vector<std::string> namespaces;
    Type type;
    std::string owner{};
    std::string access = "public";
    bool is_constexpr = false;
    SourceLocation location{};
    std::string doc{};
    Documentation documentation{};

    std::string qualified_name() const;
};

struct BaseClass {
    // Resolved declaration (qualified record name) or the written spelling when unresolved.
    Type type;
    std::string access = "public";
    bool is_virtual = false;
};

struct Record {
    std::string name;
    std::vector<std::string> namespaces;
    std::vector<Function> constructors;
    std::vector<Function> methods;
    std::vector<Field> fields;
    SourceLocation location{};
    bool complete = false;
    bool abstract = false;
    bool public_destructor = true;
    bool has_bases = false;
    std::string unsupported_reason{};
    std::string owner{};
    std::vector<BaseClass> bases{};
    std::vector<Variable> static_fields{};
    bool polymorphic = false;
    bool virtual_destructor = false;
    bool is_final = false;
    // Primary template of an explicitly requested specialization, empty otherwise.
    std::string template_name{};
    std::string doc{};
    Documentation documentation{};

    std::string qualified_name() const;
};

struct Enumeration {
    std::string name;
    std::vector<std::string> namespaces;
    std::vector<std::string> enumerators;
    bool scoped = false;
    bool selected = true;
    SourceLocation location;
    std::string owner{};

    // Decimal compiler-evaluated values, parallel to enumerators (no host-width loss).
    std::vector<std::string> enumerator_values{};
    std::string underlying_type{};
    std::string doc{};
    Documentation documentation{};

    std::string qualified_name() const;
};

struct ParseContext {
    std::string compiler;
    std::string directory;
    std::vector<std::string> flags;
    std::string compilation_database;
    std::string translation_unit;
    std::size_t command_index = 0;
};

// A using-declaration in a selected header that makes an entity visible in another namespace
// (namespace short { using long_name::Thing; }): bindings may expose it under that name too.
struct Reexport {
    std::vector<std::string> namespaces; // where the using-declaration appears
    std::string name;
    std::string target;                  // qualified name of the entity
    std::string kind;                    // "record", "enum", "function" or "type" (an alias of a record or enum)
    SourceLocation location{};
};

struct Module {
    std::string header;
    std::vector<Function> functions;
    std::vector<std::string> diagnostics;
    std::vector<Enumeration> enumerations;
    std::vector<Record> records;
    std::vector<Variable> variables{};
    std::vector<Reexport> reexports{};
    ParseContext parse_context;
    // Compiler-discovered regular-file inputs, collected on explicit request.
    std::vector<std::string> dependencies;
};

std::string serialize_ir(const Module& module);

} // namespace rosetta
