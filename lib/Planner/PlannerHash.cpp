#include "PlannerHash.h"

#include <cstddef>
#include <string>

namespace planner {
namespace {
// 32-bit FNV-1a: tiny, no tables, good enough to detect changes.
constexpr uint32_t kFnvOffset = 2166136261u;
constexpr uint32_t kFnvPrime = 16777619u;

void mixBytes(uint32_t& h, const void* data, const size_t size) {
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; i++) {
    h ^= p[i];
    h *= kFnvPrime;
  }
}

void mixString(uint32_t& h, const std::string& s) {
  mixBytes(h, s.data(), s.size());
  const uint8_t separator = 0xFF;  // never valid UTF-8, so "ab"+"c" != "a"+"bc"
  mixBytes(h, &separator, 1);
}

template <typename T>
void mixValue(uint32_t& h, const T value) {
  mixBytes(h, &value, sizeof(value));
}
}  // namespace

uint32_t visibleTodosHash(const TodoList& list) {
  uint32_t h = kFnvOffset;
  for (const auto& item : list.items) {
    if (item.deleted) continue;
    mixString(h, item.id);
    mixString(h, item.title);
    mixValue(h, item.due);
    mixValue(h, static_cast<uint8_t>(item.done ? 1 : 0));
    mixValue(h, item.listIndex);
    mixValue(h, item.order);
  }
  for (const auto& name : list.lists) mixString(h, name);
  return h;
}

uint32_t calendarEventsHash(const Calendar& calendar) {
  uint32_t h = kFnvOffset;
  mixValue(h, calendar.utcOffsetMin);
  for (const auto& ev : calendar.events) {
    mixString(h, ev.title);
    mixString(h, ev.location);
    mixString(h, ev.calendar);
    mixValue(h, ev.startDate);
    mixValue(h, ev.startMin);
    mixValue(h, ev.endDate);
    mixValue(h, ev.endMin);
    mixValue(h, static_cast<uint8_t>(ev.allDay ? 1 : 0));
  }
  return h;
}

}  // namespace planner
