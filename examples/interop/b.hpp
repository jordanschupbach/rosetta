#pragma once
#include "shared_record.hpp"
namespace interop_b {
class Record {
    std::shared_ptr<rosetta_example::exchange::State> state_;
    explicit Record(std::shared_ptr<rosetta_example::exchange::State> state) : state_(std::move(state)) {}
public:
    Record(std::string text, int value) : state_(rosetta_example::exchange::create(std::move(text),value)) {}
    static Record from_ticket(std::string ticket) { return Record(rosetta_example::exchange::import_ticket(ticket)); }
    std::string ticket() { return rosetta_example::exchange::ticket(state_); }
    std::string text() const { return rosetta_example::exchange::text(state_); }
    int value() const { return rosetta_example::exchange::number(state_); }
    void set_text(std::string text) { rosetta_example::exchange::set_text(state_,std::move(text)); }
    void set_value(int value) { rosetta_example::exchange::set_number(state_,value); }
    int shift(int delta) { return rosetta_example::exchange::shift(state_,delta); }
    Record share() { return Record(state_); }
    const Record& readonly() const { return *this; }
};
inline int take_value(const Record& record) { return record.value(); }
inline int live() { return rosetta_example::exchange::live(); }
inline int tickets() { return rosetta_example::exchange::tickets(); }
}
