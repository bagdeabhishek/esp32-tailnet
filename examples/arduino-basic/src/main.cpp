#include <Arduino.h>
#include <WiFi.h>

#include "ESP32Tailnet.h"

#ifndef WIFI_SSID
#define WIFI_SSID "replace-me"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "replace-me"
#endif
#ifndef TAILNET_AUTH_KEY
#define TAILNET_AUTH_KEY "replace-with-auth-key"
#endif

ESP32Tailnet tailnet;
WiFiServer server(8080);

static void onWiFiEvent(arduino_event_id_t event, arduino_event_info_t) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP && tailnet.nativeHandle() != nullptr) {
    tailnet.rebind();
  }
}

static void onTailnetEvent(ESP32Tailnet &instance,
                           esp32_tailnet_event_t event,
                           const esp32_tailnet_status_t &status,
                           void *) {
  if (event != ESP32_TAILNET_EVENT_CONNECTED) return;
  Serial.printf("Tailnet ready: http://%s:8080\n",
                instance.localIP().toString().c_str());
  if (status.node_key_expiry > 0) {
    Serial.println("Action required: disable node-key expiry in the admin console");
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.onEvent(onWiFiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(250);

  tailnet.onEvent(onTailnetEvent);
  ESP32Tailnet::Config config;
  config.authKey = TAILNET_AUTH_KEY;
  config.hostname = "esp32-tailnet-demo";
  config.maxPeers = 4;

  if (!tailnet.begin(config)) {
    Serial.println("Tailnet startup failed");
  }

  // WiFiServer listens on INADDR_ANY, so this accepts LAN and tailnet clients.
  server.begin();
}

void loop() {
  WiFiClient client = server.accept();
  if (client) {
    client.print("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
                 "Connection: close\r\n\r\nHello from ESP32 Tailnet\n");
    client.stop();
  }
  delay(5);
}
