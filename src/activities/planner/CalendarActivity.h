#pragma once

#include <PlannerTypes.h>

#include <cstdint>
#include <string>
#include <vector>

#include "PlannerListActivity.h"

/**
 * Planner → Calendar: read-only agenda from today onwards, built from the last
 * snapshot the companion app pushed (calendar.json). Laid out like the
 * companion app: a heading per day ("Martedì 17 novembre - 1 evento"), then
 * one row per event with the title and "time - place - calendar" below it.
 * Multi-day events appear once per day they cover. Confirm on a row opens the
 * event on its own screen, where a long title or place is not cut short.
 * The Next Page side button (right edge on the X3) goes back to Home.
 */
class CalendarActivity final : public PlannerListActivity {
 public:
  CalendarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 protected:
  int itemCount() const override;
  void fillItems(std::vector<freeink::ui::ListItem>& items) override;
  void activate(int index, bool longPress) override;
  const char* screenTitle() const override;
  const char* confirmHint() const override;
  const char* emptyText() const override;
  void onSideButton(bool next) override;
  void reloadData() override { reload(); }
  bool pageDot(PlannerPageDots::Page& page) const override {
    page = PlannerPageDots::Page::Calendar;
    return true;
  }

 private:
  struct Row {
    uint16_t event;  // index into calendar.events
    int32_t day;     // the day this row represents
  };

  planner::Calendar calendar;
  std::vector<Row> agenda;
  int32_t todayYmd = 0;
  bool hasSnapshot = false;
  char updatedLabel[64] = {};
  // Row text buffers; rebuilt on every render, reserved so pointers stay valid.
  std::vector<std::string> subtitles;
  std::vector<std::string> headings;  // "Martedì 17 novembre - 2 eventi", one per day

  void reload();
  void buildAgenda();
};
