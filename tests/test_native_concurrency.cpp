#include "concurrency_library.hpp"
#include <iostream>

int main() {
    using namespace feature_library::concurrency;
    try {
        for (int pass = 0; pass < 10; ++pass) {
            State value(0, 0);
            JoinedThreads callers(4);
            for (int n = 0; n < 4; ++n) callers.start([&] {
                for (int i = 0; i < 1000; ++i) {
                    value.add(1);
                    value.register_exchange(static_cast<unsigned>(i));
                    (void)value.register_load();
                }
            });
            const auto result = value.parallel_add(8, 100, 1);
            callers.join();
            if (value.load() != 4800 || result.size() != 8 || State::workers.load() != 0)
                throw std::runtime_error("overlapping native writers lost updates or workers");
            const auto tls = probe(7);
            if (std::get<0>(tls) != 0 || std::get<1>(tls) != 0 || std::get<2>(tls) != 7 || std::get<3>(tls) != 0)
                throw std::runtime_error("native thread-local isolation failed");
        }
        {
            State value(std::numeric_limits<int>::max(), 0);
            bool rejected = false;
            try { value.parallel_add(8, 100, 1); }
            catch (const std::overflow_error&) { rejected = true; }
            if (!rejected || value.load() != std::numeric_limits<int>::max() || State::workers.load() != 0)
                throw std::runtime_error("native worker exception did not preserve bounds and join");
        }
        if (State::alive.load() != 0) throw std::runtime_error("native concurrency fixture leaked state");
        std::cout << "C++17 native contention, TLS, volatile access and worker errors passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
