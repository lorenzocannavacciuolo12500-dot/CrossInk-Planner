#pragma once

#include <cstdint>

#include "PlannerTypes.h"

namespace planner {

// Fingerprints of what the Planner screens show, so a sync that changed
// nothing visible does not reload (and refresh) the e-ink page.
// Tasks: non-deleted items in list order (id, title, due, done).
uint32_t visibleTodosHash(const TodoList& list);
// Calendar: every event field, not the snapshot time.
uint32_t calendarEventsHash(const Calendar& calendar);

}  // namespace planner
