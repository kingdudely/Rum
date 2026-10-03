#include "runtime/roblox_platform_protocol_symbols.h"

#include <string>

#include "linker/linker.h"

namespace mocktail {
namespace runtime {
namespace {

template <typename Function>
Function Resolve(void* library, const char* name) {
  return reinterpret_cast<Function>(
      linker::ResolveSymbol(library, std::string(name)));
}

}  // namespace

RobloxPlatformProtocolSymbols ResolveRobloxPlatformProtocolSymbols(
    void* roblox_library, DeleteMessageBusConnectionFn delete_connection) {
  RobloxPlatformProtocolSymbols symbols;
  if (roblox_library == nullptr) {
    return symbols;
  }

  symbols.system_theme.get_message_id =
      Resolve<decltype(symbols.system_theme.get_message_id)>(
          roblox_library,
          "Java_com_roblox_universalapp_messagebus_MessageBus_getMessageId");
  symbols.system_theme.publish_raw =
      Resolve<decltype(symbols.system_theme.publish_raw)>(
          roblox_library,
          "Java_com_roblox_universalapp_messagebus_MessageBus_publishRaw");

  symbols.permissions.subscribe_request =
      Resolve<decltype(symbols.permissions.subscribe_request)>(
          roblox_library,
          "Java_com_roblox_universalapp_messagebus_MessageBus_"
          "doSubscribeProtocolMethodRequestRaw");
  symbols.permissions.delete_connection = delete_connection;
  symbols.permissions.publish_response =
      Resolve<decltype(symbols.permissions.publish_response)>(
          roblox_library,
          "Java_com_roblox_universalapp_messagebus_MessageBus_"
          "publishProtocolMethodResponseRaw");
  symbols.permissions.set_async_handler =
      Resolve<decltype(symbols.permissions.set_async_handler)>(
          roblox_library,
          "Java_com_roblox_universalapp_messagebus_MessageBus_"
          "setRequestHandlerAsyncRaw");
  symbols.permissions.clear_handler =
      Resolve<decltype(symbols.permissions.clear_handler)>(
          roblox_library,
          "Java_com_roblox_universalapp_messagebus_MessageBus_"
          "clearRequestHandler");
  symbols.permissions.call_response_handler =
      Resolve<decltype(symbols.permissions.call_response_handler)>(
          roblox_library,
          "Java_com_roblox_universalapp_messagebus_MessageBus_"
          "callResponseHandlerRaw");
  return symbols;
}

}  // namespace runtime
}  // namespace mocktail