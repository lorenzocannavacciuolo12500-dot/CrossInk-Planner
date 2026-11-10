#include "PlannerApi.h"

#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PlannerJson.h>
#include <PlannerTodos.h>
#include <StreamingJsonParser.h>
#include <WebServer.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <utility>

#include "CrossPointSettings.h"
#include "PlannerStore.h"

namespace PlannerApi {
namespace {

constexpr int kApiVersion = 1;
// Generous for 200 tasks or 150 events with long titles, small enough that the
// request String plus decoded vectors stay well inside File Transfer's heap.
constexpr size_t kMaxBodyBytes = 48 * 1024;
constexpr size_t kSendChunk = 512;

// Streams encoder output as a chunked HTTP response.
struct ResponseSink {
  WebServer* server;
  char buf[kSendChunk];
  size_t used = 0;

  void flush() {
    if (used == 0) return;
    server->sendContent(buf, used);
    used = 0;
  }
  static void write(void* ctx, const char* data, size_t len) {
    auto* self = static_cast<ResponseSink*>(ctx);
    while (len > 0) {
      const size_t room = sizeof(self->buf) - self->used;
      const size_t n = len < room ? len : room;
      std::memcpy(self->buf + self->used, data, n);
      self->used += n;
      data += n;
      len -= n;
      if (self->used == sizeof(self->buf)) self->flush();
    }
  }
};

void sendTodos(WebServer& server, const planner::TodoList& list) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  // Static: ~520 bytes is too much for the web server task stack, and only one
  // request is served at a time.
  static ResponseSink sink;
  sink.server = &server;
  sink.used = 0;
  planner::writeTodoListJson(list, planner::TodoJsonMode::Api, planner::JsonSink{&sink, &ResponseSink::write});
  sink.flush();
  server.sendContent("");
}

// Returns the request body, or sends an error and returns false.
bool takeBody(WebServer& server, String& body) {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Missing JSON body");
    return false;
  }
  body = server.arg("plain");
  if (body.length() > kMaxBodyBytes) {
    body = String();
    server.send(413, "text/plain", "Body too large");
    return false;
  }
  return true;
}

// Minimal decoder for {"utc": <int>, "utcOffsetMin": <int>}.
struct TimeBody {
  char key[16] = {};
  int depth = 0;
  bool hasUtc = false;
  bool hasOffset = false;
  long long utc = 0;
  long offsetMin = 0;
};

bool parseTimeBody(const String& body, TimeBody& out) {
  JsonCallbacks cb{};
  cb.ctx = &out;
  cb.onKey = [](void* c, const char* k, size_t n) {
    auto* t = static_cast<TimeBody*>(c);
    const size_t len = n < sizeof(t->key) - 1 ? n : sizeof(t->key) - 1;
    std::memcpy(t->key, k, len);
    t->key[len] = '\0';
  };
  cb.onNumber = [](void* c, const char* v, size_t) {
    auto* t = static_cast<TimeBody*>(c);
    if (t->depth != 1) return;
    if (std::strcmp(t->key, "utc") == 0) {
      t->utc = std::strtoll(v, nullptr, 10);
      t->hasUtc = true;
    } else if (std::strcmp(t->key, "utcOffsetMin") == 0) {
      t->offsetMin = std::strtol(v, nullptr, 10);
      t->hasOffset = true;
    }
  };
  cb.onObjectStart = [](void* c) { static_cast<TimeBody*>(c)->depth++; };
  cb.onObjectEnd = [](void* c) { static_cast<TimeBody*>(c)->depth--; };
  cb.onArrayStart = [](void* c) { static_cast<TimeBody*>(c)->depth++; };
  cb.onArrayEnd = [](void* c) { static_cast<TimeBody*>(c)->depth--; };

  auto parser = std::unique_ptr<StreamingJsonParser>(new (std::nothrow) StreamingJsonParser(cb));
  if (!parser) {
    LOG_ERR("PLN", "OOM: time parser");
    return false;
  }
  parser->feed(body.c_str(), body.length());
  return !parser->hasError() && out.depth == 0 && out.hasUtc;
}

// Caller holds PlannerStore::lock(). A brand-new list has no file yet: persist
// it so the storeId the companion sees stays stable until its next POST.
void loadTodosPersistingStoreId(planner::TodoList& todos) {
  const bool existed = Storage.exists(PlannerStore::kTodosPath);
  if (!PlannerStore::loadTodos(todos)) LOG_ERR("PLN", "todos.json unreadable; serving an empty list");
  if (!existed && !PlannerStore::saveTodos(todos)) LOG_ERR("PLN", "Could not create todos.json");
}

int offsetMinutesFromSettings() { return (static_cast<int>(SETTINGS.clockUtcOffsetQ) - 48) * 15; }

void handleStatus(WebServer& server) {
  planner::TodoList todos;
  planner::Calendar calendar;
  {
    std::lock_guard<std::mutex> guard(PlannerStore::lock());
    loadTodosPersistingStoreId(todos);
    PlannerStore::loadCalendar(calendar);
  }
  const auto todoCount = static_cast<size_t>(std::count_if(
      todos.items.begin(), todos.items.end(), [](const planner::TodoItem& item) { return !item.deleted; }));
  time_t now = 0;
  const bool clockValid = halClock.getUnixTime(now);

  char json[320];
  std::snprintf(json, sizeof(json),
                "{\"api\":%d,\"storeId\":\"%s\",\"revision\":%u,\"todoCount\":%u,\"clockValid\":%s,"
                "\"deviceTime\":%lld,\"utcOffsetMin\":%d,\"hasRtc\":%s,\"calendarGeneratedAt\":%lld,"
                "\"calendarEventCount\":%u}",
                kApiVersion, todos.storeId.c_str(), static_cast<unsigned>(todos.revision),
                static_cast<unsigned>(todoCount), clockValid ? "true" : "false",
                static_cast<long long>(clockValid ? now : 0), offsetMinutesFromSettings(),
                halClock.isAvailable() ? "true" : "false", static_cast<long long>(calendar.generatedAt),
                static_cast<unsigned>(calendar.events.size()));
  server.send(200, "application/json", json);
}

void handleSetTime(WebServer& server) {
  String body;
  if (!takeBody(server, body)) return;
  TimeBody time;
  if (!parseTimeBody(body, time)) {
    server.send(400, "text/plain", "Expected {\"utc\":<seconds>,\"utcOffsetMin\":<minutes>}");
    return;
  }
  if (!halClock.setFromUnixTime(static_cast<time_t>(time.utc))) {
    server.send(400, "text/plain", "Implausible time");
    return;
  }

  bool settingsChanged = false;
  if (time.hasOffset && time.offsetMin >= -12 * 60 && time.offsetMin <= 14 * 60) {
    // Settings store the offset as biased quarter hours (48 = UTC+0).
    const long quarters = (time.offsetMin >= 0 ? time.offsetMin + 7 : time.offsetMin - 7) / 15;
    const auto biased = static_cast<uint8_t>(48 + quarters);
    if (SETTINGS.clockUtcOffsetQ != biased) {
      SETTINGS.clockUtcOffsetQ = biased;
      settingsChanged = true;
    }
  }
  if (halClock.isAvailable() && (!SETTINGS.clockHasBeenSynced || !SETTINGS.clockDateHasBeenSynced)) {
    // The X3 RTC now holds a trusted time, so the status bar may show the date.
    SETTINGS.clockHasBeenSynced = 1;
    SETTINGS.clockDateHasBeenSynced = 1;
    settingsChanged = true;
  }
  if (settingsChanged && !SETTINGS.saveToFile()) LOG_ERR("PLN", "Could not save clock settings");

  LOG_INF("PLN", "Clock set by companion (offset %d min)", offsetMinutesFromSettings());
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleGetTodos(WebServer& server) {
  planner::TodoList todos;
  {
    std::lock_guard<std::mutex> guard(PlannerStore::lock());
    loadTodosPersistingStoreId(todos);
  }
  sendTodos(server, todos);
}

void handlePostTodos(WebServer& server) {
  String body;
  if (!takeBody(server, body)) return;

  planner::CompanionTodoPush push;
  {
    planner::PlannerJsonDecoder decoder(push);
    decoder.feed(body.c_str(), body.length());
    body = String();  // release the request copy before merging
    if (!decoder.finish()) {
      server.send(400, "text/plain", "Invalid to-do payload");
      return;
    }
    if (decoder.skippedItems() > 0) {
      LOG_INF("PLN", "Ignored %u invalid tasks from companion", static_cast<unsigned>(decoder.skippedItems()));
    }
  }

  planner::TodoList todos;
  {
    std::lock_guard<std::mutex> guard(PlannerStore::lock());
    PlannerStore::loadTodos(todos);
    const bool baselineValid = push.hasBaseline && push.storeId == todos.storeId;
    if (!baselineValid) LOG_INF("PLN", "Companion cursor not valid for this store; merging without it");
    planner::applyCompanionTodos(todos, std::move(push.items), push.baseline, baselineValid);
    if (!PlannerStore::saveTodos(todos)) {
      server.send(500, "text/plain", "Could not save tasks");
      return;
    }
  }
  LOG_INF("PLN", "Synced %u tasks (revision %u)", static_cast<unsigned>(todos.items.size()),
          static_cast<unsigned>(todos.revision));
  sendTodos(server, todos);
}

void handlePostCalendar(WebServer& server) {
  String body;
  if (!takeBody(server, body)) return;

  planner::Calendar calendar;
  size_t skipped = 0;
  {
    planner::PlannerJsonDecoder decoder(calendar);
    decoder.feed(body.c_str(), body.length());
    body = String();
    if (!decoder.finish()) {
      server.send(400, "text/plain", "Invalid calendar payload");
      return;
    }
    skipped = decoder.skippedItems();
  }
  {
    std::lock_guard<std::mutex> guard(PlannerStore::lock());
    if (!PlannerStore::saveCalendar(calendar)) {
      server.send(500, "text/plain", "Could not save calendar");
      return;
    }
  }
  LOG_INF("PLN", "Stored %u calendar events (%u skipped)", static_cast<unsigned>(calendar.events.size()),
          static_cast<unsigned>(skipped));
  char json[64];
  std::snprintf(json, sizeof(json), "{\"eventCount\":%u,\"skipped\":%u}", static_cast<unsigned>(calendar.events.size()),
                static_cast<unsigned>(skipped));
  server.send(200, "application/json", json);
}

}  // namespace

void registerRoutes(WebServer& server) {
  WebServer* s = &server;
  server.on("/api/planner/status", HTTP_GET, [s] { handleStatus(*s); });
  server.on("/api/planner/time", HTTP_POST, [s] { handleSetTime(*s); });
  server.on("/api/planner/todos", HTTP_GET, [s] { handleGetTodos(*s); });
  server.on("/api/planner/todos", HTTP_POST, [s] { handlePostTodos(*s); });
  server.on("/api/planner/calendar", HTTP_POST, [s] { handlePostCalendar(*s); });
}

}  // namespace PlannerApi
