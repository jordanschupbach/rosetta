#include "rosetta/frontend.hpp"
#include "process.hpp"
#include "type_spelling.hpp"
#include "dependencies.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>
#include <memory>
#include <regex>
#include <set>

namespace rosetta {
namespace {

// Object insertion order matters: Clang elides filenames relative to the last
// emitted location, including spelling/expansion locations and source ranges.
using Json = nlohmann::ordered_json;
const Json empty_object = Json::object();
const Json empty_array = Json::array();

std::string trim(std::string text) {
    const auto begin = text.find_first_not_of(" \n\r\t");
    if (begin == std::string::npos) return {};
    return text.substr(begin, text.find_last_not_of(" \n\r\t") - begin + 1);
}

class AstReader {
public:
    explicit AstReader(std::string header) {
        module_.header = std::move(header);
        const auto text = detail::read_file(module_.header);
        line_starts_.push_back(0);
        for (std::size_t i = 0; i < text.size(); ++i)
            if (text[i] == '\n') line_starts_.push_back(i + 1);
    }

    Module read(const Json& root) {
        walk(root, {}, false);
        merge_redeclarations(module_.functions);
        for (auto& function : module_.functions) {
            if (function.default_arguments && (overloaded_default_names_.count(function.qualified_name()) ||
                visible_overloads_[function.qualified_name()].size() > 1))
                function.unsupported_reason = "default calls with unsupported overloads require an explicit adapter";
        }
        for (auto& record : module_.records) {
            for (auto* functions : {&record.constructors, &record.methods}) {
                merge_redeclarations(*functions);
                for (auto& function : *functions)
                    if (function.default_arguments && member_templates_[record.qualified_name()].count(function.name))
                        function.unsupported_reason = "default calls with member template overloads require an explicit adapter";
            }
        }
        return std::move(module_);
    }

private:
    Module module_;
    std::string current_file_;
    struct Alias { std::string target; std::vector<std::string> namespaces; };
    std::map<std::string, Alias> aliases_;
    std::map<std::string, std::size_t> enum_indices_;
    std::map<std::string, std::size_t> record_indices_;
    std::map<std::string, std::string> record_type_names_;
    std::set<std::string> inline_namespaces_;
    std::set<std::string> overloaded_default_names_;
    std::map<std::string, std::set<std::string>> visible_overloads_;
    struct DeclarationContext { std::vector<std::string> scopes; std::string owner; bool accessible = true; };
    std::map<std::string, DeclarationContext> declaration_contexts_, record_contexts_;
    std::map<std::string, std::string> member_access_;
    std::map<std::string, std::set<std::string>> member_templates_;
    std::map<std::string, std::string> namespace_ids_, namespace_aliases_, type_ids_, function_ids_;
    std::map<std::string, std::string> template_ids_, template_names_;
    std::set<std::string> namespace_names_;
    unsigned record_depth_ = 0;
    std::set<std::size_t> pending_records_;
    std::vector<std::size_t> line_starts_;

    static void merge_redeclarations(std::vector<Function>& functions) {
        std::map<std::string, std::size_t> identities;
        std::vector<Function> unique;
        for (auto& function : functions) {
            const auto inserted = identities.emplace(function.identity(), unique.size());
            if (inserted.second) unique.push_back(std::move(function));
            else unique[inserted.first->second].default_arguments =
                std::max(unique[inserted.first->second].default_arguments, function.default_arguments);
        }
        functions = std::move(unique);
    }

    static std::string qualify(const std::vector<std::string>& namespaces, const std::string& name) {
        std::string result;
        for (const auto& part : namespaces) result += part + "::";
        return result + name;
    }

    // Bind the first qualifier in its lexical scope before expanding aliases.
    // Template arguments retain their original lexical lookup context.
    std::pair<std::string, bool> namespace_name(std::string name, const std::vector<std::string>& scopes, bool absolute, unsigned depth) const {
        const auto angle = name.find('<');
        std::string head = name.substr(0, angle);
        const auto separator = head.find("::");
        if (separator == std::string::npos) return {name, absolute};
        bool bound = absolute;
        if (!absolute) {
            for (std::size_t depth = scopes.size() + 1; depth > 0; --depth) {
                const std::vector<std::string> scope(scopes.begin(), scopes.begin() + static_cast<std::ptrdiff_t>(depth - 1));
                const auto first = qualify(scope, head.substr(0, separator));
                if (namespace_aliases_.count(first) || namespace_names_.count(first) || record_type_names_.count(first) ||
                    enum_indices_.count(first) || aliases_.count(first)) {
                    head = first + head.substr(separator);
                    bound = true;
                    break;
                }
            }
        }
        if (bound) {
            for (unsigned expansions = 0; expansions <= 64; ++expansions) {
                bool changed = false;
                for (auto end = head.find("::"); end != std::string::npos; end = head.find("::", end + 2)) {
                    const auto alias = namespace_aliases_.find(head.substr(0, end));
                    if (alias != namespace_aliases_.end()) {
                        head = alias->second + head.substr(end);
                        changed = true;
                        break;
                    }
                    const auto type_alias = aliases_.find(head.substr(0, end));
                    if (type_alias != aliases_.end()) {
                        Type target{type_alias->second.target, type_alias->second.target};
                        resolve(target, type_alias->second.namespaces, {}, depth + 1);
                        if ((target.kind == TypeKind::record || target.kind == TypeKind::enumeration) &&
                            target.declaration != head.substr(0, end)) {
                            head = target.declaration + head.substr(end);
                            changed = true;
                            break;
                        }
                    }
                }
                if (!changed) return {head + (angle == std::string::npos ? "" : name.substr(angle)), true};
            }
            throw BindingError("namespace alias expansion exceeds the frontend limit");
        }
        return {name, absolute};
    }

    void read_using(const Json& node, const std::vector<std::string>& scopes) {
        if (node.value("kind", "") != "UsingShadowDecl" || !node.contains("target")) return;
        const auto& target = node.at("target");
        const auto kind = target.value("kind", "");
        const auto id = target.value("id", "");
        const auto type = type_ids_.find(id);
        const auto imported_template = template_ids_.find(id);
        for (const auto& imported : visible_names(scopes, target.value("name", ""))) {
            if (type != type_ids_.end()) aliases_[imported] = {"::" + type->second, {}};
            else if (imported_template != template_ids_.end()) template_names_[imported] = imported_template->second;
            else if (kind == "FunctionTemplateDecl") overloaded_default_names_.insert(imported);
            else if (kind == "FunctionDecl") {
                const auto function = function_ids_.find(id);
                if (function != function_ids_.end()) visible_overloads_[imported].insert(function->second);
            }
        }
    }

    std::vector<std::string> visible_names(const std::vector<std::string>& namespaces, const std::string& name) const {
        std::vector<std::string> prefixes{std::string{}};
        std::string actual;
        for (const auto& part : namespaces) {
            actual += (actual.empty() ? "" : "::") + part;
            const auto count = prefixes.size();
            for (std::size_t i = 0; i < count; ++i) {
                if (inline_namespaces_.count(actual)) prefixes.push_back(prefixes[i]);
                prefixes[i] += part + "::";
            }
        }
        for (auto& prefix : prefixes) prefix += name;
        return prefixes;
    }

    SourceLocation location(const Json& loc) const {
        SourceLocation result;
        result.file = current_file_;
        result.offset = loc.value("offset", std::size_t{0});
        if (current_file_ == module_.header) {
            const auto next = std::upper_bound(line_starts_.begin(), line_starts_.end(), result.offset);
            result.line = static_cast<std::size_t>(next - line_starts_.begin());
            result.column = result.offset - line_starts_.at(result.line - 1) + 1;
        } else {
            result.line = loc.value("line", std::size_t{0});
            result.column = loc.value("col", std::size_t{0});
        }
        return result;
    }

    void resolve(Type& type, std::vector<std::string> namespaces,
                 std::set<std::string> aliases = {}, unsigned depth = 0) const {
        if (depth > 64) throw BindingError("type nesting exceeds the frontend limit");
        static const std::set<std::string> builtins = {"void", "bool", "char", "signed char", "unsigned char",
            "short", "unsigned short", "int", "unsigned int", "long", "unsigned long", "long long",
            "unsigned long long", "float", "double", "long double", "wchar_t", "char16_t", "char32_t", "char8_t"};
        std::string name = trim(type.canonical);
        const auto decorate = [&] {
            if (type.is_volatile) type.canonical = "volatile " + type.canonical;
            if (type.is_const) type.canonical = "const " + type.canonical;
        };
        // Resolve the outer declarator before leading cv qualifiers: in
        // `const char*` const belongs to char, while `char* const` is a const pointer.
        if (!name.empty() && name.back() == '&') {
            const bool rvalue = name.size() > 1 && name[name.size() - 2] == '&';
            const auto element = trim(name.substr(0, name.size() - (rvalue ? 2 : 1)));
            Type child{element, element};
            resolve(child, namespaces, aliases, depth + 1);
            type.kind = rvalue ? TypeKind::rvalue_reference : TypeKind::lvalue_reference;
            type.canonical = child.canonical + (rvalue ? " &&" : " &");
            type.arguments = {std::move(child)};
            return;
        }
        const auto function = detail::function_spelling(name);
        if (function && function->suffix.empty() && !function->result.empty()) {
            type.kind = TypeKind::function;
            type.arguments.clear();
            Type returned{function->result, function->result};
            resolve(returned, namespaces, aliases, depth + 1);
            type.arguments.push_back(std::move(returned));
            const auto parameters = detail::type_arguments(function->parameters);
            for (const auto& parameter : parameters) {
                if (parameter == "void" && parameters.size() == 1) continue;
                Type child{parameter, parameter};
                resolve(child, namespaces, aliases, depth + 1);
                type.arguments.push_back(std::move(child));
            }
            type.canonical = type.arguments.front().canonical + " (";
            for (std::size_t i = 1; i < type.arguments.size(); ++i) {
                if (i > 1) type.canonical += ", ";
                type.canonical += type.arguments[i].canonical;
            }
            type.canonical += ")";
            return;
        }
        bool changed = true;
        while (changed) {
            changed = false;
            for (const std::string qualifier : {"const", "volatile"}) {
                if (name.size() > qualifier.size() && name.substr(name.size() - qualifier.size()) == qualifier &&
                    (name[name.size() - qualifier.size() - 1] == ' ' || name[name.size() - qualifier.size() - 1] == '*')) {
                    name = trim(name.substr(0, name.size() - qualifier.size()));
                    if (qualifier == "const") type.is_const = true; else type.is_volatile = true;
                    changed = true;
                }
            }
        }
        if (!name.empty() && name.back() == '*') {
            const auto element = trim(name.substr(0, name.size() - 1));
            Type child{element, element};
            resolve(child, namespaces, aliases, depth + 1);
            type.kind = TypeKind::pointer;
            type.canonical = child.canonical + " *" + (type.is_const ? " const" : "") + (type.is_volatile ? " volatile" : "");
            type.arguments = {std::move(child)};
            return;
        }
        changed = true;
        while (changed) {
            changed = false;
            if (name.rfind("const ", 0) == 0) { name = trim(name.substr(6)); type.is_const = true; changed = true; }
            if (name.rfind("volatile ", 0) == 0) { name = trim(name.substr(9)); type.is_volatile = true; changed = true; }
        }
        if (name.rfind("enum ", 0) == 0) name = name.substr(5);
        if (name.rfind("struct ", 0) == 0) name = name.substr(7);
        if (name.rfind("class ", 0) == 0) name = name.substr(6);
        const bool absolute = name.rfind("::", 0) == 0;
        if (absolute) name.erase(0, 2);
        const auto expanded = namespace_name(name, namespaces, absolute, depth);
        name = expanded.first;
        const auto lookup_scopes = expanded.second ? std::vector<std::string>{} : namespaces;
        for (std::size_t scope_depth = lookup_scopes.size() + 1; scope_depth > 0; --scope_depth) {
            const std::vector<std::string> scope(lookup_scopes.begin(), lookup_scopes.begin() + static_cast<std::ptrdiff_t>(scope_depth - 1));
            const auto candidate = qualify(scope, name);
            if (enum_indices_.count(candidate)) {
                const auto declaration = module_.enumerations[enum_indices_.at(candidate)].qualified_name();
                type.canonical = declaration;
                type.kind = TypeKind::enumeration;
                type.declaration = declaration;
                decorate();
                return;
            }
            if (record_type_names_.count(candidate)) {
                const auto declaration = record_type_names_.at(candidate);
                type.canonical = declaration;
                type.kind = TypeKind::record;
                type.declaration = declaration;
                decorate();
                return;
            }
            const auto alias = aliases_.find(candidate);
            if (alias != aliases_.end() && aliases.insert(candidate).second) {
                type.canonical = alias->second.target;
                resolve(type, alias->second.namespaces, std::move(aliases), depth + 1);
                return;
            }
        }
        const auto open = name.find('<');
        if (open != std::string::npos && name.back() == '>') {
            auto primary = trim(name.substr(0, open));
            for (std::size_t scope_depth = lookup_scopes.size() + 1; scope_depth > 0; --scope_depth) {
                const std::vector<std::string> scope(lookup_scopes.begin(), lookup_scopes.begin() + static_cast<std::ptrdiff_t>(scope_depth - 1));
                const auto found = template_names_.find(qualify(scope, primary));
                if (found != template_names_.end()) { primary = found->second; break; }
            }
            // Standard-library alias definitions often spell these without std::.
            // Preserve inline ABI namespace spelling when already present.
            if (primary.find("::") == std::string::npos && !namespaces.empty() && namespaces.front() == "std")
                primary = qualify(namespaces, primary);
            type.kind = TypeKind::template_instance;
            type.declaration = primary;
            type.arguments.clear();
            std::size_t start = open + 1;
            int angle_depth = 0, paren_depth = 0;
            for (std::size_t i = start; i < name.size(); ++i) {
                const char c = name[i];
                if ((c == ',' && angle_depth == 0 && paren_depth == 0) || i == name.size() - 1) {
                    const auto argument = trim(name.substr(start, i - start));
                    Type child{argument, argument};
                    resolve(child, namespaces, aliases, depth + 1);
                    if (!argument.empty()) type.arguments.push_back(std::move(child));
                    start = i + 1;
                } else if (c == '<') ++angle_depth;
                else if (c == '>') --angle_depth;
                else if (c == '(') ++paren_depth;
                else if (c == ')') --paren_depth;
            }
            type.canonical = primary + "<";
            for (std::size_t i = 0; i < type.arguments.size(); ++i) {
                if (i) type.canonical += ", ";
                type.canonical += type.arguments[i].canonical;
            }
            type.canonical += ">";
        } else {
            type.canonical = name;
            if (builtins.count(name)) type.kind = TypeKind::builtin;
        }
        decorate();
    }

    void advance_locations(const Json& value) {
        if (value.is_object() && value.contains("offset") && value.contains("file"))
            current_file_ = std::filesystem::weakly_canonical(value.at("file").get<std::string>()).string();
        if (value.is_structured())
            for (const auto& child : value) advance_locations(child);
    }

    Function callable(const Json& node, const std::vector<std::string>& scope, const SourceLocation& source,
                      const std::string& owner = {}, const std::string& access = "public") const {
        Function result;
        result.name = node.value("name", "<anonymous>");
        result.namespaces = scope;
        result.location = source;
        result.owner = owner;
        result.access = access;
        result.is_constructor = node.value("kind", "") == "CXXConstructorDecl";
        result.is_static = node.value("storageClass", "") == "static";
        result.is_deleted = node.value("explicitlyDeleted", false);
        const auto signature = node.at("type").at("qualType").get<std::string>();
        if (result.is_deleted) result.unsupported_reason = "deleted functions are unsupported";
        if (node.value("immediate", false)) result.unsupported_reason = "consteval functions require a constant-expression facade";
        if (node.value("variadic", false) || signature.find("...") != std::string::npos)
            result.unsupported_reason = "variadic functions are unsupported";
        const auto& children = node.contains("inner") ? node.at("inner") : empty_array;
        for (const auto& child : children) {
            if (child.value("kind", "") != "ParmVarDecl") continue;
            if (child.contains("init")) ++result.default_arguments;
            const auto& type = child.at("type");
            const auto spelling = type.at("qualType").get<std::string>();
            result.parameters.push_back({spelling, type.value("desugaredQualType", spelling)});
        }
        const auto function = detail::function_spelling(signature);
        if (!function || (function->suffix != "" && function->suffix != "const" &&
            function->suffix != "noexcept" && function->suffix != "const noexcept")) {
            result.unsupported_reason = "unsupported function type " + signature;
            result.result = {"<unsupported>", "<unsupported>"};
        } else {
            const auto returned = result.is_constructor ? owner : function->result;
            result.result = {returned, returned};
            result.is_const = function->suffix == "const" || function->suffix == "const noexcept";
            result.is_noexcept = function->suffix == "noexcept" || function->suffix == "const noexcept";
        }
        return result;
    }

    void read_enum(const Json& node, const std::vector<std::string>& scopes, const SourceLocation& source,
                   bool own, const std::string& owner = {}, bool accessible = true) {
        type_ids_[node.at("id").get<std::string>()] = qualify(scopes, node.at("name").get<std::string>());
        declaration_contexts_[node.at("id").get<std::string>()] = {scopes, owner, accessible};
        if (!accessible) return;
        Enumeration enumeration;
        enumeration.name = node.at("name").get<std::string>();
        enumeration.namespaces = scopes;
        enumeration.owner = owner;
        enumeration.scoped = node.contains("scopedEnumTag");
        enumeration.selected = own;
        enumeration.location = source;
        const auto& children = node.contains("inner") ? node.at("inner") : empty_array;
        for (const auto& child : children)
            if (child.value("kind", "") == "EnumConstantDecl") enumeration.enumerators.push_back(child.at("name").get<std::string>());
        const auto key = enumeration.qualified_name();
        const auto previous = enum_indices_.find(key);
        if (previous == enum_indices_.end()) {
            for (const auto& visible : visible_names(scopes, enumeration.name)) enum_indices_[visible] = module_.enumerations.size();
            module_.enumerations.push_back(std::move(enumeration));
        } else if (!enumeration.enumerators.empty()) {
            enumeration.selected = enumeration.selected || module_.enumerations[previous->second].selected;
            module_.enumerations[previous->second] = std::move(enumeration);
        }
    }

    void read_record(const Json& node, const std::vector<std::string>& namespaces, const SourceLocation& source,
                     bool own, const std::string& parent_owner = {}, bool accessible = true) {
        struct DepthScope { unsigned& depth; ~DepthScope() { --depth; } } depth_scope{record_depth_};
        const bool outermost = record_depth_++ == 0;
        Record record;
        record.name = node.at("name").get<std::string>();
        record.namespaces = namespaces;
        record.owner = parent_owner;
        record.complete = node.value("completeDefinition", false);
        record.location = source;
        record.has_bases = node.contains("bases") && !node.at("bases").empty();
        record.abstract = node.value("definitionData", empty_object).value("isAbstract", false);
        if (node.value("tagUsed", "") == "union") record.unsupported_reason = "unions require a tagged/custom adapter";
        const auto owner = record.qualified_name();
        type_ids_[node.at("id").get<std::string>()] = owner;
        if (accessible)
            for (const auto& visible : visible_names(namespaces, record.name)) record_type_names_[visible] = owner;
        auto scope = namespaces;
        scope.push_back(record.name);
        declaration_contexts_[node.at("id").get<std::string>()] = {namespaces, parent_owner, accessible};
        record_contexts_[node.at("id").get<std::string>()] = {scope, owner, accessible};
        std::string access = node.value("tagUsed", "") == "class" ? "private" : "public";
        const auto& children = node.contains("inner") ? node.at("inner") : empty_array;
        std::set<std::string> member_templates;
        for (const auto& child : children) {
            const auto& loc = child.contains("loc") ? child.at("loc") : empty_object;
            advance_locations(loc);
            const auto& expanded = loc.contains("expansionLoc") ? loc.at("expansionLoc") : loc;
            const auto child_location = expanded.contains("offset") ? location(expanded) : source;
            advance_locations(child.contains("range") ? child.at("range") : empty_object);
            const auto kind = child.value("kind", "");
            read_using(child, scope);
            if (kind == "FunctionTemplateDecl") member_templates.insert(child.value("name", ""));
            if (kind == "AccessSpecDecl") access = child.at("access").get<std::string>();
            if (!child.value("isImplicit", false) && child.contains("name")) {
                if (kind == "EnumDecl") read_enum(child, scope, child_location, own, owner, accessible && access == "public");
                if (kind == "CXXRecordDecl") read_record(child, scope, child_location, own, owner, accessible && access == "public");
            }
            if (kind == "TypeAliasDecl" || kind == "TypedefDecl") {
                type_ids_[child.at("id").get<std::string>()] = qualify(scope, child.at("name").get<std::string>());
                const auto& type = child.at("type");
                aliases_[qualify(scope, child.at("name").get<std::string>())] =
                    {type.value("desugaredQualType", type.value("qualType", "")), scope};
            }
            if (own && accessible && !child.value("isImplicit", false)) {
                if (kind == "CXXConstructorDecl" || kind == "CXXMethodDecl" || kind == "CXXConversionDecl")
                    member_access_[child.at("id").get<std::string>()] = access;
                if (kind == "CXXConstructorDecl") record.constructors.push_back(callable(child, scope, child_location, owner, access));
                if (kind == "CXXMethodDecl" || kind == "CXXConversionDecl") record.methods.push_back(callable(child, scope, child_location, owner, access));
                if (kind == "CXXDestructorDecl") {
                    const auto signature = child.at("type").at("qualType").get<std::string>();
                    record.public_destructor = access == "public" && !child.value("explicitlyDeleted", false) &&
                        signature.find("noexcept(false)") == std::string::npos;
                }
                if (kind == "FieldDecl") {
                    const auto& type = child.at("type");
                    const auto spelling = type.at("qualType").get<std::string>();
                    record.fields.push_back({child.value("name", "<anonymous>"),
                        {spelling, type.value("desugaredQualType", spelling)}, access, child.value("isBitfield", false), child_location});
                }
            }
            advance_locations(child.contains("inner") ? child.at("inner") : empty_array);
        }
        if (!own || !accessible) return;
        member_templates_[owner].insert(member_templates.begin(), member_templates.end());
        const auto definition = node.value("definitionData", empty_object);
        for (auto* functions : {&record.constructors, &record.methods})
            for (auto& function : *functions)
                if (function.default_arguments && member_templates.count(function.name))
                    function.unsupported_reason = "default calls with member template overloads require an explicit adapter";
        if (record.complete && record.constructors.empty() && definition.value("defaultCtor", empty_object).value("exists", false)) {
            Function constructor;
            constructor.name = record.name;
            constructor.namespaces = scope;
            constructor.owner = owner;
            constructor.is_constructor = true;
            constructor.result = {owner, owner};
            constructor.location = source;
            record.constructors.push_back(std::move(constructor));
        }
        const auto previous = record_indices_.find(owner);
        if (previous == record_indices_.end()) {
            pending_records_.insert(module_.records.size());
            for (const auto& visible : visible_names(namespaces, record.name)) record_indices_[visible] = module_.records.size();
            module_.records.push_back(std::move(record));
        } else if (record.complete) {
            pending_records_.insert(previous->second);
            module_.records[previous->second] = std::move(record);
        }
        if (outermost) {
            // Complete class contexts may refer to later member aliases, even
            // from a nested method body. Namespace declarations after the
            // enclosing class must not alter those member signatures.
            for (const auto index : pending_records_) {
                auto& pending = module_.records[index];
                auto context = pending.namespaces;
                context.push_back(pending.name);
                for (auto* functions : {&pending.constructors, &pending.methods})
                    for (auto& function : *functions) {
                        resolve(function.result, context);
                        for (auto& parameter : function.parameters) resolve(parameter, context);
                    }
                for (auto& field : pending.fields) resolve(field.type, context);
            }
            pending_records_.clear();
        }
    }

    void walk(const Json& node, std::vector<std::string> namespaces, bool anonymous) {
        const auto& raw_loc = node.contains("loc") ? node.at("loc") : empty_object;
        advance_locations(raw_loc);
        const auto& loc = raw_loc.contains("expansionLoc") ? raw_loc.at("expansionLoc") : raw_loc;
        const bool own = current_file_ == module_.header && loc.contains("offset");
        const auto source_location = loc.contains("offset") ? location(loc) : SourceLocation{};
        advance_locations(node.contains("range") ? node.at("range") : empty_object);
        const auto kind = node.value("kind", "");
        const auto name = node.value("name", "<anonymous>");
        const auto& children = node.contains("inner") ? node.at("inner") : empty_array;
        if (kind == "NamespaceDecl") {
            if (node.contains("name") && !anonymous) {
                const auto full_name = qualify(namespaces, name);
                namespace_ids_[node.at("id").get<std::string>()] = full_name;
                for (const auto& visible : visible_names(namespaces, name)) namespace_names_.insert(visible);
            }
            if (node.value("isInline", false)) inline_namespaces_.insert(qualify(namespaces, name));
            if (node.contains("name")) namespaces.push_back(name);
            for (const auto& child : children)
                walk(child, namespaces, anonymous || !node.contains("name"));
            return;
        }
        if (kind == "TranslationUnitDecl" || kind == "LinkageSpecDecl") {
            for (const auto& child : children) walk(child, namespaces, anonymous);
            return;
        }
        if (kind == "NamespaceAliasDecl") {
            const auto target = namespace_ids_.find(node.at("aliasedNamespace").at("id").get<std::string>());
            if (target != namespace_ids_.end() && !anonymous) {
                for (const auto& visible : visible_names(namespaces, name)) namespace_aliases_[visible] = target->second;
                namespace_ids_[node.at("id").get<std::string>()] = target->second;
            }
            return;
        }
        std::string owner;
        bool accessible = true;
        const auto parent = record_contexts_.find(node.value("parentDeclContextId", ""));
        if (parent != record_contexts_.end()) {
            namespaces = parent->second.scopes;
            owner = parent->second.owner;
            accessible = parent->second.accessible;
        }
        const auto previous_context = declaration_contexts_.find(node.value("previousDecl", ""));
        if (previous_context != declaration_contexts_.end()) {
            namespaces = previous_context->second.scopes;
            owner = previous_context->second.owner;
            accessible = previous_context->second.accessible;
        }
        // Included overloads participate in C++ lookup even though they are not
        // selected for export. Mangled identities merge alias redeclarations.
        if (!anonymous && kind == "FunctionDecl" && node.contains("mangledName")) {
            function_ids_[node.at("id").get<std::string>()] = node.at("mangledName").get<std::string>();
            visible_overloads_[qualify(namespaces, name)].insert(node.at("mangledName").get<std::string>());
        }
        if (!anonymous) read_using(node, namespaces);
        if (!anonymous && kind == "FunctionTemplateDecl") overloaded_default_names_.insert(qualify(namespaces, name));
        if (!anonymous && (kind == "ClassTemplateDecl" || kind == "TypeAliasTemplateDecl")) {
            const auto declaration = qualify(namespaces, name);
            template_ids_[node.at("id").get<std::string>()] = declaration;
            for (const auto& visible : visible_names(namespaces, name)) template_names_[visible] = declaration;
        }
        if (kind == "CXXRecordDecl" && node.contains("name") && !node.value("isImplicit", false) && !anonymous) {
            read_record(node, namespaces, source_location, own, owner, accessible);
            return;
        }
        advance_locations(children);
        if (parent != record_contexts_.end() && (kind == "CXXConstructorDecl" || kind == "CXXMethodDecl" ||
            kind == "CXXConversionDecl" || kind == "CXXDestructorDecl")) {
            const auto found = record_indices_.find(parent->second.owner);
            if (accessible && found != record_indices_.end() && kind != "CXXDestructorDecl") {
                const auto previous_access = member_access_.find(node.value("previousDecl", ""));
                const auto access = previous_access == member_access_.end() ? "private" : previous_access->second;
                auto function = callable(node, parent->second.scopes, source_location, parent->second.owner, access);
                resolve(function.result, parent->second.scopes);
                for (auto& parameter : function.parameters) resolve(parameter, parent->second.scopes);
                auto& record = module_.records[found->second];
                (function.is_constructor ? record.constructors : record.methods).push_back(std::move(function));
                member_access_[node.at("id").get<std::string>()] = access;
            }
            return;
        }
        if (kind == "ClassTemplateDecl" &&
            std::any_of(children.begin(), children.end(), [](const Json& child) {
                return child.value("kind", "") == "CXXRecordDecl" && !child.value("completeDefinition", false);
            }) && std::none_of(children.begin(), children.end(), [](const Json& child) {
                return child.value("kind", "") == "CXXRecordDecl" && child.value("completeDefinition", false);
            })) {
            // A forward template declaration can support an opaque private
            // implementation without requesting template instantiation/export.
            return;
        }
        if ((kind == "TypedefDecl" || kind == "TypeAliasDecl") && node.contains("type") && !anonymous) {
            type_ids_[node.at("id").get<std::string>()] = qualify(namespaces, name);
            const auto& type = node.at("type");
            for (const auto& key : visible_names(namespaces, name))
                aliases_[key] = {type.value("desugaredQualType", type.value("qualType", "")), namespaces};
        }
        if (kind == "EnumDecl" && node.contains("name") && !anonymous) {
            read_enum(node, namespaces, source_location, own, owner, accessible);
            return;
        }
        if (!own || node.value("isImplicit", false)) return;
        const auto label = qualify(namespaces, name);
        const auto reject = [&](const std::string& reason) { module_.diagnostics.push_back(label + ": " + reason); };
        static const std::set<std::string> ignored = {"TypedefDecl", "TypeAliasDecl", "EmptyDecl",
            "UsingDirectiveDecl", "UsingDecl", "UsingShadowDecl", "StaticAssertDecl"};
        if (ignored.count(kind)) return;
        if (kind != "FunctionDecl") {
            if (kind == "FunctionTemplateDecl") overloaded_default_names_.insert(label);
            reject("unsupported declaration " + kind); return;
        }
        if (anonymous || node.value("storageClass", "") == "static" || name.rfind("operator", 0) == 0) {
            reject("internal linkage or operator function is unsupported"); return;
        }
        auto function = callable(node, namespaces, source_location);
        if (!function.unsupported_reason.empty()) {
            overloaded_default_names_.insert(label);
            reject(function.unsupported_reason); return;
        }
        // Namespace aliases and using declarations introduced later must not
        // change a signature that was already declared in this lexical scope.
        resolve(function.result, function.namespaces);
        for (auto& parameter : function.parameters) resolve(parameter, function.namespaces);
        module_.functions.push_back(std::move(function));
    }
};
} // namespace

Module parse(const std::filesystem::path& input, const std::string& clang,
             const std::vector<std::string>& flags, const std::filesystem::path& directory, bool collect_dependencies) {
    const auto header = std::filesystem::canonical(input);
    if (directory.string().find('\0') != std::string::npos) throw BindingError("compiler working directory cannot contain NUL bytes");
    const auto working_directory = std::filesystem::canonical(directory.empty() ? std::filesystem::current_path() : directory);
    const auto compiler = std::filesystem::path(clang).has_parent_path() ? std::filesystem::absolute(clang).string() : clang;
    std::vector<std::string> command{compiler};
    command.insert(command.end(), flags.begin(), flags.end());
    std::unique_ptr<detail::TemporaryDirectory> dependency_directory;
    std::filesystem::path dependency_file;
    std::filesystem::path dependency_base = working_directory;
    if (collect_dependencies) {
        detail::validate_dependency_flags(flags);
        dependency_base = detail::dependency_working_directory(flags, working_directory);
        dependency_directory = std::make_unique<detail::TemporaryDirectory>();
        dependency_file = dependency_directory->path() / "compiler.d";
        command.insert(command.end(), {"-MD", "-MF", dependency_file.string(), "-MT", "rosetta-dependencies"});
    }
    command.insert(command.end(), {"-x", "c++", "-fsyntax-only", "-Wno-unused-command-line-argument",
        "-Wno-pragma-once-outside-header", "-Xclang", "-ast-dump=json", header.string()});
    const auto process = detail::run_process(command, working_directory);
    if (process.exit_code != 0)
        throw BindingError("Clang rejected " + header.string() + " (" + clang + "):\n" + process.error);
    try {
        const auto root = Json::parse(process.output);
        auto module = AstReader(header.string()).read(root);
        if (collect_dependencies) module.dependencies = detail::compiler_dependencies(detail::read_file(dependency_file), dependency_base);
        module.parse_context = {compiler, working_directory.string(), flags, {}, {}, 0};
        return module;
    } catch (const Json::exception& error) {
        throw BindingError("cannot interpret Clang JSON AST: " + std::string(error.what()));
    }
}

} // namespace rosetta
