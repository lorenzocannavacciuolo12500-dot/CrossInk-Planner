#include "PlannerPageDots.h"

#include <GfxRenderer.h>

#include "UITheme.h"

namespace PlannerPageDots {
namespace {
constexpr int kDotSize = 8;
constexpr int kCurrentDotSize = 14;
constexpr int kPitch = 24;  // distance between dot centres
constexpr int kPageCount = 3;
// Render task only (Home and Planner renders), so no synchronisation needed.
bool gCenterReserved = false;
bool gClockOnly = false;
}  // namespace

bool centerReserved() { return gCenterReserved; }
bool clockOnlyStatusBar() { return gClockOnly; }
ClockOnlyStatusBar::ClockOnlyStatusBar(const bool active) : active(active) {
  if (active) gClockOnly = true;
}
ClockOnlyStatusBar::~ClockOnlyStatusBar() {
  if (active) gClockOnly = false;
}
CenterReservation::CenterReservation(const bool active) : active(active) {
  if (active) gCenterReserved = true;
}
CenterReservation::~CenterReservation() {
  if (active) gCenterReserved = false;
}

void draw(const GfxRenderer& renderer, const Page current) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Centre of the status-bar row; the item configured there moves aside
  // (see CenterReservation and BaseTheme::drawDisplayStatusBar).
  const int centerY = UITheme::getTopStatusBarY(renderer) + metrics.batteryHeight / 2 + 4;
  const int groupCenterX = renderer.getScreenWidth() / 2;

  for (int i = 0; i < kPageCount; i++) {
    const int size = i == static_cast<int>(current) ? kCurrentDotSize : kDotSize;
    const int cx = groupCenterX + (i - 1) * kPitch;
    renderer.fillRoundedRect(cx - size / 2, centerY - size / 2, size, size, size / 2, Color::Black);
  }
}
}  // namespace PlannerPageDots
