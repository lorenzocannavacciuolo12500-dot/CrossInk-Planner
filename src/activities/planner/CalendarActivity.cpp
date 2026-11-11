#include "CalendarActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <PlannerDate.h>

#include <algorithm>
#include <cstdio>
#include <mutex>

#include "CalendarEventActivity.h"
#include "PlannerFormat.h"
#include "PlannerStore.h"

namespace fui = freeink::ui;

namespace {
// Upper bound on agenda rows: long multi-day events are expanded per day, so
// cap the expansion to keep the row list (and its text buffers) small.
constexpr size_t kMaxAgendaRows = 250;
constexpr int kMaxDaysPerEvent = 14;
}  // namespace

CalendarActivity::CalendarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : PlannerListActivity("PlannerCalendar", renderer, mappedInput) {}

void CalendarActivity::onEnter() {
  reload();
  selectedIndex = 0;
  topIndex = 0;
  PlannerListActivity::onEnter();
}

void CalendarActivity::reload() {
  {
    std::lock_guard<std::mutex> guard(PlannerStore::lock());
    hasSnapshot = Storage.exists(PlannerStore::kCalendarPath);
    if (!PlannerStore::loadCalendar(calendar)) {
      LOG_ERR("PLN", "calendar.json unreadable");
      hasSnapshot = false;
    }
  }
  todayYmd = PlannerFormat::today();
  if (todayYmd == 0) todayYmd = calendar.rangeStart;  // clock not set yet: start from the snapshot
  buildAgenda();

  char stamp[48];
  PlannerFormat::stamp(calendar.generatedAt, todayYmd, stamp, sizeof(stamp));
  std::snprintf(updatedLabel, sizeof(updatedLabel), tr(STR_PLANNER_UPDATED_FORMAT), stamp);
}

void CalendarActivity::onExit() {
  calendar = planner::Calendar{};
  std::vector<Row>().swap(agenda);
  std::vector<std::string>().swap(subtitles);
  std::vector<std::string>().swap(headings);
  PlannerListActivity::onExit();
}

void CalendarActivity::buildAgenda() {
  agenda.clear();
  agenda.reserve(calendar.events.size());
  // Events are sorted by start date. Expand each one over the days it covers,
  // then order rows by day (stable, so all-day/start-time order is preserved).
  for (size_t i = 0; i < calendar.events.size() && agenda.size() < kMaxAgendaRows; i++) {
    const auto& ev = calendar.events[i];
    // An event ending exactly at midnight does not occupy its end day.
    const int32_t lastDay =
        (ev.endDate > ev.startDate && ev.endMin == 0 && !ev.allDay) ? planner::addDays(ev.endDate, -1) : ev.endDate;
    int32_t day = ev.startDate;
    if (todayYmd != 0 && day < todayYmd) day = todayYmd;
    for (int n = 0; day <= lastDay && n < kMaxDaysPerEvent && agenda.size() < kMaxAgendaRows; n++) {
      agenda.push_back(Row{static_cast<uint16_t>(i), day});
      day = planner::addDays(day, 1);
    }
  }
  std::stable_sort(agenda.begin(), agenda.end(), [](const Row& a, const Row& b) { return a.day < b.day; });
}

int CalendarActivity::itemCount() const {
  if (!hasSnapshot) return 0;
  return static_cast<int>(agenda.size()) + 1;  // + "Updated …" footer row
}

const char* CalendarActivity::screenTitle() const { return tr(STR_PLANNER_CALENDAR); }

const char* CalendarActivity::emptyText() const { return tr(STR_PLANNER_CAL_EMPTY_HINT); }

void CalendarActivity::fillItems(std::vector<fui::ListItem>& items) {
  subtitles.clear();
  headings.clear();
  subtitles.reserve(agenda.size());
  headings.reserve(agenda.size());  // upper bound: one per row

  for (size_t r = 0; r < agenda.size(); r++) {
    const Row& row = agenda[r];
    const auto& ev = calendar.events[row.event];

    fui::ListItem item;
    item.label = ev.title.c_str();

    // Day heading before the first event of each day.
    if (r == 0 || agenda[r - 1].day != row.day) {
      char day[48];
      PlannerFormat::longDayLabel(row.day, todayYmd, day, sizeof(day));
      headings.push_back(day);
      item.sectionHeading = headings.back().c_str();
    }

    char time[24];
    PlannerFormat::eventTime(ev, row.day, time, sizeof(time));
    std::string subtitle = time;
    if (!ev.location.empty()) {
      subtitle += " - ";
      subtitle += ev.location;
    }
    if (!ev.calendar.empty()) {
      subtitle += " - ";
      subtitle += ev.calendar;
    }
    subtitles.push_back(std::move(subtitle));
    item.subtitle = subtitles.back().c_str();
    items.push_back(item);
  }

  fui::ListItem footer;
  footer.label = agenda.empty() ? tr(STR_PLANNER_NO_EVENTS) : updatedLabel;
  footer.subtitle = agenda.empty() ? updatedLabel : nullptr;
  footer.enabled = false;
  items.push_back(footer);
}

const char* CalendarActivity::confirmHint() const { return agenda.empty() ? "" : tr(STR_PLANNER_EVENT_READ); }

void CalendarActivity::activate(const int index, const bool longPress) {
  if (longPress || index < 0 || index >= static_cast<int>(agenda.size())) return;
  const Row& row = agenda[static_cast<size_t>(index)];
  if (row.event >= calendar.events.size()) return;
  auto screen =
      makeUniqueNoThrow<CalendarEventActivity>(renderer, mappedInput, calendar.events[row.event], row.day, todayYmd);
  if (!screen) {
    LOG_ERR("PLN", "no memory for the event screen");
    return;
  }
  startActivityForResult(std::move(screen), [](const ActivityResult&) {});
}

void CalendarActivity::onSideButton(const bool next) {
  // Calendar <- Home -> To-Do List: Next Page returns to Home.
  if (next) onGoHome();
}
