#pragma once

// Clock and random-number access for the Planner, with a native fallback so the
// Planner screens also build in the simulator (its HalClock has no Unix-time API
// and there is no esp_random()). On the device these are thin wrappers.

#include <HalClock.h>

#include <cstdint>
#include <ctime>

#ifdef SIMULATOR
#include <random>

namespace planner_platform {
inline uint32_t random32() {
  static std::mt19937 generator{std::random_device{}()};
  return static_cast<uint32_t>(generator());
}
inline bool getUnixTime(time_t& utcSeconds) {
  utcSeconds = std::time(nullptr);
  return true;
}
inline bool setUnixTime(time_t) { return true; }
}  // namespace planner_platform
#else
#include <Arduino.h>

namespace planner_platform {
inline uint32_t random32() { return esp_random(); }
inline bool getUnixTime(time_t& utcSeconds) { return halClock.getUnixTime(utcSeconds); }
inline bool setUnixTime(time_t utcSeconds) { return halClock.setFromUnixTime(utcSeconds); }
}  // namespace planner_platform
#endif
