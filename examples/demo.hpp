#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <functional>
#include <tuple>
#include <utility>
#include <map>
#include <unordered_map>
#include <variant>
#include <deque>
#include <list>
#include <forward_list>
#include <set>
#include <unordered_set>

namespace feature_library { struct Entity; struct Named; struct Valued; class Series; class SeriesCursor; class AsyncTask; class TaggedRecord; class ValueStore; class ByteBufferStorage; class ByteSlice; namespace calls { class Object; } }
namespace feature_library::errors { template<class T> class Result; }
namespace feature_library::ownership { class UniqueSlot; class SharedSlot; class WeakSlot; }
namespace feature_library::coroutines { class Sequence; }
namespace feature_library::time { class Point; }
namespace feature_library::files { class Workspace; }
namespace feature_library::worklists { class State; }
namespace feature_library::concurrency { class State; }
namespace feature_library::members { class Object; class Selector; }
namespace feature_library::abi { class Owner; }
namespace feature_library::forms { class Stationary; }
namespace feature_library::callables { class Slot; }
namespace feature_library::class_callbacks { struct Record; class Frame; }
namespace feature_library::awaitables { struct Control; class Executor; }

namespace demo {
enum class AwaitPhase { pending, succeeded, failed, cancelled };
class AwaitTask {
public:
    explicit AwaitTask(std::shared_ptr<feature_library::awaitables::Control> task);
    AwaitPhase state() const;
    std::string result() const;
    bool cancel();
    AwaitTask share() const;
    static int live();
private:
    std::shared_ptr<feature_library::awaitables::Control> native_;
};
class AwaitExecutor {
public:
    AwaitExecutor();
    AwaitTask submit(int seed, int steps, std::string prefix, bool fail, std::function<int(int)> callback);
    bool run_one();
    int pending() const;
    AwaitExecutor share() const;
    static int live();
    static int frames();
    static int bodies();
    static unsigned long long allocations();
    static unsigned long long releases();
private:
    explicit AwaitExecutor(std::shared_ptr<feature_library::awaitables::Executor> executor);
    std::shared_ptr<feature_library::awaitables::Executor> native_;
};
class CallbackRecord {
    friend class ClassCallbackFrame;
public:
    CallbackRecord(std::string text, int number);
    std::string text() const;
    int number() const;
    void set_text(std::string text);
    void set_number(int number);
    CallbackRecord copy() const;
    const CallbackRecord& readonly() const;
    static int live();
private:
    explicit CallbackRecord(std::shared_ptr<feature_library::class_callbacks::Record> value);
    std::shared_ptr<feature_library::class_callbacks::Record> native_;
};
class ClassCallbackFrame {
public:
    ClassCallbackFrame();
    CallbackRecord argument() const;
    void set_result(const CallbackRecord& value);
    CallbackRecord transform(const CallbackRecord& input, std::function<void()> callback);
    int depth() const;
    static int live();
private:
    std::shared_ptr<feature_library::class_callbacks::Frame> native_;
};
class NativeCallable {
public:
    explicit NativeCallable(std::shared_ptr<feature_library::callables::Slot> slot);
    std::string invoke(int value);
    bool empty() const;
    void clear();
    bool same_slot(const NativeCallable& other) const;
    NativeCallable copy() const;
    NativeCallable share() const;
    static int live();
    static int captures();
    static int targets();
private:
    std::shared_ptr<feature_library::callables::Slot> native_;
};
NativeCallable make_native_capture(std::string prefix, int seed);
NativeCallable make_native_bound(std::string prefix, int seed);
NativeCallable make_native_pointer();
NativeCallable make_native_empty();
NativeCallable make_native_callback(std::function<std::string(int)> callback);
class NativeStationary {
public:
    NativeStationary(std::string text, bool fail);
    std::string text() const;
    unsigned long long inspections() const;
    bool same(const NativeStationary& other) const;
    NativeStationary share() const;
    const NativeStationary& readonly() const;
    static int live();
    static int setting();
    static void set_setting(int value);
private:
    explicit NativeStationary(std::shared_ptr<feature_library::forms::Stationary> native);
    std::shared_ptr<feature_library::forms::Stationary> native_;
};
std::map<std::string, bool> native_storage_traits();
int native_internal_add(int left, int right);
std::array<std::array<int, 3>, 2> native_matrix_shift(std::array<std::array<int, 3>, 2> values, int bias);
bool native_modules_available();
int native_module_shift(int value);
std::string native_module_text(std::string value, bool fail);
int native_module_live();
int native_header_unit_answer();
int native_pch_shift(int value);
enum class BoundaryOperation { add = 1, multiply = 2 };
std::map<std::string, unsigned long long> native_abi_info();
class NativeBoundary {
public:
    NativeBoundary(BoundaryOperation operation, long long parameter, std::string payload);
    long long apply(long long value) const;
    long long with_callback(std::function<long long(long long)> callback, long long value) const;
    std::string payload() const;
    NativeBoundary share() const;
    static unsigned long long live();
private:
    explicit NativeBoundary(std::shared_ptr<feature_library::abi::Owner> native);
    std::shared_ptr<feature_library::abi::Owner> native_;
};
enum class MemberChoice { none = 0, left = 1, right = 2, label = 3, measure = 4, add = 5, null_method = 6 };
enum class MemberKind { none = 0, integer = 1, text = 2, const_method = 3, mutable_method = 4 };
class MemberObject {
    friend class MemberSelector;
public:
    MemberObject(int left, int right, std::string text, bool derived);
    const MemberObject& readonly() const;
    MemberObject share() const;
    static int live();
private:
    explicit MemberObject(std::shared_ptr<feature_library::members::Object> native);
    std::shared_ptr<feature_library::members::Object> native_;
};
class MemberSelector {
public:
    explicit MemberSelector(MemberChoice choice);
    bool null() const;
    MemberKind kind() const;
    int read_integer(const MemberObject& object) const;
    void write_integer(MemberObject& object, int value) const;
    std::string read_text(const MemberObject& object) const;
    void write_text(MemberObject& object, std::string text) const;
    int invoke_read(const MemberObject& object, int value) const;
    int invoke_write(MemberObject& object, int value) const;
    MemberSelector copy() const;
    static int live();
private:
    explicit MemberSelector(std::shared_ptr<feature_library::members::Selector> native);
    std::shared_ptr<feature_library::members::Selector> native_;
};
int thread_local_get();
void thread_local_set(int value);
std::tuple<int, int, int, int> thread_local_probe(int worker_value);
class ConcurrentState {
public:
    static std::pair<int, int> limits();
    static int register_bits();
    ConcurrentState(int counter, unsigned int register_value);
    int load() const;
    void store(int value);
    int add(int delta);
    std::tuple<bool, int> compare_exchange(int expected, int desired);
    unsigned int register_load() const;
    void register_store(unsigned int value);
    unsigned int register_exchange(unsigned int value);
    std::vector<int> parallel_add(int workers, int iterations, int delta);
    ConcurrentState share() const;
    static int live();
    static int workers();
private:
    explicit ConcurrentState(std::shared_ptr<feature_library::concurrency::State> native);
    std::shared_ptr<feature_library::concurrency::State> native_;
};
enum class WorkOrder { stack = 1, queue = 2, highest = 3, lowest = 4 };
class Worklist {
public:
    explicit Worklist(WorkOrder order);
    WorkOrder order() const;
    unsigned long long size() const;
    void push(std::string text);
    std::optional<std::string> peek() const;
    std::optional<std::string> pop();
    std::vector<std::string> snapshot() const;
    Worklist clone() const;
    Worklist share() const;
    static int live();
private:
    explicit Worklist(std::shared_ptr<feature_library::worklists::State> native);
    std::shared_ptr<feature_library::worklists::State> native_;
};
std::string path_normalize(std::string bytes);
std::string path_join(std::string base, std::string child);
std::vector<std::string> path_components(std::string bytes);
std::tuple<std::string, std::string, std::string> path_parts(std::string bytes);
class FileWorkspace {
public:
    FileWorkspace();
    FileWorkspace share() const;
    std::string location() const;
    bool exists(std::string relative = "") const;
    void directories(std::string relative);
    void write(std::string relative, std::string bytes);
    std::string read(std::string relative) const;
    unsigned long long size(std::string relative) const;
    std::vector<std::string> entries(std::string relative = "", bool recursive = false) const;
    void rename(std::string from, std::string to);
    bool erase(std::string relative, bool recursive = false);
    static int live();
    static int cleanup_errors();
private:
    explicit FileWorkspace(std::shared_ptr<feature_library::files::Workspace> native);
    std::shared_ptr<feature_library::files::Workspace> native_;
};
using Count = int;
enum class Operation : unsigned char { add = 1, multiply = 2, plus = add };
enum Feature { none = 0, classes = 1 << 0, templates = 1 << 1, callbacks = 1 << 2 };
enum class ByteFlags : unsigned char { low = 1, high = 128, alias = low, reserved = 2 };
enum class SignedFlags : signed char { first = 1, second = 2, invalid = -1 };
enum class WideFlags : unsigned long long { low = 1, high = 1ULL << 62 };
enum class BoolFlags : bool { enabled = true };
enum class Boundary : long long {
    minimum = (-9223372036854775807LL - 1), negative = -7, next,
    maximum = 9223372036854775807LL
};
using SelectedOperation = Operation;
using IntCallback = std::function<int(int)>;
using TextCallback = std::function<std::string(const std::string&)>;

int add(int left, int right);
double scale(double value, double factor = 2.0);
bool negate(bool value);
const char* greet(const char* name);
const char* nothing();
void reset();
int count();
void fail();
unsigned char byte(unsigned char value);
unsigned long long too_large();
Count apply(SelectedOperation operation, Count left, Count right);
SelectedOperation operation_identity(SelectedOperation operation = Operation::add);
Feature feature_identity(Feature feature);
Feature combine_features(Feature left, Feature right);
bool has_features(Feature value, Feature required);
ByteFlags byte_flags(ByteFlags value);
SignedFlags signed_flags(SignedFlags value);
WideFlags wide_flags(WideFlags value);
BoolFlags bool_flags(BoolFlags value);
std::string flags_text(std::string value, ByteFlags flags);
Boundary boundary_identity(Boundary value);

// Concrete facades instantiate the C++ templates; no Lua API appears in them.
int template_max_int(int left, int right);
double template_max_double(double left, double right);
int template_box_int(int value);
int template_sum_three(int first, int second, int third);

using Text = std::string;
using Numbers = std::vector<int>;
Text echo(Text value);
Text append(const Text& left, const Text& right);
Text text_then_integer(Text value = Text(128, 'd'), int suffix = 42);
int next_default();
Text default_text(Text prefix = Text(128, 'd'), int value = next_default());
int failing_default();
Text default_failure(Text value = Text(128, 'd'), int suffix = failing_default());
std::string_view default_view(const Text& value = Text(128, 'v'));
const char* default_cstring(const Text& value = Text(128, 'p'));
Text text_failure(Text value);
unsigned long long text_large_result(Text value);
int echo_calls();
std::string_view view_identity(std::string_view value);
Numbers reverse_numbers(const Numbers& values);
std::vector<Text> reverse_words(std::vector<Text> values);
std::vector<std::vector<int>> matrix_identity(const std::vector<std::vector<int>>& values);
std::array<int, 3> fixed_three(std::array<int, 3> values);
std::optional<Text> optional_text(std::optional<Text> value = Text(128, 'o'));
std::vector<bool> bits_identity(std::vector<bool> values);

struct Point {
    int x = 0;
    int y = 0;
    const int version = 1;
    int sum() const;
};

class Counter {
public:
    using Value = int;
    explicit Counter(Value initial, std::string label = std::string(128, 'c'));
    Counter(const Counter& other);
    Counter(Counter&& other) noexcept;
    ~Counter() noexcept;
    Value value() const;
    void add(Value amount = 1);
    Counter clone() const;
    Counter& alias();
    const Counter& readonly() const;
    Counter* maybe_self(bool present);
    Point& point();
    const Point& readonly_point() const;
    void fail() const;
    int with_callback(IntCallback callback);
    static int live();
    static int destroyed();
    static int adjusted(Value value, Value offset = 2);
    std::string label;
private:
    Value value_;
    Point point_;
    void secret();
};

class MoveOnly {
public:
    explicit MoveOnly(std::string text);
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&& other) noexcept;
    ~MoveOnly() noexcept;
    std::string text() const;
    static int live();
private:
    std::string text_;
};

Counter make_counter_value(int value);
std::unique_ptr<Counter> make_counter(int value);
std::unique_ptr<Counter> null_counter();
std::shared_ptr<Counter> make_shared_counter(int value);
std::shared_ptr<Counter> share_counter(std::shared_ptr<Counter> counter);
int counter_value(const Counter& counter);
void counter_add(Counter& counter, int amount);
int counter_pointer(const Counter* counter);
Counter copy_counter(Counter counter);
MoveOnly make_move_only(std::string text);
std::string inspect_move_only(MoveOnly value);

int invoke_callback(IntCallback callback, int value = 41);
int callback_entries();
int invoke_twice(const IntCallback& callback, int value);
int invoke_binary(std::function<int(int, int)> callback, int left, int right);
int compose_callbacks(IntCallback first, IntCallback second, int value);
void run_action(std::function<void()> callback);
int catch_callback(IntCallback callback, int value);
std::string transform_text(std::string text, TextCallback callback);
std::string transform_after_notify(std::function<void()> before, std::string text, TextCallback callback);
std::vector<int> transform_numbers(std::function<std::vector<int>(const std::vector<int>&)> callback, std::vector<int> values);
void notify(std::function<void(std::string)> callback, std::string message);
int c_style_callback(IntCallback callback, int value);
int invoke_on_worker(IntCallback callback, int value);
void store_callback(IntCallback callback);
void store_call_only(IntCallback callback);
int fire_saved(int value);
void clear_saved();
void clear_saved_on_worker();
void store_text_callback(std::function<std::string()> callback);
std::string fire_saved_text();
void clear_saved_text();
int use_counter_callback(Counter& counter, IntCallback callback);
int use_point_callback(Point& point, IntCallback callback);

class EventSource {
public:
    EventSource();
    ~EventSource() noexcept;
    void set_callback(IntCallback callback);
    void clear_callback() noexcept;
    bool has_callback() const;
    int emit(int value) const;
    static int live();
private:
    IntCallback callback_;
};

// These facades retain a shared native hierarchy independently of Lua handles.
// Conversions use C++ casts, including virtual-base adjustment; no ABI offsets
// or Lua types are part of the reusable adapter interface.
class NamedView;
class ValueView;
class EntityView {
public:
    explicit EntityView(std::shared_ptr<feature_library::Entity> native);
    std::string kind() const;
    std::unique_ptr<NamedView> as_named() const;
    std::unique_ptr<ValueView> as_value() const;
    bool same_entity(const EntityView& other) const;
    static int live();
private:
    std::shared_ptr<feature_library::Entity> native_;
};
class NamedView {
public:
    explicit NamedView(std::shared_ptr<feature_library::Named> native);
    std::string name() const;
    EntityView as_entity() const;
    std::unique_ptr<ValueView> as_value() const;
private:
    std::shared_ptr<feature_library::Named> native_;
};
class ValueView {
public:
    explicit ValueView(std::shared_ptr<feature_library::Valued> native);
    int value() const;
    void set_value(int value);
    EntityView as_entity() const;
private:
    std::shared_ptr<feature_library::Valued> native_;
};
EntityView make_diamond(std::string name, int value);
EntityView make_named(std::string name);
EntityView make_scripted(std::string name, int seed, IntCallback value_override);

// Named adapters for native operators; the Lua protocol layer wraps these same
// operations without putting target-language details in the C++ interface.
class SeriesCursor;
class IntSeries {
public:
    explicit IntSeries(std::vector<int> values);
    int length() const;
    int at(int index) const;
    void set(int index, int value);
    IntSeries plus(const IntSeries& other) const;
    IntSeries minus(const IntSeries& other) const;
    IntSeries times(int factor) const;
    IntSeries quotient(int divisor) const;
    IntSeries remainder(int divisor) const;
    IntSeries floor_quotient(int divisor) const;
    IntSeries floor_modulo(int divisor) const;
    IntSeries negative() const;
    IntSeries bits_and(const IntSeries& other) const;
    IntSeries bits_or(const IntSeries& other) const;
    IntSeries bits_xor(const IntSeries& other) const;
    IntSeries bits_not() const;
    IntSeries shift_left(int count) const;
    IntSeries shift_right(int count) const;
    bool equals(const IntSeries& other) const;
    bool less(const IntSeries& other) const;
    bool less_equal(const IntSeries& other) const;
    IntSeries invoke(int factor) const;
    std::string text() const;
    bool nonempty() const;
    IntSeries clone() const;
    SeriesCursor cursor() const;
    static int live();
private:
    std::shared_ptr<feature_library::Series> native_;
};
class SeriesCursor {
public:
    explicit SeriesCursor(std::shared_ptr<feature_library::SeriesCursor> native);
    std::optional<int> next();
    static int live();
private:
    std::shared_ptr<feature_library::SeriesCursor> native_;
};

enum class TaskState { running = 0, succeeded = 1, failed = 2, cancelled = 3 };
class AsyncTask {
public:
    AsyncTask(int value, int delay_ms, bool fail);
    AsyncTask(const AsyncTask&) = delete;
    AsyncTask& operator=(const AsyncTask&) = delete;
    ~AsyncTask() noexcept;
    TaskState state() const;
    bool ready() const;
    bool wait(int timeout_ms) const;
    bool cancel();
    int result() const;
    void on_complete(std::function<void(int, int)> callback);
    bool dispatch();
    static int live();
    static int workers();
private:
    std::unique_ptr<feature_library::AsyncTask> native_;
};

enum class ValueKind { missing = 0, integer = 1, text = 2, pair = 3 };
class TaggedRecord {
public:
    TaggedRecord();
    TaggedRecord(TaggedRecord&&) noexcept;
    TaggedRecord(const TaggedRecord&) = delete;
    ~TaggedRecord() noexcept;
    ValueKind kind() const;
    int integer() const;
    std::string text() const;
    void set_integer(int value);
    void set_text(std::string value);
    int priority() const;
    void set_priority(int value);
    int mask() const;
    void set_mask(int value);
    unsigned int counter() const;
    void set_counter(unsigned int value);
    TaggedRecord clone() const;
    static int packed_size();
    static int live();
private:
    std::unique_ptr<feature_library::TaggedRecord> native_;
};
class ValueStore {
public:
    ValueStore();
    ValueStore(const ValueStore&) = delete;
    ~ValueStore() noexcept;
    void put_integer(std::string key, int value);
    void put_text(std::string key, std::string value);
    ValueKind kind(std::string key) const;
    int integer(std::string key) const;
    std::string text(std::string key) const;
    bool erase(std::string key);
    std::vector<std::string> keys() const;
    void scratch_integer(int value);
    void scratch_text(std::string value);
    void scratch_pair(int value, std::string text);
    ValueKind scratch_kind() const;
    int scratch_as_integer() const;
    std::string scratch_as_text() const;
    int pair_integer() const;
    std::string pair_text() const;
    void clear_scratch();
    static int live();
private:
    std::unique_ptr<feature_library::ValueStore> native_;
};

int constexpr_answer();
int immediate_answer();
int initialized_constant();
bool trait_is_integral();
bool template_variable_pointer();
int template_classify_int();
int template_classify_pointer();
int template_add_bias(int value);
std::array<int, 3> template_pattern(int start);
int template_template_box(int value);
std::string template_alias_text(std::string value);
int deduced_box(int value);
int constrained_sum(std::vector<int> values);
int sfinae_double(int value);
int macro_bias();
int macro_apply(int value);
bool concepts_available();
int literal_answer();
int compare_ints(int left, int right);

class ByteSlice;
class ByteBuffer {
public:
    explicit ByteBuffer(std::string bytes);
    int size() const;
    std::string read() const;
    void write(int offset, std::string bytes);
    void append(std::string bytes);
    void resize(int size, unsigned char fill);
    ByteSlice slice(int offset, int length, bool readonly) const;
    ByteBuffer share() const;
    static int live();
private:
    explicit ByteBuffer(std::shared_ptr<feature_library::ByteBufferStorage> storage);
    std::shared_ptr<feature_library::ByteBufferStorage> native_;
};
class ByteSlice {
public:
    explicit ByteSlice(std::shared_ptr<feature_library::ByteSlice> native);
    int size() const;
    bool readonly() const;
    std::string read() const;
    void write(int offset, std::string bytes);
    void xor_bytes(unsigned char mask);
    static int live();
private:
    std::shared_ptr<feature_library::ByteSlice> native_;
};
std::optional<unsigned int> decode_u16(std::string bytes);
std::vector<unsigned int> decode_u16_with_count(std::string bytes);
std::string out_buffer(int size, unsigned char fill);
int inout_increment(int value);

int overload_integer(int value);
double overload_real(double value);
std::string overload_text(std::string value);
int signed_choice(int value);
int unsigned_choice(unsigned int value);
int call_int_min();
int call_int_max();
unsigned int call_uint_max();
int default_scale(int value);
int explicit_scale(int value, int factor);
int default_factor_calls();
int variadic_sum(std::vector<int> values);
int initializer_sum(std::array<int, 3> values);
class CallObject {
public:
    explicit CallObject(std::unique_ptr<feature_library::calls::Object> native);
    CallObject(const CallObject&) = delete;
    ~CallObject() noexcept;
    int adjust_mutable(int amount);
    int adjust_readonly(int amount) const;
    int take();
    int as_integer() const;
    int secret() const;
    const CallObject& readonly() const;
    static int live();
private:
    std::unique_ptr<feature_library::calls::Object> native_;
};
std::unique_ptr<CallObject> make_call_default();
std::unique_ptr<CallObject> make_call_number(int value);
std::unique_ptr<CallObject> make_call_text(std::string value);

enum class ErrorKind { none, invalid_input, arithmetic, system, allocation, callback, unknown, nested };
class IntResult {
public:
    explicit IntResult(std::unique_ptr<feature_library::errors::Result<int>> result);
    IntResult(IntResult&&) noexcept;
    IntResult(const IntResult&) = delete;
    ~IntResult() noexcept;
    bool ok() const;
    int value() const;
    int value_or(int fallback) const;
    ErrorKind error_kind() const;
    int code() const;
    std::string category() const;
    std::string message() const;
    std::vector<std::string> causes() const;
    static int live();
private:
    std::unique_ptr<feature_library::errors::Result<int>> native_;
};
IntResult parse_result(std::string text);
IntResult divide_result(int left, int right);
IntResult status_divide_result(int left, int right);
IntResult system_result(bool fail);
IntResult error_code_result(bool fail);
IntResult status_result(bool fail, std::string detail);
IntResult nested_result();
IntResult unknown_result();
IntResult allocation_result();
IntResult callback_result(IntCallback callback, int value);

class SharedResource;
class ResourceSlot {
public:
    ResourceSlot();
    ResourceSlot(std::string text, int tag, bool fail);
    ResourceSlot(const ResourceSlot&) = delete;
    ~ResourceSlot() noexcept;
    bool has_value() const;
    std::string text() const;
    bool aligned() const;
    int deleter_tag() const;
    void reset();
    void move_to(ResourceSlot& destination);
    void promote_to(SharedResource& destination);
    static unsigned long long live();
    static unsigned long long allocations();
    static unsigned long long releases();
    static unsigned long long deleted();
    static int last_deleter_tag();
    static int alignment();
private:
    std::unique_ptr<feature_library::ownership::UniqueSlot> native_;
};
class WeakResource;
class SharedResource {
    friend class ResourceSlot;
    friend class WeakResource;
public:
    SharedResource();
    SharedResource(const SharedResource&) = delete;
    ~SharedResource() noexcept;
    bool has_value() const;
    std::string text() const;
    int deleter_tag() const;
    void reset();
    std::unique_ptr<SharedResource> clone() const;
    std::unique_ptr<WeakResource> weak() const;
private:
    explicit SharedResource(feature_library::ownership::SharedSlot native);
    std::unique_ptr<feature_library::ownership::SharedSlot> native_;
};
class WeakResource {
public:
    explicit WeakResource(feature_library::ownership::WeakSlot native);
    WeakResource(const WeakResource&) = delete;
    ~WeakResource() noexcept;
    bool expired() const;
    std::unique_ptr<SharedResource> lock() const;
private:
    std::unique_ptr<feature_library::ownership::WeakSlot> native_;
};
std::unique_ptr<ResourceSlot> empty_resource();
std::unique_ptr<ResourceSlot> make_resource(std::string text, int tag, bool fail);

class Generator {
public:
    Generator(int start, int count, std::string prefix, int fail_after);
    Generator(const Generator&) = delete;
    ~Generator() noexcept;
    bool advance();
    std::string value() const;
    bool done() const;
    bool cancelled() const;
    bool cancel();
    static unsigned long long frames();
    static unsigned long long bodies();
    static unsigned long long allocations();
    static unsigned long long releases();
private:
    std::unique_ptr<feature_library::coroutines::Sequence> native_;
};

std::vector<unsigned int> unicode_decode(std::string bytes);
std::string unicode_encode(std::vector<unsigned int> points);
std::vector<unsigned int> unicode_utf16(std::string bytes);
std::string unicode_from_utf16(std::vector<unsigned int> units);
std::string unicode_char8(std::string bytes);
std::string unicode_wide(std::string bytes);
unsigned long long unicode_wide_length(std::string bytes);
unsigned long long unicode_scalar_count(std::string bytes);
int unicode_wchar_bits();

std::string u64_decimal(std::string text);
std::string u64_add(std::string left, std::string right);
bool int128_available();
std::string signed128_decimal(std::string text);
std::string unsigned128_decimal(std::string text);
std::string signed128_add(std::string left, std::string right);
std::string unsigned128_add(std::string left, std::string right);
std::string signed128_multiply(std::string left, std::string right);
std::string unsigned128_multiply(std::string left, std::string right);
std::string long_double_decimal(std::string text);
std::string long_double_add(std::string left, std::string right);
std::string long_double_multiply(std::string left, std::string right);
int long_double_compare(std::string left, std::string right);
bool long_double_signbit(std::string text);
int long_double_precision();
std::vector<std::string> long_double_bounds();

// Explicit integral-duration and clock-domain adapters. TimePoint ticks are ms.
long long time_convert(long long ticks, std::string from, std::string to, std::string rounding = "truncate");
class TimePoint {
public:
    explicit TimePoint(long long ticks, bool steady = false);
    long long ticks() const;
    bool steady() const;
    TimePoint shifted(long long delta) const;
    long long difference(const TimePoint& other) const;
    bool before(const TimePoint& other) const;
    static int live();
private:
    std::shared_ptr<feature_library::time::Point> native_;
};
TimePoint time_now(bool steady = false);
std::vector<long long> time_clock_info(bool steady = false);

using Product = std::tuple<std::string, int, std::optional<std::string>>;
using TextPair = std::pair<const std::string, bool>;
using NestedProduct = std::pair<Product, std::vector<std::tuple<int, bool>>>;
using ProductCallback = std::function<Product(Product)>;
Product product_identity(Product value);
TextPair pair_identity(TextPair value);
std::tuple<> empty_product(std::tuple<> value);
NestedProduct nested_product(const NestedProduct& value);
std::vector<Product> product_vector(std::vector<Product> values);
std::optional<Product> optional_product(std::optional<Product> value);
Product product_failure(Product value);
std::tuple<std::string, unsigned long long> product_large_result(Product value);
std::tuple<std::string_view, int> product_view(const std::string& value = std::string(128, 'v'));
Product product_callback(ProductCallback callback, Product value);
using ConstProduct = std::tuple<const bool, std::array<const std::string, 2>, std::optional<const bool>>;
ConstProduct product_const(ConstProduct value);
using SparseProduct = std::tuple<std::optional<int>, std::optional<std::string>, std::optional<bool>>;
std::optional<SparseProduct> sparse_product(std::optional<SparseProduct> value);
std::string product_view_after(std::tuple<std::string_view, int> value, std::function<void()> mutate);
std::vector<std::string> vector_view_after(std::vector<std::string_view> value, std::function<void()> mutate);
using EnumProduct = std::tuple<std::string, Operation, std::optional<ByteFlags>, std::vector<SignedFlags>>;
std::vector<Operation> operation_vector(std::vector<Operation> value);
std::array<ByteFlags, 3> byte_flag_array(std::array<ByteFlags, 3> value);
std::optional<Operation> optional_operation(std::optional<Operation> value);
EnumProduct enum_product(EnumProduct value);
std::tuple<BoolFlags, WideFlags> enum_widths(std::tuple<BoolFlags, WideFlags> value);
Operation enum_callback(std::function<Operation(Operation)> callback, Operation value);
EnumProduct enum_product_callback(std::function<EnumProduct(EnumProduct)> callback, EnumProduct value);
std::tuple<Operation, ByteFlags> enum_native_values();
EnumProduct enum_product_failure(EnumProduct value);
std::tuple<std::string, WideFlags> enum_oversized_result(EnumProduct value);

class NestedOwner {
public:
    using Count = int;
    enum class Mode : unsigned char;
    enum Access : unsigned char { read = 1, write = 2 };
    class Item;
    explicit NestedOwner(Count value, std::string label = std::string(128, 'n'));
    ~NestedOwner() noexcept;
    Item& item();
    const Item& readonly_item() const;
    Mode mode() const;
    void set_mode(Mode value);
    Access access = read;
    static int live();
private:
    class Hidden;
    enum class Secret { hidden = 7 };
    std::unique_ptr<Item> item_;
    Mode mode_;
};
enum class NestedOwner::Mode : unsigned char { ready = 1, active = 2 };
class NestedOwner::Item {
public:
    using Value = Count;
    enum class State { idle = 1, busy = 2 };
    struct Tags { enum class Mark { present = 1 }; };
    explicit Item(Value value, std::string label = std::string(128, 'i'));
    Item(const Item& other);
    Item(Item&& other) noexcept;
    ~Item() noexcept;
    Value value() const;
    void set_value(Value value);
    Value shifted(Value offset) const;
    State state() const;
    void set_state(State value);
    Value with_callback(IntCallback callback);
    static int live();
    std::string label;
private:
    Value value_;
    State state_ = State::idle;
    Value adjusted_impl(Value offset) const;
};
// Defaults on a definition outside the class must merge with its original declaration.
inline NestedOwner::Item::Value NestedOwner::Item::shifted(Value offset = 1) const { return adjusted_impl(offset); }
class NestedOwner::Hidden { public: class Deep; enum class Visible { inaccessible = 1 }; };
class NestedOwner::Hidden::Deep { public: enum class Visible { inaccessible = 2 }; };
std::vector<NestedOwner::Mode> nested_modes(std::vector<NestedOwner::Mode> values);
NestedOwner::Mode nested_mode_callback(std::function<NestedOwner::Mode(NestedOwner::Mode)> callback, NestedOwner::Mode value);
using StringMap = std::map<std::string, std::string>;
using OptionalMap = std::map<std::string, std::optional<std::string>>;
using ComplexMap = std::map<std::tuple<std::optional<int>, std::string>, std::pair<Operation, std::optional<std::string>>>;
StringMap string_map(StringMap value);
std::unordered_map<std::string, Operation> unordered_enum_map(std::unordered_map<std::string, Operation> value);
OptionalMap optional_map(OptionalMap value);
ComplexMap complex_map(const ComplexMap& value);
std::map<double, std::string> number_map(std::map<double, std::string> value);
std::unordered_map<Operation, std::string> hashed_enum_map(std::unordered_map<Operation, std::string> value);
std::unordered_map<std::optional<int>, std::string> hashed_optional_map(std::unordered_map<std::optional<int>, std::string> value);
std::unordered_map<std::vector<bool>, std::string> hashed_bits_map(std::unordered_map<std::vector<bool>, std::string> value);
StringMap map_failure(StringMap value);
std::map<std::string, WideFlags> map_oversized_result(StringMap value);
StringMap map_callback(std::function<StringMap(StringMap)> callback, StringMap value);
StringMap map_views_after(std::map<std::string, std::string_view> value, std::function<void()> mutate);
using Sum = std::variant<std::monostate, int, std::string, Operation, std::optional<std::string>, std::vector<int>>;
using DuplicateSum = std::variant<int, int, std::string>;
using ConstSum = std::variant<int, const std::string>;
using ViewSum = std::variant<std::string_view, int>;
Sum variant_identity(Sum value);
DuplicateSum duplicate_variant(DuplicateSum value);
std::vector<Sum> variant_vector(std::vector<Sum> values);
std::optional<Sum> optional_variant(std::optional<Sum> value);
std::map<std::string, Sum> variant_map(std::map<std::string, Sum> value);
std::unordered_map<DuplicateSum, std::string> variant_keys(std::unordered_map<DuplicateSum, std::string> value);
std::map<std::string, std::monostate> monostate_map(std::map<std::string, std::monostate> value);
std::monostate monostate_identity(std::monostate value);
Sum variant_callback(std::function<Sum(Sum)> callback, Sum value);
Sum variant_failure(Sum value);
std::variant<std::string, WideFlags> variant_oversized_result(std::string value, bool oversized);
ConstSum variant_exception();
ConstSum const_variant(ConstSum value);
bool variant_exception_is_valueless();
ViewSum variant_view(ViewSum value = ViewSum(std::in_place_index<0>, std::string(128, 'v')));
std::string variant_view_after(ViewSum value, std::function<void()> mutate);
std::deque<std::string> deque_values(std::deque<std::string> value);
std::list<Operation> list_operations(std::list<Operation> value);
std::forward_list<Sum> forward_values(std::forward_list<Sum> value);
std::set<std::string> ordered_set(std::set<std::string> value);
std::unordered_set<Operation> hashed_set(std::unordered_set<Operation> value);
std::multiset<std::string> multi_set(std::multiset<std::string> value);
std::unordered_multiset<std::string> hashed_multi_set(std::unordered_multiset<std::string> value);
std::set<std::optional<std::string>> optional_set(std::set<std::optional<std::string>> value);
std::unordered_set<std::optional<int>> optional_hash_set(std::unordered_set<std::optional<int>> value);
std::multiset<std::monostate> monostate_multi_set(std::multiset<std::monostate> value);
std::set<std::vector<int>> nested_set(std::set<std::vector<int>> value);
std::multimap<std::string, std::optional<std::string>> multi_map(std::multimap<std::string, std::optional<std::string>> value);
std::unordered_multimap<std::string, std::string> hashed_multi_map(std::unordered_multimap<std::string, std::string> value);
std::list<std::string> sequence_callback(std::function<std::list<std::string>(std::list<std::string>)> callback, std::list<std::string> value);
std::set<std::string> set_callback(std::function<std::set<std::string>(std::set<std::string>)> callback, std::set<std::string> value);
std::list<std::string> sequence_failure(std::list<std::string> value);
std::deque<std::pair<std::string, WideFlags>> sequence_oversized_result(std::deque<std::string> value);
std::deque<std::string_view> sequence_view(std::deque<std::string_view> value = {std::string(128, 'q')});
std::vector<std::string> set_views_after(std::set<std::string_view> value, std::function<void()> mutate);
}

// Imported names retain the declaration's native identity and class policy.
namespace demo_alias = demo;
namespace demo_names {
namespace selected = demo_alias;
namespace chained = selected;
namespace values = std;
using chained::Count;
using chained::Operation;
using chained::Counter;
using chained::NestedOwner;
using values::vector;
using values::optional;
using values::tuple;
Count imported_count(Count value = 42);
Operation imported_operation(Operation value = Operation::add);
values::vector<Operation> imported_values(values::vector<Operation> value);
int imported_counter(const Counter& value);
values::string imported_text(values::string value);
NestedOwner::Mode imported_nested(NestedOwner::Mode value);
vector<Operation> using_vector(vector<Operation> value);
optional<values::string> using_optional(optional<values::string> value);
tuple<values::string, Count> using_tuple(tuple<values::string, Count> value);
}
