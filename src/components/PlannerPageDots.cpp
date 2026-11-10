#include "PlannerPageDots.h"

#include <GfxRenderer.h>

#include "CrossPointSettings.h"
#include "UITheme.h"

namespace PlannerPageDots {
namespace {
constexpr int kDotSize = 8;
constexpr int kCurrentDotSize = 14;
constexpr int kPitch = 24;  // distance between dot centres
constexpr int kPageCount = 3;
}  // namespace

void draw(const GfxRenderer& renderer, const Page current) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Same row as the status bar (battery/clock). Its centre slot is empty by
  // default; when the user put something there, move the dots to the left slot.
  const int centerY = UITheme::getTopStatusBarY(renderer) + metrics.batteryHeight / 2 + 4;
  const bool centerSlotUsed = SETTINGS.displayStatusBar.slots[1] != ReaderStatusBarItem::Empty;
  const int groupWidth = kPitch * (kPageCount - 1) + kCurrentDotSize;
  const int groupCenterX =
      centerSlotUsed ? metrics.contentSidePadding + groupWidth / 2 : renderer.getScreenWidth() / 2;

  for (int i = 0; i < kPageCount; i++) {
    const int size = i == static_cast<int>(current) ? kCurrentDotSize : kDotSize;
    const int cx = groupCenterX + (i - 1) * kPitch;
    renderer.fillRoundedRect(cx - size / 2, centerY - size / 2, size, size, size / 2, Color::Black);
  }
}
}  // namespace PlannerPageDots
