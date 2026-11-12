#include "rosetta/model.hpp"
#include <nlohmann/json.hpp>
#include <cctype>

namespace rosetta {

const char* type_kind_name(TypeKind kind) {
    switch (kind) {
    case TypeKind::builtin: return "builtin";
    case TypeKind::enumeration: return "enumeration";
    case TypeKind::record: return "record";
    case TypeKind::function: return "function";
    case TypeKind::pointer: return "pointer";
    case TypeKind::lvalue_reference: return "lvalue_reference";
    case TypeKind::rvalue_reference: return "rvalue_reference";
    case TypeKind::template_instance: return "template_instance";
    case TypeKind::unresolved: return "unresolved";
    }
    return "unresolved";
}

std::string Function::qualified_name() const {
    std::string result;
    for (const auto& part : namespaces) result += part + "::";
    return result + name;
}

std::string Function::identity() const {
    std::string result = qualified_name() + "(";
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        if (i) result += ",";
        result += parameters[i].canonical;
    }
    return result + ")->" + this->result.canonical + (is_const ? " const" : "");
}

std::string plain_documentation(const Documentation& documentation) {
    std::string result;
    const auto add = [&](const std::string& text) {
        if (!text.empty()) result += (result.empty() ? "" : "\n\n") + text;
    };
    const auto indented = [](const std::string& text, const std::string& indent) {
        std::string out = indent;
        for (const char c : text) {
            out += c;
            if (c == '\n') out += indent;
        }
        return out;
    };
    add(documentation.brief);
    for (const auto& block : documentation.details) {
        if (block.kind == "heading") add(block.text + ":");
        else if (block.kind == "code" || block.kind == "math") add(indented(block.text, "    "));
        else add(block.text);
    }
    const auto items = [&](const char* title, const std::vector<DocItem>& entries) {
        if (entries.empty()) return;
        std::string section = title;
        for (const auto& entry : entries) {
            std::string text = entry.text;
            for (std::size_t at = 0; (at = text.find('\n', at)) != std::string::npos; at += 9) text.replace(at, 1, "\n        ");
            section += "\n    " + entry.name + (text.empty() ? "" : ": " + text);
        }
        add(section);
    };
    items("Parameters:", documentation.params);
    if (!documentation.returns.empty()) add("Returns: " + documentation.returns);
    items("Return values:", documentation.retvals);
    items("Throws:", documentation.throws);
    for (const auto& note : documentation.notes) {
        auto label = note.name;
        if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        add(label + ": " + note.text);
    }
    if (!documentation.see.empty()) {
        std::string see = "See also: ";
        for (std::size_t i = 0; i < documentation.see.size(); ++i) see += (i ? ", " : "") + documentation.see[i];
        add(see);
    }
    return result;
}

std::string Enumeration::qualified_name() const {
    std::string result;
    for (const auto& part : namespaces) result += part + "::";
    return result + name;
}

std::string Variable::qualified_name() const {
    std::string result;
    for (const auto& part : namespaces) result += part + "::";
    return result + name;
}

std::string Record::qualified_name() const {
    std::string result;
    for (const auto& part : namespaces) result += part + "::";
    return result + name;
}

std::string serialize_ir(const Module& module) {
    using Json = nlohmann::ordered_json;
    const auto type_json = [](const auto& self, const Type& t) -> Json {
        auto arguments = Json::array();
        for (const auto& argument : t.arguments) arguments.push_back(self(self, argument));
        return Json{{"spelling", t.spelling}, {"canonical", t.canonical},
                    {"kind", type_kind_name(t.kind)}, {"declaration", t.declaration},
                    {"arguments", arguments}, {"const", t.is_const}, {"volatile", t.is_volatile}};
    };
    const auto location_json = [](const SourceLocation& location) {
        return Json{{"file", location.file}, {"line", location.line},
                    {"column", location.column}, {"offset", location.offset}};
    };
    const auto items_json = [](const std::vector<DocItem>& items) {
        auto result = Json::array();
        for (const auto& item : items) result.push_back({{"name", item.name}, {"text", item.text}, {"direction", item.direction}});
        return result;
    };
    const auto documentation_json = [&](const Documentation& d) {
        auto details = Json::array();
        for (const auto& block : d.details) details.push_back({{"kind", block.kind}, {"text", block.text}, {"language", block.language}});
        return Json{{"brief", d.brief}, {"details", details}, {"params", items_json(d.params)}, {"tparams", items_json(d.tparams)},
                    {"returns", d.returns}, {"retvals", items_json(d.retvals)}, {"throws", items_json(d.throws)},
                    {"notes", items_json(d.notes)}, {"see", d.see}};
    };
    const auto function_json = [&](const Function& f) {
        auto defaults = Json::array();
        for (const auto& value : f.default_values) defaults.push_back({{"kind", value.kind}, {"value", value.value}});
        auto parameters = Json::array();
        for (const auto& p : f.parameters) parameters.push_back(type_json(type_json, p));
        return Json{{"name", f.name}, {"namespaces", f.namespaces},
                             {"result", type_json(type_json, f.result)}, {"parameters", parameters},
                             {"identity", f.identity()}, {"location", location_json(f.location)},
                             {"owner", f.owner}, {"access", f.access}, {"constructor", f.is_constructor},
                             {"static", f.is_static}, {"const", f.is_const}, {"deleted", f.is_deleted},
                             {"noexcept", f.is_noexcept},
                             {"default_arguments", f.default_arguments},
                             {"unsupported_reason", f.unsupported_reason},
                             {"parameter_names", f.parameter_names},
                             {"virtual", f.is_virtual}, {"pure", f.is_pure}, {"doc", f.doc},
                             {"documentation", documentation_json(f.documentation)}, {"default_values", defaults}};
    };
    const auto variable_json = [&](const Variable& v) {
        return Json{{"name", v.name}, {"namespaces", v.namespaces}, {"identity", v.qualified_name()},
                    {"type", type_json(type_json, v.type)}, {"owner", v.owner}, {"access", v.access},
                    {"constexpr", v.is_constexpr}, {"location", location_json(v.location)}, {"doc", v.doc},
                    {"documentation", documentation_json(v.documentation)}};
    };
    auto functions = Json::array();
    for (const auto& f : module.functions) functions.push_back(function_json(f));
    auto enumerations = Json::array();
    for (const auto& e : module.enumerations)
        enumerations.push_back({{"name", e.name}, {"namespaces", e.namespaces},
            {"identity", e.qualified_name()}, {"enumerators", e.enumerators}, {"enumerator_values", e.enumerator_values}, {"underlying_type", e.underlying_type},
            {"scoped", e.scoped}, {"selected", e.selected}, {"owner", e.owner}, {"location", location_json(e.location)}, {"doc", e.doc},
            {"documentation", documentation_json(e.documentation)}});
    auto records = Json::array();
    for (const auto& r : module.records) {
        auto constructors = Json::array(), methods = Json::array(), fields = Json::array();
        for (const auto& c : r.constructors) constructors.push_back(function_json(c));
        for (const auto& m : r.methods) methods.push_back(function_json(m));
        for (const auto& f : r.fields)
            fields.push_back({{"name", f.name}, {"type", type_json(type_json, f.type)},
                {"access", f.access}, {"bitfield", f.bitfield}, {"location", location_json(f.location)}, {"doc", f.doc},
                {"documentation", documentation_json(f.documentation)}});
        auto bases = Json::array(), statics = Json::array();
        for (const auto& b : r.bases)
            bases.push_back({{"type", type_json(type_json, b.type)}, {"access", b.access}, {"virtual", b.is_virtual}});
        for (const auto& v : r.static_fields) statics.push_back(variable_json(v));
        records.push_back({{"name", r.name}, {"identity", r.qualified_name()}, {"namespaces", r.namespaces},
            {"constructors", constructors}, {"methods", methods}, {"fields", fields}, {"complete", r.complete},
            {"abstract", r.abstract}, {"public_destructor", r.public_destructor}, {"has_bases", r.has_bases},
            {"unsupported_reason", r.unsupported_reason}, {"owner", r.owner}, {"location", location_json(r.location)},
            {"bases", bases}, {"static_fields", statics}, {"polymorphic", r.polymorphic},
            {"virtual_destructor", r.virtual_destructor}, {"final", r.is_final}, {"template_name", r.template_name}, {"doc", r.doc},
            {"documentation", documentation_json(r.documentation)}});
    }
    auto variables = Json::array();
    for (const auto& v : module.variables) variables.push_back(variable_json(v));
    auto reexports = Json::array();
    for (const auto& r : module.reexports)
        reexports.push_back({{"namespaces", r.namespaces}, {"name", r.name}, {"target", r.target}, {"kind", r.kind},
                             {"location", location_json(r.location)}});
    const auto& context = module.parse_context;
    return Json{{"header", module.header}, {"functions", functions}, {"diagnostics", module.diagnostics},
                {"enumerations", enumerations}, {"records", records}, {"variables", variables}, {"reexports", reexports}, {"dependencies", module.dependencies},
                {"parse_context", {{"compiler", context.compiler}, {"directory", context.directory}, {"flags", context.flags},
                    {"compilation_database", context.compilation_database}, {"translation_unit", context.translation_unit},
                    {"command_index", context.compilation_database.empty() ? Json(nullptr) : Json(context.command_index)}}}}.dump(2) + "\n";
}

} // namespace rosetta
