#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

class CrossPointWebServer;
class GfxRenderer;

/**
 * Background Wi-Fi + web server for the To-Do List and Calendar pages, so the
 * companion app can sync while one of them is open (no File Transfer needed).
 *
 * Non-blocking: start() only schedules the connection, poll() advances it from
 * the activity loop and serves HTTP requests (on the main task, like the File
 * Transfer screen), stop() tears everything down. It joins the last network
 * used (or the other saved ones) and never asks for a password: without a saved
 * network it just reports NoSavedNetwork.
 */
class PlannerBackgroundSync {
 public:
  enum class State : uint8_t { Off, Waiting, Connecting, Online, NoSavedNetwork, Unavailable };

  PlannerBackgroundSync();
  ~PlannerBackgroundSync();
  PlannerBackgroundSync(const PlannerBackgroundSync&) = delete;
  PlannerBackgroundSync& operator=(const PlannerBackgroundSync&) = delete;

  // Arms the connection; the radio starts kStartDelayMs later so the page's
  // first paint is not slowed down by the Wi-Fi driver start.
  void start(GfxRenderer& renderer);
  // Returns true when the state shown to the user changed.
  bool poll();
  // Stops the server and the radio. Returns true when Wi-Fi was used and the
  // heap no longer looks like before it started (largest free block shrank):
  // the caller should then reboot to defragment, like the other Wi-Fi screens.
  bool stop();

  State state() const { return state_; }
  const char* ip() const { return ip_; }

 private:
  static constexpr unsigned long kStartDelayMs = 1500;
  static constexpr unsigned long kConnectTimeoutMs = 15000;
  static constexpr unsigned long kGiveUpOfflineMs = 60000;
  static constexpr int kRequestsPerPoll = 8;

  std::unique_ptr<CrossPointWebServer> server;
  State state_ = State::Off;
  unsigned long stateSince = 0;
  unsigned long offlineSince = 0;
  size_t credentialIndex = 0;  // next saved network to try after the last used one
  bool triedLast = false;
  bool radioUsed = false;
  bool radioBroken = false;  // WiFi.mode() failed: never touch the driver again
  uint32_t baselineMaxAlloc = 0;
  char ip_[16] = {};

  bool beginNextNetwork();
  bool startServer();
  void setState(State next);
  void shutdownRadio();
};
