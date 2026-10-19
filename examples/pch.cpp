#include "demo.hpp"
#ifndef ROSETTA_DEMO_PCH_BIAS
#error This fixture requires the CMake precompiled header configuration
#endif
namespace demo {
int native_pch_shift(int value) {
    if (value > std::numeric_limits<int>::max() - ROSETTA_DEMO_PCH_BIAS)
        throw std::out_of_range("PCH shift overflow");
    return value + ROSETTA_DEMO_PCH_BIAS;
}
}
