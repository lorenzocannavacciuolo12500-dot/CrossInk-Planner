#include "PlannerJson.h"

#include <StreamingJsonParser.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>

#include "PlannerDate.h"
#include "PlannerTodos.h"

namespace planner {

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------
namespace {
class Out {
 public:
  explicit Out(const JsonSink& sink) : sink(sink) {}
  void raw(const char* s) { sink.write(sink.ctx, s, std::strlen(s)); }
  void raw(const char* s, const size_t len) { sink.write(sink.ctx, s, len); }
  void key(const char* k, const bool first = false) {
    if (!first) raw(",");
    str(k);
    raw(":");
  }
  void str(const std::string& s) { str(s.data(), s.size()); }
  void str(const char* s) { str(s, std::strlen(s)); }
  void str(const char* s, const size_t len) {
    raw("\"");
    size_t runStart = 0;
    for (size_t i = 0; i < len; i++) {
      const auto c = static_cast<unsigned char>(s[i]);
      if (c != '"' && c != '\\' && c >= 0x20) continue;
      if (i > runStart) raw(s + runStart, i - runStart);
      char esc[8];
      switch (c) {
        case '"':
          raw("\\\"");
          break;
        case '\\':
          raw("\\\\");
          break;
        case '\n':
          raw("\\n");
          break;
        case '\r':
          raw("\\r");
          break;
        case '\t':
          raw("\\t");
          break;
        default:
          std::snprintf(esc, sizeof(esc), "\\u%04x", c);
          raw(esc);
          break;
      }
      runStart = i + 1;
    }
    if (len > runStart) raw(s + runStart, len - runStart);
    raw("\"");
  }
  void num(const int64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
    raw(buf);
  }
  void boolean(const bool v) { raw(v ? "true" : "false"); }
  void date(const int32_t ymd) {
    char buf[12];
    formatIsoDate(ymd, buf, sizeof(buf));
    str(buf);
  }

 private:
  const JsonSink& sink;
};
}  // namespace

void writeTodoListJson(const TodoList& list, const TodoJsonMode mode, const JsonSink& sink) {
  Out out(sink);
  out.raw("{");
  if (mode == TodoJsonMode::File) {
    out.key("version", true);
    out.num(1);
    out.key("storeId");
  } else {
    out.key("storeId", true);
  }
  out.str(list.storeId);
  out.key("revision");
  out.num(list.revision);
  out.key("items");
  out.raw("[");
  bool first = true;
  for (const auto& item : list.items) {
    if (!first) out.raw(",");
    first = false;
    out.raw("{");
    out.key("id", true);
    out.str(item.id);
    out.key("title");
    out.str(item.title);
    out.key("due");
    out.date(item.due);
    out.key("done");
    out.boolean(item.done);
    out.key("deleted");
    out.boolean(item.deleted);
    out.key("updatedAt");
    out.num(item.updatedAt);
    if (mode == TodoJsonMode::File) {
      out.key("localRev");
      out.num(item.localRev);
    }
    out.raw("}");
  }
  out.raw("]}");
}

void writeCalendarJson(const Calendar& calendar, const JsonSink& sink) {
  Out out(sink);
  out.raw("{");
  out.key("version", true);
  out.num(1);
  out.key("generatedAt");
  out.num(calendar.generatedAt);
  out.key("utcOffsetMin");
  out.num(calendar.utcOffsetMin);
  out.key("rangeStart");
  out.date(calendar.rangeStart);
  out.key("rangeEnd");
  out.date(calendar.rangeEnd);
  out.key("events");
  out.raw("[");
  bool first = true;
  for (const auto& ev : calendar.events) {
    if (!first) out.raw(",");
    first = false;
    out.raw("{");
    out.key("title", true);
    out.str(ev.title);
    out.key("location");
    out.str(ev.location);
    out.key("calendar");
    out.str(ev.calendar);
    out.key("startDate");
    out.date(ev.startDate);
    out.key("startMin");
    out.num(ev.startMin);
    out.key("endDate");
    out.date(ev.endDate);
    out.key("endMin");
    out.num(ev.endMin);
    out.key("allDay");
    out.boolean(ev.allDay);
    out.raw("}");
  }
  out.raw("]}");
}

// ---------------------------------------------------------------------------
// \uXXXX decoding
// ---------------------------------------------------------------------------
namespace {
int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool readHex4(const std::string& s, const size_t pos, uint32_t& out) {
  if (pos + 4 > s.size()) return false;
  out = 0;
  for (size_t i = 0; i < 4; i++) {
    const int v = hexValue(s[pos + i]);
    if (v < 0) return false;
    out = (out << 4) | static_cast<uint32_t>(v);
  }
  return true;
}

void appendUtf8(std::string& out, const uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}
}  // namespace

void decodeUnicodeEscapes(std::string& s) {
  if (s.find("\\u") == std::string::npos) return;
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    uint32_t cp = 0;
    if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'u' && readHex4(s, i + 2, cp)) {
      size_t consumed = 6;
      if (cp >= 0xD800 && cp <= 0xDBFF) {
        uint32_t low = 0;
        if (i + 7 < s.size() && s[i + 6] == '\\' && s[i + 7] == 'u' && readHex4(s, i + 8, low) && low >= 0xDC00 &&
            low <= 0xDFFF) {
          cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
          consumed = 12;
        } else {
          cp = 0xFFFD;  // lone high surrogate
        }
      } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
        cp = 0xFFFD;  // lone low surrogate
      }
      if (cp == 0) cp = 0xFFFD;  // never embed NUL in C strings
      appendUtf8(out, cp);
      i += consumed;
      continue;
    }
    out.push_back(s[i]);
    i++;
  }
  s = std::move(out);
}

// ---------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------
namespace {
PlannerJsonDecoder* self(void* ctx) { return static_cast<PlannerJsonDecoder*>(ctx); }

const JsonCallbacks kCallbacks = {
    nullptr,
    [](void* c, const char* v, size_t n) { self(c)->onKey(v, n); },
    [](void* c, const char* v, size_t n) { self(c)->onString(v, n); },
    [](void* c, const char* v, size_t n) { self(c)->onNumber(v, n); },
    [](void* c, bool v) { self(c)->onBool(v); },
    [](void* c) { self(c)->onNull(); },
    [](void* c) { self(c)->onObjectStart(); },
    [](void* c) { self(c)->onObjectEnd(); },
    [](void* c) { self(c)->onArrayStart(); },
    [](void* c) { self(c)->onArrayEnd(); },
};

bool keyIs(const char* key, const char* name) { return std::strcmp(key, name) == 0; }

// Store ids are generated as hex; keep only [0-9A-Za-z-] so the id can be
// echoed into hand-built JSON responses without escaping.
void assignStoreId(std::string& target, const char* str, const size_t len) {
  target.clear();
  for (size_t i = 0; i < len && target.size() < kMaxStoreIdBytes; i++) {
    const char c = str[i];
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-') target.push_back(c);
  }
}

int16_t clampMinutes(const int64_t v) {
  if (v < 0) return 0;
  if (v > kMinutesPerDay) return kMinutesPerDay;
  return static_cast<int16_t>(v);
}
}  // namespace

PlannerJsonDecoder::PlannerJsonDecoder(TodoList& out) : kind(Kind::TodoFile), todoFile(&out) { init(); }
PlannerJsonDecoder::PlannerJsonDecoder(CompanionTodoPush& out) : kind(Kind::TodoPush), todoPush(&out) { init(); }
PlannerJsonDecoder::PlannerJsonDecoder(Calendar& out) : kind(Kind::Calendar), calendar(&out) { init(); }

void PlannerJsonDecoder::init() {
  switch (kind) {
    case Kind::TodoFile:
      *todoFile = TodoList{};
      todoFile->items.reserve(32);
      break;
    case Kind::TodoPush:
      *todoPush = CompanionTodoPush{};
      todoPush->items.reserve(32);
      break;
    case Kind::Calendar:
      *calendar = Calendar{};
      calendar->events.reserve(32);
      break;
  }
  JsonCallbacks cb = kCallbacks;
  cb.ctx = this;
  // Heap, not stack: the parser carries a 512-byte token buffer and decoders
  // run on the web server task. Allocation failure is reported by finish().
  parser = new (std::nothrow) StreamingJsonParser(cb);
}

PlannerJsonDecoder::~PlannerJsonDecoder() { delete parser; }

void PlannerJsonDecoder::feed(const char* data, const size_t len) {
  if (parser) parser->feed(data, len);
}

bool PlannerJsonDecoder::finish() {
  if (!parser || parser->hasError() || !sawRoot || depth != 0 || badShape || badValue) return false;
  if (kind == Kind::Calendar) {
    std::stable_sort(calendar->events.begin(), calendar->events.end(),
                     [](const CalendarEvent& a, const CalendarEvent& b) {
                       if (a.startDate != b.startDate) return a.startDate < b.startDate;
                       if (a.allDay != b.allDay) return a.allDay;
                       return a.startMin < b.startMin;
                     });
  }
  return true;
}

void PlannerJsonDecoder::onKey(const char* k, const size_t len) {
  const size_t n = std::min(len, sizeof(key) - 1);
  std::memcpy(key, k, n);
  key[n] = '\0';
}

void PlannerJsonDecoder::onObjectStart() {
  depth++;
  if (depth == 1) {
    sawRoot = true;
  } else if (inItemsArray && depth == itemsArrayDepth + 1) {
    beginItem();
  }
}

void PlannerJsonDecoder::onObjectEnd() {
  if (inItem && depth == itemsArrayDepth + 1) endItem();
  depth--;
}

void PlannerJsonDecoder::onArrayStart() {
  depth++;
  if (depth == 1) {
    badShape = true;  // root must be an object
    return;
  }
  const char* expected = kind == Kind::Calendar ? "events" : "items";
  if (depth == 2 && keyIs(key, expected)) {
    inItemsArray = true;
    itemsArrayDepth = depth;
  }
}

void PlannerJsonDecoder::onArrayEnd() {
  if (inItemsArray && depth == itemsArrayDepth) inItemsArray = false;
  depth--;
}

void PlannerJsonDecoder::onString(const char* value, const size_t len) {
  if (depth == 1) {
    handleTopScalar(value, len, 0, false, false, false);
  } else if (inItem && depth == itemsArrayDepth + 1) {
    handleItemScalar(value, len, 0, false, false, false);
  }
}

void PlannerJsonDecoder::onNumber(const char* value, size_t) {
  const int64_t v = std::strtoll(value, nullptr, 10);
  if (depth == 1) {
    handleTopScalar(nullptr, 0, v, true, false, false);
  } else if (inItem && depth == itemsArrayDepth + 1) {
    handleItemScalar(nullptr, 0, v, true, false, false);
  }
}

void PlannerJsonDecoder::onBool(const bool value) {
  if (depth == 1) {
    handleTopScalar(nullptr, 0, 0, false, true, value);
  } else if (inItem && depth == itemsArrayDepth + 1) {
    handleItemScalar(nullptr, 0, 0, false, true, value);
  }
}

void PlannerJsonDecoder::onNull() {}

void PlannerJsonDecoder::handleTopScalar(const char* str, const size_t len, const int64_t num, const bool isNum,
                                         bool, bool) {
  switch (kind) {
    case Kind::TodoFile:
      if (keyIs(key, "storeId") && str) {
        assignStoreId(todoFile->storeId, str, len);
      } else if (keyIs(key, "revision") && isNum && num >= 0) {
        todoFile->revision = static_cast<uint32_t>(num);
      }
      break;
    case Kind::TodoPush:
      if (keyIs(key, "storeId") && str) {
        assignStoreId(todoPush->storeId, str, len);
      } else if (keyIs(key, "baseline") && isNum && num >= 0) {
        todoPush->baseline = static_cast<uint32_t>(num);
        todoPush->hasBaseline = true;
      }
      break;
    case Kind::Calendar:
      if (keyIs(key, "generatedAt") && isNum) {
        calendar->generatedAt = num;
      } else if (keyIs(key, "utcOffsetMin") && isNum && num >= -14 * 60 && num <= 14 * 60) {
        calendar->utcOffsetMin = static_cast<int16_t>(num);
      } else if (keyIs(key, "rangeStart") && str) {
        if (!parseIsoDate(str, len, calendar->rangeStart)) badValue = true;
      } else if (keyIs(key, "rangeEnd") && str) {
        if (!parseIsoDate(str, len, calendar->rangeEnd)) badValue = true;
      }
      break;
  }
}

void PlannerJsonDecoder::beginItem() {
  inItem = true;
  pendingValid = true;
  pendingTodo = TodoItem{};
  pendingEvent = CalendarEvent{};
  pendingEvent.endMin = -1;  // "not provided" marker, resolved in endItem()
}

void PlannerJsonDecoder::handleItemScalar(const char* str, const size_t len, const int64_t num, const bool isNum,
                                          const bool isBool, const bool boolValue) {
  const auto assignText = [str, len](std::string& target, const size_t maxBytes) {
    target.assign(str, len);
    decodeUnicodeEscapes(target);
    truncateUtf8(target, maxBytes);
  };

  if (kind == Kind::Calendar) {
    CalendarEvent& ev = pendingEvent;
    if (str && keyIs(key, "title")) {
      assignText(ev.title, kMaxTitleBytes * 4);  // cleanText() trims and truncates in endItem()
    } else if (str && keyIs(key, "location")) {
      assignText(ev.location, kMaxLocationBytes);
    } else if (str && keyIs(key, "calendar")) {
      assignText(ev.calendar, kMaxCalendarBytes);
    } else if (str && keyIs(key, "startDate")) {
      if (!parseIsoDate(str, len, ev.startDate)) pendingValid = false;
    } else if (str && keyIs(key, "endDate")) {
      if (!parseIsoDate(str, len, ev.endDate)) ev.endDate = 0;
    } else if (isNum && keyIs(key, "startMin")) {
      ev.startMin = clampMinutes(num);
    } else if (isNum && keyIs(key, "endMin")) {
      ev.endMin = clampMinutes(num);
    } else if (isBool && keyIs(key, "allDay")) {
      ev.allDay = boolValue;
    }
    return;
  }

  TodoItem& t = pendingTodo;
  if (str && keyIs(key, "id")) {
    t.id.assign(str, len);
  } else if (str && keyIs(key, "title")) {
    assignText(t.title, kMaxTitleBytes * 4);  // sanitizeTodo() trims and truncates
  } else if (str && keyIs(key, "due")) {
    if (!parseIsoDate(str, len, t.due)) t.due = 0;
  } else if (isBool && keyIs(key, "done")) {
    t.done = boolValue;
  } else if (isBool && keyIs(key, "deleted")) {
    t.deleted = boolValue;
  } else if (isNum && keyIs(key, "updatedAt")) {
    t.updatedAt = num;
  } else if (isNum && keyIs(key, "localRev") && kind == Kind::TodoFile && num >= 0) {
    t.localRev = static_cast<uint32_t>(num);
  }
}

void PlannerJsonDecoder::endItem() {
  inItem = false;
  if (kind == Kind::Calendar) {
    CalendarEvent& ev = pendingEvent;
    cleanText(ev.title, kMaxTitleBytes);
    cleanText(ev.location, kMaxLocationBytes);
    cleanText(ev.calendar, kMaxCalendarBytes);
    if (!pendingValid || ev.startDate == 0 || ev.title.empty() || calendar->events.size() >= kMaxEvents) {
      skipped++;
      return;
    }
    if (ev.endDate == 0 || ev.endDate < ev.startDate) ev.endDate = ev.startDate;
    if (ev.endMin < 0) ev.endMin = ev.startMin;
    if (ev.allDay) {
      ev.startMin = 0;
      ev.endMin = kMinutesPerDay;
    }
    calendar->events.push_back(std::move(ev));
    return;
  }

  TodoItem& t = pendingTodo;
  std::vector<TodoItem>& items = kind == Kind::TodoFile ? todoFile->items : todoPush->items;
  if (kind == Kind::TodoPush) {
    // The companion deletes by omission; a tombstone in a push is ignored rather
    // than resurrected.
    if (t.deleted) return;
    t.localRev = 0;
  }
  if (!sanitizeTodo(t) || items.size() >= kMaxTodos) {
    skipped++;
    return;
  }
  items.push_back(std::move(t));
}

}  // namespace planner
