#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace rosetta {

// Arguments exclude argv[0]. Streams make CLI behavior testable without subprocesses.
int run_cli(const std::vector<std::string>& arguments, std::ostream& out, std::ostream& err);

} // namespace rosetta
