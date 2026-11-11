#pragma once

#include <PlannerTypes.h>

#include <cstdint>
#include <string>

#include "activities/Activity.h"

/**
 * Planner → Calendar → one event, read in full.
 *
 * The agenda rows cut long titles and places to one line; this screen shows
 * them whole, wrapped over as many lines as fit. Any button closes it.
 */
class CalendarEventActivity final : public Activity {
 public:
  CalendarEventActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const planner::CalendarEvent& event,
                        int32_t onDay, int32_t todayYmd);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  // Copied: the agenda behind this screen may be rebuilt while it is open.
  std::string title;
  std::string location;
  std::string calendar;
  char dayLabel[48] = {};
  char timeLabel[32] = {};
  bool needsPaint = true;
};
