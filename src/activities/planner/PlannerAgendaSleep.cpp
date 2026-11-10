#include "PlannerAgendaSleep.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <PlannerDate.h>
#include <PlannerTodos.h>
#include <PlannerTypes.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "PlannerFormat.h"
#include "PlannerStore.h"
#include "components/UiAppHelpers.h"
#include "components/icons/plannerCheckboxIcons.h"
#include "fontIds.h"

namespace PlannerAgendaSleep {
namespace {
constexpr int kMargin = 28;
constexpr int kTop = 36;
constexpr int kBottom = 24;
constexpr int kSectionGap = 22;
constexpr int kRowGap = 8;
constexpr int kTimeColumn = 118;  // fits "09:00-10:00" in UI_10
constexpr int kIconGap = 10;

// One line of text, truncated to `maxWidth`; returns the next free y.
int drawLine(const GfxRenderer& renderer, const int fontId, const int x, const int y, const char* text,
             const int maxWidth, const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const std::string visible = renderer.truncatedText(fontId, text, std::max(1, maxWidth), style);
  renderer.drawText(fontId, x, y, visible.c_str(), true, style);
  return y + renderer.getLineHeight(fontId);
}

int drawSectionTitle(const GfxRenderer& renderer, const int y, const char* title, const int width) {
  const int next = drawLine(renderer, UI_12_FONT_ID, kMargin, y, title, width, EpdFontFamily::BOLD);
  renderer.drawLine(kMargin, next + 2, kMargin + width, next + 2, 2, true);
  return next + 2 + kRowGap + 2;
}

// "+3 more" under a list that did not fit.
int drawMore(const GfxRenderer& renderer, const int y, const size_t hidden, const int width) {
  if (hidden == 0) return y;
  char buf[32];
  std::snprintf(buf, sizeof(buf), tr(STR_PLANNER_AGENDA_MORE_FORMAT), static_cast<unsigned>(hidden));
  return drawLine(renderer, SMALL_FONT_ID, kMargin, y, buf, width) + kRowGap;
}

bool coversDay(const planner::CalendarEvent& ev, const int32_t day) {
  // An event ending exactly at midnight does not occupy its end day.
  const int32_t lastDay =
      (ev.endDate > ev.startDate && ev.endMin == 0 && !ev.allDay) ? planner::addDays(ev.endDate, -1) : ev.endDate;
  return ev.startDate <= day && day <= lastDay;
}
}  // namespace

void draw(const GfxRenderer& renderer) {
  renderer.clearScreen();
  const int width = renderer.getScreenWidth() - 2 * kMargin;
  const int bottom = renderer.getScreenHeight() - kBottom;
  const int footerTop = bottom - renderer.getLineHeight(SMALL_FONT_ID);
  const int bodyLine = renderer.getLineHeight(UI_10_FONT_ID) + kRowGap;

  // The calendar is loaded first and released before the to-do list, so only
  // one of the two (each up to a few tens of KB) is resident at a time.
  int32_t today = PlannerFormat::today();
  int y = kTop;
  int64_t generatedAt = 0;
  {
    planner::Calendar calendar;
    {
      std::lock_guard<std::mutex> guard(PlannerStore::lock());
      if (!PlannerStore::loadCalendar(calendar)) LOG_ERR("PLN", "Agenda sleep: calendar.json unreadable");
    }
    if (today == 0) today = calendar.rangeStart;  // clock not set yet: use the snapshot's first day
    generatedAt = calendar.generatedAt;

    char dateText[40] = {};
    PlannerFormat::dayLabel(today, 0, dateText, sizeof(dateText));  // 0: always "Tue 10 Nov"
    y = drawLine(renderer, UI_12_FONT_ID, kMargin, y, dateText[0] ? dateText : tr(STR_PLANNER), width,
                 EpdFontFamily::BOLD);
    y += kSectionGap;

    // Events get at most half of the remaining height; tasks get the rest.
    y = drawSectionTitle(renderer, y, tr(STR_PLANNER_TODAY), width);
    const int eventsBottom = y + (footerTop - y) / 2;
    size_t shown = 0;
    size_t total = 0;
    for (const auto& ev : calendar.events) {
      if (today == 0 || !coversDay(ev, today)) continue;
      total++;
      // Keep one line free for "+N more" unless this is the last event.
      if (y + 2 * bodyLine > eventsBottom) continue;
      char time[24];
      PlannerFormat::eventTime(ev, today, time, sizeof(time));
      drawLine(renderer, UI_10_FONT_ID, kMargin, y, time, kTimeColumn - kIconGap);
      y = drawLine(renderer, UI_10_FONT_ID, kMargin + kTimeColumn, y, ev.title.c_str(), width - kTimeColumn) + kRowGap;
      shown++;
    }
    if (total == 0) {
      y = drawLine(renderer, UI_10_FONT_ID, kMargin, y, tr(STR_PLANNER_AGENDA_NO_EVENTS), width) + kRowGap;
    }
    y = drawMore(renderer, y, total - shown, width);
  }
  y += kSectionGap;

  {
    planner::TodoList todos;
    {
      std::lock_guard<std::mutex> guard(PlannerStore::lock());
      if (!PlannerStore::loadTodos(todos)) LOG_ERR("PLN", "Agenda sleep: todos.json unreadable");
    }
    y = drawSectionTitle(renderer, y, tr(STR_PLANNER_TODO), width);
    const auto& icon = icon_planner_checkbox_empty_24;
    const int rowHeight = std::max(static_cast<int>(icon.h), renderer.getLineHeight(UI_10_FONT_ID)) + kRowGap;
    const int textX = kMargin + icon.w + kIconGap;
    const std::vector<uint16_t> order = planner::visibleTodoOrder(todos);
    size_t open = 0;
    size_t shown = 0;
    for (const uint16_t index : order) {
      const auto& task = todos.items[index];
      if (task.done) break;  // open tasks come first
      open++;
      if (y + 2 * rowHeight > footerTop) continue;
      const int textY = y + (rowHeight - kRowGap - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
      drawLucideIcon(renderer, icon, kMargin, y + (rowHeight - kRowGap - icon.h) / 2);
      drawLine(renderer, UI_10_FONT_ID, textX, textY, task.title.c_str(), width - (textX - kMargin));
      y += rowHeight;
      shown++;
    }
    if (open == 0) {
      y = drawLine(renderer, UI_10_FONT_ID, kMargin, y, tr(STR_PLANNER_ALL_DONE), width) + kRowGap;
    }
    drawMore(renderer, y, open - shown, width);
  }

  if (generatedAt > 0) {
    char stamp[48];
    char footer[64];
    PlannerFormat::stamp(generatedAt, today, stamp, sizeof(stamp));
    std::snprintf(footer, sizeof(footer), tr(STR_PLANNER_UPDATED_FORMAT), stamp);
    drawLine(renderer, SMALL_FONT_ID, kMargin, footerTop, footer, width);
  }
}

}  // namespace PlannerAgendaSleep
