#include "demo.hpp"
#include "feature_library.hpp"
#include "async_task.hpp"
#include "advanced_values.hpp"
#include <stdexcept>
#include <cstring>
#include <limits>
#include <algorithm>
#include <thread>
#include <utility>
#include <exception>

namespace demo {
static int calls = 0;
int add(int left, int right) { ++calls; return left + right; }
double scale(double value, double factor) { return value * factor; }
bool negate(bool value) { return !value; }
const char* greet(const char* name) { return std::strcmp(name, "Lua") == 0 ? "Hello, Lua!" : "Hello!"; }
const char* nothing() { return nullptr; }
void reset() { calls = 0; }
int count() { return calls; }
void fail() { throw std::runtime_error("demo failure"); }
unsigned char byte(unsigned char value) { return value; }
unsigned long long too_large() { return std::numeric_limits<unsigned long long>::max(); }
Count apply(SelectedOperation operation, Count left, Count right) {
    switch (operation) {
    case Operation::add: return left + right;
    case Operation::multiply: return left * right;
    }
    throw std::invalid_argument("unknown operation");
}
SelectedOperation operation_identity(SelectedOperation operation) { return operation; }
Feature feature_identity(Feature feature) { return feature; }
Boundary boundary_identity(Boundary value) { return value; }
int template_max_int(int left, int right) { return feature_library::maximum<int>(left, right); }
double template_max_double(double left, double right) { return feature_library::maximum<double>(left, right); }
int template_box_int(int value) { return feature_library::Box<int>(value).value(); }
int template_sum_three(int first, int second, int third) { return feature_library::sum(first, second, third); }
static int echoes = 0;
Text echo(Text value) { ++echoes; return value; }
Text append(const Text& left, const Text& right) { return left + right; }
Text text_then_integer(Text value, int suffix) { return value + std::to_string(suffix); }
int next_default() {
    static int sequence = 0;
    if (sequence == std::numeric_limits<int>::max()) throw std::overflow_error("default sequence overflow");
    return ++sequence;
}
Text default_text(Text prefix, int value) { return prefix + std::to_string(value); }
int failing_default() { throw std::runtime_error("default expression failed"); }
Text default_failure(Text value, int suffix) { return value + std::to_string(suffix); }
std::string_view default_view(const Text& value) { return value; }
const char* default_cstring(const Text& value) { return value.c_str(); }
Text text_failure(Text) { throw std::runtime_error("text failure"); }
unsigned long long text_large_result(Text) { return std::numeric_limits<unsigned long long>::max(); }
int echo_calls() { return echoes; }
std::string_view view_identity(std::string_view value) { return value; }
Numbers reverse_numbers(const Numbers& values) { return {values.rbegin(), values.rend()}; }
std::vector<Text> reverse_words(std::vector<Text> values) { std::reverse(values.begin(), values.end()); return values; }
std::vector<std::vector<int>> matrix_identity(const std::vector<std::vector<int>>& values) { return values; }
std::array<int, 3> fixed_three(std::array<int, 3> values) { return values; }
std::optional<Text> optional_text(std::optional<Text> value) { return value; }
std::vector<bool> bits_identity(std::vector<bool> values) { return values; }

static int counters_alive = 0;
static int counters_destroyed = 0;
Counter::Counter(Value initial, std::string name) : label(std::move(name)), value_(initial) {
    if (initial < 0) throw std::invalid_argument("negative initial counter");
    ++counters_alive;
}
Counter::Counter(const Counter& other) : label(other.label), value_(other.value_), point_(other.point_) { ++counters_alive; }
Counter::Counter(Counter&& other) noexcept : label(std::move(other.label)), value_(other.value_), point_(other.point_) { ++counters_alive; }
Counter::~Counter() noexcept { --counters_alive; ++counters_destroyed; }
Counter::Value Counter::value() const { return value_; }
void Counter::add(Value amount) {
    const long long updated = static_cast<long long>(value_) + amount;
    if (updated < std::numeric_limits<Value>::min() || updated > std::numeric_limits<Value>::max())
        throw std::overflow_error("counter overflow");
    value_ = static_cast<Value>(updated);
}
Counter Counter::clone() const { return *this; }
Counter& Counter::alias() { return *this; }
const Counter& Counter::readonly() const { return *this; }
Counter* Counter::maybe_self(bool present) { return present ? this : nullptr; }
Point& Counter::point() { return point_; }
const Point& Counter::readonly_point() const { return point_; }
void Counter::fail() const { throw std::runtime_error("counter failure"); }
int Counter::live() { return counters_alive; }
int Counter::destroyed() { return counters_destroyed; }
int Counter::adjusted(Value value, Value offset) {
    const long long result = static_cast<long long>(value) + offset;
    if (result < std::numeric_limits<Value>::min() || result > std::numeric_limits<Value>::max())
        throw std::overflow_error("adjustment overflow");
    return static_cast<Value>(result);
}
void Counter::secret() { ++value_; }

static int move_only_alive = 0;
MoveOnly::MoveOnly(std::string text) : text_(std::move(text)) { ++move_only_alive; }
MoveOnly::MoveOnly(MoveOnly&& other) noexcept : text_(std::move(other.text_)) { ++move_only_alive; }
MoveOnly::~MoveOnly() noexcept { --move_only_alive; }
std::string MoveOnly::text() const { return text_; }
int MoveOnly::live() { return move_only_alive; }
int Point::sum() const {
    const long long result = static_cast<long long>(x) + y;
    if (result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max())
        throw std::overflow_error("point overflow");
    return static_cast<int>(result);
}

Counter make_counter_value(int value) { return Counter(value, "value"); }
std::unique_ptr<Counter> make_counter(int value) { return std::make_unique<Counter>(value, "unique"); }
std::unique_ptr<Counter> null_counter() { return {}; }
std::shared_ptr<Counter> make_shared_counter(int value) { return std::make_shared<Counter>(value, "shared"); }
std::shared_ptr<Counter> share_counter(std::shared_ptr<Counter> counter) { return counter; }
int counter_value(const Counter& counter) { return counter.value(); }
void counter_add(Counter& counter, int amount) { counter.add(amount); }
int counter_pointer(const Counter* counter) { return counter ? counter->value() : -1; }
Counter copy_counter(Counter counter) { return counter; }
MoveOnly make_move_only(std::string text) { return MoveOnly(std::move(text)); }
std::string inspect_move_only(MoveOnly value) { return value.text(); }

int Counter::with_callback(IntCallback callback) { const auto amount = callback(value_); add(amount); return value_; }
static int native_callback_entries = 0;
int invoke_callback(IntCallback callback, int value) { ++native_callback_entries; return callback(value); }
int callback_entries() { return native_callback_entries; }
int invoke_twice(const IntCallback& callback, int value) { return callback(callback(value)); }
int invoke_binary(std::function<int(int, int)> callback, int left, int right) { return callback(left, right); }
int compose_callbacks(IntCallback first, IntCallback second, int value) { return second(first(value)); }
void run_action(std::function<void()> callback) { callback(); }
int catch_callback(IntCallback callback, int value) {
    try { return callback(value); } catch (const std::exception&) { return -1; }
}
std::string transform_text(std::string text, TextCallback callback) { ++native_callback_entries; return callback(text); }
std::string transform_after_notify(std::function<void()> before, std::string text, TextCallback callback) {
    ++native_callback_entries;
    before();
    return callback(text);
}
std::vector<int> transform_numbers(std::function<std::vector<int>(const std::vector<int>&)> callback, std::vector<int> values) { return callback(values); }
void notify(std::function<void(std::string)> callback, std::string message) { callback(std::move(message)); }

namespace {
// A C-style callback/context facade catches exceptions before returning through
// the C-facing trampoline, then rethrows after the native operation completes.
int c_apply(int (*callback)(void*, int), void* context, int value) { return callback(context, value); }
struct CCallbackContext { IntCallback& callback; std::exception_ptr failure; };
int c_trampoline(void* opaque, int value) noexcept {
    auto& context = *static_cast<CCallbackContext*>(opaque);
    try { return context.callback(value); }
    catch (...) { context.failure = std::current_exception(); return 0; }
}
IntCallback saved_callback;
std::function<std::string()> saved_text_callback;
}
int c_style_callback(IntCallback callback, int value) {
    CCallbackContext context{callback, {}};
    const auto result = c_apply(c_trampoline, &context, value);
    if (context.failure) std::rethrow_exception(context.failure);
    return result;
}
int invoke_on_worker(IntCallback callback, int value) {
    int result = 0;
    std::exception_ptr failure;
    std::thread worker([&] { try { result = callback(value); } catch (...) { failure = std::current_exception(); } });
    worker.join();
    if (failure) std::rethrow_exception(failure);
    return result;
}
void store_callback(IntCallback callback) { saved_callback = std::move(callback); }
void store_call_only(IntCallback callback) { saved_callback = std::move(callback); }
int fire_saved(int value) {
    auto callback = saved_callback;
    if (!callback) throw std::runtime_error("no saved callback");
    return callback(value);
}
void clear_saved() { saved_callback = {}; }
void clear_saved_on_worker() {
    // std::exchange guarantees saved_callback is emptied; a moved-from
    // std::function may still hold its target (libc++ copies small targets).
    std::thread worker([callback = std::exchange(saved_callback, nullptr)]() mutable { callback = {}; });
    worker.join();
}
void store_text_callback(std::function<std::string()> callback) { saved_text_callback = std::move(callback); }
std::string fire_saved_text() { auto callback = saved_text_callback; return callback(); }
void clear_saved_text() { saved_text_callback = {}; }
int use_counter_callback(Counter& counter, IntCallback callback) { counter.add(callback(counter.value())); return counter.value(); }
int use_point_callback(Point& point, IntCallback callback) { const auto value = callback(point.sum()); point.x = value; point.y = 0; return point.sum(); }

static int event_sources_alive = 0;
EventSource::EventSource() { ++event_sources_alive; }
EventSource::~EventSource() noexcept { --event_sources_alive; }
void EventSource::set_callback(IntCallback callback) { callback_ = std::move(callback); }
void EventSource::clear_callback() noexcept { callback_ = {}; }
bool EventSource::has_callback() const { return static_cast<bool>(callback_); }
int EventSource::emit(int value) const {
    auto callback = callback_; // keep the callable alive during self-unregistration
    if (!callback) throw std::runtime_error("no event callback");
    return callback(value);
}
int EventSource::live() { return event_sources_alive; }

EntityView::EntityView(std::shared_ptr<feature_library::Entity> native) : native_(std::move(native)) {
    if (!native_) throw std::invalid_argument("entity view requires an owner");
}
std::string EntityView::kind() const { return native_->kind(); }
std::unique_ptr<NamedView> EntityView::as_named() const {
    auto cast = std::dynamic_pointer_cast<feature_library::Named>(native_);
    return cast ? std::make_unique<NamedView>(std::move(cast)) : nullptr;
}
std::unique_ptr<ValueView> EntityView::as_value() const {
    auto cast = std::dynamic_pointer_cast<feature_library::Valued>(native_);
    return cast ? std::make_unique<ValueView>(std::move(cast)) : nullptr;
}
bool EntityView::same_entity(const EntityView& other) const { return native_ == other.native_; }
int EntityView::live() { return feature_library::Entity::alive; }
NamedView::NamedView(std::shared_ptr<feature_library::Named> native) : native_(std::move(native)) {
    if (!native_) throw std::invalid_argument("named view requires an owner");
}
std::string NamedView::name() const { return native_->name(); }
EntityView NamedView::as_entity() const { return EntityView(native_); }
std::unique_ptr<ValueView> NamedView::as_value() const {
    auto cast = std::dynamic_pointer_cast<feature_library::Valued>(native_);
    return cast ? std::make_unique<ValueView>(std::move(cast)) : nullptr;
}
ValueView::ValueView(std::shared_ptr<feature_library::Valued> native) : native_(std::move(native)) {
    if (!native_) throw std::invalid_argument("value view requires an owner");
}
int ValueView::value() const { return native_->value(); }
void ValueView::set_value(int value) { native_->set_value(value); }
EntityView ValueView::as_entity() const { return EntityView(native_); }
EntityView make_diamond(std::string name, int value) {
    return EntityView(std::make_shared<feature_library::Diamond>(std::move(name), value));
}
EntityView make_named(std::string name) { return EntityView(std::make_shared<feature_library::Label>(std::move(name))); }
EntityView make_scripted(std::string name, int seed, IntCallback value_override) {
    return EntityView(std::make_shared<feature_library::Scripted>(std::move(name), seed, std::move(value_override)));
}

IntSeries::IntSeries(std::vector<int> values) : native_(std::make_shared<feature_library::Series>(std::move(values))) {}
int IntSeries::length() const { return native_->size(); }
int IntSeries::at(int index) const {
    if (index <= 0) throw std::out_of_range("series index out of range");
    return (*native_)[index - 1];
}
void IntSeries::set(int index, int value) {
    if (index <= 0) throw std::out_of_range("series index out of range");
    native_->set(index - 1, value);
}
IntSeries IntSeries::plus(const IntSeries& other) const { return IntSeries(*native_ + *other.native_); }
IntSeries IntSeries::minus(const IntSeries& other) const { return IntSeries(*native_ - *other.native_); }
IntSeries IntSeries::times(int factor) const { return IntSeries(*native_ * factor); }
IntSeries IntSeries::quotient(int divisor) const { return IntSeries(*native_ / divisor); }
IntSeries IntSeries::remainder(int divisor) const { return IntSeries(*native_ % divisor); }
IntSeries IntSeries::floor_quotient(int divisor) const { return IntSeries(native_->floor_quotient(divisor)); }
IntSeries IntSeries::floor_modulo(int divisor) const { return IntSeries(native_->floor_modulo(divisor)); }
IntSeries IntSeries::negative() const { return IntSeries(-*native_); }
IntSeries IntSeries::bits_and(const IntSeries& other) const { return IntSeries(*native_ & *other.native_); }
IntSeries IntSeries::bits_or(const IntSeries& other) const { return IntSeries(*native_ | *other.native_); }
IntSeries IntSeries::bits_xor(const IntSeries& other) const { return IntSeries(*native_ ^ *other.native_); }
IntSeries IntSeries::bits_not() const { return IntSeries(~*native_); }
IntSeries IntSeries::shift_left(int count) const { return IntSeries(*native_ << count); }
IntSeries IntSeries::shift_right(int count) const { return IntSeries(*native_ >> count); }
bool IntSeries::equals(const IntSeries& other) const { return *native_ == *other.native_; }
bool IntSeries::less(const IntSeries& other) const { return *native_ < *other.native_; }
bool IntSeries::less_equal(const IntSeries& other) const { return *native_ <= *other.native_; }
IntSeries IntSeries::invoke(int factor) const { return IntSeries((*native_)(factor)); }
std::string IntSeries::text() const { return static_cast<std::string>(*native_); }
bool IntSeries::nonempty() const { return static_cast<bool>(*native_); }
IntSeries IntSeries::clone() const { return IntSeries(native_->values()); }
SeriesCursor IntSeries::cursor() const { return SeriesCursor(std::make_shared<feature_library::SeriesCursor>(native_)); }
int IntSeries::live() { return feature_library::Series::alive; }
SeriesCursor::SeriesCursor(std::shared_ptr<feature_library::SeriesCursor> native) : native_(std::move(native)) {
    if (!native_) throw std::invalid_argument("series cursor requires an owner");
}
std::optional<int> SeriesCursor::next() { return native_->next(); }
int SeriesCursor::live() { return feature_library::SeriesCursor::alive; }

AsyncTask::AsyncTask(int value, int delay_ms, bool fail)
    : native_(std::make_unique<feature_library::AsyncTask>(value, delay_ms, fail)) {}
AsyncTask::~AsyncTask() noexcept = default;
TaskState AsyncTask::state() const { return static_cast<TaskState>(native_->state()); }
bool AsyncTask::ready() const { return native_->ready(); }
bool AsyncTask::wait(int timeout_ms) const { return native_->wait(timeout_ms); }
bool AsyncTask::cancel() { return native_->cancel(); }
int AsyncTask::result() const { return native_->result(); }
void AsyncTask::on_complete(std::function<void(int, int)> callback) { native_->on_complete(std::move(callback)); }
bool AsyncTask::dispatch() { return native_->dispatch(); }
int AsyncTask::live() { return feature_library::AsyncTask::alive.load(); }
int AsyncTask::workers() { return feature_library::AsyncTask::active_workers.load(); }

TaggedRecord::TaggedRecord() : native_(std::make_unique<feature_library::TaggedRecord>()) {}
TaggedRecord::TaggedRecord(TaggedRecord&&) noexcept = default;
TaggedRecord::~TaggedRecord() noexcept = default;
ValueKind TaggedRecord::kind() const { return native_->is_text() ? ValueKind::text : ValueKind::integer; }
int TaggedRecord::integer() const { return native_->integer(); }
std::string TaggedRecord::text() const { return native_->text(); }
void TaggedRecord::set_integer(int value) { native_->set_integer(value); }
void TaggedRecord::set_text(std::string value) { native_->set_text(std::move(value)); }
int TaggedRecord::priority() const { return native_->priority(); }
void TaggedRecord::set_priority(int value) { native_->set_priority(value); }
int TaggedRecord::mask() const { return native_->mask(); }
void TaggedRecord::set_mask(int value) { native_->set_mask(value); }
unsigned int TaggedRecord::counter() const { return native_->counter(); }
void TaggedRecord::set_counter(unsigned int value) { native_->set_counter(value); }
TaggedRecord TaggedRecord::clone() const {
    TaggedRecord result;
    result.set_priority(priority()); result.set_mask(mask()); result.set_counter(counter());
    if (kind() == ValueKind::text) result.set_text(text());
    else result.set_integer(integer());
    return result;
}
int TaggedRecord::packed_size() { return sizeof(feature_library::PackedFields); }
int TaggedRecord::live() { return feature_library::TaggedRecord::alive; }
ValueStore::ValueStore() : native_(std::make_unique<feature_library::ValueStore>()) {}
ValueStore::~ValueStore() noexcept = default;
void ValueStore::put_integer(std::string key, int value) { native_->put_integer(std::move(key), value); }
void ValueStore::put_text(std::string key, std::string value) { native_->put_text(std::move(key), std::move(value)); }
ValueKind ValueStore::kind(std::string key) const { return static_cast<ValueKind>(native_->kind(key)); }
int ValueStore::integer(std::string key) const { return native_->integer(key); }
std::string ValueStore::text(std::string key) const { return native_->text(key); }
bool ValueStore::erase(std::string key) { return native_->erase(key); }
std::vector<std::string> ValueStore::keys() const { return native_->keys(); }
void ValueStore::scratch_integer(int value) { native_->scratch_integer(value); }
void ValueStore::scratch_text(std::string value) { native_->scratch_text(std::move(value)); }
void ValueStore::scratch_pair(int value, std::string text) { native_->scratch_pair(value, std::move(text)); }
ValueKind ValueStore::scratch_kind() const { return static_cast<ValueKind>(native_->scratch_kind()); }
int ValueStore::scratch_as_integer() const { return native_->scratch_as_integer(); }
std::string ValueStore::scratch_as_text() const { return native_->scratch_as_text(); }
int ValueStore::pair_integer() const { return native_->pair_integer(); }
std::string ValueStore::pair_text() const { return native_->pair_text(); }
void ValueStore::clear_scratch() { native_->clear_scratch(); }
int ValueStore::live() { return feature_library::ValueStore::alive; }
}
