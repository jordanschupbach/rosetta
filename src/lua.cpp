#include "rosetta/lua.hpp"
#include "lua_runtime.hpp"
#include "lua_callbacks.hpp"

#include <algorithm>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace rosetta {
namespace {

enum class Category { integer, number, boolean, cstring, enumeration, value, nothing, unsupported };

const Type& value_type(const Type& type) {
    if ((type.kind == TypeKind::lvalue_reference || type.kind == TypeKind::rvalue_reference) && type.arguments.size() == 1)
        return value_type(type.arguments.front());
    return type;
}

bool standard_template(const Type& type, const std::string& name) {
    if (type.kind != TypeKind::template_instance) return false;
    return std::regex_match(type.declaration, std::regex("std::(__[A-Za-z0-9_]+::)*" + name));
}
bool default_hash_supported(const Type& type);
bool monostate_type(const Type& type) {
    auto name = type.canonical;
    if (type.is_const && name.rfind("const ", 0) == 0) name.erase(0, 6);
    return std::regex_match(name, std::regex("std::(__[A-Za-z0-9_]+::)*monostate"));
}
bool nil_value_type(const Type& type) { return standard_template(type, "optional") || monostate_type(type); }
bool contains_unordered_associative(const Type& type) {
    for (const auto* name : {"unordered_map", "unordered_multimap", "unordered_set", "unordered_multiset"})
        if (standard_template(type, name)) return true;
    return std::any_of(type.arguments.begin(), type.arguments.end(), contains_unordered_associative);
}

Category category(const Type& type) {
    if (type.is_volatile || type.kind == TypeKind::rvalue_reference) return Category::unsupported;
    if (type.kind == TypeKind::lvalue_reference)
        return type.arguments.size() == 1 ? category(type.arguments.front()) : Category::unsupported;
    if (type.kind == TypeKind::enumeration) return Category::enumeration;
    static const std::set<std::string> integers = {"char", "signed char", "unsigned char", "short",
        "unsigned short", "int", "unsigned int", "long", "unsigned long", "long long", "unsigned long long"};
    auto name = type.canonical;
    if (type.is_const && name.rfind("const ", 0) == 0) name = name.substr(6);
    if (integers.count(name)) return Category::integer;
    if (name == "float" || name == "double") return Category::number;
    if (name == "bool") return Category::boolean;
    if (name == "const char *") return Category::cstring;
    if (name == "void") return Category::nothing;
    if (monostate_type(type)) return Category::value;
    if ((standard_template(type, "basic_string") || standard_template(type, "basic_string_view")) &&
        type.arguments.size() == 1 && type.arguments[0].canonical == "char") return Category::value;
    const bool ordered_map = standard_template(type, "map") || standard_template(type, "multimap");
    const bool unordered_map = standard_template(type, "unordered_map") || standard_template(type, "unordered_multimap");
    if (ordered_map || unordered_map) {
        if (type.arguments.size() != 2) return Category::unsupported; // custom policies need explicit adapters
        for (const auto& element : type.arguments) {
            if (element.kind == TypeKind::lvalue_reference || element.kind == TypeKind::rvalue_reference) return Category::unsupported;
            const auto kind = category(element);
            if (kind != Category::integer && kind != Category::number && kind != Category::boolean && kind != Category::enumeration && kind != Category::value)
                return Category::unsupported;
        }
        if ((ordered_map && contains_unordered_associative(type.arguments[0])) || (unordered_map && !default_hash_supported(type.arguments[0])))
            return Category::unsupported;
        return Category::value;
    }
    const bool ordered_set = standard_template(type, "set") || standard_template(type, "multiset");
    const bool unordered_set = standard_template(type, "unordered_set") || standard_template(type, "unordered_multiset");
    if (ordered_set || unordered_set) {
        if (type.arguments.size() != 1) return Category::unsupported;
        const auto& element = type.arguments.front();
        if (element.kind == TypeKind::lvalue_reference || element.kind == TypeKind::rvalue_reference) return Category::unsupported;
        const auto kind = category(element);
        if (kind != Category::integer && kind != Category::number && kind != Category::boolean && kind != Category::enumeration && kind != Category::value)
            return Category::unsupported;
        if ((ordered_set && contains_unordered_associative(element)) || (unordered_set && !default_hash_supported(element)))
            return Category::unsupported;
        return Category::value;
    }
    if (standard_template(type, "tuple") || standard_template(type, "pair") || standard_template(type, "variant")) {
        if (standard_template(type, "pair") && type.arguments.size() != 2) return Category::unsupported;
        if (standard_template(type, "variant") && type.arguments.empty()) return Category::unsupported;
        for (const auto& element : type.arguments) {
            // Product values own their elements; reference tuples need a lifetime policy.
            if (element.kind == TypeKind::lvalue_reference || element.kind == TypeKind::rvalue_reference)
                return Category::unsupported;
            const auto kind = category(element);
            if (kind != Category::integer && kind != Category::number && kind != Category::boolean && kind != Category::enumeration && kind != Category::value)
                return Category::unsupported;
        }
        return Category::value;
    }
    const bool dynamic_sequence = standard_template(type, "vector") || standard_template(type, "deque") ||
        standard_template(type, "list") || standard_template(type, "forward_list");
    const bool sequence = dynamic_sequence || standard_template(type, "array");
    const bool optional = standard_template(type, "optional");
    if ((sequence || optional) && !type.arguments.empty()) {
        const auto& element = type.arguments.front();
        const auto element_category = category(element);
        const bool supported_element = element_category == Category::integer || element_category == Category::number ||
            element_category == Category::boolean || element_category == Category::enumeration || element_category == Category::value;
        // nil cannot represent an engaged empty optional inside another optional,
        // or a missing sequence element. Keep those representations explicit.
        if (!supported_element || nil_value_type(element)) return Category::unsupported;
        if ((dynamic_sequence || optional) && type.arguments.size() == 1) return Category::value;
        if (standard_template(type, "array") && type.arguments.size() == 2 &&
            std::regex_match(type.arguments[1].canonical, std::regex("[0-9]+"))) return Category::value;
    }
    return Category::unsupported;
}

bool default_hash_supported(const Type& type) {
    if (type.is_const || type.is_volatile) return false;
    const auto kind = category(type);
    if (kind == Category::integer || kind == Category::number || kind == Category::boolean || kind == Category::enumeration) return true;
    if (monostate_type(type)) return true;
    if (standard_template(type, "basic_string") || standard_template(type, "basic_string_view")) return true;
    if (standard_template(type, "optional") && type.arguments.size() == 1) {
        auto element = type.arguments.front();
        element.is_const = false; // std::hash<optional<T>> hashes remove_const_t<T>
        if (element.canonical.rfind("const ", 0) == 0) element.canonical.erase(0, 6);
        return default_hash_supported(element);
    }
    if (standard_template(type, "variant")) {
        return std::all_of(type.arguments.begin(), type.arguments.end(), [](Type element) {
            element.is_const = false;
            if (element.canonical.rfind("const ", 0) == 0) element.canonical.erase(0, 6);
            return default_hash_supported(element);
        });
    }
    return standard_template(type, "vector") && type.arguments.size() == 1 && type.arguments[0].canonical == "bool";
}

bool parameter_supported(const Type& type) {
    // A const reference receives a temporary copied value. Mutable references,
    // retained views and rvalue-reference semantics need explicit policies.
    if (type.kind == TypeKind::lvalue_reference &&
        (type.arguments.size() != 1 || !type.arguments.front().is_const)) return false;
    return category(type) != Category::unsupported && category(type) != Category::nothing;
}

const Type* callback_signature(const Type& type) {
    if (type.is_volatile || type.kind == TypeKind::rvalue_reference) return nullptr;
    const auto& base = value_type(type);
    if (!base.is_volatile && standard_template(base, "function") && base.arguments.size() == 1 &&
        base.arguments[0].kind == TypeKind::function) return &base.arguments[0];
    return nullptr;
}

bool borrowed_value(const Type& type) {
    if (category(type) == Category::cstring || standard_template(type, "basic_string_view")) return true;
    return std::any_of(type.arguments.begin(), type.arguments.end(), borrowed_value);
}

bool callback_supported(const Type& type) {
    const auto* signature = callback_signature(type);
    if (!signature || signature->arguments.empty() ||
        (type.kind == TypeKind::lvalue_reference && !value_type(type).is_const)) return false;
    const auto& returned = signature->arguments.front();
    if (returned.kind == TypeKind::lvalue_reference || returned.kind == TypeKind::rvalue_reference || borrowed_value(returned)) return false;
    const auto result = category(returned);
    if (result != Category::integer && result != Category::number && result != Category::boolean &&
        result != Category::value && result != Category::enumeration && result != Category::nothing) return false;
    for (std::size_t i = 1; i < signature->arguments.size(); ++i) {
        const auto& argument = signature->arguments[i];
        if (!parameter_supported(argument)) return false;
    }
    return true;
}

std::string quote(const std::string& value) {
    std::string result = "\"";
    for (const unsigned char c : value) {
        if (c == '\\' || c == '"') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 32 || c == 127) {
            // Fixed-width octal cannot consume digits from the following character.
            result += '\\';
            result += static_cast<char>('0' + ((c >> 6) & 7));
            result += static_cast<char>('0' + ((c >> 3) & 7));
            result += static_cast<char>('0' + (c & 7));
        } else result += static_cast<char>(c);
    }
    return result + '"';
}

std::string diagnostic_text(const std::vector<std::string>& diagnostics) {
    std::string result;
    for (const auto& message : diagnostics) result += "\n" + message;
    return result;
}

enum class ObjectMode { none, value, reference, pointer, unique, shared };
struct ObjectType { ObjectMode mode = ObjectMode::none; const Type* native = nullptr; };

ObjectType object_type(const Type& type, const std::set<std::string>& classes) {
    if (type.is_volatile || type.kind == TypeKind::rvalue_reference) return {};
    const auto& base = value_type(type);
    if (base.is_volatile) return {};
    if (base.kind == TypeKind::record && classes.count(base.declaration))
        return {type.kind == TypeKind::lvalue_reference ? ObjectMode::reference : ObjectMode::value, &base};
    if (base.kind == TypeKind::pointer && base.arguments.size() == 1 && base.arguments[0].kind == TypeKind::record &&
        !base.arguments[0].is_volatile && classes.count(base.arguments[0].declaration))
        return {ObjectMode::pointer, &base.arguments[0]};
    if (base.arguments.size() == 1 && base.arguments[0].kind == TypeKind::record && !base.arguments[0].is_const &&
        !base.arguments[0].is_volatile && classes.count(base.arguments[0].declaration)) {
        if (standard_template(base, "unique_ptr")) return {ObjectMode::unique, &base.arguments[0]};
        if (standard_template(base, "shared_ptr")) return {ObjectMode::shared, &base.arguments[0]};
    }
    return {};
}

std::string export_name(const std::vector<std::string>& namespaces, const std::string& name) {
    std::string result;
    for (const auto& part : namespaces) result += part + "__";
    return result + name;
}

struct Call {
    Function function;
    std::string exported;
    std::string field;
    bool setter = false;
    bool borrow_self = false;
};

struct ClassPlan {
    const Record* record;
    std::string exported;
    std::vector<std::size_t> calls;
};

} // namespace

GeneratedSource generate_lua(const Module& module, const std::string& name, bool skip_unsupported, const BindingPolicy& policy) {
    if (!std::regex_match(name, std::regex("[A-Za-z][A-Za-z0-9_]*")))
        throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    GeneratedSource result{{}, module.diagnostics};
    std::vector<Call> calls;
    std::vector<ClassPlan> class_plans;
    std::set<std::string> classes;
    std::set<std::string> exported;
    for (const auto& selected : policy.classes) {
        const auto found = std::find_if(module.records.begin(), module.records.end(), [&](const Record& r) { return r.qualified_name() == selected.first; });
        if (found == module.records.end()) throw BindingError("class policy refers to a class not found in the input header: " + selected.first);
    }
    for (const auto& record : module.records) {
        const auto identity = record.qualified_name();
        const auto selected = policy.classes.find(identity);
        std::string reason;
        if (selected == policy.classes.end()) {
            if (policy.explicit_class_selection || !policy.classes.empty()) continue;
            reason = "unsupported declaration CXXRecordDecl (explicit class policy required)";
        }
        else if (!record.complete) reason = "class definition is incomplete";
        else if (!record.public_destructor) reason = "class requires a public nonthrowing destructor";
        else if (record.abstract && selected->second.constructors) reason = "abstract classes cannot expose constructors";
        else if (record.has_bases) reason = "class inheritance requires an explicit adapter (hierarchy binding is pending)";
        else if (!record.unsupported_reason.empty()) reason = record.unsupported_reason;
        if (!reason.empty()) { result.diagnostics.push_back(identity + ": " + reason); continue; }
        const auto exported_name = export_name(record.namespaces, record.name);
        if (!exported.insert(exported_name).second) throw BindingError("Lua class export name collision: " + exported_name);
        classes.insert(identity);
        class_plans.push_back({&record, exported_name, {}});
    }
    std::vector<std::pair<const Enumeration*, std::string>> enumerations;
    std::map<std::string, std::size_t> enum_indices;
    for (std::size_t i = 0; i < module.enumerations.size(); ++i)
        enum_indices[module.enumerations[i].qualified_name()] = i;
    for (const auto& rule : policy.enum_flags) {
        const auto found = enum_indices.find(rule.first);
        if (found == enum_indices.end()) throw BindingError("enum flag policy refers to an unknown enum: " + rule.first);
        const auto& enumeration = module.enumerations[found->second];
        if (enumeration.enumerators.empty()) throw BindingError("enum flag policy requires an enum definition: " + rule.first);
        if (rule.second.empty()) throw BindingError("enum flag basis must not be empty: " + rule.first);
        for (const auto& name : rule.second)
            if (std::find(enumeration.enumerators.begin(), enumeration.enumerators.end(), name) == enumeration.enumerators.end())
                throw BindingError("enum flag policy names an unknown enumerator: " + rule.first + "::" + name);
    }
    const auto enum_readable = [&](const auto& self, const Type& type) -> bool {
        if (type.kind == TypeKind::enumeration) {
            const auto found = enum_indices.find(type.declaration);
            return found != enum_indices.end() && !module.enumerations[found->second].enumerators.empty();
        }
        return std::all_of(type.arguments.begin(), type.arguments.end(), [&](const Type& child) { return self(self, child); });
    };
    const auto parameter_valid = [&](const Type& type) {
        const auto object = object_type(type, classes);
        if (object.mode != ObjectMode::none) {
            if (object.mode == ObjectMode::unique) return false; // transfer needs a separate consuming policy
            if (object.mode == ObjectMode::pointer && type.kind == TypeKind::lvalue_reference) return false;
            if (object.mode == ObjectMode::shared && type.kind == TypeKind::lvalue_reference && !value_type(type).is_const) return false;
            return true;
        }
        return parameter_supported(type) && enum_readable(enum_readable, type);
    };
    const auto call_valid = [&](const Call& call) {
        const auto& f = call.function;
        if (f.default_arguments > f.parameters.size()) return std::string("invalid default argument metadata");
        if (!f.unsupported_reason.empty()) return f.unsupported_reason;
        if (f.access != "public") return std::string("member is not public");
        if (f.is_deleted) return std::string("deleted member cannot be bound");
        const auto object = object_type(f.result, classes);
        if (object.mode == ObjectMode::unique && f.result.kind == TypeKind::lvalue_reference)
            return std::string("unique_ptr reference results require an explicit transfer policy");
        if ((object.mode == ObjectMode::reference || object.mode == ObjectMode::pointer) && !call.borrow_self)
            return std::string("class pointer/reference result requires an explicit borrow_self return policy");
        if (call.borrow_self && (f.owner.empty() || f.is_static || f.is_constructor ||
            (object.mode != ObjectMode::reference && object.mode != ObjectMode::pointer)))
            return std::string("borrow_self requires an instance method returning a selected class pointer/reference");
        if (object.mode == ObjectMode::none && category(f.result) == Category::unsupported)
            return std::string("unsupported type in signature");
        for (std::size_t i = 0; i < f.parameters.size(); ++i) {
            const auto& parameter = f.parameters[i];
            if (callback_signature(parameter)) {
                if (!callback_supported(parameter)) return std::string("unsupported callback signature (only copied values and void results are supported)");
                if (!enum_readable(enum_readable, callback_signature(parameter)->arguments.front()))
                    return std::string("callback result enum requires a definition for validation");
                if (f.is_noexcept) return std::string("callbacks in noexcept native calls require an error-status adapter");
                const auto function_policy = policy.callbacks.find(f.qualified_name());
                if (function_policy == policy.callbacks.end() || !function_policy->second.count(i + 1))
                    return std::string("callback parameter ") + std::to_string(i + 1) + " requires an explicit call or retained lifetime policy";
            } else if (!parameter_valid(parameter)) return std::string("unsupported type in signature");
        }
        return std::string{};
    };
    std::map<std::string, std::size_t> counts;
    for (const auto& f : module.functions) ++counts[f.qualified_name()];
    for (const auto& f : module.functions) {
        const auto exported_name = export_name(f.namespaces, f.name);
        Call call{f, exported_name, {}, false, false};
        std::string reason;
        if (counts[f.qualified_name()] > 1) reason = "overloads are unsupported";
        else reason = call_valid(call);
        if (reason.empty() && exported.count(exported_name)) reason = "Lua export name collision: " + exported_name;
        if (!reason.empty()) result.diagnostics.push_back(f.qualified_name() + ": " + reason);
        else { exported.insert(exported_name); calls.push_back(std::move(call)); }
    }
    const auto free_calls = calls.size();
    for (auto& plan : class_plans) {
        const auto& record = *plan.record;
        const auto& config = policy.classes.at(record.qualified_name());
        std::set<std::string> selected_names{"close", "__gc", "__close", "__index", "__metatable", "__name"};
        std::vector<const Function*> constructors;
        std::size_t constructor_names = 0;
        for (const auto& constructor : record.constructors) {
            const bool copy_or_move = constructor.parameters.size() == 1 &&
                (constructor.parameters[0].kind == TypeKind::lvalue_reference || constructor.parameters[0].kind == TypeKind::rvalue_reference) &&
                value_type(constructor.parameters[0]).declaration == record.qualified_name();
            if (!copy_or_move) ++constructor_names;
            if (!copy_or_move && constructor.access == "public" && !constructor.is_deleted) constructors.push_back(&constructor);
        }
        const auto append = [&](Call call) {
            auto reason = call_valid(call);
            if (reason.empty() && !selected_names.insert(call.exported).second) reason = "reserved or colliding Lua member name " + call.exported;
            if (!reason.empty()) result.diagnostics.push_back(call.function.qualified_name() + ": " + reason);
            else { plan.calls.push_back(calls.size()); calls.push_back(std::move(call)); }
        };
        if (config.constructors) {
            if (constructors.size() > 1 || (!constructors.empty() && constructors.front()->default_arguments && constructor_names > 1))
                result.diagnostics.push_back(record.qualified_name() + ": constructor overloads require explicit adapters");
            else if (!constructors.empty()) append({*constructors.front(), "new", {}, false, false});
        }
        std::map<std::string, std::size_t> method_counts;
        for (const auto& method : record.methods)
            if (method.access == "public" && !method.is_deleted) ++method_counts[method.name];
        for (const auto& requested : config.methods)
            if (std::none_of(record.methods.begin(), record.methods.end(), [&](const Function& m) { return m.name == requested; }))
                result.diagnostics.push_back(record.qualified_name() + ": unknown method in policy: " + requested);
        for (const auto& requested : config.borrow_self)
            if (std::none_of(record.methods.begin(), record.methods.end(), [&](const Function& m) { return m.name == requested &&
                m.access == "public" && !m.is_deleted && (!config.restrict_methods || config.methods.count(requested)); }))
                result.diagnostics.push_back(record.qualified_name() + ": borrow_self policy refers to an unselected method: " + requested);
        for (const auto& method : record.methods) {
            if (config.restrict_methods && !config.methods.count(method.name)) continue;
            if (!config.restrict_methods && (method.access != "public" || method.is_deleted || method.name.rfind("operator", 0) == 0)) continue;
            if (method.name.rfind("operator", 0) == 0) { result.diagnostics.push_back(method.qualified_name() + ": operators need a named/protocol adapter"); continue; }
            if (method_counts[method.name] > 1 || (method.default_arguments &&
                std::count_if(record.methods.begin(), record.methods.end(), [&](const Function& other) { return other.name == method.name; }) > 1)) {
                result.diagnostics.push_back(method.qualified_name() + ": overloads are unsupported"); continue;
            }
            append({method, method.name, {}, false, config.borrow_self.count(method.name) != 0});
        }
        for (const auto& requested : config.fields)
            if (std::none_of(record.fields.begin(), record.fields.end(), [&](const Field& f) { return f.name == requested; }))
                result.diagnostics.push_back(record.qualified_name() + ": unknown field in policy: " + requested);
        for (const auto& field : record.fields) {
            if (config.restrict_fields && !config.fields.count(field.name)) continue;
            if (!config.restrict_fields && field.access != "public") continue;
            if (field.access != "public" || field.bitfield || category(field.type) == Category::unsupported) {
                result.diagnostics.push_back(record.qualified_name() + "::" + field.name + ": unsupported or nonpublic field; use an explicit accessor");
                continue;
            }
            Function get;
            get.name = "get_" + field.name;
            get.namespaces = record.namespaces;
            get.namespaces.push_back(record.name);
            get.owner = record.qualified_name();
            get.is_const = true;
            get.result = field.type;
            append({get, get.name, field.name, false, false});
            if (!field.type.is_const && !borrowed_value(field.type)) {
                Function set = get;
                set.name = "set_" + field.name;
                set.is_const = false;
                set.result = {"void", "void", TypeKind::builtin};
                set.parameters.push_back(field.type);
                append({set, set.name, field.name, true, false});
            }
        }
    }
    for (const auto& function : policy.callbacks) {
        const auto found = std::find_if(calls.begin(), calls.end(), [&](const Call& call) { return call.field.empty() && call.function.qualified_name() == function.first; });
        if (found == calls.end()) throw BindingError("callback policy refers to an unselected or unsupported callable: " + function.first + diagnostic_text(result.diagnostics));
        for (const auto& parameter : function.second)
            if (parameter.first == 0 || parameter.first > found->function.parameters.size() ||
                !callback_signature(found->function.parameters[parameter.first - 1]))
                throw BindingError("callback policy parameter is not a std::function argument: " + function.first + " parameter " + std::to_string(parameter.first));
    }
    for (const auto& enumeration : module.enumerations) {
        if (!enumeration.selected) continue;
        std::string export_name;
        for (const auto& part : enumeration.namespaces) export_name += part + "__";
        export_name += enumeration.name;
        if (!exported.insert(export_name).second)
            result.diagnostics.push_back(enumeration.qualified_name() + ": Lua export name collision: " + export_name);
        else enumerations.emplace_back(&enumeration, export_name);
    }
    if (!result.diagnostics.empty() && !skip_unsupported)
        throw BindingError("cannot bind all declarations:" + diagnostic_text(result.diagnostics) +
                           "\nUse --skip-unsupported to explicitly allow a partial module.");
    if (calls.empty() && enumerations.empty() && class_plans.empty())
        throw BindingError("no supported functions found" + diagnostic_text(result.diagnostics));
    if (module.header.find_first_of("\r\n\"") != std::string::npos)
        throw BindingError("input header path cannot contain quotes or newlines");
    const bool has_callbacks = std::any_of(calls.begin(), calls.end(), [](const Call& call) {
        return std::any_of(call.function.parameters.begin(), call.function.parameters.end(), [](const Type& type) { return callback_signature(type); });
    });
    std::ostringstream source;
    source << detail::lua_runtime;
    if (has_callbacks) source << detail::lua_callbacks;
    source << "#include \"" << module.header << "\"\n\n";
    for (const auto& rule : policy.enum_flags) {
        const auto index = enum_indices.at(rule.first);
        const auto underlying = "std::underlying_type_t<::" + rule.first + ">";
        const auto bits = "rosetta_flag_bits_" + std::to_string(index);
        const auto mask = "rosetta_flag_mask_" + std::to_string(index);
        source << "using " << bits << " = std::make_unsigned_t<std::conditional_t<std::is_same_v<"
               << underlying << ", bool>, unsigned char, " << underlying << ">>;\n";
        std::vector<std::string> terms;
        for (const auto& name : rule.second) {
            const auto term = mask + "_" + std::to_string(terms.size());
            const auto native = "::" + rule.first + "::" + name;
            source << "static constexpr auto " << term << " = static_cast<" << bits << ">(" << native << ");\n"
                   << "static_assert(static_cast<" << underlying << ">(" << native << ") > 0 && (" << term << " & ("
                   << term << " - 1)) == 0, \"flag basis must be a positive single-bit enumerator\");\n"
                   << "static_assert(" << term << " <= static_cast<" << bits
                   << ">(std::numeric_limits<lua_Integer>::max()), \"flag basis exceeds Lua integer range\");\n";
            terms.push_back(term);
        }
        source << "[[maybe_unused]] static constexpr " << bits << " " << mask << " = static_cast<" << bits << ">(0)";
        for (const auto& term : terms) source << " | " << term;
        source << ";\n\n";
    }
    std::set<std::string> enum_arguments;
    const auto collect_enums = [&](const auto& self, const Type& type) -> void {
        if (type.kind == TypeKind::enumeration) {
            if (!enum_indices.count(type.declaration)) throw BindingError("enum declaration metadata missing: " + type.declaration);
            enum_arguments.insert(type.declaration);
        }
        for (const auto& child : type.arguments) self(self, child);
    };
    for (const auto& call : calls) {
        collect_enums(collect_enums, call.function.result);
        for (const auto& p : call.function.parameters) collect_enums(collect_enums, p);
    }
    for (const auto& declaration : enum_arguments) {
        const auto index = enum_indices.at(declaration);
        const auto& enumeration = module.enumerations.at(index);
        if (enumeration.enumerators.empty()) {
            source << "namespace { template<> struct rosetta_codec<::" << declaration << "> {\n"
                   << "    static ::" << declaration << " read(lua_State*, int argument) { throw rosetta_conversion_error(argument, \"enum definition required\"); }\n"
                   << "    static void push(lua_State* L, ::" << declaration << " value) { rosetta_push_integer(L, rosetta_enum_number(value)); }\n}; }\n\n";
            continue;
        }
        source << "static ::" << declaration << " rosetta_enum_" << index << "(lua_State* L, int argument) {\n"
               << "    auto value = rosetta_integer<std::underlying_type_t<::"
               << declaration << ">>(L, argument);\n    if (";
        if (policy.enum_flags.count(declaration)) {
            const auto bits = "rosetta_flag_bits_" + std::to_string(index);
            source << "(static_cast<" << bits << ">(value) & static_cast<" << bits << ">(~rosetta_flag_mask_" << index
                   << ")) != 0) throw rosetta_conversion_error(argument, \"unknown enum flag bits\");\n";
        } else {
            for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
                if (i) source << " && ";
                source << "value != static_cast<std::underlying_type_t<::" << declaration << ">>(::"
                       << declaration << "::" << enumeration.enumerators[i] << ")";
            }
            source << ") throw rosetta_conversion_error(argument, \"unknown enum value\");\n";
        }
        source << "    return static_cast<::"
               << declaration << ">(value);\n}\n\n";
        source << "namespace { template<> struct rosetta_codec<::" << declaration << "> {\n"
               << "    static ::" << declaration << " read(lua_State* L, int argument) { return ::rosetta_enum_" << index << "(L, argument); }\n"
               << "    static void push(lua_State* L, ::" << declaration << " value) { rosetta_push_integer(L, rosetta_enum_number(value)); }\n}; }\n\n";
    }
    for (const auto& plan : class_plans)
        source << "static_assert(std::is_nothrow_destructible_v<::" << plan.record->qualified_name()
               << ">, \"bound classes require a public nonthrowing destructor\");\n";
    for (std::size_t index = 0; index < calls.size(); ++index) {
        const auto& call = calls[index];
        const auto& f = call.function;
        const bool instance = !f.owner.empty() && !f.is_static && !f.is_constructor;
        const auto arity = f.parameters.size() + (instance ? 1 : 0);
        const auto minimum = arity - f.default_arguments;
        source << "static int rosetta_wrap_" << index << "(lua_State* L) {\n";
        if (minimum == arity)
            source << "    if (lua_gettop(L) != " << arity << ") return luaL_error(L, \"expected " << arity << " arguments\");\n";
        else
            source << "    if (lua_gettop(L) < " << minimum << " || lua_gettop(L) > " << arity
                   << ") return luaL_error(L, \"expected " << minimum << " to " << arity << " arguments\");\n";
        source
               << "    luaL_checkstack(L, 4, \"cannot reserve conversion stack\");\n"
               << "    char error[512] = {};\n    try {\n";
        if (has_callbacks) source << "        rosetta_drain_callbacks(L);\n";
        if (std::any_of(f.parameters.begin(), f.parameters.end(), borrowed_value))
            source << "        rosetta_view_scope view_scope(L);\n";
        if (instance) source << "        auto* self = rosetta_get<::" << f.owner << ">(L, 1, " << (f.is_const ? "false" : "true") << ");\n"
                             << "        rosetta_object_pin self_pin(rosetta_test_object<::" << f.owner << ">(L, 1));\n";
        if (std::any_of(f.parameters.begin(), f.parameters.end(), [](const Type& type) { return callback_signature(type); }))
            source << "        rosetta_callback_scope callback_scope;\n";
        for (std::size_t supplied = f.parameters.size() - f.default_arguments; supplied <= f.parameters.size(); ++supplied) {
            if (f.default_arguments) source << "        if (lua_gettop(L) == " << supplied + (instance ? 1 : 0) << ") {\n";
            std::vector<std::string> arguments;
            for (std::size_t p = 0; p < supplied; ++p) {
                const auto& type = f.parameters[p];
                const auto i = p + 1 + (instance ? 1 : 0);
                const auto variable = "a" + std::to_string(i);
                const auto object = object_type(type, classes);
                auto expression = variable;
                if (object.mode != ObjectMode::none)
                    source << "        rosetta_object_pin pin" << i << "(lua_isnil(L, " << i << ") ? nullptr : rosetta_get_object<::"
                           << object.native->declaration << ">(L, " << i << "));\n";
                if (const auto* signature = callback_signature(type)) {
                    const bool retained = policy.callbacks.at(f.qualified_name()).at(p + 1) == CallbackLifetime::retained;
                    source << "        auto " << variable << " = rosetta_callback<" << signature->canonical << ">::read(L, " << i << ", "
                           << (retained ? "true" : "false") << ", callback_scope);\n";
                    if (type.kind != TypeKind::lvalue_reference) expression = "std::move(" + variable + ")";
                } else if (object.mode == ObjectMode::value) {
                    source << "        auto " << variable << " = rosetta_copy_object<::" << object.native->declaration << ">(L, " << i << ");\n";
                    expression = "std::move_if_noexcept(" + variable + ")";
                } else if (object.mode == ObjectMode::reference || object.mode == ObjectMode::pointer) {
                    source << "        auto* " << variable << " = " << (object.mode == ObjectMode::pointer ? "rosetta_get_pointer<::" : "rosetta_get<::")
                           << object.native->declaration << ">(L, " << i << ", " << (object.native->is_const ? "false" : "true") << ");\n";
                    if (object.mode == ObjectMode::reference) expression = "*" + variable;
                } else if (object.mode == ObjectMode::shared) {
                    source << "        auto " << variable << " = rosetta_get_shared<::" << object.native->declaration << ">(L, " << i << ");\n";
                    if (type.kind != TypeKind::lvalue_reference) expression = "std::move(" + variable + ")";
                } else if (category(type) == Category::enumeration) {
                    source << "        auto a" << i << " = rosetta_enum_" << enum_indices.at(value_type(type).declaration)
                           << "(L, " << i << ");\n";
                } else {
                    source << "        auto " << variable << " = rosetta_read<" << type.canonical << ">(L, " << i << ");\n";
                    if (type.kind != TypeKind::lvalue_reference) expression = "std::move(" + variable + ")";
                }
                arguments.push_back(std::move(expression));
            }
            const auto argument_list = [&] {
                std::string joined;
                for (const auto& argument : arguments) { if (!joined.empty()) joined += ", "; joined += argument; }
                return joined;
            }();
            if (f.is_constructor) {
                source << "        auto* object = rosetta_new_object<::" << f.owner << ">(L);\n"
                       << "        auto* pointer = new ::" << f.owner << "(" << argument_list << ");\n"
                       << "        object->pointer = pointer;\n        object->state = pointer;\n"
                       << "        object->destroy = &rosetta_delete_native<::" << f.owner << ">;\n        return 1;\n";
            } else {
                const auto returned_object = object_type(f.result, classes);
                const auto cat = category(f.result);
                // Defaults can create temporary backing storage for views/pointers.
                // Publish within the same full expression, while that storage lives.
                const bool inline_publication = supplied < f.parameters.size() &&
                    (cat == Category::cstring || borrowed_value(f.result));
                if (call.field.empty() && supplied == f.parameters.size()) {
                    source << "        auto function = static_cast<" << f.result.canonical;
                    if (instance) source << " (::" << f.owner << "::*)(";
                    else source << " (*)(";
                    for (std::size_t p = 0; p < f.parameters.size(); ++p) {
                        if (p) source << ", ";
                        source << f.parameters[p].canonical;
                    }
                    source << ")" << (instance && f.is_const ? " const" : "") << ">(&::" << f.qualified_name() << ");\n";
                }
                source << "        ";
                if (inline_publication) {
                    source << "return [&](auto&& result) { ";
                    if (has_callbacks) source << "rosetta_drain_callbacks(L); ";
                    source << "return rosetta_publish(L, result); }(";
                }
                else if (returned_object.mode == ObjectMode::reference) source << "decltype(auto) result = ";
                else if (cat != Category::nothing || returned_object.mode != ObjectMode::none) source << "auto result = ";
                if (!call.field.empty()) {
                    source << "self->" << call.field;
                    if (call.setter) source << " = " << arguments.at(0);
                } else if (supplied < f.parameters.size()) {
                    source << (instance ? "self->" + f.name : "::" + f.qualified_name()) << "(" << argument_list << ")";
                } else source << (instance ? "(self->*function)(" : "function(") << argument_list << ")";
                source << (inline_publication ? ");\n" : ";\n");
                if (!inline_publication) {
                    if (has_callbacks) source << "        rosetta_drain_callbacks(L);\n";
                    if (returned_object.mode == ObjectMode::value) source << "        return rosetta_publish_owned(L, std::move(result));\n";
                    else if (returned_object.mode == ObjectMode::unique) source << "        return rosetta_publish_unique(L, result);\n";
                    else if (returned_object.mode == ObjectMode::shared) source << "        return rosetta_publish_shared(L, std::move(result));\n";
                    else if (returned_object.mode == ObjectMode::reference || returned_object.mode == ObjectMode::pointer) {
                        source << "        return rosetta_publish_borrow<::" << returned_object.native->declaration << ">(L, "
                               << (returned_object.mode == ObjectMode::reference ? "&result" : "result") << ", 1, "
                               << (returned_object.native->is_const ? "true" : "false") << ");\n";
                    } else {
                        switch (cat) {
                        case Category::nothing: source << "        return 0;\n"; break;
                        case Category::integer: case Category::number: case Category::boolean: case Category::cstring: case Category::value:
                            source << "        return rosetta_publish(L, result);\n";
                            break;
                        case Category::enumeration:
                            source << "        return rosetta_publish(L, rosetta_enum_number(result));\n";
                            break;
                        default: throw BindingError("unexpected result type after validation");
                        }
                    }
                }
            }
            if (f.default_arguments) source << "        }\n";
        }
        if (f.default_arguments) source << "        throw rosetta_conversion_error(\"invalid native call arity\");\n";
        source << R"cpp(    } catch (const std::exception& e) {
        std::snprintf(error, sizeof(error), "%s", e.what());
    } catch (...) {
        std::snprintf(error, sizeof(error), "unknown C++ exception");
    }
    return luaL_error(L, "%s", error);
}

)cpp";
    }
    source << "extern \"C\" int luaopen_" << name << "(lua_State* L) {\n";
    for (const auto& plan : class_plans) {
        const auto owner = plan.record->qualified_name();
        source << "    lua_rawgetp(L, LUA_REGISTRYINDEX, &rosetta_type_key<::" << owner << ">);\n"
               << "    if (lua_isnil(L, -1)) {\n        lua_pop(L, 1);\n        lua_newtable(L);\n"
               << "        lua_pushstring(L, " << quote(owner) << "); lua_setfield(L, -2, \"__name\");\n"
               << "        lua_pushliteral(L, \"protected Rosetta class\"); lua_setfield(L, -2, \"__metatable\");\n"
               << "        lua_pushcfunction(L, &rosetta_finalize_object<::" << owner << ">); lua_setfield(L, -2, \"__gc\");\n"
               << "        lua_pushcfunction(L, &rosetta_close_object<::" << owner << ">); lua_setfield(L, -2, \"__close\");\n"
               << "        lua_pushcfunction(L, &rosetta_close_object<::" << owner << ">); lua_setfield(L, -2, \"close\");\n";
        for (const auto index : plan.calls)
            if (!calls[index].function.is_static && !calls[index].function.is_constructor)
                source << "        lua_pushcfunction(L, rosetta_wrap_" << index << "); lua_setfield(L, -2, " << quote(calls[index].exported) << ");\n";
        source << "        lua_pushvalue(L, -1); lua_setfield(L, -2, \"__index\");\n"
               << "        lua_pushvalue(L, -1); lua_rawsetp(L, LUA_REGISTRYINDEX, &rosetta_type_key<::" << owner << ">);\n    }\n    lua_pop(L, 1);\n";
    }
    source << "    static const luaL_Reg functions[] = {\n";
    for (std::size_t i = 0; i < free_calls; ++i)
        source << "        {" << quote(calls[i].exported) << ", rosetta_wrap_" << i << "},\n";
    source << "        {nullptr, nullptr}\n    };\n    luaL_newlib(L, functions);\n";
    for (const auto& plan : class_plans) {
        source << "    lua_newtable(L);\n";
        for (const auto index : plan.calls)
            if (calls[index].function.is_static || calls[index].function.is_constructor)
                source << "    lua_pushcfunction(L, rosetta_wrap_" << index << "); lua_setfield(L, -2, " << quote(calls[index].exported) << ");\n";
        source << "    lua_setfield(L, -2, " << quote(plan.exported) << ");\n";
    }
    for (const auto& entry : enumerations) {
        const auto& enumeration = *entry.first;
        source << "    lua_newtable(L);\n";
        for (const auto& enumerator : enumeration.enumerators) {
            source << "    rosetta_push_integer(L, static_cast<std::underlying_type_t<::" << enumeration.qualified_name()
                   << ">>(::" << enumeration.qualified_name() << "::" << enumerator << "));\n"
                   << "    lua_setfield(L, -2, " << quote(enumerator) << ");\n";
        }
        source << "    lua_setfield(L, -2, " << quote(entry.second) << ");\n";
    }
    source << "    return 1;\n}\n";
    result.source = source.str();
    return result;
}

} // namespace rosetta
