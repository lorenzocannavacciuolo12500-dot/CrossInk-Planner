#pragma once

#include <PlannerTypes.h>

#include <cstddef>
#include <cstdint>

// Display helpers shared by the Planner screens.
namespace PlannerFormat {

// Today's local date (yyyymmdd) using the device clock and the clock offset
// from Settings, or 0 when the clock is not trusted yet.
int32_t today();

// Minutes after local midnight, or -1 when the clock is not trusted yet.
int minuteOfDay();

// "Today" / "Tomorrow" / "Yesterday" relative to `today`, otherwise "Wed 12 Nov".
void dayLabel(int32_t ymd, int32_t today, char* buf, size_t bufSize);

// "09:00–10:00", "09:00" or "All day"; follows the 12/24h clock setting.
// Agenda section heading, like the companion app: "Martedì 17 novembre",
// or "Oggi, 11 novembre" / "Domani, 12 novembre".
void longDayLabel(int32_t ymd, int32_t today, char* buf, size_t bufSize);

void eventTime(const planner::CalendarEvent& event, int32_t onDay, char* buf, size_t bufSize);

// Local date and time of a unix timestamp, e.g. "Today 18:40", for "Updated …".
void stamp(int64_t utcSeconds, int32_t today, char* buf, size_t bufSize);

}  // namespace PlannerFormat
