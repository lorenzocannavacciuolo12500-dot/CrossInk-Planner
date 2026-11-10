#pragma once

#include <vector>

#include "PlannerListActivity.h"

/**
 * Home → Planner: entry point with a short summary of the to-do list and the
 * calendar. Opens TodoListActivity and CalendarActivity.
 */
class PlannerActivity final : public PlannerListActivity {
 public:
  PlannerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  int itemCount() const override { return 2; }
  void fillItems(std::vector<freeink::ui::ListItem>& items) override;
  void activate(int index, bool longPress) override;
  const char* screenTitle() const override;
  const char* confirmHint() const override;

 private:
  char todoSummary[48] = {};
  char calendarSummary[64] = {};

  void refreshSummaries();
};
