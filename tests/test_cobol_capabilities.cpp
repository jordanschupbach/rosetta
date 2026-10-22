#include "rosetta/frontend.hpp"
#include "rosetta/cobol.hpp"
#include "rosetta/cli.hpp"
#include "process.hpp"
#include <functional>
#include <iostream>
#include <sstream>

namespace {
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
void rejects(const std::function<void()>& action, const std::string& expected) {
    try { action(); }
    catch (const rosetta::BindingError& error) {
        require(contains(error.what(), expected), "expected '" + expected + "', got: " + error.what());
        return;
    }
    throw std::runtime_error("expected rejection: " + expected);
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 1;
    try {
        rosetta::detail::TemporaryDirectory temporary;
        const auto header = temporary.path() / "cobol api.hpp";
        auto parse = [&](const std::string& source) {
            rosetta::detail::write_file(header, source);
            return rosetta::parse(header, argv[1], {"-std=c++20"});
        };
        struct Case { const char* label; const char* source; const char* reason; bool select = false; };
        const Case cases[] = {
            {"extended numeric", "long double unsupported();", "unsupported COBOL result type"},
            {"enum", "enum class Mode { a, b };", "enum bindings are not yet supported"},
            {"overload", "int unsupported(int); int unsupported(double);", "overloads"},
            {"pointer", "void unsupported(int*);", "unsupported COBOL parameter type"},
            {"mutable reference", "void unsupported(int&);", "unsupported COBOL parameter type"},
            {"rvalue reference", "void unsupported(int&&);", "unsupported COBOL parameter type"},
            {"function pointer", "void unsupported(int (*)(int));", "unsupported COBOL parameter type"},
            {"fixed array", "void unsupported(int[4]);", "unsupported COBOL parameter type"},
            {"string view", "#include <string_view>\nvoid unsupported(std::string_view);", "unsupported COBOL parameter type"},
            {"vector", "#include <vector>\nvoid unsupported(std::vector<int>);", "unsupported COBOL parameter type"},
            {"optional", "#include <optional>\nvoid unsupported(std::optional<int>);", "unsupported COBOL parameter type"},
            {"class value", "struct Object {}; void unsupported(Object);", "unsupported COBOL parameter type"},
            {"union", "union Object { int i; double d; };", "complete, concrete", true},
            {"bitfield", "struct Object { unsigned field : 3; };", "unsupported COBOL field type", true},
            {"volatile field", "struct Object { volatile int field; };", "unsupported COBOL field type", true},
            {"constructor overload", "struct Object { Object(int); Object(double); };", "constructor overloads", true},
            {"private destructor", "class Object { ~Object(); };", "public nonthrowing destructor", true},
            {"throwing destructor", "struct Object { ~Object() noexcept(false); };", "public nonthrowing destructor", true},
            {"method overload", "struct Object { int unsupported(int); int unsupported(double); };", "method overloads", true},
            {"inheritance", "struct Base {}; struct Object : Base {};", "complete, concrete", true},
            {"abstract", "struct Object { virtual int value() = 0; };", "complete, concrete", true},
            {"template", "template<class T> T unsupported(T);", "FunctionTemplateDecl"},
            {"callback", "#include <functional>\nvoid unsupported(std::function<int(int)>);", "unsupported COBOL parameter type"},
            {"immediate", "consteval int unsupported(int x) { return x; }", "consteval"},
            {"coroutine", "#include <coroutine>\nvoid unsupported(std::coroutine_handle<>);", "unsupported COBOL parameter type"},
            {"variable", "inline constexpr int unsupported = 42;", "VarDecl"},
            {"cobol word", "int ___();", "cannot be a COBOL word"},
            {"long name", "int unsupported_with_a_name_that_is_far_too_long_for_cobol_words();", "longer than 56 characters"},
        };
        for (const auto& test : cases) {
            const auto module = parse(std::string(test.source) + "\nint sentinel();\n");
            rosetta::BindingPolicy policy; policy.explicit_class_selection = true;
            if (test.select) policy.classes["Object"] = {};
            rejects([&] { rosetta::generate_cobol(module, "api", false, policy); }, test.reason);
            const auto partial = rosetta::generate_cobol(module, "api", true, policy);
            std::string diagnostics;
            for (const auto& entry : partial.diagnostics) diagnostics += entry + '\n';
            require(contains(diagnostics, test.reason), "partial diagnostic missing");
            require(contains(partial.copybook, "       01 API-SENTINEL.\n") && contains(partial.source, "extern \"C\" int api_sentinel(") &&
                !contains(partial.copybook, "UNSUPPORTED") && !contains(partial.source, "api_unsupported"), "partial exports incorrect");
            std::cout << "PASS " << test.label << '\n';
        }
        const auto module = parse("#include <string>\nstruct Object { Object(int = 42); int get(int = 1); static int make(); int field; const int fixed = 1;"
                                  " long long wide; unsigned long long big; double real; float single; char letter; bool flag; std::string text;"
                                  " short tiny; unsigned char byte; private: int hidden(); int secret; }; int value(int = 42);"
                                  " int pair(int left, int right = 2); void none(); std::string join(std::string a, int b);"
                                  " int clash(int result, int self); void echo_text(std::string long_parameter_name); struct AsyncTask { AsyncTask(); int run(); };"
                                  " namespace ns { namespace inner { int deep(); } }");
        rosetta::BindingPolicy policy; policy.classes["Object"] = {}; policy.classes["AsyncTask"] = {};
        const auto generated = rosetta::generate_cobol(module, "api", false, policy);
        const auto& source = generated.source;
        const auto& cpy = generated.copybook;
        // Records: fields in order, with -GIVEN for defaults and -SELF for methods.
        require(contains(cpy, "       01 API-PAIR.\n          05 API-PAIR-GIVEN USAGE BINARY-LONG SIGNED VALUE 1.\n"
                              "          05 API-PAIR-LEFT USAGE BINARY-LONG SIGNED.\n          05 API-PAIR-RIGHT USAGE BINARY-LONG SIGNED.\n"
                              "          05 API-PAIR-RESULT USAGE BINARY-LONG SIGNED.\n") &&
            contains(cpy, "       01 API-NONE.\n          05 FILLER PIC X.\n") &&
            contains(cpy, "       01 API-OBJECT-GET.\n          05 API-OBJECT-GET-SELF USAGE BINARY-DOUBLE UNSIGNED.\n"
                          "          05 API-OBJECT-GET-GIVEN USAGE BINARY-LONG SIGNED VALUE 0.\n") &&
            contains(cpy, "       01 API-OBJECT-NEW.\n          05 API-OBJECT-NEW-GIVEN USAGE BINARY-LONG SIGNED VALUE 0.\n") &&
            contains(cpy, "          05 API-OBJECT-NEW-RESULT USAGE BINARY-DOUBLE UNSIGNED.\n") &&
            contains(cpy, "       01 API-OBJECT-MAKE.\n          05 API-OBJECT-MAKE-RESULT") &&
            contains(cpy, "       01 API-OBJECT-CLOSE.\n          05 API-OBJECT-CLOSE-SELF") &&
            contains(cpy, "       01 API-DEEP.\n"), "records incorrect");
        // Strings are PIC X(n) with a length; parameters named like record fields become ARG-n.
        require(contains(cpy, "          05 API-JOIN-A PIC X(256).\n          05 API-JOIN-A-LENGTH USAGE BINARY-LONG SIGNED VALUE -1.\n") &&
            contains(cpy, "          05 API-JOIN-RESULT-LENGTH USAGE BINARY-LONG SIGNED VALUE 0.\n") &&
            // A clause that would pass column 72 continues on the next line.
            contains(cpy, "          05 API-ECHO-TEXT-LONG-PARAMETER-NAME-LENGTH\n                USAGE BINARY-LONG SIGNED VALUE -1.\n") &&
            contains(cpy, "API-CLASH-ARG-1 USAGE") && contains(cpy, "API-CLASH-ARG-2 USAGE") && contains(cpy, "01 API-ASYNC-TASK-RUN."),
            "strings or renamed fields incorrect");
        require(contains(cpy, "API-OBJECT-WIDE-VALUE USAGE BINARY-DOUBLE SIGNED.") && contains(cpy, "API-OBJECT-BIG-VALUE USAGE BINARY-DOUBLE UNSIGNED.") &&
            contains(cpy, "API-OBJECT-REAL-VALUE USAGE COMP-2.") && contains(cpy, "API-OBJECT-SINGLE-VALUE USAGE COMP-1.") &&
            contains(cpy, "API-OBJECT-LETTER-VALUE PIC X.") && contains(cpy, "API-OBJECT-FLAG-VALUE USAGE BINARY-CHAR UNSIGNED.") &&
            contains(cpy, "API-OBJECT-TINY-VALUE USAGE BINARY-SHORT SIGNED.") && contains(cpy, "API-OBJECT-BYTE-VALUE USAGE BINARY-CHAR UNSIGNED."),
            "field usages incorrect");
        require(contains(cpy, "       01 API-STATUS.\n          05 API-STATUS-CODE USAGE BINARY-LONG SIGNED VALUE 0.\n             88 API-STATUS-OK VALUE 0.\n") &&
            contains(cpy, "             88 API-STATUS-CLOSED VALUE 6.\n") && contains(cpy, "          05 API-STATUS-MESSAGE PIC X(256) VALUE SPACES.\n"),
            "status record incorrect");
        // Every copybook line fits fixed format: code in columns 8-72.
        std::istringstream lines(cpy);
        for (std::string text; std::getline(lines, text);)
            require(text.empty() || (text.size() <= 72 && text.compare(0, 7, "       ") == 0 && text.size() > 7) ||
                (text.size() <= 72 && text.compare(0, 9, "      *> ") == 0), "copybook line outside columns 8-72: " + text);
        // Stubs: getters and setters, no setter for const fields, and record offsets.
        require(contains(source, "extern \"C\" int api_object_get_field(unsigned char* record, unsigned char* status)") &&
            contains(source, "extern \"C\" int api_object_set_field(") && contains(source, "extern \"C\" int api_object_get_fixed(") &&
            !contains(source, "api_object_set_fixed") && contains(source, "rosetta_cobol::guard(status, \"pair\"") &&
            contains(source, "rosetta_cobol::count(record + 0, 1, 2)") &&
            contains(source, "rosetta_cobol::integer<int>(rosetta_cobol::load<std::int32_t>(record + 8))") &&
            contains(source, "rosetta_cobol::text(record + 0, 256, record + 256)") &&
            contains(source, "rosetta_cobol::live<::Object>(rosetta_cobol::load<std::uint64_t>(record + 0), \"Object\")") &&
            contains(source, "rosetta_cobol::close<::Object>("), "C++ stubs incorrect");
        require(!contains(source, "hidden") && !contains(source, "secret") && !contains(cpy, "HIDDEN") && !contains(cpy, "SECRET"),
            "private members exposed");
        require(source == rosetta::generate_cobol(module, "api", false, policy).source &&
            cpy == rosetta::generate_cobol(module, "api", false, policy).copybook, "nondeterministic output");
        require(contains(rosetta::generate_cobol(module, "api", false, policy, 40).copybook, "05 API-JOIN-A PIC X(40).") &&
            contains(rosetta::generate_cobol(module, "api", false, policy, 40).source, "rosetta_cobol::text(record + 0, 40, record + 40)"),
            "string size incorrect");
        rejects([&] { rosetta::generate_cobol(module, "bad-name", false, policy); }, "module name");
        rejects([&] { rosetta::generate_cobol(module, "api_", false, policy); }, "module name");
        rejects([&] { rosetta::generate_cobol(module, "api", false, policy, 0); }, "string size");
        rejects([&] { rosetta::generate_cobol(module, "a_module_name_long_enough_to_overflow_names", false, policy); }, "too long");
        policy.classes["Object"].restrict_methods = true; policy.classes["Object"].methods = {"hidden"};
        rejects([&] { rosetta::generate_cobol(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].restrict_fields = true; policy.classes["Object"].fields = {"secret"};
        rejects([&] { rosetta::generate_cobol(module, "api", false, policy); }, "not public");
        policy.classes["Object"] = {}; policy.classes["Object"].borrow_self.insert("get");
        rejects([&] { rosetta::generate_cobol(module, "api", false, policy); }, "borrowed COBOL");
        policy = {}; policy.callbacks["value"][1] = rosetta::CallbackLifetime::call;
        rejects([&] { rosetta::generate_cobol(module, "api", true, policy); }, "callback or enum flag policies");
        // COBOL words are case-insensitive and namespaces are dropped, so names can collide.
        rejects([&] { rosetta::generate_cobol(parse("int Value(); int value();"), "api"); }, "COBOL name collision: API-VALUE");
        rejects([&] { rosetta::generate_cobol(parse("namespace a { int f(); } int f();"), "api"); }, "COBOL name collision: API-F");
        rejects([&] { rosetta::generate_cobol(parse("int f_result(); int f();"), "api"); }, "COBOL name collision: API-F-RESULT");
        rejects([&] { rosetta::generate_cobol(parse("int status();"), "api"); }, "COBOL name collision: API-STATUS");
        rosetta::BindingPolicy closing; closing.classes["Object"] = {};
        rejects([&] { rosetta::generate_cobol(parse("struct Object { void close(); };"), "api", false, closing); }, "COBOL name collision: API-OBJECT-CLOSE");
        rejects([&] { rosetta::generate_cobol(parse("struct Object { int count; int get_count(); };"), "api", false, closing); },
            "COBOL CALL name collision: api_object_get_count");

        parse("int value(int = 42);");
        const auto output = temporary.path() / "cobol bindings.cpp";
        const auto dep = temporary.path() / "cobol bindings.d";
        const auto ir = temporary.path() / "cobol bindings.json";
        const std::vector<std::string> args{header.string(), "--language", "cobol", "--module", "api", "--clang", argv[1],
            "-o", output.string(), "--dump-ir", ir.string(), "--depfile", dep.string()};
        std::ostringstream out, error;
        require(rosetta::run_cli(args, out, error) == 0, error.str());
        require(contains(rosetta::detail::read_file(output), "rosetta_cobol::guard") &&
            contains(rosetta::detail::read_file(temporary.path() / "api.cpy"), "01 API-VALUE.") &&
            std::filesystem::exists(ir) && contains(rosetta::detail::read_file(dep), "api.cpy:"), "COBOL CLI artifacts missing");
        auto placed = args;
        placed.insert(placed.end(), {"--cobol-output", (temporary.path() / "copy" / "x.cpy").string(), "--cobol-string-size", "32"});
        require(rosetta::run_cli(placed, out, error) == 0 &&
            contains(rosetta::detail::read_file(temporary.path() / "copy" / "x.cpy"), "01 API-VALUE."), "copybook not written to --cobol-output");
        auto bad = args; bad.insert(bad.end(), {"--cobol-string-size", "12x"});
        require(rosetta::run_cli(bad, out, error) == 1 && contains(error.str(), "decimal integer"), "bad string size accepted");
        std::vector<std::string> invalid{header.string(), "--language", "go", "--module", "api", "-o", output.string(), "--cobol-output", "x.cpy"};
        require(rosetta::run_cli(invalid, out, error) == 1 && contains(error.str(), "require --language cobol"), "COBOL-only option accepted for Go");
        invalid = {header.string(), "--language", "cobol", "--module", "api", "-o", header.string()};
        require(rosetta::run_cli(invalid, out, error) == 1, "COBOL output overwrote input");
        std::cout << "PASS COBOL records, usages, strings, defaults, access, names, collisions, layout, CLI and artifacts\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
