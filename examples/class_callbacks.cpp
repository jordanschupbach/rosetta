#include "demo.hpp"
#include "class_callback_library.hpp"
namespace demo {
namespace native = feature_library::class_callbacks;
CallbackRecord::CallbackRecord(std::string text, int number) : native_(std::make_shared<native::Record>(std::move(text),number)) {}
CallbackRecord::CallbackRecord(std::shared_ptr<native::Record> value) : native_(std::move(value)) {}
std::string CallbackRecord::text() const { return native_->text; }
int CallbackRecord::number() const { return native_->number; }
void CallbackRecord::set_text(std::string text) { native_->text = std::move(text); }
void CallbackRecord::set_number(int number) { native_->number = number; }
CallbackRecord CallbackRecord::copy() const { return CallbackRecord(std::make_shared<native::Record>(*native_)); }
const CallbackRecord& CallbackRecord::readonly() const { return *this; }
int CallbackRecord::live() { return native::Record::alive.load(); }
ClassCallbackFrame::ClassCallbackFrame() : native_(std::make_shared<native::Frame>()) {}
CallbackRecord ClassCallbackFrame::argument() const { return CallbackRecord(std::make_shared<native::Record>(native_->argument())); }
void ClassCallbackFrame::set_result(const CallbackRecord& value) { native_->set_result(*value.native_); }
CallbackRecord ClassCallbackFrame::transform(const CallbackRecord& input, std::function<void()> callback) {
    auto result = native_->transform(*input.native_, callback);
    return CallbackRecord(std::make_shared<native::Record>(std::move(result)));
}
int ClassCallbackFrame::depth() const { return native_->depth(); }
int ClassCallbackFrame::live() { return native::Frame::alive.load(); }
}
