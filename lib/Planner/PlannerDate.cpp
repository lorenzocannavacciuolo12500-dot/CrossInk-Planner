#include "PlannerDate.h"

#include <cstdio>

namespace planner {
namespace {
bool isLeap(const int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int daysInMonth(const int y, const int m) {
  static const uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) return 0;
  return (m == 2 && isLeap(y)) ? 29 : kDays[m - 1];
}

int64_t floorDiv(const int64_t a, const int64_t b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
}  // namespace

bool isValidYmd(const int32_t ymd) {
  const int y = ymdYear(ymd);
  const int m = ymdMonth(ymd);
  const int d = ymdDay(ymd);
  return y >= 1970 && y <= 2199 && d >= 1 && d <= daysInMonth(y, m);
}

int32_t makeYmd(const int year, const int month, const int day) { return year * 10000 + month * 100 + day; }

// Howard Hinnant's days_from_civil / civil_from_days.
int64_t ymdToDays(const int32_t ymd) {
  int64_t y = ymdYear(ymd);
  const int64_t m = ymdMonth(ymd);
  const int64_t d = ymdDay(ymd);
  y -= m <= 2 ? 1 : 0;
  const int64_t era = floorDiv(y, 400);
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

int32_t daysToYmd(int64_t days) {
  days += 719468;
  const int64_t era = floorDiv(days, 146097);
  const int64_t doe = days - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t y = yoe + era * 400;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  const int64_t d = doy - (153 * mp + 2) / 5 + 1;
  const int64_t m = mp + (mp < 10 ? 3 : -9);
  y += m <= 2 ? 1 : 0;
  return makeYmd(static_cast<int>(y), static_cast<int>(m), static_cast<int>(d));
}

int32_t addDays(const int32_t ymd, const int delta) { return daysToYmd(ymdToDays(ymd) + delta); }

int weekdayOf(const int32_t ymd) {
  // 1970-01-01 was a Thursday (4).
  const int64_t days = ymdToDays(ymd);
  return static_cast<int>(((days % 7) + 7 + 4) % 7);
}

bool parseIsoDate(const char* text, const size_t len, int32_t& out) {
  out = 0;
  if (len == 0) return true;
  if (len != 10 || text[4] != '-' || text[7] != '-') return false;
  int parts[3] = {0, 0, 0};
  const int starts[3] = {0, 5, 8};
  const int lengths[3] = {4, 2, 2};
  for (int p = 0; p < 3; p++) {
    for (int i = 0; i < lengths[p]; i++) {
      const char c = text[starts[p] + i];
      if (c < '0' || c > '9') return false;
      parts[p] = parts[p] * 10 + (c - '0');
    }
  }
  const int32_t ymd = makeYmd(parts[0], parts[1], parts[2]);
  if (!isValidYmd(ymd)) return false;
  out = ymd;
  return true;
}

void formatIsoDate(const int32_t ymd, char* buf, const size_t bufSize) {
  if (bufSize == 0) return;
  if (ymd == 0 || !isValidYmd(ymd)) {
    buf[0] = '\0';
    return;
  }
  std::snprintf(buf, bufSize, "%04d-%02d-%02d", ymdYear(ymd), ymdMonth(ymd), ymdDay(ymd));
}

int32_t localYmdFromUtc(const int64_t utcSeconds, const int utcOffsetMin, int* minuteOfDay) {
  const int64_t localMinutes = floorDiv(utcSeconds, 60) + utcOffsetMin;
  constexpr int64_t kMinutesPerDay = 24 * 60;
  const int64_t days = floorDiv(localMinutes, kMinutesPerDay);
  if (minuteOfDay) *minuteOfDay = static_cast<int>(localMinutes - days * kMinutesPerDay);
  return daysToYmd(days);
}

}  // namespace planner
