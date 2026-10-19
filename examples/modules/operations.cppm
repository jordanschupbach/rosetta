module;
#include <limits>
#include <stdexcept>
export module rosetta_demo_native:operations;
export namespace rosetta_module {
template<int Bias> int shift(int value) {
    static_assert(Bias >= 0);
    if (value > std::numeric_limits<int>::max() - Bias)
        throw std::out_of_range("module shift overflow");
    return value + Bias;
}
}
