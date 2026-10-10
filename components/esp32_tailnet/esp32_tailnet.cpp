#include "esp32_tailnet.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <new>

#include "esp_log.h"
#include "microlink.h"

static const char *TAG = "esp32_tailnet";

struct esp32_tailnet {
    microlink_t *ml{nullptr};
    char *auth_key{nullptr};
    char *hostname{nullptr};
    char *control_url{nullptr};
    char *client_version{nullptr};
    uint8_t max_peers{4};
    uint32_t priority_peer_ip{0};
    bool netcheck_override{false};
    uint32_t netcheck_threshold_ms{50};
    uint16_t preferred_derp_region{0};
    esp32_tailnet_event_cb_t event_callback{nullptr};
    void *event_context{nullptr};
    std::atomic<esp32_tailnet_state_t> state{ESP32_TAILNET_STATE_STOPPED};
    std::atomic<bool> stopping{false};
};

static char *duplicate_string(const char *value) {
    if (value == nullptr || value[0] == '\0') return nullptr;
    const size_t size = std::strlen(value) + 1;
    auto *copy = static_cast<char *>(std::malloc(size));
    if (copy != nullptr) std::memcpy(copy, value, size);
    return copy;
}

static void erase_and_free(char *value) {
    if (value == nullptr) return;
    volatile char *cursor = value;
    const size_t size = std::strlen(value);
    for (size_t i = 0; i < size; ++i) cursor[i] = 0;
    std::free(value);
}

static esp32_tailnet_state_t map_state(microlink_state_t state) {
    switch (state) {
        case ML_STATE_IDLE: return ESP32_TAILNET_STATE_STOPPED;
        case ML_STATE_WIFI_WAIT: return ESP32_TAILNET_STATE_WAITING_FOR_NETWORK;
        case ML_STATE_CONNECTING: return ESP32_TAILNET_STATE_CONNECTING;
        case ML_STATE_REGISTERING: return ESP32_TAILNET_STATE_REGISTERING;
        case ML_STATE_CONNECTED: return ESP32_TAILNET_STATE_CONNECTED;
        case ML_STATE_RECONNECTING: return ESP32_TAILNET_STATE_RECONNECTING;
        case ML_STATE_ERROR: return ESP32_TAILNET_STATE_ERROR;
        default: return ESP32_TAILNET_STATE_ERROR;
    }
}

static void notify(esp32_tailnet_t *tailnet, esp32_tailnet_event_t event) {
    if (tailnet == nullptr || tailnet->event_callback == nullptr) return;
    esp32_tailnet_status_t status{};
    esp32_tailnet_get_status(tailnet, &status);
    tailnet->event_callback(tailnet, event, &status, tailnet->event_context);
}

static void state_callback(microlink_t *, microlink_state_t raw_state, void *context) {
    auto *tailnet = static_cast<esp32_tailnet_t *>(context);
    if (tailnet == nullptr || tailnet->stopping.load()) return;

    const esp32_tailnet_state_t previous = tailnet->state.exchange(map_state(raw_state));
    const esp32_tailnet_state_t current = tailnet->state.load();
    ESP_LOGI(TAG, "State: %s", esp32_tailnet_state_name(current));

    notify(tailnet, ESP32_TAILNET_EVENT_STATE_CHANGED);
    if (current == ESP32_TAILNET_STATE_CONNECTED &&
        previous != ESP32_TAILNET_STATE_CONNECTED) {
        notify(tailnet, ESP32_TAILNET_EVENT_CONNECTED);
    } else if (previous == ESP32_TAILNET_STATE_CONNECTED &&
               current != ESP32_TAILNET_STATE_CONNECTED) {
        notify(tailnet, ESP32_TAILNET_EVENT_DISCONNECTED);
    }
}

static void peer_callback(microlink_t *, const microlink_peer_info_t *, void *context) {
    auto *tailnet = static_cast<esp32_tailnet_t *>(context);
    if (tailnet == nullptr || tailnet->stopping.load()) return;
    notify(tailnet, ESP32_TAILNET_EVENT_PEER_CHANGED);
}

void esp32_tailnet_config_defaults(esp32_tailnet_config_t *config) {
    if (config == nullptr) return;
    std::memset(config, 0, sizeof(*config));
    config->max_peers = 4;
    config->netcheck_switch_threshold_ms = 50;
}

esp_err_t esp32_tailnet_create(const esp32_tailnet_config_t *config,
                               esp32_tailnet_t **out_tailnet) {
    if (config == nullptr || out_tailnet == nullptr) return ESP_ERR_INVALID_ARG;
    *out_tailnet = nullptr;

    auto *tailnet = new (std::nothrow) esp32_tailnet_t();
    if (tailnet == nullptr) return ESP_ERR_NO_MEM;

    tailnet->auth_key = duplicate_string(config->auth_key);
    tailnet->hostname = duplicate_string(config->hostname);
    tailnet->control_url = duplicate_string(config->control_url);
    tailnet->client_version = duplicate_string(config->client_version);

    const auto copy_failed = [](const char *source, const char *copy) {
        return source != nullptr && source[0] != '\0' && copy == nullptr;
    };
    if (copy_failed(config->auth_key, tailnet->auth_key) ||
        copy_failed(config->hostname, tailnet->hostname) ||
        copy_failed(config->control_url, tailnet->control_url) ||
        copy_failed(config->client_version, tailnet->client_version)) {
        erase_and_free(tailnet->auth_key);
        erase_and_free(tailnet->hostname);
        erase_and_free(tailnet->control_url);
        erase_and_free(tailnet->client_version);
        delete tailnet;
        return ESP_ERR_NO_MEM;
    }

    tailnet->max_peers = config->max_peers == 0 ? 4 : config->max_peers;
    tailnet->priority_peer_ip = config->priority_peer_ip;
    tailnet->netcheck_override = config->netcheck_override;
    tailnet->netcheck_threshold_ms = config->netcheck_switch_threshold_ms == 0
                                         ? 50
                                         : config->netcheck_switch_threshold_ms;
    tailnet->preferred_derp_region = config->preferred_derp_region;
    tailnet->event_callback = config->event_callback;
    tailnet->event_context = config->event_context;
    tailnet->state.store(ESP32_TAILNET_STATE_STOPPED);

    *out_tailnet = tailnet;
    return ESP_OK;
}

esp_err_t esp32_tailnet_start(esp32_tailnet_t *tailnet) {
    if (tailnet == nullptr) return ESP_ERR_INVALID_ARG;
    if (tailnet->ml != nullptr) return ESP_ERR_INVALID_STATE;

    microlink_config_t config{};
    config.auth_key = tailnet->auth_key;
    config.device_name = tailnet->hostname;
    config.ctrl_host = tailnet->control_url;
    config.ipn_version = tailnet->client_version;
    config.enable_derp = true;
    config.enable_stun = true;
    config.enable_disco = true;
    config.max_peers = tailnet->max_peers;
    config.priority_peer_ip = tailnet->priority_peer_ip;
    config.netcheck_override_enabled = tailnet->netcheck_override;
    config.netcheck_override_threshold_ms = tailnet->netcheck_threshold_ms;
    config.preferred_derp_region = tailnet->preferred_derp_region;

    tailnet->stopping.store(false);
    tailnet->state.store(ESP32_TAILNET_STATE_CONNECTING);
    tailnet->ml = microlink_init(&config);
    if (tailnet->ml == nullptr) {
        tailnet->state.store(ESP32_TAILNET_STATE_ERROR);
        return ESP_FAIL;
    }

    microlink_set_state_callback(tailnet->ml, state_callback, tailnet);
    microlink_set_peer_callback(tailnet->ml, peer_callback, tailnet);
    const esp_err_t error = microlink_start(tailnet->ml);
    if (error != ESP_OK) {
        microlink_set_state_callback(tailnet->ml, nullptr, nullptr);
        microlink_set_peer_callback(tailnet->ml, nullptr, nullptr);
        microlink_destroy(tailnet->ml);
        tailnet->ml = nullptr;
        tailnet->state.store(ESP32_TAILNET_STATE_ERROR);
        return error;
    }

    ESP_LOGI(TAG, "Tailnet engine started for %s",
             tailnet->hostname != nullptr ? tailnet->hostname : "unnamed node");
    return ESP_OK;
}

esp_err_t esp32_tailnet_rebind(esp32_tailnet_t *tailnet) {
    if (tailnet == nullptr) return ESP_ERR_INVALID_ARG;
    if (tailnet->ml == nullptr) return esp32_tailnet_start(tailnet);
    tailnet->state.store(ESP32_TAILNET_STATE_RECONNECTING);
    return microlink_rebind(tailnet->ml);
}

esp_err_t esp32_tailnet_stop(esp32_tailnet_t *tailnet) {
    if (tailnet == nullptr) return ESP_ERR_INVALID_ARG;
    if (tailnet->ml == nullptr) {
        tailnet->state.store(ESP32_TAILNET_STATE_STOPPED);
        return ESP_OK;
    }

    tailnet->stopping.store(true);
    microlink_set_state_callback(tailnet->ml, nullptr, nullptr);
    microlink_set_peer_callback(tailnet->ml, nullptr, nullptr);
    const esp_err_t error = microlink_stop(tailnet->ml);
    microlink_destroy(tailnet->ml);
    tailnet->ml = nullptr;
    tailnet->state.store(ESP32_TAILNET_STATE_STOPPED);
    tailnet->stopping.store(false);
    return error;
}

void esp32_tailnet_destroy(esp32_tailnet_t *tailnet) {
    if (tailnet == nullptr) return;
    esp32_tailnet_stop(tailnet);
    erase_and_free(tailnet->auth_key);
    erase_and_free(tailnet->hostname);
    erase_and_free(tailnet->control_url);
    erase_and_free(tailnet->client_version);
    delete tailnet;
}

esp_err_t esp32_tailnet_factory_reset(void) {
    return microlink_factory_reset();
}

bool esp32_tailnet_is_connected(const esp32_tailnet_t *tailnet) {
    return tailnet != nullptr && tailnet->ml != nullptr &&
           microlink_is_connected(tailnet->ml);
}

esp32_tailnet_state_t esp32_tailnet_get_state(const esp32_tailnet_t *tailnet) {
    return tailnet == nullptr ? ESP32_TAILNET_STATE_STOPPED : tailnet->state.load();
}

esp_err_t esp32_tailnet_get_status(const esp32_tailnet_t *tailnet,
                                   esp32_tailnet_status_t *out_status) {
    if (tailnet == nullptr || out_status == nullptr) return ESP_ERR_INVALID_ARG;
    std::memset(out_status, 0, sizeof(*out_status));
    out_status->state = tailnet->state.load();
    if (tailnet->ml == nullptr) return ESP_OK;

    microlink_diag_t diag{};
    const esp_err_t error = microlink_get_diag(tailnet->ml, &diag);
    if (error != ESP_OK) return error;

    out_status->state = map_state(diag.state);
    out_status->connected = diag.connected;
    out_status->vpn_ip = diag.vpn_ip;
    out_status->public_ip = diag.wan_public_ip;
    out_status->derp_region = diag.derp_home_region;
    out_status->configured_derp_region = diag.derp_region_default;
    out_status->peers_total = static_cast<uint16_t>(diag.peer_count);
    out_status->peers_online = static_cast<uint16_t>(diag.peer_online);
    out_status->connected_seconds = diag.uptime_sec;
    out_status->node_key_expiry = microlink_get_key_expiry(tailnet->ml);
    out_status->identity_persistent = diag.identity_persistent;
    std::strncpy(out_status->identity_key_prefix, diag.identity_pubkey_prefix,
                 sizeof(out_status->identity_key_prefix) - 1);

    for (int i = 0; i < diag.peer_count; ++i) {
        microlink_peer_info_t peer{};
        if (microlink_get_peer_info(tailnet->ml, i, &peer) != ESP_OK || !peer.online) continue;
        if (peer.direct_path) ++out_status->peers_direct;
        else ++out_status->peers_relayed;
    }
    return ESP_OK;
}

esp_err_t esp32_tailnet_get_peer(const esp32_tailnet_t *tailnet,
                                 size_t index,
                                 esp32_tailnet_peer_t *out_peer) {
    if (tailnet == nullptr || tailnet->ml == nullptr || out_peer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    microlink_peer_info_t peer{};
    const esp_err_t error = microlink_get_peer_info(tailnet->ml,
                                                     static_cast<int>(index), &peer);
    if (error != ESP_OK) return error;
    std::memset(out_peer, 0, sizeof(*out_peer));
    out_peer->vpn_ip = peer.vpn_ip;
    std::strncpy(out_peer->hostname, peer.hostname, sizeof(out_peer->hostname) - 1);
    out_peer->online = peer.online;
    out_peer->direct_path = peer.direct_path;
    out_peer->derp_region = peer.derp_region;
    return ESP_OK;
}

bool esp32_tailnet_format_ipv4(uint32_t ip, char *buffer, size_t buffer_size) {
    if (buffer == nullptr || buffer_size < 16) return false;
    microlink_ip_to_str(ip, buffer);
    return true;
}

const char *esp32_tailnet_state_name(esp32_tailnet_state_t state) {
    switch (state) {
        case ESP32_TAILNET_STATE_STOPPED: return "STOPPED";
        case ESP32_TAILNET_STATE_WAITING_FOR_NETWORK: return "WAITING_FOR_NETWORK";
        case ESP32_TAILNET_STATE_CONNECTING: return "CONNECTING";
        case ESP32_TAILNET_STATE_REGISTERING: return "REGISTERING";
        case ESP32_TAILNET_STATE_CONNECTED: return "CONNECTED";
        case ESP32_TAILNET_STATE_RECONNECTING: return "RECONNECTING";
        case ESP32_TAILNET_STATE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

uint32_t esp32_tailnet_service_ipv4(const esp32_tailnet_t *tailnet,
                                    uint32_t fallback_ip) {
    if (!esp32_tailnet_is_connected(tailnet)) return fallback_ip;
    const uint32_t vpn_ip = microlink_get_vpn_ip(tailnet->ml);
    return vpn_ip == 0 ? fallback_ip : vpn_ip;
}
