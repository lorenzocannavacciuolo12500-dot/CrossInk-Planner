#pragma once

#include <cstdint>

class GfxRenderer;

/**
 * Three page dots at the top of the screen for the side-button pages:
 * Calendar <- Home -> To-Do List. The dot of the current page is larger.
 */
namespace PlannerPageDots {
enum class Page : uint8_t { Calendar = 0, Home = 1, Todo = 2 };

void draw(const GfxRenderer& renderer, Page current);

// While a guard is alive (one render of Home, To-Do List or Calendar) the dots
// own the centre of the status bar: an item configured there (e.g. the clock)
// is drawn in the left slot instead (or the right one if the left is taken).
bool centerReserved();
class CenterReservation {
 public:
  explicit CenterReservation(bool active = true);
  ~CenterReservation();
  CenterReservation(const CenterReservation&) = delete;
  CenterReservation& operator=(const CenterReservation&) = delete;

 private:
  bool active;
};

// While alive, the screen status bar shows only its clock / date items (the
// "Last screen" sleep frame of the planner pages: no battery, no dots).
bool clockOnlyStatusBar();
class ClockOnlyStatusBar {
 public:
  explicit ClockOnlyStatusBar(bool active);
  ~ClockOnlyStatusBar();
  ClockOnlyStatusBar(const ClockOnlyStatusBar&) = delete;
  ClockOnlyStatusBar& operator=(const ClockOnlyStatusBar&) = delete;

 private:
  bool active;
};
}  // namespace PlannerPageDots
