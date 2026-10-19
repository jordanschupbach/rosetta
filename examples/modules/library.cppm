module;
#include <string>
export module rosetta_demo_native;
export import :operations;
export namespace rosetta_module {
std::string text(std::string value, bool fail);
int live() noexcept;
}
