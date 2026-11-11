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

#include "CrossPointSettings.h"
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
constexpr int kIconGap = 10;
constexpr int kListIndent = 26;  // tasks of a list, set in under its name

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

// The events of one day, as "time  title" lines that stop above `limit`
// (with "+N more" for the rest); `emptyText` when there are none.
int drawDayEvents(const GfxRenderer& renderer, const planner::Calendar& calendar, const int32_t day, int y,
                  const int limit, const int width, const char* emptyText) {
  const int bodyLine = renderer.getLineHeight(UI_10_FONT_ID) + kRowGap;
  // Time column sized from the widest time text ("22:00-23:30"), so ranges
  // are not truncated whatever the font metrics.
  const int timeColumn = renderer.getTextWidth(UI_10_FONT_ID, "00:00-00:00") + 2 * kIconGap;
  size_t shown = 0;
  size_t total = 0;
  for (const auto& ev : calendar.events) {
    if (day == 0 || !coversDay(ev, day)) continue;
    total++;
    // Keep one line free for "+N more" unless this is the last event.
    if (y + 2 * bodyLine > limit) continue;
    char time[24];
    PlannerFormat::eventTime(ev, day, time, sizeof(time));
    drawLine(renderer, UI_10_FONT_ID, kMargin, y, time, timeColumn - kIconGap);
    y = drawLine(renderer, UI_10_FONT_ID, kMargin + timeColumn, y, ev.title.c_str(), width - timeColumn) + kRowGap;
    shown++;
  }
  if (total == 0) y = drawLine(renderer, UI_10_FONT_ID, kMargin, y, emptyText, width) + kRowGap;
  return drawMore(renderer, y, total - shown, width);
}

// After the hour chosen in Settings the evening agenda looks at tomorrow too.
bool showTomorrow() {
  const int hour = CrossPointSettings::agendaTomorrowHour(SETTINGS.agendaTomorrowFrom);
  if (hour < 0) return false;
  const int minute = PlannerFormat::minuteOfDay();
  return minute >= hour * 60;
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
    const bool tomorrow = today != 0 && showTomorrow();
    // With tomorrow below, today leaves room for its heading and one line.
    const int todayBottom = tomorrow ? eventsBottom - 2 * bodyLine : eventsBottom;
    y = drawDayEvents(renderer, calendar, today, y, todayBottom, width, tr(STR_PLANNER_AGENDA_NO_EVENTS));
    if (tomorrow) {
      y += kRowGap;
      y = drawLine(renderer, UI_10_FONT_ID, kMargin, y, tr(STR_PLANNER_TOMORROW), width, EpdFontFamily::BOLD) + kRowGap;
      y = drawDayEvents(renderer, calendar, planner::addDays(today, 1), y, eventsBottom + bodyLine, width,
                        tr(STR_PLANNER_AGENDA_NO_EVENTS_TOMORROW));
    }
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
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int rowHeight = std::max(static_cast<int>(icon.h), lineHeight) + kRowGap;
    // A task at `x`: checkbox, then its title.
    const auto drawTask = [&](const planner::TodoItem& task, const int x) {
      const int textX = x + icon.w + kIconGap;
      const int textY = y + (rowHeight - kRowGap - lineHeight) / 2;
      drawLucideIcon(renderer, icon, x, y + (rowHeight - kRowGap - icon.h) / 2);
      drawLine(renderer, UI_10_FONT_ID, textX, textY, task.title.c_str(), width - (textX - kMargin));
      y += rowHeight;
    };
    // Like the To-Do page: tasks in no list and whole lists, in the companion
    // app's order. A list's name has no box; its open tasks sit under it, set
    // in a little; a task after the list lines up with the name again.
    size_t open = 0;
    size_t shown = 0;
    bool full = false;  // once something did not fit, the rest is "+N more"
    for (const auto& block : planner::visibleTodoLayout(todos)) {
      const bool isList = block.listIndex != planner::kNoList;
      const size_t openHere = static_cast<size_t>(std::count_if(
          block.items.begin(), block.items.end(), [&todos](const uint16_t i) { return !todos.items[i].done; }));
      if (openHere == 0) continue;
      open += openHere;
      // Keep one row free for "+N more"; a list needs its name and one task.
      const int needed = (isList ? rowHeight : 0) + 2 * rowHeight;
      if (full || y + needed > footerTop) {
        full = true;
        continue;
      }
      if (isList) {
        const int textY = y + (rowHeight - kRowGap - lineHeight) / 2;
        drawLine(renderer, UI_10_FONT_ID, kMargin, textY, todos.lists[block.listIndex].c_str(), width,
                 EpdFontFamily::BOLD);
        y += rowHeight;
      }
      for (const uint16_t index : block.items) {
        const auto& task = todos.items[index];
        if (task.done) continue;
        if (full || y + 2 * rowHeight > footerTop) {
          full = true;
          continue;
        }
        drawTask(task, isList ? kMargin + kListIndent : kMargin);
        shown++;
      }
    }
    if (open == 0) {
      y = drawLine(renderer, UI_10_FONT_ID, kMargin, y, tr(STR_PLANNER_AGENDA_NO_TODOS), width) + kRowGap;
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
