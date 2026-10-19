#include "demo.hpp"
#include "concurrency_library.hpp"

namespace demo {
std::pair<int, int> ConcurrentState::limits() { return {std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}; }
int ConcurrentState::register_bits() { return std::numeric_limits<unsigned int>::digits; }
int thread_local_get() { return feature_library::concurrency::local_value; }
void thread_local_set(int value) { feature_library::concurrency::local_value = value; }
std::tuple<int, int, int, int> thread_local_probe(int worker_value) { return feature_library::concurrency::probe(worker_value); }
ConcurrentState::ConcurrentState(int counter, unsigned int register_value)
    : native_(std::make_shared<feature_library::concurrency::State>(counter, register_value)) {}
ConcurrentState::ConcurrentState(std::shared_ptr<feature_library::concurrency::State> native) : native_(std::move(native)) {}
int ConcurrentState::load() const { return native_->load(); }
void ConcurrentState::store(int value) { native_->store(value); }
int ConcurrentState::add(int delta) { return native_->add(delta); }
std::tuple<bool, int> ConcurrentState::compare_exchange(int expected, int desired) { return native_->compare_exchange(expected, desired); }
unsigned int ConcurrentState::register_load() const { return native_->register_load(); }
void ConcurrentState::register_store(unsigned int value) { native_->register_store(value); }
unsigned int ConcurrentState::register_exchange(unsigned int value) { return native_->register_exchange(value); }
std::vector<int> ConcurrentState::parallel_add(int workers, int iterations, int delta) { return native_->parallel_add(workers, iterations, delta); }
ConcurrentState ConcurrentState::share() const { return ConcurrentState(native_); }
int ConcurrentState::live() { return feature_library::concurrency::State::alive.load(); }
int ConcurrentState::workers() { return feature_library::concurrency::State::workers.load(); }
} // namespace demo
