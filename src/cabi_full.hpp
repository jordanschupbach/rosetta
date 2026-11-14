#pragma once

// Shared half of the full C ABI backends (Julia: julia_full.cpp, C#: csharp_full.cpp): what to
// bind and the generated C++ library. A target language's generator derives from
// CabiGenerator, names things through its hooks, and writes the code on its own side.
//
// The C++ library is the one the full Common Lisp backend introduced (lisp_full_runtime.hpp):
// one entry point, rosetta_<module>_call(index, arguments), with values in a compact binary
// encoding, overloads resolved at run time in C++, callbacks and overrides of virtual methods
// (directors) calling back through two function pointers the target registers at load time.
//
// Planning, in order: prepare() (classes, directors, enums), then the target names classes and
// enums, then plan_members() (constructors, methods, operators, fields, class constants),
// plan_functions() and plan_constants(); check_diagnostics(); emit_cpp().
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace rosetta::cabi {

std::string quote(const std::string& text);  // a C++ string literal
std::string exported(std::string text);      // a::b -> a__b
bool identifier(const std::string& text);    // ASCII identifier (a letter or _ first)
std::string normalize(std::string text);     // without inline namespaces and a leading ::
std::string strip_cv(std::string text);
const Type& referent(const Type& type);
bool is_void(const Type& type);
bool is_operator(const std::string& name);
std::string one_line(std::string text);
std::string shortest_double(std::string text); // a C++ floating literal as the shortest text reading back the same

enum class Use { parameter, result, element, callback_argument, callback_result };

struct Overload {
    const Function* function;
    std::string owner;
    bool extension = false;    // a free function (policy extend, or a free operator)
    bool receives_self = false;
};
struct Group {
    std::string name;          // target name (function, method, or operator)
    std::vector<Overload> overloads;
    bool is_static = false;
    std::string symbol;        // generated C++ function
    std::size_t index = 0;     // its index in the native function table
    std::string module;        // policy submodule of a module function ("" = the main module)
    std::size_t operand = 0;   // operators: position of the bound operand (0 = self)
};
struct FieldPlan { const Field* field; std::string name; std::string getter, setter; std::size_t getter_index = 0, setter_index = 0; };
struct ConstantPlan { std::string name, expression; const Type* type; const Documentation* documentation; std::string symbol; std::size_t index = 0; };
struct ClassPlan {
    const Record* record;
    std::string name;          // target type name (Outer_Inner for nested classes)
    std::vector<std::string> bases;
    std::vector<Overload> constructors;
    std::vector<Group> methods;  // instance and static methods
    std::vector<Group> operators;
    std::vector<FieldPlan> fields;
    std::vector<ConstantPlan> constants;
    std::string parent;
    std::string constructor;   // generated C++ function, empty without a public constructor
    std::size_t constructor_index = 0;
    bool director = false;
};

class CabiGenerator {
public:
    CabiGenerator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, std::size_t parts);
    virtual ~CabiGenerator() = default;

protected:
    // ------------------------------------------------------------ hooks
    virtual std::string language() const = 0;                          // "Julia", "C#" (in messages)
    virtual std::string target_name(const std::string& cpp) const = 0; // a C++ name, escaped for the target
    virtual std::string method_name(const std::string& cpp) const { return target_name(cpp); }
    // A class's (or enum's) target name; `cpp` may be a qualified name joined with __.
    virtual std::string type_name(const std::string& cpp) const { return target_name(cpp); }
    // The target spelling of a policy rename, for a name whose generated spelling is `fallback`.
    virtual std::string rename_target(const std::string& name, const std::string&) const { return target_name(name); }
    virtual std::string field_name(const std::string& cpp) const { return cpp; }
    // The target name of a C++ operator with `arity` operands besides the first ("" = not bound).
    virtual std::string operator_name(const std::string& cpp, std::size_t arity) const = 0;
    virtual bool free_operator(const std::string&) const { return true; }   // may a free operator bind to it?
    virtual std::string static_constant_name(const ClassPlan& plan, const std::string& cpp) const = 0;
    virtual std::vector<std::string> parameter_names(const Overload& overload) const = 0;
    // How the overloads of a group (or a class's constructors) are called, for "no overload" errors.
    virtual std::string signatures(const Group& group, bool is_method) const = 0;
    virtual std::string constructor_signatures(const ClassPlan& plan) const = 0;
    virtual std::string abstract_message(const ClassPlan& plan) const = 0;   // constructing an abstract class
    virtual std::string protected_message(const ClassPlan& plan) const = 0;  // a protected constructor
    virtual std::string symbol_prefix() const = 0;                     // of generated C++ functions
    virtual std::string runtime_define() const = 0;                    // ROSETTA_FULL_<LANGUAGE>
    virtual std::string module_prefix() const = 0;                     // of the C++ namespace

    // ------------------------------------------------------------ state
    const Module& module_;
    std::string name_;
    bool skip_;
    const BindingPolicy policy_;
    bool flat_;
    std::size_t parts_;
    std::vector<std::string> diagnostics_;
    std::map<std::string, const Record*> records_;
    std::map<std::string, ClassPlan> classes_;
    std::vector<std::string> class_order_;
    std::map<std::string, std::string> template_names_;
    std::map<std::string, std::string> function_template_names_;
    std::set<std::string> extension_functions_;
    std::map<std::string, const Enumeration*> enums_;
    std::map<std::string, std::string> enum_names_;   // qualified enum -> target name
    std::vector<std::string> symbols_;               // every generated C++ function, in table order (from 1)
    std::map<std::string, std::size_t> generic_index_; // overridable method name -> index (directors)
    std::vector<std::string> generic_order_;

    // ------------------------------------------------------------ helpers
    void reject(const std::string& context, const std::string& reason) { diagnostics_.push_back(context + ": " + reason); }
    bool excluded(const std::string& qualified, const std::string& identity = {}) const;
    bool readonly(const std::string& qualified, const std::string& owner) const;
    std::string renamed(const std::string& qualified, const std::string& fallback) const;
    std::string symbol(std::size_t* index);
    std::size_t generic(const std::string& name);
    bool bound(const Type& type) const { return type.kind == TypeKind::record && classes_.count(type.declaration); }
    std::string conversion(const Type& type) const;
    bool copyable_record(const std::string& name, unsigned depth = 0, bool as_base = false);
    bool copyable_type(const Type& type, unsigned depth);
    std::string value_problem(const Type& type, Use use) const;
    std::string type_problem(const Type& type, Use use) const;
    std::string function_problem(const Function& function, bool self_parameter) const;
    std::string return_policy(const Function& function) const;
    std::string class_name(const Record& record) const;
    bool copy_type(const Type& type) const;
    bool flat(const std::vector<std::string>& namespaces) const;
    std::vector<const Function*> virtuals(const Record& record, std::map<std::string, bool>& pure) const;
    static std::string virtual_key(const Function& method);
    bool callback_supported(const Function& method) const;
    std::set<std::string> bound_method_names(const std::string& owner) const;
    // The virtual methods a target subclass of `plan` overrides: (target name, method, pure).
    struct Overridable { std::string name; const Function* method; bool pure; };
    std::vector<Overridable> overridable(const ClassPlan& plan) const;
    bool derives(const std::string& from, const std::string& to) const;
    bool primary_derives(const std::string& from, const std::string& to) const;
    static std::string director_name(const ClassPlan& plan);

    // ------------------------------------------------------------ planning
    void prepare();                         // diagnostics, enums, classes, directors
    void drop_classes(const std::set<std::string>& rejected); // and the classes deriving from or nested in them
    void plan_members();                    // constructors, methods, operators, fields, class constants
    std::vector<Group> plan_functions();    // module functions by (submodule, name)
    std::vector<ConstantPlan> plan_constants(const std::function<std::string(const Variable&)>& name);
    void check_diagnostics() const;         // throws unless --skip-unsupported

    // ------------------------------------------------------------ the C++ library
    void emit_cpp(std::string& source, std::vector<std::string>& parts, std::vector<Group>& functions,
                  std::vector<ConstantPlan>& constants, const std::string& comment);

private:
    std::map<std::string, bool> copyable_cache_;
    void plan_classes();
    void plan_directors();
    void emit_director(std::ostream& out, const ClassPlan& plan);
    void emit_overload(std::ostream& out, const Overload& overload, const std::string& kind, bool checks);
    void emit_group(std::ostream& out, Group& group, bool is_method);
    void emit_constructor(std::ostream& out, ClassPlan& plan);
};

} // namespace rosetta::cabi
