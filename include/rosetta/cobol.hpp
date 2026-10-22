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
} // namespace rosetta
