#pragma once
#include <memory>
#include <string>

// Native-only facades: these reuse the same libraries as the Lua demonstration.
namespace r_verify {
char echo_char(char value);
signed char echo_schar(signed char value);
unsigned char echo_uchar(unsigned char value);
short echo_short(short value);
unsigned short echo_ushort(unsigned short value);
int echo_int(int value);
unsigned int echo_uint(unsigned int value);
long echo_long(long value);
unsigned long echo_ulong(unsigned long value);
long long echo_ll(long long value);
unsigned long long echo_ull(unsigned long long value);
float echo_float(float value);
double echo_double(double value);
bool echo_bool(bool value);
std::string echo_string(std::string value);
bool char_is_signed();
int int_bits();
using Count = int;
Count alias_identity(Count value);
int defaulted(int value = 42);
void do_nothing();
void unknown_error();
void allocation_error();
double nonfinite_result();
std::string embedded_nul();
int maximum(int left, int right);
int template_box(int value);
int variadic_sum(int a, int b, int c);
int constant_answer();
int constrained_sum(int a, int b);
int hierarchy_value(int value);
bool hierarchy_crosscast();
extern "C" int rosetta_r_c_identity(int value);

class Series {
    struct State;
    std::unique_ptr<State> state_;
public:
    Series(int first = 20, int count = 2);
    ~Series() noexcept;
    int size() const;
    int get(int index = 0) const;
    void set(int index, int value);
    int scaled_sum(int factor) const;
    std::string text() const;
    static int live();
};
class Worklist {
    struct State;
    std::unique_ptr<State> state_;
public:
    explicit Worklist(int order);
    ~Worklist() noexcept;
    unsigned long long size() const;
    void push(std::string value);
    std::string pop();
    static int live();
};
class AsyncTask {
    struct State;
    std::unique_ptr<State> state_;
public:
    AsyncTask(int value, int delay_ms, bool fail);
    ~AsyncTask() noexcept;
    bool ready() const;
    bool wait(int timeout_ms) const;
    int result() const;
    bool cancel();
    static int live();
    static int workers();
};
} // namespace r_verify
