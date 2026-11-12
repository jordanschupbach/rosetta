#include "rosetta/policy.hpp"
#include "rosetta/model.hpp"
#include "process.hpp"
#include <nlohmann/json.hpp>
#include <charconv>

namespace rosetta {
namespace {
using Json = nlohmann::json;

void keys(const Json& object, const std::set<std::string>& allowed, const std::string& context) {
    if (!object.is_object()) throw BindingError(context + " must be an object");
    for (const auto& item : object.items())
        if (!allowed.count(item.key())) throw BindingError(context + ": unknown key " + item.key());
}

std::set<std::string> names(const Json& value, const std::string& context) {
    if (!value.is_array()) throw BindingError(context + " must be an array of names");
    std::set<std::string> result;
    for (const auto& item : value) {
        if (!item.is_string() || item.get<std::string>().empty()) throw BindingError(context + ": expected a nonempty name");
        if (!result.insert(item.get<std::string>()).second) throw BindingError(context + ": duplicate name");
    }
    return result;
}
} // namespace

BindingPolicy load_policy(const std::filesystem::path& path) {
    try {
        const auto root = Json::parse(detail::read_file(path));
        keys(root, {"version", "classes", "callbacks", "enums", "select", "exclude", "rename", "templates", "extend",
                    "returns", "readonly", "docs", "convert", "names", "sequences", "directors", "scope", "exceptions", "qualify", "copy_types"}, "policy");
        if (!root.contains("version") || !root["version"].is_number_integer() || root["version"] != 1)
            throw BindingError("policy version must be 1");
        BindingPolicy policy;
        const auto string_map = [](const Json& value, const std::string& context) {
            if (!value.is_object()) throw BindingError(context + " must be an object");
            std::map<std::string, std::string> result;
            for (const auto& item : value.items()) {
                if (item.key().empty() || !item.value().is_string() || item.value().get<std::string>().empty())
                    throw BindingError(context + ": expected nonempty names and string values");
                result.emplace(item.key(), item.value().get<std::string>());
            }
            return result;
        };
        const auto list = [](const Json& value, const std::string& context) {
            if (!value.is_array()) throw BindingError(context + " must be an array of names");
            std::vector<std::string> result;
            for (const auto& item : value) {
                if (!item.is_string() || item.get<std::string>().empty()) throw BindingError(context + ": expected a nonempty name");
                result.push_back(item.get<std::string>());
            }
            return result;
        };
        const auto choice = [&](const char* key, std::initializer_list<const char*> allowed, std::string& target) {
            if (!root.contains(key)) return;
            const auto& value = root[key];
            if (value.is_string())
                for (const auto* option : allowed) if (value == option) { target = option; return; }
            std::string options;
            for (const auto* option : allowed) options += std::string(options.empty() ? "" : ", ") + option;
            throw BindingError(std::string("policy ") + key + " must be one of: " + options);
        };
        if (root.contains("select")) {
            if (root["select"] != "all") throw BindingError("policy select must be \"all\"");
            policy.select_all = true;
        }
        if (root.contains("exclude")) policy.exclude = list(root["exclude"], "policy exclude");
        if (root.contains("qualify")) policy.qualify = list(root["qualify"], "policy qualify");
        if (root.contains("copy_types"))
            for (const auto& type : list(root["copy_types"], "policy copy_types")) policy.copy_types.insert(type);
        if (root.contains("scope"))
            for (const auto& item : list(root["scope"], "policy scope")) {
                const std::filesystem::path scope(item);
                policy.scopes.push_back(scope.is_absolute() ? scope : path.parent_path() / scope);
            }
        if (root.contains("readonly")) policy.readonly = list(root["readonly"], "policy readonly");
        if (root.contains("rename")) policy.rename = string_map(root["rename"], "policy rename");
        if (root.contains("docs")) policy.docs = string_map(root["docs"], "policy docs");
        if (root.contains("convert")) policy.conversions = string_map(root["convert"], "policy convert");
        if (root.contains("templates")) {
            if (!root["templates"].is_object()) throw BindingError("policy templates must be an object");
            for (const auto& item : root["templates"].items()) {
                if (item.key().empty() || !item.value().is_string() || item.value().get<std::string>().find('<') == std::string::npos)
                    throw BindingError("policy templates map a name to a specialization such as ns::Box<int>");
                policy.templates.emplace_back(item.key(), item.value().get<std::string>());
            }
        }
        if (root.contains("returns")) {
            policy.returns = string_map(root["returns"], "policy returns");
            for (const auto& item : policy.returns)
                if (item.second != "owned" && item.second != "copy" && item.second != "borrowed")
                    throw BindingError("policy returns for " + item.first + " must be owned, copy or borrowed");
        }
        if (root.contains("extend")) {
            if (!root["extend"].is_object()) throw BindingError("policy extend must be an object");
            for (const auto& item : root["extend"].items())
                policy.extend.emplace(item.key(), string_map(item.value(), "policy extend " + item.key()));
        }
        choice("names", {"qualified", "flat"}, policy.names);
        choice("sequences", {"list", "tuple", "vector"}, policy.sequences);
        if (root.contains("exceptions")) {
            const auto& rules = root["exceptions"];
            if (!rules.is_array()) throw BindingError("policy exceptions must be an array of {match, map} rules");
            for (const auto& rule : rules) {
                keys(rule, {"match", "map"}, "policy exceptions rule");
                if (!rule.contains("match") || !rule["match"].is_string() || !rule.contains("map") || !rule["map"].is_array())
                    throw BindingError("policy exceptions rules need a match pattern and a map array");
                std::vector<std::pair<std::string, std::string>> map;
                for (const auto& entry : rule["map"]) {
                    if (!entry.is_array() || entry.size() != 2 || !entry[0].is_string() || !entry[1].is_string())
                        throw BindingError("policy exceptions map entries are [C++ exception type, Python exception name]");
                    map.emplace_back(entry[0].get<std::string>(), entry[1].get<std::string>());
                }
                policy.exceptions.emplace_back(rule["match"].get<std::string>(), std::move(map));
            }
        }
        choice("directors", {"auto", "none"}, policy.directors);
        if (root.contains("enums")) {
            if (!root["enums"].is_object()) throw BindingError("enum policy must be an object");
            for (const auto& item : root["enums"].items()) {
                const auto context = "enum policy " + item.key();
                if (item.key().empty()) throw BindingError("enum policy requires a nonempty enum name");
                keys(item.value(), {"flags"}, context);
                if (!item.value().contains("flags")) throw BindingError(context + ": explicitly name flag basis enumerators");
                auto flags = names(item.value()["flags"], context + " flags");
                if (flags.empty()) throw BindingError(context + ": flag basis must not be empty");
                policy.enum_flags.emplace(item.key(), std::move(flags));
            }
        }
        if (root.contains("classes")) {
            policy.explicit_class_selection = true;
            if (!root["classes"].is_object()) throw BindingError("policy classes must be an object");
            for (const auto& item : root["classes"].items()) {
                const auto& config = item.value();
                const auto context = "class policy " + item.key();
                keys(config, {"ownership", "constructors", "methods", "fields", "returns"}, context);
                if (!config.contains("ownership") || config["ownership"] != "owned")
                    throw BindingError(context + ": explicitly set ownership to owned");
                ClassPolicy selected;
                selected.constructors = config.value("constructors", true);
                if (config.contains("methods")) {
                    selected.restrict_methods = true;
                    selected.methods = names(config["methods"], context + " methods");
                }
                if (config.contains("fields")) {
                    selected.restrict_fields = true;
                    selected.fields = names(config["fields"], context + " fields");
                }
                if (config.contains("returns")) {
                    if (!config["returns"].is_object()) throw BindingError(context + " returns must be an object");
                    for (const auto& rule : config["returns"].items()) {
                        if (rule.value() != "borrow_self") throw BindingError(context + ": supported return policy is borrow_self");
                        selected.borrow_self.insert(rule.key());
                    }
                }
                policy.classes.emplace(item.key(), std::move(selected));
            }
        }
        if (root.contains("callbacks")) {
            if (!root["callbacks"].is_object()) throw BindingError("callback policy must be an object");
            for (const auto& function : root["callbacks"].items()) {
                if (function.key().empty() || !function.value().is_object() || function.value().empty())
                    throw BindingError("callback policy needs a function name and parameter/lifetime object");
                for (const auto& parameter : function.value().items()) {
                    std::size_t index = 0;
                    const auto& key = parameter.key();
                    const auto parsed = std::from_chars(key.data(), key.data() + key.size(), index);
                    if (parsed.ec != std::errc{} || parsed.ptr != key.data() + key.size() || index == 0 || std::to_string(index) != key)
                        throw BindingError("callback policy parameter index must be a positive decimal integer");
                    if (parameter.value() == "call") policy.callbacks[function.key()][index] = CallbackLifetime::call;
                    else if (parameter.value() == "retained") policy.callbacks[function.key()][index] = CallbackLifetime::retained;
                    else throw BindingError("callback policy lifetime must be call or retained");
                }
            }
        }
        return policy;
    } catch (const Json::exception& error) {
        throw BindingError("invalid binding policy: " + std::string(error.what()));
    }
}
bool policy_match(const std::string& pattern, const std::string& name) {
    // Iterative glob matching with backtracking to the last '*'.
    std::size_t p = 0, n = 0, star = std::string::npos, mark = 0;
    while (n < name.size()) {
        if (p < pattern.size() && pattern[p] == '*') { star = p++; mark = n; }
        else if (p < pattern.size() && pattern[p] == name[n]) { ++p; ++n; }
        else if (star != std::string::npos) { p = star + 1; n = ++mark; }
        else return false;
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

// Policy entries may name an entity through a using-declaration (repvgg::Spec::field for
// namespace repvgg { using models::Spec; }); they then apply to the entity itself.
BindingPolicy resolve_reexports(BindingPolicy policy, const Module& module) {
    std::map<std::string, std::string> targets;
    for (const auto& reexport : module.reexports) {
        std::string scope;
        for (const auto& part : reexport.namespaces) scope += part + "::";
        targets.emplace(scope + reexport.name, reexport.target);
    }
    if (targets.empty()) return policy;
    const auto resolve = [&](const std::string& key) {
        for (auto end = key.find("::"); end != std::string::npos; end = key.find("::", end + 2)) {
            const auto found = targets.find(key.substr(0, key.find("::", end + 2)));
            if (found != targets.end()) return found->second + key.substr(found->first.size());
        }
        const auto whole = targets.find(key);
        return whole == targets.end() ? key : whole->second;
    };
    // Entries for the entity itself take precedence over alias entries.
    const auto merge = [&](auto& map) {
        std::remove_reference_t<decltype(map)> result;
        for (auto& [key, value] : map) if (resolve(key) == key) result.emplace(key, value);
        for (auto& [key, value] : map) if (resolve(key) != key) result.emplace(resolve(key), value);
        map = std::move(result);
    };
    merge(policy.classes); merge(policy.callbacks); merge(policy.enum_flags); merge(policy.rename);
    merge(policy.returns); merge(policy.docs);
    {
        std::map<std::string, std::map<std::string, std::string>> extend;
        for (auto& [key, value] : policy.extend) if (resolve(key) == key) extend[key] = value;
        for (auto& [key, value] : policy.extend) if (resolve(key) != key) extend[resolve(key)].insert(value.begin(), value.end());
        policy.extend = std::move(extend);
    }
    for (auto& entry : policy.exclude) entry = resolve(entry);
    for (auto& entry : policy.readonly) entry = resolve(entry);
    std::set<std::string> copy_types;
    for (const auto& entry : policy.copy_types) copy_types.insert(resolve(entry));
    policy.copy_types = std::move(copy_types);
    return policy;
}

} // namespace rosetta
