#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace rosetta {

enum class CallbackLifetime { call, retained };

// Target-independent facts that cannot be inferred from C++ signatures.
struct ClassPolicy {
    bool constructors = true;
    bool restrict_methods = false;
    bool restrict_fields = false;
    std::set<std::string> methods;
    std::set<std::string> fields;
    std::set<std::string> borrow_self;
};

struct BindingPolicy {
    std::map<std::string, ClassPolicy> classes;
    bool explicit_class_selection = false;
    std::map<std::string, std::map<std::size_t, CallbackLifetime>> callbacks;
    // Explicit positive single-bit enumerators defining the accepted input mask.
    std::map<std::string, std::set<std::string>> enum_flags;

    // Export every complete class in the binding scope (each as owned) without listing it.
    bool select_all = false;
    // Qualified names (or identities with signatures) to leave out; '*' matches any text.
    std::vector<std::string> exclude;
    // Qualified C++ name (class, function, method, field, enum or enumerator) to target name.
    std::map<std::string, std::string> rename;
    // Target class name to an explicitly instantiated C++ class template specialization.
    std::vector<std::pair<std::string, std::string>> templates;
    // Extra headers/directories whose declarations are exported (resolved against the policy file).
    std::vector<std::filesystem::path> scopes;
    // Class to {method name: free function}; a first parameter of the class type receives self.
    std::map<std::string, std::map<std::string, std::string>> extend;
    // Function or method to result ownership: owned (caller deletes), copy or borrowed.
    std::map<std::string, std::string> returns;
    // Classes whose references (method results and fields) are returned as copies, for
    // value-like types where a live reference into the owner would surprise callers.
    std::set<std::string> copy_types;
    // Fields, variables or classes (patterns) exported without setters.
    std::vector<std::string> readonly;
    // Documentation overrides keyed by qualified name.
    std::map<std::string, std::string> docs;
    // C++ type to an implicitly convertible type whose language mapping it reuses.
    std::map<std::string, std::string> conversions;
    // "qualified" (default) exports ns__name; "flat" exports unqualified names.
    std::string names = "qualified";
    // Namespace patterns whose members keep qualified (ns__name) names under "flat" naming.
    std::vector<std::string> qualify;
    // "list" (default) or "tuple" for sequence results.
    std::string sequences = "list";
    // Ordered (pattern, [(C++ exception, Python exception)]) rules: the first rule whose
    // glob matches a function's qualified name decides how its exceptions translate.
    std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> exceptions;
    // Language subclasses may override virtual methods: "auto" (default) or "none".
    std::string directors = "auto";
};

// '*' wildcard match of a pattern against a qualified name.
bool policy_match(const std::string& pattern, const std::string& name);

BindingPolicy load_policy(const std::filesystem::path& path);

} // namespace rosetta
