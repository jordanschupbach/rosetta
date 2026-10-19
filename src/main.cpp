#include "rosetta/cli.hpp"
#include <iostream>

int main(int argc, char** argv) {
    return rosetta::run_cli({argv + 1, argv + argc}, std::cout, std::cerr);
}
