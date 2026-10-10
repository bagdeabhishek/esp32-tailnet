#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <WiFi.h>

extern "C" {
#include "esp32_tailnet.h"
}

/** Arduino convenience facade for the framework-neutral esp32_tailnet API. */
class ESP32Tailnet {
 public:
  struct Config {
    const char *authKey{nullptr};
    const char *hostname{nullptr};
    const char *controlUrl{nullptr};
    const char *clientVersion{nullptr};
    uint8_t maxPeers{4};
    IPAddress priorityPeer{};
    bool netcheckOverride{false};
    uint32_t netcheckThresholdMs{50};
    uint16_t preferredDerpRegion{0};
  };

  using EventCallback = void (*)(ESP32Tailnet &tailnet,
                                  esp32_tailnet_event_t event,
                                  const esp32_tailnet_status_t &status,
                                  void *context);

  ESP32Tailnet() = default;
  ~ESP32Tailnet() { end(); }
  ESP32Tailnet(const ESP32Tailnet &) = delete;
  ESP32Tailnet &operator=(const ESP32Tailnet &) = delete;

  /** Call after Wi-Fi has obtained an address. */
  bool begin(const Config &config) {
    if (handle_ != nullptr || WiFi.status() != WL_CONNECTED) return false;

    esp32_tailnet_config_t native{};
    esp32_tailnet_config_defaults(&native);
    native.auth_key = config.authKey;
    native.hostname = config.hostname;
    native.control_url = config.controlUrl;
    native.client_version = config.clientVersion;
    native.max_peers = config.maxPeers;
    native.priority_peer_ip = toHostOrder(config.priorityPeer);
    native.netcheck_override = config.netcheckOverride;
    native.netcheck_switch_threshold_ms = config.netcheckThresholdMs;
    native.preferred_derp_region = config.preferredDerpRegion;
    native.event_callback = eventThunk;
    native.event_context = this;

    if (esp32_tailnet_create(&native, &handle_) != ESP_OK) return false;
    if (esp32_tailnet_start(handle_) != ESP_OK) {
      esp32_tailnet_destroy(handle_);
      handle_ = nullptr;
      return false;
    }
    return true;
  }

  void end() {
    if (handle_ == nullptr) return;
    esp32_tailnet_destroy(handle_);
    handle_ = nullptr;
  }

  /** Call after Wi-Fi reconnects or changes upstream networks. */
  bool rebind() {
    return handle_ != nullptr && esp32_tailnet_rebind(handle_) == ESP_OK;
  }

  bool connected() const {
    return handle_ != nullptr && esp32_tailnet_is_connected(handle_);
  }

  esp32_tailnet_state_t state() const {
    return esp32_tailnet_get_state(handle_);
  }

  IPAddress localIP() const {
    if (!connected()) return IPAddress();
    esp32_tailnet_status_t current{};
    if (esp32_tailnet_get_status(handle_, &current) != ESP_OK) return IPAddress();
    return fromHostOrder(current.vpn_ip);
  }

  IPAddress serviceIP(const IPAddress &fallback) const {
    return connected() ? localIP() : fallback;
  }

  bool status(esp32_tailnet_status_t &out) const {
    return handle_ != nullptr && esp32_tailnet_get_status(handle_, &out) == ESP_OK;
  }

  void onEvent(EventCallback callback, void *context = nullptr) {
    callback_ = callback;
    callbackContext_ = context;
  }

  esp32_tailnet_t *nativeHandle() const { return handle_; }

 private:
  static void eventThunk(esp32_tailnet_t *,
                         esp32_tailnet_event_t event,
                         const esp32_tailnet_status_t *status,
                         void *context) {
    auto *self = static_cast<ESP32Tailnet *>(context);
    if (self == nullptr || self->callback_ == nullptr || status == nullptr) return;
    self->callback_(*self, event, *status, self->callbackContext_);
  }

  static uint32_t toHostOrder(const IPAddress &ip) {
    return (static_cast<uint32_t>(ip[0]) << 24) |
           (static_cast<uint32_t>(ip[1]) << 16) |
           (static_cast<uint32_t>(ip[2]) << 8) |
           static_cast<uint32_t>(ip[3]);
  }

  static IPAddress fromHostOrder(uint32_t ip) {
    return IPAddress(static_cast<uint8_t>((ip >> 24) & 0xff),
                     static_cast<uint8_t>((ip >> 16) & 0xff),
                     static_cast<uint8_t>((ip >> 8) & 0xff),
                     static_cast<uint8_t>(ip & 0xff));
  }

  esp32_tailnet_t *handle_{nullptr};
  EventCallback callback_{nullptr};
  void *callbackContext_{nullptr};
};
