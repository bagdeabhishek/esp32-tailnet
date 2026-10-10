/**
 * @file esp32_tailnet.h
 * @brief Framework-neutral lifecycle wrapper for a Tailscale-compatible ESP32 node.
 *
 * The public API is C so the same component can be consumed by native ESP-IDF,
 * Arduino-as-an-ESP-IDF-component, ESPHome, and other C/C++ firmware stacks.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define ESP32_TAILNET_VERSION_MAJOR 0
#define ESP32_TAILNET_VERSION_MINOR 1
#define ESP32_TAILNET_VERSION_PATCH 0
#define ESP32_TAILNET_VERSION_STRING "0.1.0"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp32_tailnet esp32_tailnet_t;

typedef enum {
    ESP32_TAILNET_STATE_STOPPED = 0,
    ESP32_TAILNET_STATE_WAITING_FOR_NETWORK,
    ESP32_TAILNET_STATE_CONNECTING,
    ESP32_TAILNET_STATE_REGISTERING,
    ESP32_TAILNET_STATE_CONNECTED,
    ESP32_TAILNET_STATE_RECONNECTING,
    ESP32_TAILNET_STATE_ERROR,
} esp32_tailnet_state_t;

typedef enum {
    ESP32_TAILNET_EVENT_STATE_CHANGED = 0,
    ESP32_TAILNET_EVENT_CONNECTED,
    ESP32_TAILNET_EVENT_DISCONNECTED,
    ESP32_TAILNET_EVENT_PEER_CHANGED,
} esp32_tailnet_event_t;

typedef struct {
    uint32_t vpn_ip;            /**< Tailnet IPv4 address, host byte order. */
    char hostname[64];
    bool online;
    bool direct_path;
    uint16_t derp_region;
} esp32_tailnet_peer_t;

typedef struct {
    esp32_tailnet_state_t state;
    bool connected;
    uint32_t vpn_ip;            /**< Tailnet IPv4 address, host byte order. */
    uint32_t public_ip;         /**< STUN-discovered public IPv4, host byte order. */
    uint16_t derp_region;
    uint16_t configured_derp_region;
    uint16_t peers_total;
    uint16_t peers_online;
    uint16_t peers_direct;
    uint16_t peers_relayed;
    uint32_t connected_seconds;
    int64_t node_key_expiry;    /**< Unix epoch seconds; 0 means unknown or disabled. */
    bool identity_persistent;
    char identity_key_prefix[17];
} esp32_tailnet_status_t;

typedef void (*esp32_tailnet_event_cb_t)(esp32_tailnet_t *tailnet,
                                          esp32_tailnet_event_t event,
                                          const esp32_tailnet_status_t *status,
                                          void *context);

/*
 * Event callbacks run on a MicroLink networking task. Keep them non-blocking;
 * copy data or signal another task for substantial work. Do not start, stop,
 * destroy, or factory-reset the instance from inside its callback.
 */

typedef struct {
    /** Enrollment key. The wrapper copies it and never logs it. */
    const char *auth_key;
    /** Tailnet hostname. NULL/empty lets the control plane choose one. */
    const char *hostname;
    /** Optional Headscale-compatible control-plane URL. NULL uses Tailscale SaaS. */
    const char *control_url;
    /** Optional version.Long()-style value reported to the control plane. */
    const char *client_version;

    /** Active peer slots, not the total number of devices in the tailnet. */
    uint8_t max_peers;
    /** Peer guaranteed a slot, in host byte order. Zero disables priority. */
    uint32_t priority_peer_ip;

    bool netcheck_override;
    uint32_t netcheck_switch_threshold_ms;
    uint16_t preferred_derp_region;

    esp32_tailnet_event_cb_t event_callback;
    void *event_context;
} esp32_tailnet_config_t;

/** Fill a config with conservative defaults. Strings remain NULL. */
void esp32_tailnet_config_defaults(esp32_tailnet_config_t *config);

/**
 * Allocate a wrapper and copy the configuration.
 *
 * This does not connect and does not own Wi-Fi/Ethernet. The application must
 * initialize NVS and bring up an IP-capable interface before start().
 */
esp_err_t esp32_tailnet_create(const esp32_tailnet_config_t *config,
                               esp32_tailnet_t **out_tailnet);

/** Start the tailnet engine after the upstream interface has an IP address. */
esp_err_t esp32_tailnet_start(esp32_tailnet_t *tailnet);

/**
 * Re-open control, DERP, STUN, and discovery paths after an uplink change.
 * WireGuard identity and the current peer table are preserved.
 */
esp_err_t esp32_tailnet_rebind(esp32_tailnet_t *tailnet);

/**
 * Stop the engine. This can block for up to 15 seconds while network tasks exit.
 */
esp_err_t esp32_tailnet_stop(esp32_tailnet_t *tailnet);

/** Stop if necessary, erase copied secrets, and free the wrapper. */
void esp32_tailnet_destroy(esp32_tailnet_t *tailnet);

/**
 * Erase only MicroLink's device identity and peer cache from NVS.
 * Call after NVS initialization and before creating any tailnet instance.
 */
esp_err_t esp32_tailnet_factory_reset(void);

bool esp32_tailnet_is_connected(const esp32_tailnet_t *tailnet);
esp32_tailnet_state_t esp32_tailnet_get_state(const esp32_tailnet_t *tailnet);
esp_err_t esp32_tailnet_get_status(const esp32_tailnet_t *tailnet,
                                   esp32_tailnet_status_t *out_status);
esp_err_t esp32_tailnet_get_peer(const esp32_tailnet_t *tailnet,
                                 size_t index,
                                 esp32_tailnet_peer_t *out_peer);

/** Format a host-byte-order IPv4 address. Returns false if the buffer is too small. */
bool esp32_tailnet_format_ipv4(uint32_t ip, char *buffer, size_t buffer_size);

/** Return a stable, human-readable name for a state. */
const char *esp32_tailnet_state_name(esp32_tailnet_state_t state);

/**
 * Pick the address applications should advertise in URLs and discovery data.
 * Returns the VPN address while connected, otherwise fallback_ip.
 */
uint32_t esp32_tailnet_service_ipv4(const esp32_tailnet_t *tailnet,
                                    uint32_t fallback_ip);

#ifdef __cplusplus
}
#endif
