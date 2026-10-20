#include "rosetta/java.hpp"
#include "java_runtime.hpp"
#include "type_spelling.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace rosetta {
namespace {
std::string quote(const std::string& text) {
    std::string result = "\"";
    for (char c : text) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else result += c;
    }
    return result + '"';
}
std::string exported(std::string text) {
    for (std::size_t i = 0; (i = text.find("::", i)) != std::string::npos; i += 2) text.replace(i, 2, "__");
    return text;
}
bool identifier(const std::string& text) {
    auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    return !text.empty() && letter(text.front()) && std::all_of(text.begin(), text.end(), [&](char c) {
        return letter(c) || (c >= '0' && c <= '9') || c == '_';
    });
}
std::string scalar(const Type& type) {
    auto text = detail::trim_type(type.canonical);
    const bool reference = text.rfind("const ", 0) == 0 && !text.empty() && text.back() == '&' &&
        (text.size() < 2 || text[text.size() - 2] != '&');
    if (reference) text = detail::trim_type(text.substr(0, text.size() - 1));
    if (text.rfind("const ", 0) == 0) text = text.substr(6);
    if (text == "std::__cxx11::basic_string<char>" || text == "std::__1::basic_string<char>" || text == "std::basic_string<char>")
        text = "std::string";
    if (reference && text != "std::string") return {};
    static const std::set<std::string> supported = {"bool", "char", "signed char", "unsigned char", "short", "unsigned short",
        "int", "unsigned int", "long", "unsigned long", "long long", "unsigned long long", "float", "double", "std::string"};
    return supported.count(text) ? text : "";
}
std::string problem(const Function& fn) {
    if (!fn.unsupported_reason.empty()) return fn.unsupported_reason;
    if (fn.is_deleted) return "deleted function";
    if (fn.default_arguments > fn.parameters.size()) return "invalid default argument metadata";
    if (!fn.is_constructor && fn.result.canonical != "void" && scalar(fn.result).empty())
        return "unsupported Java result type " + fn.result.canonical;
    for (const auto& type : fn.parameters)
        if (scalar(type).empty()) return "unsupported Java parameter type " + type.canonical;
    return {};
}
} // namespace

GeneratedJavaSource generate_java(const Module& module, const std::string& name, bool skip, const BindingPolicy& policy, const std::string& package) {
    static const std::set<std::string> reserved = {"abstract", "assert", "boolean", "break", "byte", "case", "catch", "char", "class", "const", "continue",
        "default", "do", "double", "else", "enum", "extends", "final", "finally", "float", "for", "goto", "if", "implements", "import", "instanceof",
        "int", "interface", "long", "native", "new", "package", "private", "protected", "public", "return", "short", "static", "strictfp", "super",
        "switch", "synchronized", "this", "throw", "throws", "transient", "try", "void", "volatile", "while", "true", "false", "null", "var", "yield",
        "record", "sealed", "permits", "java", "invoke", "destroy", "shutdown", "abi", "utf8", "text", "Cleanup", "Token", "CLEANER",
        "getClass", "wait", "notify", "notifyAll", "hashCode", "toString", "equals", "clone", "finalize"};
    if (!identifier(name)) throw BindingError("module name must be a simple ASCII identifier starting with a letter");
    if (reserved.count(name)) throw BindingError("module name is a reserved Java name");
    if (!package.empty()) {
        std::size_t begin = 0;
        do {
            const auto end = package.find('.', begin);
            const auto part = package.substr(begin, end == std::string::npos ? end : end - begin);
            if (!identifier(part) || reserved.count(part)) throw BindingError("invalid or reserved Java package component: " + part);
            if (end == std::string::npos) break;
            begin = end + 1;
        } while (true);
    }
    if (!policy.callbacks.empty() || !policy.enum_flags.empty()) throw BindingError("Java backend does not yet support callback or enum flag policies");
    GeneratedJavaSource result{{}, {}, module.diagnostics};
    auto reject = [&](const std::string& context, const std::string& reason) { result.diagnostics.push_back(context + ": " + reason); };
    auto java_type = [&](const Type& type) -> std::string {
        const auto value = scalar(type);
        if (type.canonical == "void") return "void";
        if (value == "bool") return "boolean";
        if (value == "std::string") return "byte[]";
        if (value == "float" || value == "double") return value;
        if (value.rfind("unsigned", 0) == 0) return "java.math.BigInteger";
        return "long";
    };
    auto boxed = [&](const Type& type) -> std::string {
        const auto value = java_type(type);
        if (value == "long") return "java.lang.Long";
        if (value == "boolean") return "java.lang.Boolean";
        if (value == "float") return "java.lang.Float";
        if (value == "double") return "java.lang.Double";
        return value;
    };
    std::ostringstream out, dispatch, java;
    out << "// Generated by Rosetta: Java JNI bridge.\n#include <jni.h>\n#include <cmath>\n#include <cstdint>\n#include <cstring>\n#include <limits>\n"
        << "#include <memory>\n#include <mutex>\n#include <new>\n#include <stdexcept>\n#include <string>\n#include <type_traits>\n#include <unordered_map>\n"
        << "#include " << quote(module.header) << '\n' << detail::java_runtime;
    java << "// Generated by Rosetta; compile with javac --release 17.\n";
    if (!package.empty()) java << "package " << package << ";\n";
    java << "public final class " << name << " {\n"
        << "private " << name << "() {}\n"
        << "private static native java.lang.Object invoke(int operation, long owner, java.lang.Object[] args);\n"
        << "private static native void destroy(long owner);\nprivate static native void shutdown();\nprivate static native long abi();\n"
        << "private static final java.lang.ref.Cleaner CLEANER = java.lang.ref.Cleaner.create();\n"
        << "private enum Token { VALUE }\n"
        << "private static final class Cleanup implements java.lang.Runnable {\n"
        << "private final long id; Cleanup(long value) { id = value; }\npublic void run() { destroy(id); }\n}\n"
        << "static { java.lang.System.loadLibrary(" << quote(name) << ");\n"
        << "if (abi() != 1) throw new java.lang.UnsatisfiedLinkError(\"Rosetta Java ABI mismatch\");\n"
        << "java.lang.Runtime.getRuntime().addShutdownHook(new java.lang.Thread(" << name << "::shutdown, " << quote("rosetta-" + name + "-shutdown") << ")); }\n"
        << "public static byte[] utf8(java.lang.String text) { return text.getBytes(java.nio.charset.StandardCharsets.UTF_8); }\n"
        << "public static java.lang.String text(byte[] bytes) { return new java.lang.String(bytes, java.nio.charset.StandardCharsets.UTF_8); }\n";
    std::set<std::string> exports{name};
    auto reserve = [&](const std::string& key) {
        if (!identifier(key) || reserved.count(key)) throw BindingError("invalid or reserved Java export: " + key);
        if (!exports.insert(key).second) throw BindingError("Java export name collision: " + key);
    };
    int next = 0;
    auto invocation = [&](const std::string& call, const Type& type, bool instance) {
        if (instance) java << "try {\n";
        if (type.canonical == "void") java << call << ";\n";
        else java << "return (" << boxed(type) << ") " << call << ";\n";
        if (instance) java << "} finally { java.lang.ref.Reference.reachabilityFence(this); }\n";
    };
    auto emit = [&](const Function& fn, const std::string& key, const std::string& target, const std::string& owner, bool instance) {
        for (std::size_t count = fn.parameters.size() - fn.default_arguments; count <= fn.parameters.size(); ++count) {
            const int operation = next++;
            std::string actuals, parameters;
            for (std::size_t i = 0; i < count; ++i) {
                if (i) { actuals += ", "; parameters += ", "; }
                actuals += "value" + std::to_string(i);
                parameters += java_type(fn.parameters[i]) + " value" + std::to_string(i);
            }
            dispatch << "case " << operation << ": { rosetta_java::arity(count, " << count << ");\n";
            for (std::size_t i = 0; i < count; ++i)
                dispatch << "auto value" << i << " = rosetta_java::from<" << scalar(fn.parameters[i]) << ">(env, args, " << i << ");\n";
            if (fn.is_constructor) dispatch << "return rosetta_java::own(env, std::make_unique<::" << owner << ">(" << actuals << "));\n";
            else if (fn.result.canonical == "void") dispatch << target << '(' << actuals << "); return nullptr;\n";
            else dispatch << "auto result = " << target << '(' << actuals << "); return rosetta_java::to(env, result);\n";
            dispatch << "}\n";
            const auto call = "invoke(" + std::to_string(operation) + ", " + (instance ? "handle" : "0") + ", new java.lang.Object[]{" + actuals + "})";
            if (fn.is_constructor)
                java << "public " << key << '(' << parameters << ") { this((java.lang.Long) " << call << ", Token.VALUE); }\n";
            else {
                java << "public " << (instance ? "" : "static ") << java_type(fn.result) << ' ' << key << '(' << parameters << ") {\n";
                invocation(call, fn.result, instance);
                java << "}\n";
            }
        }
    };
    std::map<std::string, std::size_t> counts;
    for (const auto& fn : module.functions) ++counts[fn.qualified_name()];
    for (const auto& fn : module.functions) {
        auto why = problem(fn);
        if (counts[fn.qualified_name()] > 1) why = "overloads require an explicit facade";
        if (!why.empty()) { reject(fn.qualified_name(), why); continue; }
        const auto key = exported(fn.qualified_name()); reserve(key);
        emit(fn, key, "::" + fn.qualified_name(), {}, false);
    }
    for (const auto& selection : policy.classes)
        if (std::none_of(module.records.begin(), module.records.end(), [&](const Record& r) { return r.qualified_name() == selection.first; }))
            throw BindingError("unknown class in Java policy: " + selection.first);
    for (const auto& rec : module.records) {
        const auto owner = rec.qualified_name();
        const auto selected = policy.classes.find(owner);
        if (selected == policy.classes.end()) {
            if (!policy.explicit_class_selection && policy.classes.empty()) reject(owner, "class requires an owned class policy");
            continue;
        }
        const auto& config = selected->second;
        for (const auto& requested : config.methods) {
            auto found = std::find_if(rec.methods.begin(), rec.methods.end(), [&](const Function& f) { return f.name == requested; });
            if (found == rec.methods.end()) throw BindingError("unknown Java policy method: " + owner + "::" + requested);
            if (found->access != "public") throw BindingError("Java policy method is not public: " + owner + "::" + requested);
        }
        for (const auto& requested : config.fields) {
            auto found = std::find_if(rec.fields.begin(), rec.fields.end(), [&](const Field& f) { return f.name == requested; });
            if (found == rec.fields.end()) throw BindingError("unknown Java policy field: " + owner + "::" + requested);
            if (found->access != "public") throw BindingError("Java policy field is not public: " + owner + "::" + requested);
        }
        if (!rec.complete || rec.abstract || rec.has_bases || !rec.public_destructor || !rec.unsupported_reason.empty()) {
            reject(owner, "Java requires a complete, concrete class without bases and a public nonthrowing destructor"); continue;
        }
        if (!config.borrow_self.empty()) { reject(owner, "borrowed Java results are not yet supported"); continue; }
        const auto key = exported(owner); reserve(key);
        out << "static_assert(std::is_nothrow_destructible_v<::" << owner << ">, \"native owners require nonthrowing destruction\");\n";
        java << "public static final class " << key << " implements java.lang.AutoCloseable {\n"
            << "private final long handle;\nprivate final java.lang.ref.Cleaner.Cleanable cleanable;\n"
            << "private " << key << "(long id, Token token) { handle = id;\n"
            << "try { cleanable = CLEANER.register(this, new Cleanup(id)); } catch (java.lang.Throwable error) { destroy(id); throw error; }\n}\n"
            << "@Override public void close() { cleanable.clean(); }\n";
        std::vector<const Function*> constructors;
        for (const auto& ctor : rec.constructors) if (!ctor.is_deleted) constructors.push_back(&ctor);
        if (config.constructors) {
            if (constructors.size() != 1) reject(owner, "constructor overloads require an explicit facade");
            else {
                const auto& ctor = *constructors.front();
                auto why = problem(ctor);
                if (ctor.access != "public") why = "constructor is not public";
                if (!why.empty()) reject(owner, why);
                else emit(ctor, key, {}, owner, false);
            }
        }
        std::set<std::string> members = reserved; members.erase("text"); members.erase("utf8"); members.insert("close"); members.insert("handle"); members.insert("cleanable");
        auto member = [&](const std::string& value) {
            if (!identifier(value) || !members.insert(value).second) throw BindingError("reserved or duplicate Java member: " + value);
        };
        counts.clear();
        for (const auto& fn : rec.methods) ++counts[fn.name];
        for (const auto& fn : rec.methods) {
            if (fn.access != "public" || (config.restrict_methods && !config.methods.count(fn.name))) continue;
            auto why = problem(fn);
            if (counts[fn.name] > 1) why = "method overloads require an explicit facade";
            if (!why.empty()) { reject(owner + "::" + fn.name, why); continue; }
            const auto method_key = fn.name == "wait" ? "waitFor" : fn.name;
            member(method_key);
            const auto target = fn.is_static ? "::" + owner + "::" + fn.name : "rosetta_java::object<::" + owner + ">(id)->" + fn.name;
            emit(fn, method_key, target, {}, !fn.is_static);
        }
        for (const auto& field : rec.fields) {
            if (field.access != "public" || (config.restrict_fields && !config.fields.count(field.name))) continue;
            if (scalar(field.type).empty() || field.bitfield || field.type.is_volatile) { reject(owner + "::" + field.name, "unsupported Java field type"); continue; }
            const auto getter = "get_" + field.name, setter = "set_" + field.name;
            member(getter);
            auto operation = next++;
            dispatch << "case " << operation << ": { rosetta_java::arity(count, 0); return rosetta_java::to(env, rosetta_java::object<::" << owner << ">(id)->" << field.name << "); }\n";
            java << "public " << java_type(field.type) << ' ' << getter << "() {\n";
            invocation("invoke(" + std::to_string(operation) + ", handle, new java.lang.Object[]{})", field.type, true); java << "}\n";
            if (!field.type.is_const) {
                member(setter); operation = next++;
                dispatch << "case " << operation << ": { rosetta_java::arity(count, 1); auto value = rosetta_java::from<" << scalar(field.type) << ">(env, args, 0);\n"
                    << "rosetta_java::object<::" << owner << ">(id)->" << field.name << " = value; return nullptr; }\n";
                java << "public void " << setter << '(' << java_type(field.type) << " value) { try { invoke(" << operation
                    << ", handle, new java.lang.Object[]{value}); } finally { java.lang.ref.Reference.reachabilityFence(this); } }\n";
            }
        }
        java << "}\n";
    }
    for (const auto& enumeration : module.enumerations)
        if (enumeration.selected) reject(enumeration.qualified_name(), "Java enum bindings are not yet supported");
    if (!skip && !result.diagnostics.empty()) {
        std::string error = "unsupported declaration for Java";
        for (const auto& entry : result.diagnostics) error += "\n  " + entry;
        throw BindingError(error);
    }
    std::string jni;
    for (char c : (package.empty() ? name : package + "." + name))
        jni += c == '.' ? "_" : c == '_' ? "_1" : std::string(1, c);
    const auto prefix = "Java_" + jni + "_";
    out << "extern \"C\" {\nJNIEXPORT jlong JNICALL " << prefix << "abi(JNIEnv*, jclass) noexcept { return 1; }\n"
        << "JNIEXPORT void JNICALL " << prefix << "destroy(JNIEnv* env, jclass, jlong id) noexcept {\n"
        << "rosetta_java::guard(env, [&]() -> jobject { rosetta_java::registry().owners.erase(id); return nullptr; });\n}\n"
        << "JNIEXPORT void JNICALL " << prefix << "shutdown(JNIEnv* env, jclass) noexcept {\n"
        << "rosetta_java::guard(env, [&]() -> jobject { rosetta_java::registry().owners.clear(); return nullptr; });\n}\n"
        << "JNIEXPORT jobject JNICALL " << prefix << "invoke(JNIEnv* env, jclass, jint operation, jlong id, jobjectArray args) noexcept {\n"
        << "return rosetta_java::guard(env, [&]() -> jobject {\n(void)id;\nif (!args) throw rosetta_java::conversion_error(\"null argument array\");\n"
        << "const auto count = env->GetArrayLength(args); rosetta_java::check(env);\nswitch (operation) {\n"
        << dispatch.str() << "}\nthrow rosetta_java::conversion_error(\"unknown native operation\");\n});\n}\n}\n";
    java << "}\n";
    result.source = out.str(); result.interface = java.str();
    return result;
}
} // namespace rosetta
