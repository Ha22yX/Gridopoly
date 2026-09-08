#include "tile_network.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP.h>
#include <HTTPClient.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <WiFi.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "tile_assignment.h"

#if __has_include("config/secrets.local.h")
#include "config/secrets.local.h"
#elif __has_include("../../PlayerConsole/config/secrets.local.h")
// Reuse the ignored local AP credentials already used by PlayerConsole.
#include "../../PlayerConsole/config/secrets.local.h"
#endif

#ifndef GRIDOPOLY_TILE_WIFI_SSID
#ifdef GRIDOPOLY_WIFI_UDP_SSID
#define GRIDOPOLY_TILE_WIFI_SSID GRIDOPOLY_WIFI_UDP_SSID
#elif defined(GRIDOPOLY_WIFI_SSID)
#define GRIDOPOLY_TILE_WIFI_SSID GRIDOPOLY_WIFI_SSID
#else
#define GRIDOPOLY_TILE_WIFI_SSID "gridopoly"
#endif
#endif

#ifndef GRIDOPOLY_TILE_WIFI_PASSWORD
#ifdef GRIDOPOLY_WIFI_UDP_PASSWORD
#define GRIDOPOLY_TILE_WIFI_PASSWORD GRIDOPOLY_WIFI_UDP_PASSWORD
#elif defined(GRIDOPOLY_WIFI_PASSWORD)
#define GRIDOPOLY_TILE_WIFI_PASSWORD GRIDOPOLY_WIFI_PASSWORD
#else
#define GRIDOPOLY_TILE_WIFI_PASSWORD "replace-locally"
#endif
#endif


#ifndef GRIDOPOLY_TILE_HTTP_HOST
#define GRIDOPOLY_TILE_HTTP_HOST "10.42.0.1"
#endif

#ifndef GRIDOPOLY_TILE_HTTP_PORT
#define GRIDOPOLY_TILE_HTTP_PORT 80
#endif

namespace gridopoly::tile {
namespace {

constexpr std::uint32_t kHeartbeatPeriodMs = 2000;
constexpr std::uint32_t kWifiRetryPeriodMs = 10000;
constexpr std::uint32_t kDhcpWaitTimeoutMs = 30000;
constexpr std::uint32_t kTaskPollMs = 100;
constexpr std::uint32_t kHttpConnectTimeoutMs = 1500;
constexpr std::uint32_t kHttpReadTimeoutMs = 1500;

volatile bool gWifiAssociated = false;
volatile bool gWifiHasIp = false;

WiFiClient gHeartbeatClient;
HTTPClient gHeartbeatHttp;
bool gHeartbeatInitialized = false;

void resetHeartbeatTransport() {
  if (gHeartbeatInitialized) {
    gHeartbeatHttp.end();
    gHeartbeatInitialized = false;
  }
  gHeartbeatClient.stop();
}

void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
    gWifiAssociated = true;
    gWifiHasIp = false;
    Serial.println(F("[NET] wifi_event=ASSOCIATED"));
  } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    gWifiAssociated = true;
    gWifiHasIp = true;
    Serial.println(F("[NET] wifi_event=GOT_IP"));
  } else if (event == ARDUINO_EVENT_WIFI_STA_LOST_IP) {
    gWifiHasIp = false;
    Serial.println(F("[NET] wifi_event=LOST_IP"));
  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    gWifiAssociated = false;
    gWifiHasIp = false;
    Serial.printf("[NET] wifi_event=DISCONNECTED reason=%u\r\n",
                  static_cast<unsigned>(info.wifi_sta_disconnected.reason));
  }
}

void copyText(char *destination, std::size_t capacity, const char *source) {
  if (capacity == 0) return;
  std::strncpy(destination, source == nullptr ? "" : source, capacity - 1U);
  destination[capacity - 1U] = '\0';
}

bool equalsIgnoreCase(const char *left, const char *right) {
  if (left == nullptr || right == nullptr) return false;
  while (*left != '\0' && *right != '\0') {
    if (std::tolower(static_cast<unsigned char>(*left)) !=
        std::tolower(static_cast<unsigned char>(*right))) {
      return false;
    }
    ++left;
    ++right;
  }
  return *left == '\0' && *right == '\0';
}

void encodeQuery(const char *input, char *output, std::size_t capacity) {
  constexpr char kHex[] = "0123456789ABCDEF";
  std::size_t used = 0;
  for (const unsigned char *cursor =
           reinterpret_cast<const unsigned char *>(input);
       *cursor != 0 && used + 1U < capacity; ++cursor) {
    const unsigned char value = *cursor;
    const bool safe = std::isalnum(value) != 0 || value == '-' || value == '_' ||
                      value == '.' || value == '~';
    if (safe) {
      output[used++] = static_cast<char>(value);
    } else if (used + 3U < capacity) {
      output[used++] = '%';
      output[used++] = kHex[value >> 4U];
      output[used++] = kHex[value & 0x0FU];
    } else {
      break;
    }
  }
  output[used] = '\0';
}

bool snapshotChanged(const TileNetworkSnapshot &left,
                     const TileNetworkSnapshot &right) {
  return left.link != right.link || left.assigned != right.assigned ||
         left.server_revision != right.server_revision ||
         left.assignment_revision != right.assignment_revision ||
         left.movement.mode != right.movement.mode ||
         left.movement.player_id != right.movement.player_id ||
         left.movement.revision != right.movement.revision ||
         left.tag_contract_supported != right.tag_contract_supported ||
         left.lease_ms != right.lease_ms || left.http_status != right.http_status ||
         std::strcmp(left.source, right.source) != 0 ||
         std::memcmp(&left.tile, &right.tile, sizeof(TileState)) != 0;
}

JsonVariantConst findLegacyAssignment(JsonVariantConst root,
                                      const char *module_id,
                                      const char *device_id) {
  for (JsonVariantConst item : root["assignments"].as<JsonArrayConst>()) {
    const char *module = item["moduleId"] | "";
    const char *device = item["deviceId"] | "";
    if (equalsIgnoreCase(module, module_id) ||
        equalsIgnoreCase(device, device_id)) {
      return item;
    }
  }
  return JsonVariantConst{};
}

}  // namespace

const char *tileNetworkLabel(TileNetworkLink link) {
  switch (link) {
    case TileNetworkLink::Starting: return "START";
    case TileNetworkLink::WifiConnecting: return "WIFI";
    case TileNetworkLink::ServerConnecting: return "SERVER";
    case TileNetworkLink::OnlineAuto: return "AUTO";
    case TileNetworkLink::OnlineManual: return "MANUAL";
    case TileNetworkLink::NoFreeTile: return "NO TILE";
    case TileNetworkLink::Fault: return "NET ERR";
  }
  return "NET ERR";
}

void TileNetworkClient::begin() {
  mutex_ = xSemaphoreCreateMutex();
  if (mutex_ == nullptr) {
    Serial.println(F("[NET] mutex allocation failed"));
    return;
  }

  // Read the immutable eFuse identity before changing the network base MAC.
  // The server must continue to see the same moduleId/deviceId across boots.
  std::uint8_t factory_mac[6]{};
  const esp_err_t factory_result = esp_efuse_mac_get_default(factory_mac);
  if (factory_result != ESP_OK) {
    Serial.printf("[NET] factory_mac_read_failed result=%d\r\n",
                  static_cast<int>(factory_result));
    publishLink(TileNetworkLink::Fault);
    return;
  }
  std::snprintf(device_id_, sizeof(device_id_),
                "%02x:%02x:%02x:%02x:%02x:%02x", factory_mac[0],
                factory_mac[1], factory_mac[2], factory_mac[3],
                factory_mac[4], factory_mac[5]);
  std::snprintf(module_id_, sizeof(module_id_),
                "tile-%02x%02x%02x%02x%02x%02x", factory_mac[0],
                factory_mac[1], factory_mac[2], factory_mac[3],
                factory_mac[4], factory_mac[5]);

  shared_ = {};
  shared_.sequence = 1;
  shared_.link = TileNetworkLink::WifiConnecting;
  copyText(shared_.module_id, sizeof(shared_.module_id), module_id_);
  copyText(shared_.device_id, sizeof(shared_.device_id), device_id_);

  // A hard reset cannot deauthenticate the old AP station. Use a fresh,
  // boot-stable locally administered base MAC so the new station never waits
  // for the old one. ESP-IDF requires this call before any interface is
  // initialized; setting it after WiFi.mode() is explicitly unsupported.
  std::uint8_t session_mac[6]{};
  std::memcpy(session_mac, factory_mac, sizeof(session_mac));
  const std::uint32_t nonce = esp_random();
  session_mac[0] = static_cast<std::uint8_t>((session_mac[0] | 0x02U) & 0xfeU);
  session_mac[2] ^= static_cast<std::uint8_t>(nonce >> 24U);
  session_mac[3] ^= static_cast<std::uint8_t>(nonce >> 16U);
  session_mac[4] ^= static_cast<std::uint8_t>(nonce >> 8U);
  session_mac[5] ^= static_cast<std::uint8_t>(nonce);
  const esp_err_t base_mac_result = esp_base_mac_addr_set(session_mac);
  if (base_mac_result != ESP_OK) {
    Serial.printf("[NET] session_base_mac_failed result=%d\r\n",
                  static_cast<int>(base_mac_result));
    publishLink(TileNetworkLink::Fault);
    return;
  }

  gWifiAssociated = false;
  gWifiHasIp = false;
  WiFi.onEvent(onWifiEvent);
  WiFi.persistent(false);
  // This task owns reconnect timing. Arduino auto-reconnect running in
  // parallel causes an authentication storm after an abrupt hardware reset,
  // which keeps the AP's stale station entry alive and delays recovery.
  WiFi.setAutoReconnect(false);
  // AP virtual-interface recreation may change both BSSID and channel. Always
  // perform a fresh all-channel SSID scan instead of trusting cached radio
  // state from the AP that disappeared.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.mode(WIFI_STA);

  std::uint8_t actual_mac[6]{};
  WiFi.macAddress(actual_mac);
  const bool session_mac_matches =
      std::memcmp(actual_mac, session_mac, sizeof(session_mac)) == 0;
  Serial.printf(
      "[NET] session_mac=%02x:%02x:%02x:%02x:%02x:%02x "
      "actual=%02x:%02x:%02x:%02x:%02x:%02x verified=%s\r\n",
      session_mac[0], session_mac[1], session_mac[2], session_mac[3],
      session_mac[4], session_mac[5], actual_mac[0], actual_mac[1],
      actual_mac[2], actual_mac[3], actual_mac[4], actual_mac[5],
      session_mac_matches ? "YES" : "NO");
  if (!session_mac_matches) {
    publishLink(TileNetworkLink::Fault);
    return;
  }

  // The tile can sit at the edge of the cabinet/AP coverage. Keep the radio
  // awake and use the maximum legal Arduino power preset so association and
  // heartbeat traffic are not delayed by modem sleep.
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  Serial.printf("[NET] module_id=%s device_id=%s ssid=%s server=%s:%u\r\n",
                module_id_, device_id_, GRIDOPOLY_TILE_WIFI_SSID,
                GRIDOPOLY_TILE_HTTP_HOST,
                static_cast<unsigned>(GRIDOPOLY_TILE_HTTP_PORT));
  const BaseType_t created = xTaskCreatePinnedToCore(
      taskEntry, "tile-network", 12288, this, 1,
      reinterpret_cast<TaskHandle_t *>(&task_), 0);
  if (created != pdPASS) {
    task_ = nullptr;
    publishLink(TileNetworkLink::Fault);
    Serial.println(F("[NET] task creation failed"));
  }
}

bool TileNetworkClient::consume(TileNetworkSnapshot &snapshot) {
  if (mutex_ == nullptr) return false;
  auto mutex = reinterpret_cast<SemaphoreHandle_t>(mutex_);
  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(5)) != pdTRUE) return false;
  const bool changed = shared_.sequence != consumed_sequence_;
  if (changed) {
    snapshot = shared_;
    consumed_sequence_ = shared_.sequence;
  }
  xSemaphoreGive(mutex);
  return changed;
}

void TileNetworkClient::updateTagObservation(TileTagReaderState state,
                                             const char uids[][9],
                                             std::uint8_t count,
                                             bool overflow) {
  if (mutex_ == nullptr) return;
  const std::uint8_t normalized_count =
      state == TileTagReaderState::Stable
          ? static_cast<std::uint8_t>(
                std::min<std::size_t>(count, kMaximumReportedTags))
          : 0U;
  const bool normalized_overflow =
      state == TileTagReaderState::Stable && overflow;
  auto mutex = reinterpret_cast<SemaphoreHandle_t>(mutex_);
  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
  bool changed = observed_tags_.state != state ||
                 observed_tags_.count != normalized_count ||
                 observed_tags_.overflow != normalized_overflow;
  if (!changed) {
    for (std::size_t index = 0; index < normalized_count; ++index) {
      if (std::strcmp(observed_tags_.uids[index], uids[index]) != 0) {
        changed = true;
        break;
      }
    }
  }
  if (changed) {
    std::uint32_t revision = observed_tags_.revision + 1U;
    if (revision == 0U) revision = 1U;
    observed_tags_ = {};
    observed_tags_.revision = revision;
    observed_tags_.state = state;
    observed_tags_.count = normalized_count;
    observed_tags_.overflow = normalized_overflow;
    for (std::size_t index = 0; index < normalized_count; ++index) {
      copyText(observed_tags_.uids[index],
               sizeof(observed_tags_.uids[index]), uids[index]);
    }
  }
  xSemaphoreGive(mutex);
}

TileTagObservation TileNetworkClient::tagObservation() {
  TileTagObservation result;
  if (mutex_ == nullptr) return result;
  auto mutex = reinterpret_cast<SemaphoreHandle_t>(mutex_);
  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(20)) != pdTRUE) return result;
  result = observed_tags_;
  xSemaphoreGive(mutex);
  return result;
}

void TileNetworkClient::taskEntry(void *context) {
  static_cast<TileNetworkClient *>(context)->taskLoop();
}

void TileNetworkClient::startWifiAttempt() {
  // esp_wifi_connect can remain in an obsolete internal attempt after the AP
  // disappears. On an association timeout, cancel that attempt once before
  // starting a fresh all-channel SSID scan. This function is never called
  // while STA_CONNECTED is true, so it cannot interrupt DHCP.
  const bool restarting_stale_attempt = last_wifi_attempt_ms_ != 0U;
  bool disconnect_ok = true;
  if (restarting_stale_attempt) {
    gWifiAssociated = false;
    gWifiHasIp = false;
    disconnect_ok = WiFi.disconnect(false, false);
    delay(50);
  }

  const wl_status_t status =
      WiFi.begin(GRIDOPOLY_TILE_WIFI_SSID, GRIDOPOLY_TILE_WIFI_PASSWORD);
  last_wifi_attempt_ms_ = millis();
  Serial.printf(
      "[NET] wifi_attempt scan=ALL_CHANNEL timeout=%lums reset=%s "
      "disconnect_ok=%s status=%d\r\n",
      static_cast<unsigned long>(kWifiRetryPeriodMs),
      restarting_stale_attempt ? "YES" : "NO",
      disconnect_ok ? "YES" : "NO", static_cast<int>(status));
}
void TileNetworkClient::taskLoop() {
  std::uint32_t last_heartbeat_ms = millis() - kHeartbeatPeriodMs;
  std::uint32_t last_attempted_tag_revision = 0;
  bool server_connecting_published = false;
  for (;;) {
    const std::uint32_t now = millis();
    const wl_status_t wifi_status = WiFi.status();
    const IPAddress local_address = WiFi.localIP();
    const IPAddress gateway_address = WiFi.gatewayIP();
    // WiFi.status() can remain WL_IDLE/WL_CONNECTED with stale IP and RSSI
    // after the AP interface disappears. Only real STA events define the
    // association/IP phase; status and addresses are final readiness checks.
    const bool station_associated = gWifiAssociated;
    const bool wifi_ready =
        gWifiHasIp && wifi_status == WL_CONNECTED &&
        local_address[0] != 0U && gateway_address[0] != 0U;

    if (!wifi_ready) {
      if (wifi_was_ready_) {
        wifi_was_ready_ = false;
        consecutive_heartbeat_failures_ = 0;
        last_wifi_attempt_ms_ = 0;
        resetHeartbeatTransport();
      }
      server_connecting_published = false;
      publishLink(TileNetworkLink::WifiConnecting);

      if (station_associated) {
        // Association and DHCP are separate phases. Pi cold boot can expose
        // the AP before dnsmasq is ready; give DHCP a full window and never
        // interrupt it with the normal association retry timer.
        if (!awaiting_dhcp_) {
          awaiting_dhcp_ = true;
          dhcp_wait_started_ms_ = now;
          Serial.printf("[NET] wifi_phase=AWAITING_DHCP timeout=%lums\r\n",
                        static_cast<unsigned long>(kDhcpWaitTimeoutMs));
        } else if (static_cast<std::uint32_t>(now - dhcp_wait_started_ms_) >=
                   kDhcpWaitTimeoutMs) {
          Serial.println(F("[NET] dhcp_timeout reconnect_wifi=YES"));
          awaiting_dhcp_ = false;
          dhcp_wait_started_ms_ = 0;
          last_wifi_attempt_ms_ = 0;
          gWifiAssociated = false;
          gWifiHasIp = false;
          WiFi.disconnect(false, false);
        }
      } else {
        awaiting_dhcp_ = false;
        dhcp_wait_started_ms_ = 0;
        if (last_wifi_attempt_ms_ == 0U ||
            static_cast<std::uint32_t>(now - last_wifi_attempt_ms_) >=
                kWifiRetryPeriodMs) {
          startWifiAttempt();
        }
      }

      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }

    awaiting_dhcp_ = false;
    dhcp_wait_started_ms_ = 0;
    if (!wifi_was_ready_) {
      wifi_was_ready_ = true;
      consecutive_heartbeat_failures_ = 0;
      last_heartbeat_ms = now - kHeartbeatPeriodMs;
    }

    const TileTagObservation tag_report = tagObservation();
    const bool tag_changed =
        tag_report.revision != last_attempted_tag_revision;
    if (tag_changed || static_cast<std::uint32_t>(now - last_heartbeat_ms) >=
                           kHeartbeatPeriodMs) {
      last_heartbeat_ms = now;
      if (!server_connecting_published) {
        const String address = WiFi.localIP().toString();
        const String gateway = WiFi.gatewayIP().toString();
        Serial.printf(
            "[NET] wifi_connected ip=%s gateway=%s channel=%d rssi=%d\r\n",
            address.c_str(), gateway.c_str(), static_cast<int>(WiFi.channel()),
            static_cast<int>(WiFi.RSSI()));
        publishLink(TileNetworkLink::ServerConnecting);
        server_connecting_published = true;
      }

      TileNetworkSnapshot next{};
      const bool heartbeat_ok = heartbeat(next, tag_report);
      last_attempted_tag_revision = tag_report.revision;
      if (!heartbeat_ok) {
        ++consecutive_heartbeat_failures_;
        publishLink(TileNetworkLink::Fault, next.http_status);
        Serial.printf(
            "[NET] heartbeat_failed count=%u http=%d retry_http=YES "
            "keep_wifi=YES\r\n",
            static_cast<unsigned>(consecutive_heartbeat_failures_),
            next.http_status);
      } else {
        consecutive_heartbeat_failures_ = 0;
        publish(next);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(kTaskPollMs));
  }
}
void TileNetworkClient::publishLink(TileNetworkLink link, int http_status) {
  if (mutex_ == nullptr) return;
  auto mutex = reinterpret_cast<SemaphoreHandle_t>(mutex_);
  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
  if (shared_.link != link || shared_.http_status != http_status) {
    shared_.link = link;
    shared_.http_status = http_status;
    ++shared_.sequence;
  }
  xSemaphoreGive(mutex);
}

void TileNetworkClient::publish(const TileNetworkSnapshot &snapshot) {
  if (mutex_ == nullptr) return;
  auto mutex = reinterpret_cast<SemaphoreHandle_t>(mutex_);
  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
  bool changed = false;
  if (snapshotChanged(shared_, snapshot)) {
    const std::uint32_t next_sequence = shared_.sequence + 1U;
    shared_ = snapshot;
    shared_.sequence = next_sequence;
    changed = true;
  }
  xSemaphoreGive(mutex);
  if (changed && snapshot.assigned) {
    Serial.printf("[NET] assignment source=%s tile=%s index=%u revision=%llu\r\n",
                  snapshot.source, snapshot.tile.tile_id,
                  static_cast<unsigned>(snapshot.tile.map_index),
                  static_cast<unsigned long long>(snapshot.assignment_revision));
  }
}

bool TileNetworkClient::heartbeat(TileNetworkSnapshot &snapshot,
                                  const TileTagObservation &tags) {
  char encoded_module[100]{};
  char encoded_device[100]{};
  encodeQuery(module_id_, encoded_module, sizeof(encoded_module));
  encodeQuery(device_id_, encoded_device, sizeof(encoded_device));
  char url[320]{};
  std::snprintf(
      url, sizeof(url),
      "http://%s:%u/api/tile-modules/heartbeat?moduleId=%s&deviceId=%s",
      GRIDOPOLY_TILE_HTTP_HOST,
      static_cast<unsigned>(GRIDOPOLY_TILE_HTTP_PORT), encoded_module,
      encoded_device);

  // HTTP/1.1 keep-alive avoids a new TCP handshake every two seconds. The
  // transport is explicitly recreated after server restart or Wi-Fi epoch
  // changes, without ever destroying a healthy association or DHCP lease.
  if (!gHeartbeatInitialized) {
    gHeartbeatHttp.setConnectTimeout(kHttpConnectTimeoutMs);
    gHeartbeatHttp.setTimeout(kHttpReadTimeoutMs);
    gHeartbeatHttp.useHTTP10(false);
    gHeartbeatHttp.setReuse(true);
    if (!gHeartbeatHttp.begin(gHeartbeatClient, url)) {
      snapshot.http_status = -1;
      resetHeartbeatTransport();
      return false;
    }
    gHeartbeatInitialized = true;
  }

  JsonDocument request;
  request["tagReaderState"] = tileTagReaderStateLabel(tags.state);
  request["tagRevision"] = tags.revision;
  JsonArray tag_array = request["tags"].to<JsonArray>();
  for (std::size_t index = 0; index < tags.count; ++index) {
    tag_array.add(tags.uids[index]);
  }
  request["overflow"] = tags.overflow;
  char request_body[256]{};
  const std::size_t request_length =
      serializeJson(request, request_body, sizeof(request_body));
  if (request_length == 0U || request_length >= sizeof(request_body)) {
    snapshot.http_status = -1;
    return false;
  }
  gHeartbeatHttp.addHeader("Content-Type", "application/json", true, true);
  const int status = gHeartbeatHttp.sendRequest(
      "POST", reinterpret_cast<std::uint8_t *>(request_body), request_length);
  snapshot.http_status = status;
  if (status == 404 || status == 405) {
    resetHeartbeatTransport();
    return legacySnapshot(snapshot);
  }
  if (status != 200) {
    resetHeartbeatTransport();
    return false;
  }
  const String payload = gHeartbeatHttp.getString();
  const bool parsed = parsePayload(payload.c_str(), snapshot, false);
  // Pre-Tag servers accept the body but do not consume/reuse the connection
  // reliably. Their response has no mandatory movementCue, so close after a
  // successful response during rolling upgrade; the new server keeps HTTP/1.1.
  if (!parsed || !snapshot.tag_contract_supported) {
    resetHeartbeatTransport();
  }
  return parsed;
}

bool TileNetworkClient::legacySnapshot(TileNetworkSnapshot &snapshot) {
  char url[160]{};
  std::snprintf(url, sizeof(url),
                "http://%s:%u/api/tile-debug/assignments",
                GRIDOPOLY_TILE_HTTP_HOST,
                static_cast<unsigned>(GRIDOPOLY_TILE_HTTP_PORT));
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(kHttpConnectTimeoutMs);
  http.setTimeout(kHttpReadTimeoutMs);
  http.useHTTP10(true);
  if (!http.begin(client, url)) {
    snapshot.http_status = -1;
    return false;
  }
  const int status = http.GET();
  snapshot.http_status = status;
  if (status != 200) {
    http.end();
    return false;
  }
  const String payload = http.getString();
  http.end();
  return parsePayload(payload.c_str(), snapshot, true);
}

bool TileNetworkClient::parsePayload(const char *payload,
                                     TileNetworkSnapshot &snapshot,
                                     bool legacy) {
  JsonDocument document;
  const DeserializationError error = deserializeJson(document, payload);
  if (error) {
    Serial.printf("[NET] json_error=%s\r\n", error.c_str());
    return false;
  }
  JsonVariantConst root = document.as<JsonVariantConst>();
  if (!(root["ok"] | false)) return false;

  JsonVariantConst item;
  bool assigned = false;
  if (legacy) {
    item = findLegacyAssignment(root, module_id_, device_id_);
    assigned = !item.isNull();
  } else {
    item = root["assignment"];
    if (item.isNull()) item = root;
    assigned = root["assigned"] | !item["tile_id"].isNull();
  }

  snapshot = {};
  snapshot.http_status = 200;
  snapshot.rssi = static_cast<std::int8_t>(WiFi.RSSI());
  snapshot.server_revision = root["serverRevision"] | static_cast<std::uint64_t>(0);
  if (snapshot.server_revision == 0U) {
    snapshot.server_revision = root["revision"] | static_cast<std::uint64_t>(0);
  }
  snapshot.lease_ms = root["leaseMs"] | (legacy ? 0U : 15000U);
  copyText(snapshot.module_id, sizeof(snapshot.module_id), module_id_);
  copyText(snapshot.device_id, sizeof(snapshot.device_id), device_id_);

  JsonVariantConst movement = root["movementCue"];
  if (!movement.isNull()) {
    snapshot.tag_contract_supported = true;
    const char *mode = movement["mode"] | "none";
    if (!parseTileMovementCue(mode, snapshot.movement.mode)) {
      Serial.printf("[NET] invalid_movement_cue=%s\r\n", mode);
      return false;
    }
    snapshot.movement.player_id = movement["playerId"] | 0U;
    snapshot.movement.revision =
        movement["revision"] | static_cast<std::uint64_t>(0);
    if (snapshot.movement.mode == TileMovementCue::None) {
      snapshot.movement.player_id = 0U;
    } else if (snapshot.movement.player_id == 0U ||
               snapshot.movement.player_id > kMaximumPlayers) {
      Serial.printf("[NET] invalid_movement_player=%u\r\n",
                    static_cast<unsigned>(snapshot.movement.player_id));
      return false;
    }
  }

  if (!assigned) {
    snapshot.link = TileNetworkLink::NoFreeTile;
    copyText(snapshot.source, sizeof(snapshot.source), "none");
    return true;
  }

  TileAssignmentDto assignment{};
  assignment.assigned = true;
  const char *source = root["source"] | static_cast<const char *>(nullptr);
  if (source == nullptr || source[0] == '\0') {
    source = item["source"] | (legacy ? "manual" : "auto");
  }
  assignment.manual = equalsIgnoreCase(source, "manual");
  assignment.map_index = item["mapIndex"] | 0U;
  copyText(assignment.tile_id, sizeof(assignment.tile_id),
           item["tile_id"] | "");
  copyText(assignment.display_name, sizeof(assignment.display_name),
           item["displayName"] | "");
  copyText(assignment.kind, sizeof(assignment.kind), item["kind"] | "");
  assignment.accent_rgb = item["accent"] | 0U;
  copyText(assignment.artwork_key, sizeof(assignment.artwork_key),
           item["artworkKey"] | "");
  assignment.purchase_price = item["purchase_price"] | 0U;
  assignment.owner_player = item["owner_player"] | 0U;
  copyText(assignment.owner_display_name,
           sizeof(assignment.owner_display_name),
           item["owner_display_name"] | "");
  assignment.owner_rgb = item["owner_color"] | 0U;
  assignment.revision = item["revision"] | snapshot.server_revision;

  if (!applyTileAssignment(assignment, snapshot.tile)) return false;
  snapshot.assigned = true;
  snapshot.assignment_revision = assignment.revision;
  snapshot.link = assignment.manual ? TileNetworkLink::OnlineManual
                                    : TileNetworkLink::OnlineAuto;
  copyText(snapshot.source, sizeof(snapshot.source),
           assignment.manual ? "manual" : "auto");
  return true;
}

}  // namespace gridopoly::tile
