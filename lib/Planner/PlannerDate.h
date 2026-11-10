#pragma once

#include <cstddef>
#include <cstdint>

// Calendar arithmetic on yyyymmdd integers (proleptic Gregorian, no time zones).
namespace planner {

bool isValidYmd(int32_t ymd);
int32_t makeYmd(int year, int month, int day);
inline int ymdYear(const int32_t ymd) { return ymd / 10000; }
inline int ymdMonth(const int32_t ymd) { return (ymd / 100) % 100; }
inline int ymdDay(const int32_t ymd) { return ymd % 100; }

// Days since 1970-01-01 for a valid date.
int64_t ymdToDays(int32_t ymd);
int32_t daysToYmd(int64_t days);
int32_t addDays(int32_t ymd, int delta);
// 0 = Sunday … 6 = Saturday.
int weekdayOf(int32_t ymd);

// Parses "YYYY-MM-DD" (exactly 10 chars). Empty input yields 0 and returns true.
bool parseIsoDate(const char* text, size_t len, int32_t& out);
// Writes "YYYY-MM-DD" (or "" for 0). buf must hold at least 11 bytes.
void formatIsoDate(int32_t ymd, char* buf, size_t bufSize);

// Local date (and minutes after midnight) for a UTC instant and a fixed offset.
int32_t localYmdFromUtc(int64_t utcSeconds, int utcOffsetMin, int* minuteOfDay = nullptr);

}  // namespace planner
