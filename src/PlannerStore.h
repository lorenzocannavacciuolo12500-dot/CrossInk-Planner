#pragma once

#include <PlannerTypes.h>

#include <cstdint>
#include <ctime>
#include <mutex>

/**
 * SD card persistence for the Planner (to-do list and calendar snapshot).
 * Files live under /.crosspoint/planner/ and are decoded/encoded in a streaming
 * fashion (lib/Planner), so loading never builds a JSON tree in RAM.
 *
 * Data is not kept resident: the Planner screens and the web API load what they
 * need and drop it when done. Callers that load, modify and save must hold
 * PlannerStore::lock() for the whole sequence, because the web server task and
 * the main task may both touch the files.
 */
namespace PlannerStore {

constexpr const char* kDir = "/.crosspoint/planner";
constexpr const char* kTodosPath = "/.crosspoint/planner/todos.json";
constexpr const char* kCalendarPath = "/.crosspoint/planner/calendar.json";

std::mutex& lock();

// A missing file yields an empty list with a fresh storeId and returns true.
// Returns false (and an empty list) only when an existing file is unreadable.
bool loadTodos(planner::TodoList& out);
bool saveTodos(const planner::TodoList& list);

// A missing file yields an empty calendar and returns true.
bool loadCalendar(planner::Calendar& out);
bool saveCalendar(const planner::Calendar& calendar);

// UTC unix time for stamping edits, or 0 when the device clock is not trusted.
int64_t nowForEdits();

// Bumped by the web API whenever the companion app changed the stored data or
// the clock, so an open Planner screen knows to reload what it shows.
void noteExternalChange();
uint32_t externalChangeCount();

// Planner page open in the foreground (for resume-after-wake), or None.
enum class ResumePage : uint8_t { None = 0, Todo = 1, Calendar = 2 };
void setActivePage(ResumePage page);
ResumePage activePage();

}  // namespace PlannerStore
