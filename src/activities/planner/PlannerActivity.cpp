#include "PlannerActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <PlannerTodos.h>

#include <cstdio>
#include <mutex>

#include "CalendarActivity.h"
#include "PlannerFormat.h"
#include "PlannerStore.h"
#include "TodoListActivity.h"

namespace fui = freeink::ui;

PlannerActivity::PlannerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : PlannerListActivity("Planner", renderer, mappedInput) {}

void PlannerActivity::onEnter() {
  refreshSummaries();
  PlannerListActivity::onEnter();
}

void PlannerActivity::refreshSummaries() {
  // Summaries need the counts only; both documents are dropped on return so
  // nothing stays resident while this menu is open.
  size_t openTasks = 0;
  size_t todayEvents = 0;
  size_t upcomingEvents = 0;
  bool calendarSynced = false;
  {
    std::lock_guard<std::mutex> guard(PlannerStore::lock());
    planner::TodoList todos;
    PlannerStore::loadTodos(todos);
    openTasks = planner::countOpenTodos(todos);

    if (Storage.exists(PlannerStore::kCalendarPath)) {
      planner::Calendar calendar;
      calendarSynced = PlannerStore::loadCalendar(calendar);
      int32_t today = PlannerFormat::today();
      if (today == 0) today = calendar.rangeStart;
      for (const auto& ev : calendar.events) {
        if (ev.endDate < today) continue;
        if (ev.startDate <= today) {
          todayEvents++;
        } else {
          upcomingEvents++;
        }
      }
    }
  }

  if (openTasks == 0) {
    std::snprintf(todoSummary, sizeof(todoSummary), "%s", tr(STR_PLANNER_ALL_DONE));
  } else {
    std::snprintf(todoSummary, sizeof(todoSummary), tr(STR_PLANNER_OPEN_TASKS_FORMAT),
                  static_cast<unsigned>(openTasks));
  }
  if (!calendarSynced) {
    std::snprintf(calendarSummary, sizeof(calendarSummary), "%s", tr(STR_PLANNER_NOT_SYNCED));
  } else {
    std::snprintf(calendarSummary, sizeof(calendarSummary), tr(STR_PLANNER_EVENTS_TODAY_FORMAT),
                  static_cast<unsigned>(todayEvents), static_cast<unsigned>(upcomingEvents));
  }
}

const char* PlannerActivity::screenTitle() const { return tr(STR_PLANNER); }

const char* PlannerActivity::confirmHint() const { return tr(STR_OPEN); }

void PlannerActivity::fillItems(std::vector<fui::ListItem>& items) {
  fui::ListItem todo;
  todo.label = tr(STR_PLANNER_TODO);
  todo.subtitle = todoSummary;
  items.push_back(todo);

  fui::ListItem calendar;
  calendar.label = tr(STR_PLANNER_CALENDAR);
  calendar.subtitle = calendarSummary;
  items.push_back(calendar);
}

void PlannerActivity::activate(const int index, bool) {
  std::unique_ptr<Activity> next;
  if (index == 0) {
    next = makeUniqueNoThrow<TodoListActivity>(renderer, mappedInput);
  } else if (index == 1) {
    next = makeUniqueNoThrow<CalendarActivity>(renderer, mappedInput);
  }
  if (!next) {
    LOG_ERR("PLN", "OOM: planner screen %d", index);
    return;
  }
  startActivityForResult(std::move(next), [this](const ActivityResult&) {
    refreshSummaries();
    requestUpdate();
  });
}
