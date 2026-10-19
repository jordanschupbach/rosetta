#include "../demo.hpp"
import rosetta_demo_native;
import "header_unit.hpp";
namespace demo {
bool native_modules_available() { return true; }
int native_module_shift(int value) { return rosetta_module::shift<2>(value); }
std::string native_module_text(std::string value, bool fail) { return rosetta_module::text(std::move(value), fail); }
int native_module_live() { return rosetta_module::live(); }
int native_header_unit_answer() { return rosetta_header_unit::answer(); }
}
