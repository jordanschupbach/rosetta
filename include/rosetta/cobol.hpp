#pragma once
#include "rosetta/model.hpp"
#include "rosetta/policy.hpp"

namespace rosetta {
struct GeneratedCobolSource {
    std::string source;    // C++ stubs COBOL CALLs with a parameter record and the status record
    std::string copybook;  // NAME.cpy: the status record and one parameter record per call
    std::vector<std::string> diagnostics;
};
// STRING_SIZE is the byte capacity of every string field (PIC X(n)).
GeneratedCobolSource generate_cobol(const Module& module, const std::string& name, bool skip_unsupported = false,
                                    const BindingPolicy& policy = {}, std::size_t string_size = 256);

// The full COBOL API (--cobol-api full): handles for objects and containers, OCCURS tables,
// overloads, defaults, enums, callbacks and COBOL overrides of virtual methods, with the C++
// documentation as *>> doc comments. `copybook` goes into WORKING-STORAGE, `callbacks` into the
// LINKAGE SECTION of programs C++ calls.
struct CobolFullOptions {
    std::size_t parts = 1;             // > 1: further translation units in `parts`
    std::string prefix;                // word prefix of every name (default: the module name)
    std::size_t string_size = 256;     // bytes of each string field
    std::size_t result_size = 0;       // bytes of each string result field (0: string_size)
    std::size_t table_size = 1000;     // OCCURS capacity of the -TABLE records
    std::size_t message_size = 512;    // bytes of the status message
    std::string callbacks_name;        // the callbacks copybook's file name, for comments
};
struct GeneratedCobolFullSource {
    std::string source;
    std::string copybook;
    std::string callbacks;
    std::vector<std::string> diagnostics;
    std::vector<std::string> parts;
};
GeneratedCobolFullSource generate_cobol_full(const Module& module, const std::string& name, bool skip_unsupported = false,
                                             const BindingPolicy& policy = {}, const CobolFullOptions& options = {});
} // namespace rosetta
