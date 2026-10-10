# Arduino basic example

This mixed Arduino/ESP-IDF project connects to Wi-Fi, enrolls the VoiceS3R as a
tailnet node, and serves a plain-text response on TCP port 8080 over both LAN and
the overlay.

Replace the three placeholder build flags in `platformio.ini` locally, then:

```sh
pio run
pio run --target upload
pio device monitor
```

Open the printed `http://100.x.y.z:8080` URL from another authorized tailnet
node. Restore the placeholder values before committing; build flags and firmware
binaries can expose credentials.

The Wi-Fi event callback calls `tailnet.rebind()` after an upstream reconnect.
The server remains bound to all interfaces and needs no tailnet-specific socket
code.
