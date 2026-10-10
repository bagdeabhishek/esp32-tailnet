# Contributing

Keep application policy out of the core. The `esp32_tailnet` component should
remain a small C API that does not own Wi-Fi, Ethernet, provisioning, servers,
or an event loop. Put Arduino, ESPHome, and other framework behavior in separate
adapters.

Before opening a change:

1. Build both examples.
2. Confirm no credentials or generated `sdkconfig` files are staged.
3. Test enrollment and reboot on an ESP32-S3 with PSRAM.
4. Exercise a direct peer path and a DERP-relayed path.
5. For lifecycle changes, test repeated Wi-Fi disconnect/reconnect and stop.

Protocol-engine changes should document their upstream source and preserve all
applicable license notices.
