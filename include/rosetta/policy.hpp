#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>

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
};

BindingPolicy load_policy(const std::filesystem::path& path);

} // namespace rosetta
