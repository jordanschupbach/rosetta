#include "r_verification.hpp"
#include "feature_library.hpp"
#include "compile_time_library.hpp"
#include "worklist_library.hpp"
#include "async_task.hpp"
#include <limits>
#include <new>
#include <numeric>

namespace r_verify {
char echo_char(char v) { return v; }
signed char echo_schar(signed char v) { return v; }
unsigned char echo_uchar(unsigned char v) { return v; }
short echo_short(short v) { return v; }
unsigned short echo_ushort(unsigned short v) { return v; }
int echo_int(int v) { return v; }
unsigned int echo_uint(unsigned int v) { return v; }
long echo_long(long v) { return v; }
unsigned long echo_ulong(unsigned long v) { return v; }
long long echo_ll(long long v) { return v; }
unsigned long long echo_ull(unsigned long long v) { return v; }
float echo_float(float v) { return v; }
double echo_double(double v) { return v; }
bool echo_bool(bool v) { return v; }
std::string echo_string(std::string v) { return v; }
bool char_is_signed() { return std::numeric_limits<char>::is_signed; }
int int_bits() { return std::numeric_limits<unsigned int>::digits; }
Count alias_identity(Count v) { return v; }
int defaulted(int v) { return v; }
void do_nothing() {}
void unknown_error() { throw 42; }
void allocation_error() { throw std::bad_alloc(); }
double nonfinite_result() { return std::numeric_limits<double>::infinity(); }
std::string embedded_nul() { return std::string("a\0b", 3); }
int maximum(int a, int b) { return feature_library::maximum(a, b); }
int template_box(int v) { return feature_library::Box<int>(v).value(); }
int variadic_sum(int a, int b, int c) {
    return feature_library::checked_int(feature_library::sum(static_cast<long long>(a), static_cast<long long>(b), static_cast<long long>(c)));
}
int constant_answer() { return feature_library::compile_time::immediate_square(6) + 6; }
int constrained_sum(int a, int b) { return feature_library::compile_time::constrained_sum(std::vector<int>{a, b}); }
int hierarchy_value(int value) {
    feature_library::Diamond diamond("R", value);
    const feature_library::Entity& entity = diamond;
    return dynamic_cast<const feature_library::Valued&>(entity).value();
}
bool hierarchy_crosscast() {
    feature_library::Diamond diamond("R", 42);
    feature_library::Named* named = &diamond;
    return dynamic_cast<feature_library::Valued*>(named) == static_cast<feature_library::Valued*>(&diamond);
}
extern "C" int rosetta_r_c_identity(int value) { return value; }

struct Series::State {
    feature_library::Series series;
    static std::vector<int> values(int first, int count) {
        if (count < 0 || count > 1024) throw std::out_of_range("series count must be between 0 and 1024");
        std::vector<int> result;
        for (int i = 0; i < count; ++i) result.push_back(feature_library::checked_int(static_cast<long long>(first) + i));
        return result;
    }
    State(int first, int count) : series(values(first, count)) {}
};
Series::Series(int first, int count) : state_(std::make_unique<State>(first, count)) {}
Series::~Series() noexcept = default;
int Series::size() const { return state_->series.size(); }
int Series::get(int index) const { return state_->series[index]; }
void Series::set(int index, int value) { state_->series.set(index, value); }
int Series::scaled_sum(int factor) const {
    const auto result = state_->series * factor;
    return feature_library::checked_int(std::accumulate(result.begin(), result.end(), 0LL));
}
std::string Series::text() const { return static_cast<std::string>(state_->series); }
int Series::live() { return feature_library::Series::alive; }

struct Worklist::State { feature_library::worklists::State data; explicit State(int order) : data(order) {} };
Worklist::Worklist(int order) : state_(std::make_unique<State>(order)) {}
Worklist::~Worklist() noexcept = default;
unsigned long long Worklist::size() const { return state_->data.size(); }
void Worklist::push(std::string value) { state_->data.push(std::move(value)); }
std::string Worklist::pop() {
    auto value = state_->data.pop();
    if (!value) throw std::out_of_range("worklist is empty");
    return *value;
}
int Worklist::live() { return feature_library::worklists::State::alive.load(); }

struct AsyncTask::State { feature_library::AsyncTask task; State(int v, int delay, bool fail) : task(v, delay, fail) {} };
AsyncTask::AsyncTask(int v, int delay, bool fail) : state_(std::make_unique<State>(v, delay, fail)) {}
AsyncTask::~AsyncTask() noexcept = default;
bool AsyncTask::ready() const { return state_->task.ready(); }
bool AsyncTask::wait(int timeout) const { return state_->task.wait(timeout); }
int AsyncTask::result() const { return state_->task.result(); }
bool AsyncTask::cancel() { return state_->task.cancel(); }
int AsyncTask::live() { return feature_library::AsyncTask::alive.load(); }
int AsyncTask::workers() { return feature_library::AsyncTask::active_workers.load(); }
} // namespace r_verify
