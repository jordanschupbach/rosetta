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
    // Number of trailing parameters whose defaults are visible at the header entry point.
    std::size_t default_arguments = 0;

    std::string qualified_name() const;
    std::string identity() const;
};

struct Field {
    std::string name;
    Type type;
    std::string access = "public";
    bool bitfield = false;
    SourceLocation location{};
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

struct Module {
    std::string header;
    std::vector<Function> functions;
    std::vector<std::string> diagnostics;
    std::vector<Enumeration> enumerations;
    std::vector<Record> records;
    ParseContext parse_context;
    // Compiler-discovered regular-file inputs, collected on explicit request.
    std::vector<std::string> dependencies;
};

std::string serialize_ir(const Module& module);

} // namespace rosetta
