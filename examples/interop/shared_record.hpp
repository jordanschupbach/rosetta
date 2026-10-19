#pragma once
#include <memory>
#include <string>
#include <utility>
namespace rosetta_example::exchange {
class State;
std::shared_ptr<State> create(std::string text, int value);
std::shared_ptr<State> import_ticket(const std::string& ticket);
std::string ticket(const std::shared_ptr<State>& value);
std::string text(const std::shared_ptr<State>& value);
int number(const std::shared_ptr<State>& value);
void set_text(const std::shared_ptr<State>& value, std::string text);
void set_number(const std::shared_ptr<State>& value, int number);
int shift(const std::shared_ptr<State>& value, int delta);
int live();
int tickets();
}
