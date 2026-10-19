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
        keys(root, {"version", "classes", "callbacks", "enums"}, "policy");
        if (!root.contains("version") || !root["version"].is_number_integer() || root["version"] != 1)
            throw BindingError("policy version must be 1");
        BindingPolicy policy;
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
} // namespace rosetta
