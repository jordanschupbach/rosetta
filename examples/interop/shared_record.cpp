#include "shared_record.hpp"
#include <atomic>
#include <charconv>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string_view>

namespace rosetta_example::exchange {
namespace {
struct Registry {
    std::mutex mutex;
    std::uint64_t next = 0;
    std::map<std::uint64_t,std::weak_ptr<State>> objects;
};
Registry& registry() { static Registry result; return result; }
std::atomic<int> instances{0};
constexpr char prefix[] = "rosetta.exchange.record.v1:";
}
class State {
public:
    std::mutex mutex;
    std::string text;
    int number;
    std::uint64_t id = 0;
    State(std::string label, int value) : text(std::move(label)), number(value) { ++instances; }
    State(const State&) = delete;
    ~State() noexcept {
        if (id) {
            auto& records = registry();
            std::lock_guard<std::mutex> lock(records.mutex);
            records.objects.erase(id);
        }
        --instances;
    }
};
std::shared_ptr<State> create(std::string text, int number) {
    auto state = std::make_shared<State>(std::move(text),number);
    auto& records = registry();
    std::lock_guard<std::mutex> lock(records.mutex);
    if (records.next == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("exchange ticket space exhausted");
    const auto id = ++records.next; // failed allocations consume an ID; IDs never alias later objects
    records.objects.emplace(id,state);
    state->id = id;
    return state;
}
std::shared_ptr<State> import_ticket(const std::string& token) {
    const std::string_view view(token);
    constexpr auto prefix_size = sizeof(prefix)-1;
    if (view.substr(0,prefix_size) != prefix) throw std::invalid_argument("exchange ticket type/version mismatch");
    const auto digits = view.substr(prefix_size);
    std::uint64_t id = 0;
    const auto parsed = std::from_chars(digits.data(),digits.data()+digits.size(),id);
    if (digits.empty() || id == 0 || digits.front() == '0' || parsed.ec != std::errc{} || parsed.ptr != digits.data()+digits.size())
        throw std::invalid_argument("malformed exchange ticket");
    std::shared_ptr<State> state;
    {
        auto& records = registry();
        std::lock_guard<std::mutex> lock(records.mutex);
        const auto found = records.objects.find(id);
        if (found != records.objects.end()) state = found->second.lock();
    }
    if (!state) throw std::runtime_error("exchange ticket expired or unknown");
    return state;
}
std::string ticket(const std::shared_ptr<State>& state) {
    char digits[std::numeric_limits<std::uint64_t>::digits10+2];
    const auto result = std::to_chars(digits,digits+sizeof(digits),state->id);
    return std::string(prefix)+std::string(digits,result.ptr);
}
std::string text(const std::shared_ptr<State>& state) { std::lock_guard<std::mutex> lock(state->mutex); return state->text; }
int number(const std::shared_ptr<State>& state) { std::lock_guard<std::mutex> lock(state->mutex); return state->number; }
void set_text(const std::shared_ptr<State>& state, std::string text) { std::lock_guard<std::mutex> lock(state->mutex); state->text=std::move(text); }
void set_number(const std::shared_ptr<State>& state, int number) { std::lock_guard<std::mutex> lock(state->mutex); state->number=number; }
int shift(const std::shared_ptr<State>& state, int delta) {
    std::lock_guard<std::mutex> lock(state->mutex);
    const auto result = static_cast<long long>(state->number)+delta;
    if (result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max()) throw std::out_of_range("exchange integer overflow");
    state->number = static_cast<int>(result);
    return state->number;
}
int live() { return instances.load(); }
int tickets() {
    auto& records=registry(); std::lock_guard<std::mutex> lock(records.mutex);
    if (records.objects.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::overflow_error("too many exchange tickets");
    return static_cast<int>(records.objects.size());
}
}
