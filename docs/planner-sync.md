# Planner: To-do list, calendar and companion sync

CrossInk-Planner adds a **Planner** screen (Home → Planner) with the screens below.
The side buttons move between three pages, **Calendar ← Home → To-Do List**, shown by
three dots at the top of the screen (the current page has the larger dot). On Home,
**Next Page** (right edge on the X3, lower button on the X4) opens the To-Do List and
**Previous Page** (left edge on the X3, upper button on the X4) opens the Calendar; the
opposite side button brings you back Home. Everywhere on these pages the front
buttons move the selection.

- **To-Do List** — tasks stored on the SD card. Tasks stay on the device until they are
  deleted (on the device or in the companion app). Tasks can be checked off and
  added on the device; the companion app can add, edit and delete them.
- **Calendar** — a read-only agenda of the next days, copied by the companion app
  from the user's calendars (private iCal addresses: Google Calendar, Outlook, iCloud).

Nothing on the device talks to calendar services. The companion app
([CrossInk-fork-companion](https://github.com/lorenzocannavacciuolo12500-dot/CrossInk-fork-companion))
downloads the calendars, then pushes data to the reader over the local network while
the reader is in **File Transfer** mode.

## Files on the SD card

| Path | Content |
| --- | --- |
| `/.crosspoint/planner/todos.json` | To-do list and sync cursor |
| `/.crosspoint/planner/calendar.json` | Last calendar snapshot sent by the companion |

Both files are rewritten atomically (`.tmp` then rename, `.bak` kept until the
replacement succeeds). Deleting the folder resets the Planner.

### `todos.json`

```json
{
  "version": 1,
  "storeId": "5f3a9c21",
  "revision": 14,
  "items": [
    {"id": "c-7d1e…", "title": "Buy milk", "due": "2026-11-12", "done": false,
     "deleted": false, "updatedAt": 1762790400, "localRev": 0}
  ]
}
```

- `storeId` — random id created with the file. If the companion sends a different
  `storeId`, its sync cursor is ignored (the device was reset or swapped).
- `revision` — counter incremented by every change made **on the device**.
- `localRev` — the `revision` of the last on-device change to this item, or `0`
  when the current state came from the companion.
- `deleted` — tombstone for an on-device delete that the companion has not seen yet.

### `calendar.json`

```json
{
  "version": 1,
  "generatedAt": 1762790400,
  "utcOffsetMin": 60,
  "rangeStart": "2026-11-09",
  "rangeEnd": "2026-12-10",
  "events": [
    {"title": "Dentist", "location": "Via Roma 1", "calendar": "Personal",
     "startDate": "2026-11-12", "startMin": 540, "endDate": "2026-11-12", "endMin": 600,
     "allDay": false}
  ]
}
```

Times are **local wall-clock** values computed by the companion in the phone's time
zone (`startMin`/`endMin` are minutes after midnight). This keeps time-zone and DST
handling on the phone; the device only needs to know today's local date.

## Limits

| What | Limit |
| --- | --- |
| Tasks | 200 (extra items are dropped, oldest completed first) |
| Events | 150 (the companion should send the nearest ones) |
| Task / event title | 120 bytes UTF-8 (truncated at a character boundary) |
| Event location | 80 bytes |
| Calendar name | 40 bytes |
| Task id | 64 bytes |
| Request body | 48 KB |

Strings must be sent as raw UTF-8. `\uXXXX` escapes are decoded, but raw UTF-8 is
preferred (Dart's `jsonEncode` already does this).

## HTTP API

Available while the reader shows **File Transfer** (`CrossPointWebServer`), on port 80.
The companion finds the reader with the existing discovery broadcast: send the UDP
datagram `hello` to port `8134`; the reader answers `crosspoint (on <hostname>);81`
from its IP address.

All bodies are JSON (`Content-Type: application/json`). Errors return `4xx` with a
plain-text message.

### `GET /api/planner/status`

```json
{"api": 1, "storeId": "5f3a9c21", "revision": 14, "todoCount": 9,
 "clockValid": true, "deviceTime": 1762790400, "utcOffsetMin": 60, "hasRtc": true,
 "calendarGeneratedAt": 1762780000, "calendarEventCount": 23}
```

The device model (X3/X4) is reported by the existing `GET /api/status`.

### `POST /api/planner/time`

```json
{"utc": 1762790400, "utcOffsetMin": 60}
```

Sets the system clock (and the DS3231 RTC on the X3) and the device clock offset
(Settings → Clock offset), rounded to 15 minutes. Send this first on every sync: the
X4 has no battery-backed clock and drifts in deep sleep.

### `GET /api/planner/todos`

```json
{"storeId": "5f3a9c21", "revision": 14,
 "items": [{"id": "…", "title": "…", "due": "2026-11-12", "done": false,
            "deleted": false, "updatedAt": 1762790400}]}
```

Items with `"deleted": true` were deleted on the device: remove them in the app.

### `POST /api/planner/todos`

```json
{"storeId": "5f3a9c21", "baseline": 14,
 "items": [{"id": "…", "title": "…", "due": "", "done": true, "updatedAt": 1762790500}]}
```

`items` is the **complete** list the companion wants on the device (no tombstones:
to delete a task, leave it out). `baseline` is the `revision` the companion received
from its last `GET` or `POST` response. Response: same shape as `GET`.

Merge rules on the device, per task id:

1. A device item changed after `baseline` (`localRev > baseline`) is a change the
   companion has not seen. If the companion also sent that id, the newer `updatedAt`
   wins (ties go to the companion; a device change with `updatedAt: 0`, made while
   the clock was not set, wins). If the companion did not send it, it is kept
   (new task or unseen delete).
2. Any other device item is replaced by the companion's version, or removed when the
   companion left it out.
3. Companion items the device does not know are added.
4. On-device tombstones that the companion has seen are purged.

The companion should then adopt the returned list and remember the returned
`revision` as its next `baseline`.

### `POST /api/planner/calendar`

Body: the `calendar.json` shape above (without `version`). Replaces the stored
calendar. Response: `{"eventCount": 23, "skipped": 0}`.

### Books

Use the existing upload endpoint: `POST /upload?path=/Books` with a
`multipart/form-data` body whose file part carries the file name.
See [webserver-endpoints.md](webserver-endpoints.md).

## Verifying on hardware

1. Flash the firmware (`pio run -e default --target upload`).
2. Home → Planner → To-do → "Add task": type a title, confirm, check it off.
3. Home → File Transfer → join Wi-Fi. From a computer on the same network:
   `curl http://<ip>/api/planner/todos` shows the task and `"revision": 2` or more.
4. `curl -X POST -H 'Content-Type: application/json' -d '{"utc":'$(date +%s)',"utcOffsetMin":60}' http://<ip>/api/planner/time`
   then check the clock in the status bar.
5. Post a calendar snapshot, exit File Transfer, open Planner → Calendar.
6. Serial log tag: `PLN`.
