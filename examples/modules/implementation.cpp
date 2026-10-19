module;
#include <atomic>
#include <stdexcept>
#include <string>
#include <utility>
module rosetta_demo_native;
namespace {
std::atomic<int> instances{0};
class Record {
public:
    explicit Record(std::string value) : value_(std::move(value)) { ++instances; }
    ~Record() { --instances; }
    std::string value_;
};
}
namespace rosetta_module {
std::string text(std::string value, bool fail) {
    Record record(std::move(value));
    if (fail) throw std::runtime_error("module text failure");
    return record.value_ + record.value_;
}
int live() noexcept { return instances.load(); }
}
