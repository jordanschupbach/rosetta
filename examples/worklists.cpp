#include "demo.hpp"
#include "worklist_library.hpp"

namespace demo {
Worklist::Worklist(WorkOrder order) : native_(std::make_shared<feature_library::worklists::State>(static_cast<int>(order))) {}
Worklist::Worklist(std::shared_ptr<feature_library::worklists::State> native) : native_(std::move(native)) {}
WorkOrder Worklist::order() const { return static_cast<WorkOrder>(native_->order()); }
unsigned long long Worklist::size() const { return native_->size(); }
void Worklist::push(std::string text) { native_->push(std::move(text)); }
std::optional<std::string> Worklist::peek() const { return native_->peek(); }
std::optional<std::string> Worklist::pop() { return native_->pop(); }
std::vector<std::string> Worklist::snapshot() const { return native_->snapshot(); }
Worklist Worklist::clone() const { return Worklist(std::make_shared<feature_library::worklists::State>(*native_)); }
Worklist Worklist::share() const { return Worklist(native_); }
int Worklist::live() { return feature_library::worklists::State::alive.load(); }
} // namespace demo
