# Planner: To-do list, calendar and companion sync

CrossInk-Planner adds a **Planner** screen (Home → Planner) with the screens below.
The side buttons move between three pages, **Calendar ← Home → To-Do List**, shown by
three dots at the top of the screen (the current page has the larger dot). On Home,
**Next Page** (right edge on the X3, lower button on the X4) opens the To-Do List and
**Previous Page** (left edge on the X3, upper button on the X4) opens the Calendar; the
opposite side button brings you back Home. Everywhere on these pages the front
buttons move the selection. The dots take the centre of the status bar on these
three pages; an item configured there (e.g. the clock) moves to the left slot, or to
the right one if the left is used.

Falling asleep on the inactivity timeout shows the cover of the book being read,
whatever page was open; pressing Power keeps the chosen sleep screen.
Going to sleep on the To-Do List or Calendar reopens that page on wake-up (Back still
returns Home; holding Back while waking goes straight Home). The side buttons cannot
wake the device: on the X3/X4 they are read through an ADC ladder that is not a
deep-sleep wake source, so Power is still needed. To see the planner without waking
the device, choose **Settings → Display → Sleep Screen → Agenda** (or **Last Screen**,
which keeps the To-Do List / Calendar page itself, without page dots, battery, sync status,
selection, "+ Add task" and button hints; elsewhere Last Screen shows the book cover): the sleep screen
then shows the date, today's events and the open tasks (as of the last sync).

- **To-Do List** — tasks stored on the SD card, grouped by the lists made in the
  companion app (the list's name is a row; Confirm on it folds the list away). Tasks stay on the device until they are
  deleted (on the device or in the companion app). Tasks can be checked off and
  added on the device; the companion app can add, edit and delete them.
- **Calendar** — a read-only agenda of the next days, copied by the companion app
  from the user's calendars (the phone's own calendars and private iCal addresses:
  Google Calendar, Outlook, iCloud). Confirm on an event opens it on its own screen,
  where a long title or place is not cut to one line; any button closes it.

Nothing on the device talks to calendar services. The companion app
([CrossInk-fork-companion](https://github.com/lorenzocannavacciuolo12500-dot/CrossInk-fork-companion))
downloads the calendars, then pushes data to the reader over the local network while
the reader shows the **To-Do List** or the **Calendar**, or is in **File Transfer** mode.

### Background sync on the To-Do List and Calendar

About 1.5 s after one of these pages opens, the reader joins the last Wi-Fi network
it used (then the other saved ones, 15 s each) and starts the same web server as File
Transfer, with modem sleep on to save battery. The header shows the state instead of
the date: "Connecting…", "Sync <ip>", "No saved Wi-Fi" (join a network once through
File Transfer) or "Wi-Fi unavailable". When the companion changes tasks, the calendar
or the clock, the page reloads. Leaving the page turns Wi-Fi off; if the largest free
heap block is then below 85 % of what it was before Wi-Fi started, the reader reboots
silently to Home to clear fragmentation (like File Transfer). Wi-Fi is not started when
free heap is under 90 KB or the largest block under 32 KB (esp_netif aborts instead of
failing when it cannot allocate); the To-Do List and Calendar replace Home rather than
stacking on it so its caches are freed first. Serial log tag: `PLNSYNC`.

## Files on the SD card

| Path | Content |
| --- | --- |
| `/.crosspoint/planner/todos.json` | To-do list and sync cursor |
| `/.crosspoint/planner/calendar.json` | Last calendar snapshot sent by the companion |

Both files are rewritten atomically (`.tmp` then rename, `.bak` kept until the
replacement succeeds). Deleting the folder resets the Planner.

Each task also carries `list` (the name of the list it belongs to, empty for none)
and `order` (the position chosen by hand in the companion app, 0 for none). The
reader keeps at most 12 list names of 40 bytes; names no task points at any more
are forgotten at the next sync. Inside a block the reader follows `order` when the
app sent any, otherwise the order the tasks were made in; completed tasks keep their place.

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

Available while the reader shows **File Transfer**, the **To-Do List** or the
**Calendar** (`CrossPointWebServer`), on port 80.
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
6. Open the To-Do List with a saved network: the header shows "Sync <ip>" within ~5 s;
   run a sync from the companion app and the list updates without leaving the page.
7. Sleep on the To-Do List, wake with Power: the To-Do List reopens. With the Agenda
   sleep screen selected, the sleep screen lists today's events and the open tasks.
8. Serial log tags: `PLN`, `PLNSYNC` (heap before/after Wi-Fi).
