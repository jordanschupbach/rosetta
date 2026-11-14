// Shared half of the full C ABI backends: see cabi_full.hpp.
#include "cabi_full.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "lisp_full_runtime.hpp"
#include "type_spelling.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace rosetta::cabi {

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
std::string exported(std::string text) {
    for (std::size_t i = 0; (i = text.find("::", i)) != std::string::npos; i += 2) text.replace(i, 2, "__");
    return text;
}
bool identifier(const std::string& text) {
    auto start = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    return !text.empty() && start(text.front()) && std::all_of(text.begin(), text.end(), [&](char c) {
        return start(c) || (c >= '0' && c <= '9');
    });
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
bool is_operator(const std::string& name) {
    return name.rfind("operator", 0) == 0 && (name.size() == 8 || !identifier(name.substr(8)) || name == "operator bool");
}
std::string one_line(std::string text) {
    for (auto& c : text) if (c == '\n') c = ' ';
    return text;
}

std::string shortest_double(std::string text) {
    while (!text.empty() && (text.back() == 'f' || text.back() == 'F' || text.back() == 'l' || text.back() == 'L')) text.pop_back();
    char* end = nullptr;
    const double number = std::strtod(text.c_str(), &end);
    if (text.empty() || *end != '\0' || !std::isfinite(number)) return text;
    char buffer[64];
    for (int precision = 1; precision <= 17; ++precision) {
        std::snprintf(buffer, sizeof buffer, "%.*g", precision, number);
        if (std::strtod(buffer, nullptr) == number) break;
    }
    text = buffer;
    if (text.find_first_of(".e") == std::string::npos) text += ".0";
    return text;
}

CabiGenerator::CabiGenerator(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, std::size_t parts)
    : module_(module), name_(name), skip_(skip), policy_(resolve_reexports(policy, module)), flat_(policy.names == "flat"), parts_(parts) {
    validate_modules(policy_, module_);
}

bool CabiGenerator::excluded(const std::string& qualified, const std::string& identity) const {
    for (const auto& pattern : policy_.exclude)
        if (policy_match(pattern, qualified) || (!identity.empty() && policy_match(pattern, identity))) return true;
    return false;
}
bool CabiGenerator::readonly(const std::string& qualified, const std::string& owner) const {
    for (const auto& pattern : policy_.readonly)
        if (policy_match(pattern, qualified) || (!owner.empty() && policy_match(pattern, owner))) return true;
    return false;
}
std::string CabiGenerator::renamed(const std::string& qualified, const std::string& fallback) const {
    const auto found = policy_.rename.find(qualified);
    if (found == policy_.rename.end()) return fallback;
    if (found->second.empty() || !identifier(found->second)) throw BindingError("invalid " + language() + " rename target for " + qualified);
    return rename_target(found->second, fallback);
}
std::string CabiGenerator::symbol(std::size_t* index) {
    symbols_.push_back(symbol_prefix() + std::to_string(symbols_.size() + 1));
    if (index) *index = symbols_.size();
    return symbols_.back();
}
std::size_t CabiGenerator::generic(const std::string& name) {
    const auto found = generic_index_.find(name);
    if (found != generic_index_.end()) return found->second;
    generic_index_[name] = generic_order_.size();
    generic_order_.push_back(name);
    return generic_order_.size() - 1;
}
std::string CabiGenerator::conversion(const Type& type) const {
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
bool CabiGenerator::copy_type(const Type& type) const {
    if (type.kind == TypeKind::pointer || type.kind == TypeKind::rvalue_reference) return false;
    const auto& target = referent(type);
    return target.kind == TypeKind::record && policy_.copy_types.count(target.declaration) != 0;
}
bool CabiGenerator::flat(const std::vector<std::string>& namespaces) const {
    if (!flat_) return false;
    std::string scope;
    for (const auto& part : namespaces) scope += (scope.empty() ? "" : "::") + part;
    for (const auto& pattern : policy_.qualify) if (policy_match(pattern, scope)) return false;
    return true;
}
bool CabiGenerator::derives(const std::string& from, const std::string& to) const {
    if (from == to) return true;
    for (const auto& base : classes_.at(from).bases) if (derives(base, to)) return true;
    return false;
}
bool CabiGenerator::primary_derives(const std::string& from, const std::string& to) const {
    if (from == to) return true;
    const auto& bases = classes_.at(from).bases;
    return !bases.empty() && primary_derives(bases.front(), to);
}
std::string CabiGenerator::director_name(const ClassPlan& plan) {
    std::string name = "rosetta_director_";
    for (const char c : plan.record->qualified_name()) name += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    return name;
}

bool CabiGenerator::copyable_type(const Type& type, unsigned depth) {
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
bool CabiGenerator::copyable_record(const std::string& name, unsigned depth, bool as_base) {
    const auto found = records_.find(name);
    if (as_base && found != records_.end() && found->second->abstract) {
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

std::string CabiGenerator::value_problem(const Type& type, Use) const {
    if (!conversion(type).empty()) return {};
    const auto bare = normalize(strip_cv(type.canonical));
    switch (type.kind) {
    case TypeKind::builtin: {
        static const std::set<std::string> supported = {"bool", "char", "signed char", "unsigned char", "short", "unsigned short",
            "int", "unsigned int", "long", "unsigned long", "long long", "unsigned long long", "float", "double"};
        if (supported.count(bare)) return {};
        return "unsupported " + language() + " value type " + type.canonical;
    }
    case TypeKind::enumeration: return {};
    case TypeKind::record:
        if (bound(type)) return {};
        if (bare == "std::filesystem::path" || bare == "std::nullptr_t" || bare == "std::monostate") return {};
        return "class " + strip_cv(type.canonical) + " is not exported to " + language();
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
        return "unsupported " + language() + " template type " + type.canonical;
    }
    default:
        return "unsupported " + language() + " value type " + type.canonical;
    }
}

std::string CabiGenerator::type_problem(const Type& type, Use use) const {
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
        return "unsupported " + language() + " pointer type " + type.canonical;
    }
    if (type.kind == TypeKind::function) return "function types are unsupported";
    return value_problem(type, use);
}

std::string CabiGenerator::function_problem(const Function& function, bool self_parameter) const {
    if (!function.unsupported_reason.empty() && function.unsupported_reason.rfind("default calls with ", 0) != 0)
        return function.unsupported_reason;
    if (function.is_deleted) return "deleted function";
    if (function.default_arguments > function.parameters.size()) return "invalid default argument metadata";
    if (!function.is_constructor) {
        auto why = type_problem(function.result, Use::result);
        if (!why.empty()) return "unsupported " + language() + " result type: " + why;
    }
    for (std::size_t i = self_parameter ? 1 : 0; i < function.parameters.size(); ++i) {
        auto why = type_problem(function.parameters[i], Use::parameter);
        if (!why.empty()) return "unsupported " + language() + " parameter type: " + why;
    }
    return {};
}

std::string CabiGenerator::return_policy(const Function& function) const {
    auto found = policy_.returns.find(function.identity());
    if (found == policy_.returns.end()) found = policy_.returns.find(function.qualified_name());
    if (found == policy_.returns.end())
        return copy_type(function.result) ? "rosetta_lisp::Return::copy" : "rosetta_lisp::Return::automatic";
    return "rosetta_lisp::Return::" + found->second;
}

std::string CabiGenerator::class_name(const Record& record) const {
    const auto qualified = record.qualified_name();
    const auto instantiated = template_names_.find(qualified);
    if (instantiated != template_names_.end()) return renamed(qualified, type_name(instantiated->second));
    if (!record.owner.empty()) return renamed(qualified, type_name(record.name));
    return renamed(qualified, type_name(flat(record.namespaces) ? record.name : exported(qualified)));
}

void CabiGenerator::plan_classes() {
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
            throw BindingError("unknown class in " + language() + " policy: " + selection.first);
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
        // A policy conversion carries values of the class as its target type instead.
        if (std::any_of(policy_.conversions.begin(), policy_.conversions.end(),
                        [&](const auto& item) { return normalize(item.first) == normalize(owner); }))
            continue;
        if (!record.complete) { if (listed) reject(owner, language() + " requires a complete class definition"); continue; }
        if (!record.unsupported_reason.empty()) { reject(owner, record.unsupported_reason); continue; }
        if (!record.owner.empty() && !records_.count(record.owner)) continue;
        const auto config = policy_.classes.find(owner);
        if (config != policy_.classes.end() && !config->second.borrow_self.empty()) {
            reject(owner, "borrowed " + language() + " results are not yet supported"); continue;
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
    for (auto& [owner, plan] : classes_) plan.parent = plan.record->owner;
    for (auto& [owner, plan] : classes_) {
        std::string name = class_name(*plan.record);
        for (auto parent = plan.parent; !parent.empty(); parent = classes_.at(parent).parent)
            name = class_name(*classes_.at(parent).record) + "_" + name;
        plan.name = name;
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

void CabiGenerator::drop_classes(const std::set<std::string>& rejected) {
    for (const auto& owner : rejected) { classes_.erase(owner); class_order_.erase(std::remove(class_order_.begin(), class_order_.end(), owner), class_order_.end()); }
    for (bool changed = true; changed;) {
        changed = false;
        for (auto it = class_order_.begin(); it != class_order_.end();) {
            const auto& plan = classes_.at(*it);
            bool orphan = !plan.parent.empty() && !classes_.count(plan.parent);
            for (const auto& base : plan.bases) if (!classes_.count(base)) orphan = true;
            if (orphan) { classes_.erase(*it); it = class_order_.erase(it); changed = true; }
            else ++it;
        }
    }
}

std::string CabiGenerator::virtual_key(const Function& function) {
    std::string text = function.name + "(";
    for (const auto& parameter : function.parameters) text += parameter.canonical + ",";
    return text + (function.is_const ? ")const" : ")");
}

std::vector<const Function*> CabiGenerator::virtuals(const Record& record, std::map<std::string, bool>& pure) const {
    std::vector<const Function*> result;
    std::set<std::string> seen;
    const std::function<bool(const Record&, unsigned)> visit = [&](const Record& current, unsigned depth) {
        if (depth > 64) return false;
        for (const auto& method : current.methods) {
            if (!method.is_virtual || method.is_static || method.name.rfind("~", 0) == 0) continue;
            if (seen.insert(virtual_key(method)).second) { result.push_back(&method); pure[virtual_key(method)] = method.is_pure; }
        }
        for (const auto& base : current.bases) {
            const auto found = records_.find(base.type.declaration.empty() ? base.type.canonical : base.type.declaration);
            if (found == records_.end()) return false;
            if (!visit(*found->second, depth + 1)) return false;
        }
        return true;
    };
    if (!visit(record, 0)) { pure.clear(); return {}; }
    return result;
}

bool CabiGenerator::callback_supported(const Function& method) const {
    if (!method.unsupported_reason.empty() || !type_problem(method.result, Use::callback_result).empty()) return false;
    for (const auto& parameter : method.parameters)
        if (!type_problem(parameter, Use::callback_argument).empty()) return false;
    return !is_operator(method.name);
}

void CabiGenerator::plan_directors() {
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

std::set<std::string> CabiGenerator::bound_method_names(const std::string& owner) const {
    std::set<std::string> names;
    const std::function<void(const std::string&, unsigned)> walk = [&](const std::string& cls, unsigned depth) {
        if (depth > 64) return;
        for (const auto& group : classes_.at(cls).methods) if (!group.is_static) names.insert(group.name);
        for (const auto& base : classes_.at(cls).bases) walk(base, depth + 1);
    };
    walk(owner, 0);
    return names;
}

std::vector<CabiGenerator::Overridable> CabiGenerator::overridable(const ClassPlan& plan) const {
    std::vector<Overridable> result;
    std::map<std::string, bool> pure;
    const auto bound_names = bound_method_names(plan.record->qualified_name());
    for (const auto* method : virtuals(*plan.record, pure)) {
        if (!callback_supported(*method)) continue;
        const bool is_pure = pure[virtual_key(*method)];
        if (method->access == "private" && !is_pure) continue;
        const auto name = renamed(method->qualified_name(), method_name(method->name));
        if (!is_pure && !bound_names.count(name)) continue;
        result.push_back({name, method, is_pure});
    }
    return result;
}

void CabiGenerator::prepare() {
    if (!identifier(name_)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
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
}

void CabiGenerator::plan_members() {
    // Free functions attached to classes as methods (policy extend), and free operators.
    std::map<std::string, std::vector<Group>> extensions;
    for (const auto& [owner, methods] : policy_.extend) {
        if (!classes_.count(owner)) {
            if (records_.count(owner)) continue; // excluded, or dropped by a collision
            throw BindingError("policy extend names a class that is not exported: " + owner);
        }
        for (const auto& [name, target] : methods) {
            Group group;
            group.name = method_name(name);
            bool any = false, any_static = false;
            for (const auto& function : module_.functions) {
                if (function.qualified_name() != target && normalize(function.qualified_name()) != normalize(target)) continue;
                any = true;
                extension_functions_.insert(function.qualified_name());
                const bool self = !function.parameters.empty() && [&, cls = owner] {
                    const auto& first = function.parameters[0];
                    const auto& base = first.kind == TypeKind::pointer && !first.arguments.empty() ? first.arguments[0] : referent(first);
                    return base.kind == TypeKind::record && classes_.count(base.declaration) && derives(cls, base.declaration);
                }();
                auto why = function_problem(function, self);
                if (!why.empty()) { reject(owner + "." + name, why); continue; }
                if (!self) any_static = true;
                group.overloads.push_back({&function, owner, true, self});
            }
            if (!any) throw BindingError("policy extend function not found: " + target);
            group.is_static = any_static;
            if (!group.overloads.empty()) extensions[owner].push_back(std::move(group));
        }
    }
    // Free operators become operators of the class of their first bound operand.
    struct FreeOperator { std::string function; Overload overload; std::size_t operand; };
    std::map<std::string, std::vector<FreeOperator>> free_operators; // class -> operators
    for (const auto& function : module_.functions) {
        if (!is_operator(function.name) || extension_functions_.count(function.qualified_name())) continue;
        if (excluded(function.qualified_name(), function.identity())) continue;
        const auto arity = function.parameters.size();
        if (arity == 0 || arity > 2) continue;
        const auto target = operator_name(function.name, arity - 1);
        if (target.empty() || !free_operator(target)) continue;
        auto why = function_problem(function, false);
        if (!why.empty()) { reject(function.qualified_name(), why); continue; }
        std::size_t operand = arity;
        for (std::size_t i = 0; i < arity && operand == arity; ++i) {
            const auto& parameter = function.parameters[i];
            const auto& bare = parameter.kind == TypeKind::pointer && !parameter.arguments.empty() ? parameter.arguments[0] : referent(parameter);
            if (bound(bare)) operand = i;
        }
        if (operand == arity) { reject(function.qualified_name(), "free operator without an operand of an exported class"); continue; }
        const auto& parameter = function.parameters[operand];
        const auto& bare = parameter.kind == TypeKind::pointer && !parameter.arguments.empty() ? parameter.arguments[0] : referent(parameter);
        free_operators[bare.declaration].push_back({target, {&function, bare.declaration, true, false}, operand});
    }

    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        const auto& record = *plan.record;
        const auto config = policy_.classes.find(owner);
        const bool constructors = config == policy_.classes.end() || config->second.constructors;
        if (config != policy_.classes.end()) {
            for (const auto& requested : config->second.methods) {
                const auto found = std::find_if(record.methods.begin(), record.methods.end(), [&](const Function& f) { return f.name == requested; });
                if (found == record.methods.end()) throw BindingError("unknown " + language() + " policy method: " + owner + "::" + requested);
                if (found->access != "public") throw BindingError(language() + " policy method is not public: " + owner + "::" + requested);
            }
            for (const auto& requested : config->second.fields) {
                const auto found = std::find_if(record.fields.begin(), record.fields.end(), [&](const Field& f) { return f.name == requested; });
                if (found == record.fields.end()) throw BindingError("unknown " + language() + " policy field: " + owner + "::" + requested);
                if (found->access != "public") throw BindingError(language() + " policy field is not public: " + owner + "::" + requested);
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
        std::map<std::pair<std::string, std::size_t>, std::size_t> operator_index;
        const auto add = [&](const std::string& name, const Overload& overload, bool is_static) {
            const auto found = group_index.find(name);
            if (found == group_index.end()) {
                group_index[name] = plan.methods.size();
                plan.methods.push_back({name, {overload}, is_static, {}, 0, {}, 0});
            } else {
                auto& group = plan.methods[found->second];
                if (group.is_static != is_static) {
                    reject(overload.function->qualified_name(), "static and instance overloads cannot share a " + language() + " name");
                    return;
                }
                group.overloads.push_back(overload);
            }
        };
        // Operator groups by (target operator, position of this class's operand).
        const auto add_operator = [&](const std::string& function_name, const Overload& overload, std::size_t operand) {
            const auto key = std::make_pair(function_name, operand);
            const auto found = operator_index.find(key);
            if (found == operator_index.end()) {
                operator_index[key] = plan.operators.size();
                plan.operators.push_back({function_name, {overload}, false, {}, 0, {}, operand});
            } else plan.operators[found->second].overloads.push_back(overload);
        };
        for (const auto& method : record.methods) {
            if (method.access != "public") continue;
            if (config != policy_.classes.end() && config->second.restrict_methods && !config->second.methods.count(method.name)) continue;
            if (excluded(method.qualified_name(), method.identity())) continue;
            if (is_operator(method.name)) {
                if (method.is_static) continue;
                const auto function_name = operator_name(method.name, method.parameters.size());
                if (function_name.empty()) continue; // assignment, conversion and other operators stay C++-only
                auto why = function_problem(method, false);
                if (!why.empty()) { reject(method.qualified_name(), why); continue; }
                add_operator(function_name, {&method, owner}, 0);
                continue;
            }
            const auto name = renamed(method.qualified_name(), method_name(method.name));
            auto why = function_problem(method, false);
            if (!why.empty()) { reject(method.qualified_name(), why); continue; }
            add(name, {&method, owner}, method.is_static);
        }
        for (auto& group : extensions[owner]) for (const auto& overload : group.overloads) add(group.name, overload, group.is_static);
        for (const auto& item : free_operators[owner]) add_operator(item.function, item.overload, item.operand);
        for (const auto& field : record.fields) {
            if (field.access != "public") continue;
            if (config != policy_.classes.end() && config->second.restrict_fields && !config->second.fields.count(field.name)) continue;
            const auto qualified = owner + "::" + field.name;
            if (excluded(qualified)) continue;
            auto why = type_problem(field.type, Use::result);
            const auto unsupported = "unsupported " + language() + " field type";
            if (field.type.kind == TypeKind::rvalue_reference) why = "rvalue reference fields are unsupported";
            if (field.bitfield || field.type.is_volatile) why = unsupported;
            if (field.type.canonical.find('[') != std::string::npos) why = "array fields are unsupported";
            if (!why.empty()) { reject(qualified, why.rfind(unsupported, 0) == 0 ? why : unsupported + ": " + why); continue; }
            const auto name = renamed(qualified, field_name(field.name));
            if (name.rfind("_rosetta_", 0) == 0) { reject(qualified, "field names starting with _rosetta_ are reserved"); continue; }
            plan.fields.push_back({&field, name, {}, {}});
        }
        for (const auto& variable : record.static_fields) {
            const auto qualified = owner + "::" + variable.name;
            if (variable.access != "public" || excluded(qualified)) continue;
            if (!variable.type.is_const && !variable.is_constexpr) { reject(qualified, "mutable static members are not exported to " + language()); continue; }
            auto why = type_problem(variable.type, Use::result);
            if (!why.empty() || variable.type.kind == TypeKind::pointer) { reject(qualified, why.empty() ? "pointer constants are unsupported" : why); continue; }
            plan.constants.push_back({static_constant_name(plan, renamed(qualified, variable.name)), qualified, &variable.type, &variable.documentation, {}, 0});
        }
    }
}

std::vector<Group> CabiGenerator::plan_functions() {
    std::vector<Group> functions;
    std::map<std::string, std::size_t> function_index;
    for (const auto& function : module_.functions) {
        if (extension_functions_.count(function.qualified_name()) || is_operator(function.name)) continue;
        if (excluded(function.qualified_name(), function.identity())) continue;
        auto why = function_problem(function, false);
        if (!why.empty()) { reject(function.qualified_name(), why); continue; }
        const auto specialization = function_template_names_.find(function.qualified_name());
        if (!function.template_name.empty() && specialization == function_template_names_.end()) continue;
        const auto submodule = policy_module(policy_, function.namespaces);
        const auto name = specialization != function_template_names_.end() ? method_name(specialization->second)
            : renamed(function.qualified_name(), method_name(!submodule.empty() || flat(function.namespaces) ? function.name
                                                                                    : exported(function.qualified_name())));
        const auto key = submodule + "." + name;
        const auto found = function_index.find(key);
        if (found == function_index.end()) {
            function_index[key] = functions.size();
            functions.push_back({name, {{&function, {}}}, false, {}, 0, submodule, 0});
        } else functions[found->second].overloads.push_back({&function, {}});
    }
    return functions;
}

std::vector<ConstantPlan> CabiGenerator::plan_constants(const std::function<std::string(const Variable&)>& name) {
    std::vector<ConstantPlan> constants;
    for (const auto& variable : module_.variables) {
        const auto qualified = variable.qualified_name();
        if (excluded(qualified)) continue;
        if (!variable.type.is_const && !variable.is_constexpr) { reject(qualified, "mutable global variables are not exported to " + language()); continue; }
        auto why = type_problem(variable.type, Use::result);
        if (!why.empty() || variable.type.kind == TypeKind::pointer || variable.type.kind == TypeKind::lvalue_reference) {
            reject(qualified, why.empty() ? "pointer and reference constants are unsupported" : why); continue;
        }
        constants.push_back({name(variable), "::" + qualified, &variable.type, &variable.documentation, {}, 0});
    }
    return constants;
}

void CabiGenerator::check_diagnostics() const {
    if (skip_ || diagnostics_.empty()) return;
    std::string error = "unsupported declaration for " + language();
    for (const auto& entry : diagnostics_) error += "\n  " + entry;
    throw BindingError(error);
}

// A C++ subclass whose virtual methods call the target's overrides.
void CabiGenerator::emit_director(std::ostream& out, const ClassPlan& plan) {
    const auto owner = plan.record->qualified_name();
    out << "struct " << director_name(plan) << " final : " << owner << ", rosetta_lisp::Director {\n"
        << "    template<class... A> explicit " << director_name(plan) << "(A&&... arguments) : " << owner
        << "(std::forward<A>(arguments)...) {}\n";
    for (const auto& item : overridable(plan)) {
        const auto* method = item.method;
        const auto index = generic(item.name);
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
        out << "        if (rosetta_has_self())\n"
            << "            return rosetta_lisp::invoke_override<" << result << types << ">(*this, " << index << arguments << ");\n";
        if (item.pure) out << "        rosetta_lisp::pure_virtual(" << quote(owner + "::" + method->name) << ");\n";
        else out << "        return " << owner << "::" << method->name << "(" << call_arguments << ");\n";
        if (method->is_noexcept)
            out << "        } catch (const std::exception& error) { std::fprintf(stderr, \"error in noexcept " << language()
                << " override: %s\\n\", error.what()); }\n"
                << "        catch (...) {}\n        std::terminate();\n";
        out << "    }\n";
    }
    out << "};\n";
}

// One overload of a group. kind: "method" (self is argument 0), "constructor" (argument 0 is
// the target object id of a subclass instance, or 0) or "function".
void CabiGenerator::emit_overload(std::ostream& out, const Overload& overload, const std::string& kind, bool checks) {
    const auto& function = *overload.function;
    const std::size_t skip = overload.receives_self ? 1 : 0;
    const std::size_t count = function.parameters.size() - skip;
    const std::size_t required = function.parameters.size() - function.default_arguments - skip;
    const std::size_t base = kind == "method" || kind == "constructor" || overload.receives_self ? 1 : 0;
    const std::string indent = "            ";
    const auto owner = overload.owner;
    const auto names = parameter_names(overload);
    out << indent << "{\n"
        << indent << "    const std::size_t n = c.args.size() - " << base << ";\n"
        << indent << "    if (c.args.size() >= " << base << " && n >= " << required << " && n <= " << count;
    if (checks && kind == "method") out << "\n" << indent << "        && rosetta_lisp::is_instance<" << owner << ">(c.args[0])";
    if (checks && overload.receives_self) out << "\n" << indent << "        && rosetta_lisp::Param<" << function.parameters[0].canonical << ">::check(c.args[0], false)";
    for (std::size_t i = 0; i < count && checks; ++i)
        out << "\n" << indent << "        && (n <= " << i << " || rosetta_lisp::Param<" << function.parameters[i + skip].canonical
            << ">::check(c.args[" << base + i << "], strict))";
    out << ") {\n";
    if (kind == "method") out << indent << "        auto* native = rosetta_lisp::native<" << owner << ">(c.args[0]);\n";
    if (overload.receives_self)
        out << indent << "        rosetta_lisp::converting() = \"self\";\n"
            << indent << "        rosetta_lisp::Param<" << function.parameters[0].canonical << "> self_param(c.args[0]);\n"
            << indent << "        rosetta_lisp::converting() = nullptr;\n";
    const auto policy = function.is_constructor ? std::string() : return_policy(function);
    const bool parent = kind == "method" || overload.receives_self;
    for (std::size_t arity = required; arity <= count; ++arity) {
        out << indent << "        if (n == " << arity << ") {\n";
        std::string arguments;
        if (overload.receives_self) arguments = "self_param.get()";
        for (std::size_t i = 0; i < arity; ++i) {
            const auto& cpp = i + skip < function.parameter_names.size() ? function.parameter_names[i + skip] : std::string{};
            out << indent << "            rosetta_lisp::converting() = " << quote(cpp.empty() ? "argument " + std::to_string(i + 1) : "argument '" + names[i] + "'") << ";\n"
                << indent << "            rosetta_lisp::Param<" << function.parameters[i + skip].canonical << "> p" << i << "(c.args[" << base + i << "]);\n";
            arguments += std::string(arguments.empty() ? "" : ", ") + "p" + std::to_string(i) + ".get()";
        }
        if (arity > 0) out << indent << "            rosetta_lisp::converting() = nullptr;\n";
        if (kind == "constructor") {
            const auto& plan = classes_.at(owner);
            if (plan.director)
                out << indent << "            if (subclass) {\n"
                    << indent << "                auto* created = new " << director_name(plan) << "(" << arguments << ");\n"
                    << indent << "                c.result = rosetta_lisp::object(static_cast<" << owner << "*>(created), &rosetta_lisp::Info<" << owner << ">::value, rosetta_lisp::Ownership::owned, -1);\n"
                    << indent << "                c.result.holder->director = created;\n"
                    << indent << "                created->rosetta_holder = c.result.holder;\n"
                    << indent << "                created->rosetta_id = subclass;\n"
                    << indent << "                return;\n"
                    << indent << "            }\n";
            if (plan.record->abstract)
                out << indent << "            rosetta_lisp::fail(" << quote(abstract_message(plan)) << ");\n";
            else if (function.access != "public")
                out << indent << "            rosetta_lisp::fail(" << quote(protected_message(plan)) << ");\n";
            else
                out << indent << "            c.result = rosetta_lisp::object(new " << owner << "(" << arguments << "), &rosetta_lisp::Info<" << owner
                    << ">::value, rosetta_lisp::Ownership::owned, -1);\n"
                    << indent << "            return;\n";
        } else {
            std::string body = kind == "method" ? "return native->" + function.name + "(" + arguments + ");"
                                                : "return ::" + function.qualified_name() + "(" + arguments + ");";
            // On a target subclass object, a virtual method runs the native implementation: the
            // call reached C++ because no override exists (or the override called the base
            // method), so dispatching virtually again would come back to the target.
            if (kind == "method" && function.is_virtual && policy_.directors != "none")
                body = "if (rosetta_lisp::is_director(c.args[0])) { " + (function.is_pure
                    ? "rosetta_lisp::pure_virtual(" + quote(owner + "::" + function.name) + ");"
                    : "return native->" + owner + "::" + function.name + "(" + arguments + ");") + " } " + body;
            std::string writebacks;
            for (std::size_t i = 0; i < arity; ++i) {
                const auto& type = function.parameters[i + skip];
                if (type.kind == TypeKind::lvalue_reference && !referent(type).is_const && !bound(referent(type)))
                    writebacks += indent + "            p" + std::to_string(i) + ".writeback(c, c.args[" + std::to_string(base + i) + "]);\n";
            }
            out << indent << "            rosetta_lisp::call(c, [&]() -> decltype(auto) { " << body << " }, " << (parent ? "0" : "-1") << ", " << policy << ");\n"
                << writebacks << indent << "            return;\n";
        }
        out << indent << "        }\n";
    }
    out << indent << "    }\n" << indent << "}\n";
}

void CabiGenerator::emit_group(std::ostream& out, Group& group, bool is_method) {
    group.symbol = symbol(&group.index);
    const bool single = group.overloads.size() == 1;
    out << "void " << group.symbol << "(rosetta_lisp::Call& c) {\n"
        << "    for (int pass = " << (single ? 1 : 0) << "; pass < 2; ++pass) {\n"
        << "        const bool strict = pass == 0; (void)strict;\n";
    for (const auto& overload : group.overloads) emit_overload(out, overload, overload.extension ? "function" : is_method ? "method" : "function", !single);
    out << "    }\n"
        << "    rosetta_lisp::no_overload(c, " << quote(group.name + "(...)") << ", " << quote(signatures(group, is_method)) << ", " << (is_method ? 1 : 0) << ");\n"
        << "}\n";
}

void CabiGenerator::emit_constructor(std::ostream& out, ClassPlan& plan) {
    plan.constructor = symbol(&plan.constructor_index);
    const bool single = plan.constructors.size() == 1;
    if (plan.director) emit_director(out, plan);
    out << "void " << plan.constructor << "(rosetta_lisp::Call& c) {\n"
        << "    const unsigned long long subclass = c.args.empty() || c.args[0].kind != rosetta_lisp::tag_int ? 0 : static_cast<unsigned long long>(c.args[0].integer);\n"
        << "    (void)subclass;\n"
        << "    for (int pass = " << (single ? 1 : 0) << "; pass < 2; ++pass) {\n"
        << "        const bool strict = pass == 0; (void)strict;\n";
    for (const auto& overload : plan.constructors) emit_overload(out, overload, "constructor", !single);
    out << "    }\n    rosetta_lisp::no_overload(c, " << quote(plan.name + "(...)") << ", " << quote(constructor_signatures(plan)) << ", 1);\n"
        << "}\n";
}

void CabiGenerator::emit_cpp(std::string& source, std::vector<std::string>& parts, std::vector<Group>& functions,
                             std::vector<ConstantPlan>& constants, const std::string& comment) {
    struct Unit { std::string code; };
    std::vector<Unit> units;
    std::ostringstream out;
    out << "// Generated by Rosetta: " << comment << "\n";
    for (const auto* header : {"algorithm", "array", "cerrno", "chrono", "climits", "cmath", "complex", "cstdint", "cstdio", "cstdlib", "cstring", "deque",
                               "exception", "filesystem", "functional", "iostream", "limits", "list", "map", "memory", "mutex", "new", "optional", "set",
                               "stdexcept", "string", "string_view", "tuple", "typeindex", "type_traits", "unordered_map", "unordered_set",
                               "utility", "valarray", "variant", "vector"})
        out << "#include <" << header << ">\n";
    out << "#include " << quote(module_.header) << '\n'
        << "#define ROSETTA_LISP_MODULE " << module_prefix() << name_ << '\n'
        << "#define " << runtime_define() << " 1\n";
    for (const auto* part : detail::lisp_full_runtime_parts) out << part;
    out << "namespace ROSETTA_LISP_MODULE { namespace rosetta_lisp {\n";
    for (const auto& owner : class_order_) {
        out << "template<> struct Bound<" << owner << "> : std::true_type {};\n";
        if (!copyable_record(owner)) out << "template<> struct Copyable<" << owner << "> : std::false_type {};\n";
    }
    for (const auto& [from, to] : policy_.conversions) {
        out << "template<> struct Value<" << from << "> {\n"
            << "    static bool check(const Node& n, bool strict) { return Value<" << to << ">::check(n, strict); }\n"
            << "    static " << from << " load(const Node& n) { return " << from << "(Value<" << to << ">::load(n)); }\n"
            << "    static Node make(const " << from << "& value) { return Value<" << to << ">::make(" << to << "(value)); }\n"
            << "};\n";
    }
    out << "} }\n";

    for (auto& group : functions) {
        std::ostringstream unit;
        emit_group(unit, group, false);
        units.push_back({unit.str()});
    }
    for (const auto& owner : class_order_) {
        auto& plan = classes_.at(owner);
        std::ostringstream unit;
        for (auto& group : plan.methods) emit_group(unit, group, !group.is_static);
        for (auto& group : plan.operators) emit_group(unit, group, true);
        for (auto& field : plan.fields) {
            const auto& f = *field.field;
            const auto qualified = owner + "::" + f.name;
            field.getter = symbol(&field.getter_index);
            unit << "void " << field.getter << "(rosetta_lisp::Call& c) {\n"
                 << "    if (c.args.size() != 1) rosetta_lisp::fail(" << quote("field " + field.name + " takes no arguments") << ");\n"
                 << "    auto* native = rosetta_lisp::native<" << owner << ">(c.args[0]);\n"
                 << "    c.result = rosetta_lisp::result<decltype((native->" << f.name << "))>(c, native->" << f.name
                 << ", 0, rosetta_lisp::Return::" << (copy_type(f.type) ? "copy" : "automatic") << ");\n}\n";
            const bool writable = !f.type.is_const && !readonly(qualified, owner) && f.type.kind != TypeKind::lvalue_reference;
            field.setter = symbol(&field.setter_index);
            unit << "void " << field.setter << "(rosetta_lisp::Call& c) {\n"
                 << "    if (c.args.size() != 2) rosetta_lisp::fail(" << quote("field " + field.name + " takes one value") << ");\n"
                 << "    auto* native = rosetta_lisp::native<" << owner << ">(c.args[0]);\n";
            if (writable)
                unit << "    using F = std::remove_cv_t<std::remove_reference_t<decltype(native->" << f.name << ")>>;\n"
                     << "    if constexpr (rosetta_lisp::bound_v<F>) {\n"
                     << "        // object.field = object.field after changing the field object in place.\n"
                     << "        if (rosetta_lisp::is_instance<F>(c.args[1]) && rosetta_lisp::native<F>(c.args[1]) == &native->" << f.name << ") return;\n"
                     << "    }\n"
                     << "    rosetta_lisp::converting() = " << quote("field " + field.name) << ";\n"
                     << "    rosetta_lisp::Param<const F&> converted(c.args[1]);\n"
                     << "    rosetta_lisp::converting() = nullptr;\n"
                     << "    rosetta_lisp::assign_field(native->" << f.name << ", converted.get());\n";
            else
                unit << "    (void)native;\n    rosetta_lisp::fail(" << quote("field " + field.name + " is read-only") << ");\n";
            unit << "}\n";
        }
        for (auto& constant : plan.constants) {
            constant.symbol = symbol(&constant.index);
            unit << "void " << constant.symbol << "(rosetta_lisp::Call& c) {\n"
                 << "    c.result = rosetta_lisp::result<std::remove_cv_t<decltype(" << constant.expression << ")>>(c, static_cast<std::remove_cv_t<decltype("
                 << constant.expression << ")>>(" << constant.expression << "), -1, rosetta_lisp::Return::copy);\n}\n";
        }
        if (!plan.constructors.empty()) emit_constructor(unit, plan);
        units.push_back({unit.str()});
    }
    {
        std::ostringstream unit;
        for (auto& constant : constants) {
            constant.symbol = symbol(&constant.index);
            unit << "void " << constant.symbol << "(rosetta_lisp::Call& c) {\n"
                 << "    c.result = rosetta_lisp::result<std::remove_cv_t<decltype(" << constant.expression << ")>>(c, static_cast<std::remove_cv_t<decltype("
                 << constant.expression << ")>>(" << constant.expression << "), -1, rosetta_lisp::Return::copy);\n}\n";
        }
        units.push_back({unit.str()});
    }

    // Registration: classes (in class order: the target's class table), bases, enums.
    std::ostringstream registration;
    for (std::size_t i = 0; i < class_order_.size(); ++i) {
        const auto& owner = class_order_[i];
        registration << "        rosetta_lisp::register_type<" << owner << ">(" << quote(classes_.at(owner).name) << ", " << i << ");\n";
        for (const auto& base : classes_.at(owner).bases) registration << "        rosetta_lisp::add_base<" << owner << ", " << base << ">();\n";
    }
    for (const auto& [qualified, target] : enum_names_) {
        const auto& enumeration = *enums_.at(qualified);
        std::string entries;
        for (std::size_t i = 0; i < enumeration.enumerators.size(); ++i) {
            const auto label = renamed(qualified + "::" + enumeration.enumerators[i], enumeration.enumerators[i]);
            const auto& value = enumeration.enumerator_values[i];
            const bool negative = !value.empty() && value.front() == '-';
            const bool big = !negative && (value.size() > 19 || (value.size() == 19 && value > "9223372036854775807"));
            entries += "{" + quote(label) + ", " + (big ? "static_cast<long long>(" + value + "ULL)"
                : value == "-9223372036854775808" ? std::string("(-9223372036854775807LL - 1)") : value + "LL") + "}, ";
        }
        registration << "        rosetta_lisp::EnumTable<" << qualified << ">::entries = {" << entries << "};\n";
    }

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
        for (auto& part : extra) part << prelude << "namespace ROSETTA_LISP_MODULE {\n";
        out << "namespace ROSETTA_LISP_MODULE {\n";
        for (std::size_t i = 0; i < units.size(); ++i) extra[bin[i]] << units[i].code;
        for (const auto& name : symbols_) out << "void " << name << "(rosetta_lisp::Call&);\n";
        for (auto& part : extra) part << "} // namespace ROSETTA_LISP_MODULE\n";
    } else {
        out << "namespace ROSETTA_LISP_MODULE {\n";
        for (const auto& unit : units) out << unit.code;
    }
    out << "const rosetta_lisp::Function table[] = {rosetta_lisp::call_native";
    for (const auto& name : symbols_) out << ",\n    " << name;
    out << "};\n"
        << "} // namespace ROSETTA_LISP_MODULE\n"
        << "#if defined(_WIN32)\n#define ROSETTA_CABI_EXPORT extern \"C\" __declspec(dllexport)\n#else\n"
        << "#define ROSETTA_CABI_EXPORT extern \"C\" __attribute__((visibility(\"default\")))\n#endif\n"
        << "ROSETTA_CABI_EXPORT int rosetta_" << name_ << "_call(unsigned index, const unsigned char* in, std::size_t size, unsigned char** out, std::size_t* out_size) {\n"
        << "    return ROSETTA_LISP_MODULE::rosetta_lisp::run(ROSETTA_LISP_MODULE::table, sizeof(ROSETTA_LISP_MODULE::table) / sizeof(ROSETTA_LISP_MODULE::table[0]), index, in, size, out, out_size);\n}\n"
        << "ROSETTA_CABI_EXPORT void rosetta_" << name_ << "_free(void* pointer) { std::free(pointer); }\n"
        << "ROSETTA_CABI_EXPORT void* rosetta_" << name_ << "_alloc(std::size_t size) { return std::malloc(size ? size : 1); }\n"
        << "ROSETTA_CABI_EXPORT void rosetta_" << name_ << "_release(void* holder, int now) {\n"
        << "    ROSETTA_LISP_MODULE::rosetta_lisp::release(static_cast<ROSETTA_LISP_MODULE::rosetta_lisp::Holder*>(holder), now);\n}\n"
        << "ROSETTA_CABI_EXPORT int rosetta_" << name_ << "_init(int version, ROSETTA_LISP_MODULE::rosetta_lisp::InvokeFn invoke, ROSETTA_LISP_MODULE::rosetta_lisp::RetainFn retain) {\n"
        << "    using namespace ROSETTA_LISP_MODULE;\n"
        << "    if (version != 1) return 1;\n"
        << "    static const bool registered = [] {\n" << registration.str() << "        return true;\n    }();\n"
        << "    (void)registered;\n"
        << "    rosetta_lisp::Hooks::invoke = invoke;\n"
        << "    rosetta_lisp::Hooks::retain = retain;\n"
        << "    return 0;\n}\n";
    source = out.str();
    for (auto& part : extra) parts.push_back(part.str());
}

} // namespace rosetta::cabi
