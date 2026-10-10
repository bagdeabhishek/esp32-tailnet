# Native ESP-IDF basic example

This example exercises the framework-neutral C API. It connects Wi-Fi, starts a
small HTTP server on port 8080, enrolls the node, and rebinds the overlay after
the station obtains a new address.

Configure the four values under `ESP32 Tailnet example` with `menuconfig`, or
provide equivalent `sdkconfig.defaults` values without committing secrets:

```sh
pio run --target menuconfig
pio run
pio run --target upload
pio device monitor
```

From another authorized tailnet node, open the printed
`http://100.x.y.z:8080` URL.
