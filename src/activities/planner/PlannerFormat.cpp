#include "PlannerFormat.h"

#include <HalClock.h>
#include <I18n.h>
#include <PlannerDate.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "platform/PlannerPlatform.h"

namespace PlannerFormat {
namespace {
int offsetMinutes() { return (static_cast<int>(SETTINGS.clockUtcOffsetQ) - 48) * 15; }

void clockText(const int minuteOfDay, char* buf, const size_t bufSize) {
  const int h = (minuteOfDay / 60) % 24;
  const int m = minuteOfDay % 60;
  if (SETTINGS.clockFormat == 1) {
    const int h12 = h % 12 == 0 ? 12 : h % 12;
    std::snprintf(buf, bufSize, "%d:%02d%s", h12, m, h < 12 ? "am" : "pm");
  } else {
    std::snprintf(buf, bufSize, "%02d:%02d", h, m);
  }
}
}  // namespace

int32_t today() {
  time_t now = 0;
  if (!planner_platform::getUnixTime(now)) return 0;
  return planner::localYmdFromUtc(static_cast<int64_t>(now), offsetMinutes());
}

int nowMinuteOfDay() {
  time_t now = 0;
  if (!planner_platform::getUnixTime(now)) return -1;
  int minute = -1;
  planner::localYmdFromUtc(static_cast<int64_t>(now), offsetMinutes(), &minute);
  return minute;
}

void dayLabel(const int32_t ymd, const int32_t todayYmd, char* buf, const size_t bufSize) {
  if (bufSize == 0) return;
  if (!planner::isValidYmd(ymd)) {
    buf[0] = '\0';
    return;
  }
  if (todayYmd != 0) {
    const int64_t delta = planner::ymdToDays(ymd) - planner::ymdToDays(todayYmd);
    if (delta == 0) {
      std::snprintf(buf, bufSize, "%s", tr(STR_PLANNER_TODAY));
      return;
    }
    if (delta == 1) {
      std::snprintf(buf, bufSize, "%s", tr(STR_PLANNER_TOMORROW));
      return;
    }
    if (delta == -1) {
      std::snprintf(buf, bufSize, "%s", tr(STR_PLANNER_YESTERDAY));
      return;
    }
  }
  // Flash-resident lookup tables (static const), indexed by weekday / month.
  static const StrId kWeekdays[] = {StrId::STR_STATS_SUN, StrId::STR_STATS_MON, StrId::STR_STATS_TUE,
                                    StrId::STR_STATS_WED, StrId::STR_STATS_THU, StrId::STR_STATS_FRI,
                                    StrId::STR_STATS_SAT};
  static const StrId kMonths[] = {StrId::STR_MONTH_JAN_SHORT, StrId::STR_MONTH_FEB_SHORT, StrId::STR_MONTH_MAR_SHORT,
                                  StrId::STR_MONTH_APR_SHORT, StrId::STR_MONTH_MAY_SHORT, StrId::STR_MONTH_JUN_SHORT,
                                  StrId::STR_MONTH_JUL_SHORT, StrId::STR_MONTH_AUG_SHORT, StrId::STR_MONTH_SEP_SHORT,
                                  StrId::STR_MONTH_OCT_SHORT, StrId::STR_MONTH_NOV_SHORT, StrId::STR_MONTH_DEC_SHORT};
  std::snprintf(buf, bufSize, "%s %d %s", I18N.get(kWeekdays[planner::weekdayOf(ymd)]), planner::ymdDay(ymd),
                I18N.get(kMonths[planner::ymdMonth(ymd) - 1]));
}

void longDayLabel(const int32_t ymd, const int32_t todayYmd, char* buf, const size_t bufSize) {
  if (bufSize == 0) return;
  if (!planner::isValidYmd(ymd)) {
    buf[0] = '\0';
    return;
  }
  static const StrId kWeekdays[] = {StrId::STR_PLANNER_WEEKDAY_SUN, StrId::STR_PLANNER_WEEKDAY_MON,
                                    StrId::STR_PLANNER_WEEKDAY_TUE, StrId::STR_PLANNER_WEEKDAY_WED,
                                    StrId::STR_PLANNER_WEEKDAY_THU, StrId::STR_PLANNER_WEEKDAY_FRI,
                                    StrId::STR_PLANNER_WEEKDAY_SAT};
  static const StrId kMonths[] = {StrId::STR_MONTH_JAN, StrId::STR_MONTH_FEB, StrId::STR_MONTH_MAR,
                                  StrId::STR_MONTH_APR, StrId::STR_MONTH_MAY, StrId::STR_MONTH_JUN,
                                  StrId::STR_MONTH_JUL, StrId::STR_MONTH_AUG, StrId::STR_MONTH_SEP,
                                  StrId::STR_MONTH_OCT, StrId::STR_MONTH_NOV, StrId::STR_MONTH_DEC};
  const char* month = I18N.get(kMonths[planner::ymdMonth(ymd) - 1]);
  const int day = planner::ymdDay(ymd);
  if (todayYmd != 0) {
    const int64_t delta = planner::ymdToDays(ymd) - planner::ymdToDays(todayYmd);
    if (delta == 0 || delta == 1) {
      std::snprintf(buf, bufSize, "%s, %d %s",
                    I18N.get(delta == 0 ? StrId::STR_PLANNER_TODAY : StrId::STR_PLANNER_TOMORROW), day, month);
      return;
    }
  }
  std::snprintf(buf, bufSize, "%s %d %s", I18N.get(kWeekdays[planner::weekdayOf(ymd)]), day, month);
}

void eventTime(const planner::CalendarEvent& event, const int32_t onDay, char* buf, const size_t bufSize) {
  if (bufSize == 0) return;
  // An all-day event, or a middle day of a multi-day event, has no clock time.
  const bool startsToday = onDay == 0 || onDay <= event.startDate;
  const bool endsToday = onDay == 0 || onDay >= event.endDate;
  if (event.allDay || (!startsToday && !endsToday)) {
    std::snprintf(buf, bufSize, "%s", tr(STR_PLANNER_ALL_DAY));
    return;
  }
  char start[12];
  char end[12];
  clockText(startsToday ? event.startMin : 0, start, sizeof(start));
  if (!endsToday) {
    std::snprintf(buf, bufSize, "%s-", start);  // "09:00-": continues tomorrow (ASCII: UI fonts may lack arrows)
    return;
  }
  if (startsToday && event.endMin <= event.startMin) {
    std::snprintf(buf, bufSize, "%s", start);
    return;
  }
  clockText(event.endMin, end, sizeof(end));
  if (!startsToday) {
    std::snprintf(buf, bufSize, "-%s", end);  // "-10:00": continued from yesterday
    return;
  }
  std::snprintf(buf, bufSize, "%s-%s", start, end);
}

void stamp(const int64_t utcSeconds, const int32_t todayYmd, char* buf, const size_t bufSize) {
  if (bufSize == 0) return;
  if (utcSeconds <= 0) {
    buf[0] = '\0';
    return;
  }
  int minuteOfDay = 0;
  const int32_t ymd = planner::localYmdFromUtc(utcSeconds, offsetMinutes(), &minuteOfDay);
  char day[32];
  char time[12];
  dayLabel(ymd, todayYmd, day, sizeof(day));
  clockText(minuteOfDay, time, sizeof(time));
  std::snprintf(buf, bufSize, "%s %s", day, time);
}

}  // namespace PlannerFormat
