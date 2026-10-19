#include "../demo.hpp"
#include <stdexcept>
namespace demo {
bool native_modules_available() { return false; }
int native_module_shift(int) { throw std::runtime_error("native module example disabled; enable ROSETTA_BUILD_MODULE_EXAMPLE with Clang"); }
std::string native_module_text(std::string, bool) { (void)native_module_shift(0); return {}; }
int native_module_live() { return 0; }
int native_header_unit_answer() { return native_module_shift(0); }
}
