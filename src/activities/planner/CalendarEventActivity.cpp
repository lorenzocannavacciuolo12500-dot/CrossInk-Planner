#include "CalendarEventActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <vector>

#include "PlannerFormat.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kMargin = 28;
constexpr int kTop = 40;
constexpr int kGap = 10;

// Breaks `text` on spaces so every line fits in `maxWidth`. A word longer than
// one line is cut by the renderer instead of overflowing.
void wrap(const GfxRenderer& renderer, const int fontId, const char* text, const int maxWidth,
          const EpdFontFamily::Style style, std::vector<std::string>& out, const size_t maxLines) {
  std::string line;
  const char* cursor = text;
  while (*cursor != '\0' && out.size() < maxLines) {
    const char* space = cursor;
    while (*space != '\0' && *space != ' ') space++;
    std::string word(cursor, static_cast<size_t>(space - cursor));
    const std::string candidate = line.empty() ? word : line + ' ' + word;
    if (!line.empty() && renderer.getTextWidth(fontId, candidate.c_str(), style) > maxWidth) {
      out.push_back(line);
      line = std::move(word);
    } else {
      line = candidate;
    }
    cursor = (*space == '\0') ? space : space + 1;
  }
  if (!line.empty() && out.size() < maxLines) out.push_back(line);
}

int drawWrapped(const GfxRenderer& renderer, const int fontId, const int y, const char* text, const int width,
                const EpdFontFamily::Style style, const size_t maxLines) {
  std::vector<std::string> lines;
  lines.reserve(maxLines);
  wrap(renderer, fontId, text, width, style, lines, maxLines);
  int top = y;
  for (const auto& line : lines) {
    const std::string visible = renderer.truncatedText(fontId, line.c_str(), width, style);
    renderer.drawText(fontId, kMargin, top, visible.c_str(), true, style);
    top += renderer.getLineHeight(fontId);
  }
  return top;
}
}  // namespace

CalendarEventActivity::CalendarEventActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const planner::CalendarEvent& event, const int32_t onDay,
                                             const int32_t todayYmd)
    : Activity("PlannerEvent", renderer, mappedInput),
      title(event.title),
      location(event.location),
      calendar(event.calendar) {
  PlannerFormat::longDayLabel(onDay, todayYmd, dayLabel, sizeof(dayLabel));
  PlannerFormat::eventTime(event, onDay, timeLabel, sizeof(timeLabel));
}

void CalendarEventActivity::onEnter() {
  Activity::onEnter();
  requestUpdate(true);
}

void CalendarEventActivity::render(RenderLock&&) {
  if (!needsPaint) return;
  needsPaint = false;

  const int width = renderer.getScreenWidth() - 2 * kMargin;
  renderer.clearScreen();

  int y = kTop;
  y = drawWrapped(renderer, UI_12_FONT_ID, y, dayLabel, width, EpdFontFamily::REGULAR, 2);
  y += kGap / 2;
  y = drawWrapped(renderer, UI_10_FONT_ID, y, timeLabel, width, EpdFontFamily::BOLD, 1);
  y += kGap;
  renderer.drawLine(kMargin, y, kMargin + width, y, 2, true);
  y += kGap + 6;

  y = drawWrapped(renderer, UI_12_FONT_ID, y, title.c_str(), width, EpdFontFamily::BOLD, 6);
  if (!location.empty()) {
    y += kGap;
    y = drawWrapped(renderer, UI_10_FONT_ID, y, location.c_str(), width, EpdFontFamily::REGULAR, 3);
  }
  if (!calendar.empty()) {
    y += kGap / 2;
    drawWrapped(renderer, SMALL_FONT_ID, y, calendar.c_str(), width, EpdFontFamily::REGULAR, 1);
  }

  const int bottom = renderer.getScreenHeight() - kTop / 2 - renderer.getLineHeight(SMALL_FONT_ID);
  const std::string hint = renderer.truncatedText(SMALL_FONT_ID, tr(STR_PLANNER_EVENT_CLOSE_HINT), width);
  renderer.drawText(SMALL_FONT_ID, kMargin, bottom, hint.c_str(), true);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void CalendarEventActivity::loop() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back) || mappedInput.wasReleased(Button::Confirm) ||
      mappedInput.wasReleased(Button::PageForward) || mappedInput.wasReleased(Button::PageBack)) {
    finish();
  }
}
