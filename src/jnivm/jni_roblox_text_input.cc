#include "jnivm/jnivm.h"

#include "jni_helpers.h"
#include "jni_references.h"
#include "jni_fields.h"
#include "jni_strings.h"

#include "mocktail/audio/fmod_thread_floating_point.h"
#include "mocktail/platform/posix_primitives.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <limits>
#include <list>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "runtime/display_size.h"


namespace jnivm {

using namespace internal;

// the Roblox text-input native binding.

struct VM::RobloxTextInputBinding {
  ~RobloxTextInputBinding() {
    mocktail::platform::SecureClear(&last_request.text);
    if (context != nullptr && callbacks.shutdown != nullptr) {
      callbacks.shutdown(context.get());
    }
  }

  std::shared_ptr<void> context;
  RobloxTextInputCallbacks callbacks;
  std::recursive_mutex callback_mutex;
  bool active = false;
  RobloxTextInputShowRequest last_request;
};

void VM::SetRobloxTextInputCallbacks(
    std::shared_ptr<void> context,
    const RobloxTextInputCallbacks& callbacks) {
  auto binding = std::make_shared<RobloxTextInputBinding>();
  binding->context = std::move(context);
  binding->callbacks = callbacks;
  std::shared_ptr<RobloxTextInputBinding> old_binding;
  {
    std::lock_guard<std::mutex> lock(roblox_text_input_mutex_);
    old_binding = std::move(roblox_text_input_binding_);
    roblox_text_input_binding_ = std::move(binding);
  }
}

void VM::ClearRobloxTextInputCallbacks() {
  std::shared_ptr<RobloxTextInputBinding> old_binding;
  {
    std::lock_guard<std::mutex> lock(roblox_text_input_mutex_);
    old_binding = std::move(roblox_text_input_binding_);
  }
}

bool VM::DispatchRobloxTextInputShow(
    const RobloxTextInputShowRequest& request) {
  std::shared_ptr<RobloxTextInputBinding> binding;
  {
    std::lock_guard<std::mutex> lock(roblox_text_input_mutex_);
    binding = roblox_text_input_binding_;
  }
  if (binding == nullptr || binding->context == nullptr ||
      binding->callbacks.show == nullptr || request.text_box <= 0) {
    return false;
  }
  std::lock_guard<std::recursive_mutex> lock(binding->callback_mutex);
  // An identical show may reopen a TextBox closed without a hide callback.
  mocktail::platform::SecureClear(&binding->last_request.text);
  binding->last_request = request;
  binding->active = true;
  binding->callbacks.show(binding->context.get(), request);
  return true;
}

bool VM::DispatchRobloxTextInputHide() {
  std::shared_ptr<RobloxTextInputBinding> binding;
  {
    std::lock_guard<std::mutex> lock(roblox_text_input_mutex_);
    binding = roblox_text_input_binding_;
  }
  if (binding == nullptr || binding->context == nullptr ||
      binding->callbacks.hide == nullptr) {
    return false;
  }
  std::lock_guard<std::recursive_mutex> lock(binding->callback_mutex);
  if (!binding->active) {
    return true;
  }
  binding->active = false;
  mocktail::platform::SecureClear(&binding->last_request.text);
  binding->last_request = {};
  binding->callbacks.hide(binding->context.get());
  return true;
}

bool VM::DispatchRobloxTextInputReplaceText(const std::string& text) {
  std::shared_ptr<RobloxTextInputBinding> binding;
  {
    std::lock_guard<std::mutex> lock(roblox_text_input_mutex_);
    binding = roblox_text_input_binding_;
  }
  if (binding == nullptr || binding->context == nullptr ||
      binding->callbacks.replace_text == nullptr) {
    return false;
  }
  std::lock_guard<std::recursive_mutex> lock(binding->callback_mutex);
  if (!binding->active || binding->last_request.text == text) {
    return true;
  }
  mocktail::platform::SecureClear(&binding->last_request.text);
  binding->last_request.text = text;
  binding->callbacks.replace_text(binding->context.get(), text);
  return true;
}

bool VM::DispatchRobloxTextInputPropertiesChanged() {
  std::shared_ptr<RobloxTextInputBinding> binding;
  {
    std::lock_guard<std::mutex> lock(roblox_text_input_mutex_);
    binding = roblox_text_input_binding_;
  }
  if (binding == nullptr || binding->context == nullptr ||
      binding->callbacks.properties_changed == nullptr) {
    return false;
  }
  std::lock_guard<std::recursive_mutex> lock(binding->callback_mutex);
  if (!binding->active) {
    return true;
  }
  binding->callbacks.properties_changed(binding->context.get());
  return true;
}

}  // namespace jnivm
