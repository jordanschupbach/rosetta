#include "demo.hpp"
#include "executor_library.hpp"
namespace demo {
namespace native = feature_library::awaitables;
AwaitTask::AwaitTask(std::shared_ptr<native::Control> task) : native_(std::move(task)) {}
AwaitPhase AwaitTask::state() const { return static_cast<AwaitPhase>(native_->state()); }
std::string AwaitTask::result() const { return native_->result(); }
bool AwaitTask::cancel() { return native_->cancel(); }
AwaitTask AwaitTask::share() const { native_->executor->owner_thread(); return AwaitTask(native_); }
int AwaitTask::live() { return native::Control::alive.load(); }
AwaitExecutor::AwaitExecutor() : native_(std::make_shared<native::Executor>()) {}
AwaitExecutor::AwaitExecutor(std::shared_ptr<native::Executor> executor) : native_(std::move(executor)) {}
AwaitTask AwaitExecutor::submit(int seed, int steps, std::string prefix, bool fail, std::function<int(int)> callback) {
    return AwaitTask(native_->submit(seed,steps,std::move(prefix),fail,std::move(callback)));
}
bool AwaitExecutor::run_one() { return native_->run_one(); }
int AwaitExecutor::pending() const { return native_->pending(); }
AwaitExecutor AwaitExecutor::share() const { native_->owner_thread(); return AwaitExecutor(native_); }
int AwaitExecutor::live() { return native::Executor::alive.load(); }
int AwaitExecutor::frames() { return native::Executor::frames.load(); }
int AwaitExecutor::bodies() { return native::Executor::bodies.load(); }
unsigned long long AwaitExecutor::allocations() { return native::Executor::allocations.load(); }
unsigned long long AwaitExecutor::releases() { return native::Executor::releases.load(); }
}
