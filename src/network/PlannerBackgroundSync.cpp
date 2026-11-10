#include "PlannerBackgroundSync.h"

#include <Arduino.h>
#include <HalPowerManager.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdio>
#include <optional>

#include "SdCardFontSystem.h"
#include "WifiCredentialStore.h"
#include "network/CrossPointWebServer.h"

namespace {
// The heap counts as "back to normal" after Wi-Fi when its largest free block
// is at least this share of the one measured before the radio started.
constexpr uint32_t kDefragPercent = 85;
}  // namespace

PlannerBackgroundSync::PlannerBackgroundSync() = default;

PlannerBackgroundSync::~PlannerBackgroundSync() { stop(); }

void PlannerBackgroundSync::setState(const State next) {
  if (state_ == next) return;
  state_ = next;
  stateSince = millis();
}

void PlannerBackgroundSync::start(GfxRenderer& renderer) {
  if (state_ != State::Off) return;
  credentialIndex = 0;
  triedLast = false;
  offlineSince = 0;
  ip_[0] = '\0';
  if (WIFI_STORE.getCredentialCount() == 0) {
    setState(State::NoSavedNetwork);
    return;
  }
  // Same preparation as the other Wi-Fi screens: the radio needs several
  // contiguous driver buffers, so drop the SD reader font and its catalog
  // (the reader reloads them on entry).
  sdFontSystem.releaseForNetwork(renderer);
  setState(State::Waiting);
}

bool PlannerBackgroundSync::beginNextNetwork() {
  std::optional<WifiCredential> cred;
  const std::string last = WIFI_STORE.getLastConnectedSsid();
  if (!triedLast) {
    triedLast = true;
    if (!last.empty()) cred = WIFI_STORE.findCredential(last);
  }
  while (!cred) {
    auto next = WIFI_STORE.getCredentialAt(credentialIndex);
    if (!next) return false;  // every saved network was tried
    credentialIndex++;
    if (next->ssid != last) cred = std::move(next);
  }

  // The radio needs full CPU speed; once Wi-Fi is on, the power manager keeps
  // it there by itself (HalPowerManager::setPowerSaving).
  HalPowerManager::Lock powerLock;
  if (!radioUsed) {
    baselineMaxAlloc = ESP.getMaxAllocHeap();
    radioUsed = true;
  }
  LOG_INF("PLNSYNC", "Joining %s free=%u maxAlloc=%u", cred->ssid.c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  WiFi.persistent(false);  // credentials live in WifiCredentialStore, not in NVS
  if (!WiFi.mode(WIFI_STA)) {
    LOG_ERR("PLNSYNC", "Could not enable station mode");
    return false;
  }
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  const String hostname = "CrossPoint-Reader-" + mac;
  WiFi.setHostname(hostname.c_str());
  if (cred->password.empty()) {
    WiFi.begin(cred->ssid.c_str());
  } else {
    WiFi.begin(cred->ssid.c_str(), cred->password.c_str());
  }
  setState(State::Connecting);
  return true;
}

bool PlannerBackgroundSync::startServer() {
  server = makeUniqueNoThrow<CrossPointWebServer>();
  if (!server) {
    LOG_ERR("PLNSYNC", "No memory for the web server");
    return false;
  }
  server->begin();
  if (!server->isRunning()) {
    LOG_ERR("PLNSYNC", "Web server did not start");
    server.reset();
    return false;
  }
  // begin() turns modem sleep off for File Transfer throughput. Sync requests
  // are a few KB, so let the radio doze between beacons to save battery.
  WiFi.setSleep(true);
  const IPAddress addr = WiFi.localIP();
  std::snprintf(ip_, sizeof(ip_), "%u.%u.%u.%u", addr[0], addr[1], addr[2], addr[3]);
  LOG_INF("PLNSYNC", "Sync server on %s free=%u maxAlloc=%u", ip_, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  return true;
}

bool PlannerBackgroundSync::poll() {
  const State before = state_;
  const unsigned long now = millis();
  switch (state_) {
    case State::Off:
    case State::NoSavedNetwork:
    case State::Unavailable:
      break;
    case State::Waiting:
      if (now - stateSince >= kStartDelayMs && !beginNextNetwork()) {
        shutdownRadio();
        setState(State::Unavailable);
      }
      break;
    case State::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        if (startServer()) {
          setState(State::Online);
        } else {
          shutdownRadio();
          setState(State::Unavailable);
        }
      } else if (now - stateSince >= kConnectTimeoutMs) {
        LOG_INF("PLNSYNC", "Connection timed out (status=%d)", static_cast<int>(WiFi.status()));
        WiFi.disconnect(false);
        if (!beginNextNetwork()) {
          shutdownRadio();
          setState(State::Unavailable);
        }
      }
      break;
    case State::Online:
      if (WiFi.status() != WL_CONNECTED) {
        // The driver reconnects by itself; give up only after a long outage.
        if (offlineSince == 0) offlineSince = now;
        if (now - offlineSince >= kGiveUpOfflineMs) {
          LOG_INF("PLNSYNC", "Network lost; background sync off");
          shutdownRadio();
          setState(State::Unavailable);
        }
        break;
      }
      offlineSince = 0;
      for (int i = 0; i < kRequestsPerPoll && server && server->isRunning(); i++) server->handleClient();
      break;
  }
  return state_ != before;
}

void PlannerBackgroundSync::shutdownRadio() {
  if (server) {
    if (server->isRunning()) server->stop();
    server.reset();
  }
  if (radioUsed && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    WiFi.mode(WIFI_OFF);
  }
  ip_[0] = '\0';
}

bool PlannerBackgroundSync::stop() {
  shutdownRadio();
  setState(State::Off);
  if (!radioUsed) return false;
  radioUsed = false;
  const uint32_t maxAlloc = ESP.getMaxAllocHeap();
  LOG_INF("PLNSYNC", "Wi-Fi off free=%u maxAlloc=%u (before %u)", ESP.getFreeHeap(), maxAlloc, baselineMaxAlloc);
  return static_cast<uint64_t>(maxAlloc) * 100 < static_cast<uint64_t>(baselineMaxAlloc) * kDefragPercent;
}
