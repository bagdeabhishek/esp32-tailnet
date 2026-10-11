# ESP32 Tailnet

A reusable ESP-IDF component that gives an ESP32 its own Tailscale-compatible
IPv4 address. Existing TCP and UDP servers can keep listening on `INADDR_ANY`;
they become reachable from another network through the tailnet address.

The package deliberately does **not** own Wi-Fi, Ethernet, provisioning, or the
application server. Firmware brings up an uplink, starts ESP32 Tailnet, and
continues running its normal services.

> Experimental security-sensitive networking code. This is an independent
> implementation, not an official Tailscale product. Do not treat it as an
> audited replacement for an official Tailscale client.

## Why this shape

```text
application firmware (BirdNET mic, sensor, camera, ...)
        │ lifecycle + service address
        ▼
esp32_tailnet_arduino     optional, header-only Arduino facade
        │
esp32_tailnet             stable, framework-neutral C API
        │
microlink                 pinned control/DERP/disco implementation
        │
wireguard_lwip            encrypted network interface
        │
ESP-IDF Wi-Fi/Ethernet + lwIP
```

This keeps the public surface small and lets the same core work with native
ESP-IDF, Arduino as an ESP-IDF component, ESPHome adapters, and other firmware
stacks. The protocol engine is pinned inside this repository so an upstream
change cannot silently break every consuming firmware.

## Requirements

- ESP-IDF 5.3 through 5.5; the examples pin pioarduino `55.03.38` / IDF 5.5.4.
- ESP32-S3 with PSRAM is the primary target. The M5Stack Atom VoiceS3R
  (8 MB flash, 8 MB PSRAM) is the reference board.
- An ordinary Wi-Fi or Ethernet connection with Internet access.
- A Tailscale account, or a compatible Headscale control server. First
  enrollment can use a browser authorization URL; an auth key remains optional.

The default HTTP/2 and JSON workspaces reserve 128 KiB each, so PSRAM is a
practical requirement for applications such as audio streaming.

## Add it to firmware

Add the Arduino facade to the application's component manifest:

```yaml
dependencies:
  esp32_tailnet_arduino:
    git: https://github.com/bagdeabhishek/esp32-tailnet.git
    path: components/esp32_tailnet_arduino
    version: main
```

For native ESP-IDF, depend on `components/esp32_tailnet` instead. During local
development a sibling `path:` dependency works too; both complete examples in
this repository use that form.

Arduino projects must run Arduino **as an ESP-IDF component**:

```ini
framework = arduino, espidf
```

Then start the overlay only after Wi-Fi has an address:

```cpp
#include <ESP32Tailnet.h>

ESP32Tailnet tailnet;

void startTailnet() {
  ESP32Tailnet::Config config;
  config.hostname = "forest-mic";
  config.maxPeers = 4;
  tailnet.begin(config);
}
```

With no auth key, the control plane returns an interactive login URL. Expose it
through whatever UI the host firmware already has:

```cpp
String login = tailnet.loginURL();
if (login.length()) {
  // Render a link or QR code; never hard-code a web-server dependency here.
}
```

This is the default BirdNET hero flow. Supplying `config.authKey` is still
supported for unattended fleet provisioning and CI/manufacturing workflows.

Keep servers bound to all interfaces. When publishing a URL or an SDP address,
prefer the overlay address:

```cpp
IPAddress advertised = tailnet.serviceIP(WiFi.localIP());
```

Call `tailnet.rebind()` after Wi-Fi reconnects or switches access points. It
preserves the node identity while reopening the control, DERP, STUN, and peer
paths.

The generated device identity is persisted in NVS and survives ordinary
reboots, OTA updates, uplink changes, `end()`, and application settings resets.
Erase it only as an explicit user action, after stopping the instance:

```cpp
tailnet.end();
if (ESP32Tailnet::clearStoredIdentity()) {
  ESP.restart();  // the next boot requires authorization again
}
```

## Examples

- [`examples/arduino-basic`](examples/arduino-basic) starts an HTTP server on
  port 8080 using the Arduino facade.
- [`examples/idf-basic`](examples/idf-basic) demonstrates the C API, Wi-Fi
  reconnection, and the same HTTP service in native ESP-IDF.
- [`examples/birdnet-go-mic`](examples/birdnet-go-mic) is the hero integration:
  the Atom VoiceS3R streams BirdNET audio and exposes its Web UI over a tailnet.

For either basic example, insert temporary credentials, build with `pio run`,
flash the board, and open the printed `100.x.y.z` URL from another tailnet node.
Do not commit credentials or auth keys.

## Public API

The core API lives in
[`esp32_tailnet.h`](components/esp32_tailnet/include/esp32_tailnet.h). It covers:

- create/start/rebind/stop/destroy lifecycle;
- connection and peer-change callbacks;
- current tailnet, public, DERP, peer, and key-expiry diagnostics;
- interactive authorization URL reporting without owning an application UI;
- peer enumeration and direct-versus-relayed status;
- factory reset of the persisted node identity;
- Tailscale SaaS or a custom Headscale-compatible control URL.

Callbacks run on a networking task. Keep them short and signal an application
task for substantial work; do not stop or destroy the instance from a callback.

`max_peers` limits simultaneously installed WireGuard peers, not the total
number of nodes in the tailnet. Keep it small on constrained hardware and use
`priority_peer_ip` when one collector must always retain a slot.

## Operational and security notes

- Prefer browser authorization for one-off devices. For fleet enrollment, use a
  tagged, short-lived or one-time auth key and revoke it after provisioning.
- Disable node-key expiry for unattended microphones, or plan a renewal path.
- Tailnet reachability is not application authentication. Keep OTA, Web UI, and
  management endpoints protected according to your threat model.
- Treat identity clearing as a destructive, confirmed UI action. It removes
  only ESP32 Tailnet's keys and cached peers, not Wi-Fi or application settings.
- A relay path is expected when direct UDP cannot be established. The status API
  exposes direct and relayed peer counts.
- This package provides a node interface. It is not an exit node, subnet router,
  DNS resolver, or userspace SOCKS proxy.

## Provenance

The networking engine is a pinned snapshot of MicroLink as carried by
`Csontikka/esphome-tailscale` v0.5.14. It contains important ESP32 fixes that
were not yet present in the older upstream MicroLink default branch when this
repository was created. See [`NOTICE`](NOTICE) and the component-local license
files before redistributing binaries.

## Development

```sh
cd examples/arduino-basic
pio run

cd ../idf-basic
pio run
```

Hardware validation should cover cold enrollment, reboot with stored identity,
Wi-Fi roaming/rebind, direct and DERP paths, TCP, RTSP-over-TCP, RTP-over-UDP,
and a factory reset. Contributions should preserve the framework-neutral C API
and keep framework-specific behavior in adapters.
