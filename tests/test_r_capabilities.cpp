#include "rosetta/frontend.hpp"
#include "rosetta/r.hpp"
#include "process.hpp"
#include <functional>
#include <iostream>

namespace {
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
void rejects(const std::function<void()>& action, const std::string& expected) {
    try { action(); }
    catch (const rosetta::BindingError& e) {
        require(contains(e.what(), expected), "expected '" + expected + "', got: " + e.what());
        return;
    }
    throw std::runtime_error("expected rejection: " + expected);
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 1;
    try {
        rosetta::detail::TemporaryDirectory temporary;
        const auto header = temporary.path() / "capabilities.hpp";
        auto parse = [&](const std::string& source) {
            rosetta::detail::write_file(header, source);
            return rosetta::parse(header, argv[1], {"-std=c++20"});
        };
        struct Case { const char* label; const char* source; const char* reason; bool select_class = false; };
        const Case cases[] = {
            {"extended numbers", "long double unsupported(long double);", "unsupported R result type"},
            {"enums", "enum class Mode { a, b };", "enum bindings are not yet supported"},
            {"overloads", "int unsupported(int); int unsupported(double);", "overloads"},
            {"raw pointers", "int* unsupported(int*);", "unsupported R result type"},
            {"mutable references", "void unsupported(int&);", "unsupported R parameter type"},
            {"rvalue references", "void unsupported(int&&);", "unsupported R parameter type"},
            {"function pointers", "void unsupported(int (*)(int));", "unsupported R parameter type"},
            {"arrays", "void unsupported(int[4]);", "unsupported R parameter type"},
            {"string views", "#include <string_view>\nvoid unsupported(std::string_view);", "unsupported R parameter type"},
            {"vectors", "#include <vector>\nvoid unsupported(std::vector<int>);", "unsupported R parameter type"},
            {"optional", "#include <optional>\nvoid unsupported(std::optional<int>);", "unsupported R parameter type"},
            {"class values", "struct Object {}; void unsupported(Object);", "unsupported R parameter type"},
            {"union storage", "union Object { int i; double d; };", "complete, concrete", true},
            {"bitfields", "struct Object { unsigned bits : 3; };", "unsupported R field type", true},
            {"volatile storage", "struct Object { volatile int field; };", "unsupported R field type", true},
            {"constructor overloads", "struct Object { Object(int); Object(double); };", "constructor overloads", true},
            {"private destructor", "class Object { ~Object(); };", "public nonthrowing destructor", true},
            {"throwing destructor", "struct Object { ~Object() noexcept(false); };", "public nonthrowing destructor", true},
            {"member overloads", "struct Object { int unsupported(int); int unsupported(double); };", "method overloads", true},
            {"inheritance", "struct Base {}; struct Object : Base {};", "complete, concrete", true},
            {"abstract classes", "struct Object { virtual int value() = 0; };", "complete, concrete", true},
            {"templates", "template<class T> T unsupported(T);", "FunctionTemplateDecl"},
            {"callbacks", "#include <functional>\nvoid unsupported(std::function<int(int)>);", "unsupported R parameter type"},
            {"immediate functions", "consteval int unsupported(int x) { return x; }", "consteval"},
            {"coroutine values", "#include <coroutine>\nvoid unsupported(std::coroutine_handle<>);", "unsupported R parameter type"},
            {"variable exports", "inline constexpr int unsupported = 42;", "VarDecl"},
        };
        for (const auto& test : cases) {
            const auto module = parse(std::string(test.source) + "\nint sentinel();\n");
            rosetta::BindingPolicy policy;
            if (test.select_class) policy.classes["Object"] = {};
            // Ignore unselected helper records; inspect the unsupported callable.
            else policy.explicit_class_selection = true;
            for (const auto system : {rosetta::RObjectSystem::r6, rosetta::RObjectSystem::s3, rosetta::RObjectSystem::s4}) {
            rejects([&] { rosetta::generate_r(module, "api", system, false, policy); }, test.reason);
            const auto partial = rosetta::generate_r(module, "api", system, true, policy);
            std::string diagnostics;
            for (const auto& entry : partial.diagnostics) diagnostics += entry + '\n';
            require(contains(diagnostics, test.reason), std::string(test.label) + ": missing partial diagnostic");
            require(contains(partial.interface, "exports[[\"sentinel\"]]"), "safe declaration lost in partial output");
            require(!contains(partial.interface, "exports[[\"unsupported\"]]"), "unsupported declaration silently exported");
            }
            std::cout << "PASS " << test.label << '\n';
        }
        const auto access = parse("struct Object { Object(); int visible(); private: int hidden(); int secret; }; int sentinel();");
        rosetta::BindingPolicy policy;
        policy.classes["Object"] = {};
        const auto output = rosetta::generate_r(access, "api", rosetta::RObjectSystem::r6, false, policy);
        require(!contains(output.interface, "\"hidden\"") && !contains(output.interface, "\"secret\""), "private API exposed");
        policy.classes["Object"].restrict_methods = true;
        policy.classes["Object"].methods = {"hidden"};
        rejects([&] { rosetta::generate_r(access, "api", rosetta::RObjectSystem::r6, false, policy); }, "not public");
        policy.classes["Object"] = {};
        policy.classes["Object"].restrict_fields = true;
        policy.classes["Object"].fields = {"secret"};
        rejects([&] { rosetta::generate_r(access, "api", rosetta::RObjectSystem::r6, false, policy); }, "not public");
        policy.classes["Object"] = {};
        policy.classes["Object"].borrow_self.insert("visible");
        rejects([&] { rosetta::generate_r(access, "api", rosetta::RObjectSystem::r6, false, policy); }, "borrowed R6");
        const auto selectable = parse("struct Object { Object(); int visible(); int omitted(); int field; int excluded; };");
        policy.classes["Object"] = {};
        auto& selection = policy.classes["Object"];
        selection.restrict_methods = true; selection.methods = {"visible"};
        selection.restrict_fields = true; selection.fields = {"field"};
        selection.constructors = false;
        const auto restricted = rosetta::generate_r(selectable, "api", rosetta::RObjectSystem::r6, false, policy);
        require(contains(restricted.interface, "\"visible\"") && contains(restricted.interface, "\"field\"") &&
                !contains(restricted.interface, "\"omitted\"") && !contains(restricted.interface, "\"excluded\"") &&
                !contains(restricted.source, "new ::Object"), "class selection restrictions ignored");
        require(restricted.source == rosetta::generate_r(selectable, "api", rosetta::RObjectSystem::r6, false, policy).source,
                "R generation is not deterministic");
        selection.methods = {"nonexistent"};
        rejects([&] { rosetta::generate_r(selectable, "api", rosetta::RObjectSystem::r6, false, policy); }, "unknown R6 policy method");
        policy = {}; policy.classes["Missing"] = {};
        rejects([&] { rosetta::generate_r(selectable, "api", rosetta::RObjectSystem::r6, false, policy); }, "unknown class in R policy");
        policy = {};
        policy.callbacks["sentinel"][1] = rosetta::CallbackLifetime::call;
        rejects([&] { rosetta::generate_r(access, "api", rosetta::RObjectSystem::r6, true, policy); }, "callback or enum flag policies");
        policy = {}; policy.enum_flags["Mode"] = {"a"};
        rejects([&] { rosetta::generate_r(access, "api", rosetta::RObjectSystem::r6, true, policy); }, "callback or enum flag policies");
        const auto reserved = parse("struct Object { void close(); }; ");
        policy = {}; policy.classes["Object"] = {};
        rejects([&] { rosetta::generate_r(reserved, "api", rosetta::RObjectSystem::r6, true, policy); }, "reserved or duplicate");
        std::cout << "PASS access, ownership, callback policy and lifecycle name guards\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
