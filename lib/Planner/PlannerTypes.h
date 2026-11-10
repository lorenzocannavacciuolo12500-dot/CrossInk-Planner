#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Plain data model for the Planner (to-do list + calendar snapshot).
// This library has no Arduino/SD dependency so it can be unit-tested natively;
// src/PlannerStore owns the SD card I/O. See docs/planner-sync.md.
namespace planner {

constexpr size_t kMaxTodos = 200;
constexpr size_t kMaxEvents = 150;
constexpr size_t kMaxIdBytes = 64;
constexpr size_t kMaxStoreIdBytes = 16;
constexpr size_t kMaxTitleBytes = 120;
constexpr size_t kMaxLocationBytes = 80;
constexpr size_t kMaxCalendarBytes = 40;
constexpr int kMinutesPerDay = 24 * 60;

// Dates are stored as yyyymmdd integers (20261110); 0 means "no date".
struct TodoItem {
  std::string id;
  std::string title;
  int32_t due = 0;
  bool done = false;
  // Tombstone for an on-device delete the companion has not seen yet.
  bool deleted = false;
  // Unix seconds (UTC) of the last change; 0 when the clock was not valid.
  int64_t updatedAt = 0;
  // Device revision of the last on-device change; 0 = state came from the companion.
  uint32_t localRev = 0;
};

struct TodoList {
  std::string storeId;
  // Incremented by every change made on the device. Used as the sync cursor.
  uint32_t revision = 0;
  std::vector<TodoItem> items;
};

struct CalendarEvent {
  std::string title;
  std::string location;
  std::string calendar;
  int32_t startDate = 0;
  int16_t startMin = 0;  // minutes after local midnight
  int32_t endDate = 0;
  int16_t endMin = 0;
  bool allDay = false;
};

struct Calendar {
  int64_t generatedAt = 0;
  int16_t utcOffsetMin = 0;
  int32_t rangeStart = 0;
  int32_t rangeEnd = 0;
  std::vector<CalendarEvent> events;
};

// Truncates s to at most maxBytes without splitting a UTF-8 sequence.
void truncateUtf8(std::string& s, size_t maxBytes);
// Trims surrounding whitespace, turns line breaks into spaces, then truncates.
void cleanText(std::string& s, size_t maxBytes);

}  // namespace planner
