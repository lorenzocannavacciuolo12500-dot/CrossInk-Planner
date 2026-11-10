// Native tests for lib/Planner (to-do merge, JSON codec, date math).
// Plain asserts so the suite also builds without GoogleTest.
#include <PlannerDate.h>
#include <PlannerJson.h>
#include <PlannerTodos.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace planner;

namespace {
int checks = 0;
#define CHECK(cond)                                                                 \
  do {                                                                              \
    checks++;                                                                       \
    if (!(cond)) {                                                                  \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      std::abort();                                                                 \
    }                                                                               \
  } while (0)

void appendSink(void* ctx, const char* data, size_t len) { static_cast<std::string*>(ctx)->append(data, len); }

std::string encode(const TodoList& list, TodoJsonMode mode) {
  std::string out;
  writeTodoListJson(list, mode, JsonSink{&out, appendSink});
  return out;
}

std::string encode(const Calendar& cal) {
  std::string out;
  writeCalendarJson(cal, JsonSink{&out, appendSink});
  return out;
}

template <typename T>
bool decode(const std::string& json, T& out, size_t chunk = 7) {
  PlannerJsonDecoder decoder(out);
  for (size_t i = 0; i < json.size(); i += chunk) {
    decoder.feed(json.data() + i, std::min(chunk, json.size() - i));
  }
  return decoder.finish();
}

TodoItem companionItem(const char* id, const char* title, bool done, int64_t updatedAt) {
  TodoItem t;
  t.id = id;
  t.title = title;
  t.done = done;
  t.updatedAt = updatedAt;
  return t;
}

void testDates() {
  CHECK(ymdToDays(19700101) == 0);
  CHECK(daysToYmd(0) == 19700101);
  CHECK(addDays(20261231, 1) == 20270101);
  CHECK(addDays(20240301, -1) == 20240229);
  CHECK(addDays(20250301, -1) == 20250228);
  CHECK(weekdayOf(20261110) == 2);  // Tuesday
  CHECK(weekdayOf(19700101) == 4);  // Thursday
  int32_t d = -1;
  CHECK(parseIsoDate("2026-11-10", 10, d) && d == 20261110);
  CHECK(parseIsoDate("", 0, d) && d == 0);
  CHECK(!parseIsoDate("2026-02-30", 10, d));
  CHECK(!parseIsoDate("2026/11/10", 10, d));
  char buf[12];
  formatIsoDate(20260105, buf, sizeof(buf));
  CHECK(std::strcmp(buf, "2026-01-05") == 0);
  int minute = -1;
  // 2026-11-09T23:30:00Z is 00:30 on the 10th in UTC+1.
  CHECK(localYmdFromUtc(1794267000, 60, &minute) == 20261110 && minute == 30);
  CHECK(localYmdFromUtc(1794267000, 0, &minute) == 20261109 && minute == 23 * 60 + 30);
}

void testUtf8Helpers() {
  std::string s = "caff\xC3\xA8";  // "caffè"
  truncateUtf8(s, 5);
  CHECK(s == "caff");
  std::string t = "  riga\nnuova  ";
  cleanText(t, 100);
  CHECK(t == "riga nuova");
  std::string u = "Citt\\u00e0 \\ud83d\\ude00";
  decodeUnicodeEscapes(u);
  CHECK(u == "Citt\xC3\xA0 \xF0\x9F\x98\x80");
}

void testLocalEdits() {
  TodoList list;
  list.storeId = "abcd1234";
  CHECK(addLocalTodo(list, "  Comprare il latte ", 0, 1000, 42));
  CHECK(list.revision == 1);
  CHECK(list.items[0].title == "Comprare il latte");
  CHECK(list.items[0].localRev == 1);
  CHECK(list.items[0].id.rfind("d-abcd1234-", 0) == 0);
  CHECK(!addLocalTodo(list, "   ", 0, 1000, 43));  // empty title rejected
  CHECK(addLocalTodo(list, "Dentista", 20261112, 1001, 44));
  CHECK(setLocalTodoDone(list, 0, true, 1002));
  CHECK(!setLocalTodoDone(list, 0, true, 1003));  // no-op does not bump revision
  CHECK(list.revision == 3);
  CHECK(countOpenTodos(list) == 1);

  auto order = visibleTodoOrder(list);
  CHECK(order.size() == 2 && order[0] == 1 && order[1] == 0);  // open first, done last

  CHECK(deleteCompletedLocalTodos(list, 1004) == 1);
  CHECK(list.items[0].deleted);
  order = visibleTodoOrder(list);
  CHECK(order.size() == 1 && order[0] == 1);
}

void testMergeCompanionWins() {
  TodoList list;
  list.storeId = "s1";
  list.items.push_back(companionItem("a", "A", false, 100));  // synced earlier (localRev 0)
  list.items.push_back(companionItem("b", "B", false, 100));
  list.revision = 5;

  std::vector<TodoItem> incoming = {companionItem("a", "A edited", true, 200), companionItem("c", "C", false, 150)};
  applyCompanionTodos(list, std::move(incoming), 5, true);
  // b was left out by the companion => deleted; c is new.
  CHECK(list.items.size() == 2);
  CHECK(list.items[0].id == "a" && list.items[0].title == "A edited" && list.items[0].done);
  CHECK(list.items[1].id == "c");
  CHECK(list.revision == 5);
}

void testMergeKeepsUnseenDeviceChanges() {
  TodoList list;
  list.storeId = "s1";
  list.items.push_back(companionItem("a", "A", false, 100));
  list.revision = 2;
  // Device checks off "a" and adds a task after the companion's GET (baseline 2).
  CHECK(setLocalTodoDone(list, 0, true, 300));
  CHECK(addLocalTodo(list, "Nuovo", 0, 301, 7));
  const std::string newId = list.items[1].id;

  // Companion still has the old "a" (older timestamp) and does not know the new task.
  std::vector<TodoItem> incoming = {companionItem("a", "A", false, 100)};
  applyCompanionTodos(list, std::move(incoming), 2, true);
  CHECK(list.items.size() == 2);
  CHECK(list.items[0].id == "a" && list.items[0].done);  // device change is newer
  CHECK(list.items[1].id == newId);                      // unseen device task kept

  // Next push with baseline = current revision: the companion has seen everything.
  const uint32_t baseline = list.revision;
  incoming = {companionItem("a", "A", true, 300)};
  applyCompanionTodos(list, std::move(incoming), baseline, true);
  CHECK(list.items.size() == 1);  // companion deleted the new task by omission
}

void testMergeTombstones() {
  TodoList list;
  list.storeId = "s1";
  list.items.push_back(companionItem("a", "A", false, 100));
  list.revision = 1;
  CHECK(deleteLocalTodo(list, 0, 500));
  // Companion (baseline 1) still sends "a" with an older timestamp: tombstone wins and is reported.
  std::vector<TodoItem> incoming = {companionItem("a", "A", false, 100)};
  applyCompanionTodos(list, std::move(incoming), 1, true);
  CHECK(list.items.size() == 1 && list.items[0].deleted);
  // Companion acknowledges (baseline 2) and omits it: tombstone purged.
  applyCompanionTodos(list, {}, 2, true);
  CHECK(list.items.empty());
}

void testMergeUnknownClock() {
  TodoList list;
  list.storeId = "s1";
  list.items.push_back(companionItem("a", "A", false, 100));
  list.revision = 1;
  // Checked off while the clock was not set (now = 0).
  CHECK(setLocalTodoDone(list, 0, true, 0));
  applyCompanionTodos(list, {companionItem("a", "A", false, 100)}, 1, true);
  CHECK(list.items.size() == 1 && list.items[0].done);
}

void testMergeInvalidBaseline() {
  TodoList list;
  list.storeId = "new-store";
  CHECK(addLocalTodo(list, "Locale", 0, 10, 1));
  // A companion with a cursor from another device must not wipe local tasks.
  applyCompanionTodos(list, {companionItem("x", "Remoto", false, 20)}, 999, false);
  CHECK(list.items.size() == 2);
}

void testTodoLimit() {
  TodoList list;
  list.storeId = "s";
  std::vector<TodoItem> incoming;
  for (size_t i = 0; i < kMaxTodos + 10; i++) {
    incoming.push_back(companionItem(("id" + std::to_string(i)).c_str(), "t", i < 20, static_cast<int64_t>(i)));
  }
  applyCompanionTodos(list, std::move(incoming), 0, true);
  CHECK(list.items.size() == kMaxTodos);
  // The 10 oldest completed items were dropped, open ones kept.
  CHECK(findTodo(list, "id0") < 0 && findTodo(list, "id9") < 0 && findTodo(list, "id10") >= 0);
}

void testTodoRoundTrip() {
  TodoList list;
  list.storeId = "abcd";
  CHECK(addLocalTodo(list, "Quote \" and \\ slash", 20261201, 1762790400, 9));
  CHECK(addLocalTodo(list, "Caff\xC3\xA8", 0, 1762790401, 10));
  CHECK(deleteLocalTodo(list, 1, 1762790402));

  const std::string json = encode(list, TodoJsonMode::File);
  TodoList back;
  CHECK(decode(json, back));
  CHECK(back.storeId == "abcd" && back.revision == list.revision);
  CHECK(back.items.size() == 2);
  CHECK(back.items[0].title == "Quote \" and \\ slash");
  CHECK(back.items[0].due == 20261201);
  CHECK(back.items[1].deleted && back.items[1].localRev == 3);
  CHECK(back.items[1].title == "Caff\xC3\xA8");

  const std::string api = encode(list, TodoJsonMode::Api);
  CHECK(api.find("localRev") == std::string::npos);
  CHECK(api.find("\"deleted\":true") != std::string::npos);
}

void testTodoPushDecode() {
  const std::string body =
      R"({"storeId":"abcd","baseline":12,"extra":{"nested":[1,2]},)"
      R"("items":[{"id":"c-1","title":"Uno","due":"2026-11-20","done":true,"updatedAt":1762790400},)"
      R"({"id":"","title":"no id"},{"id":"c-2","title":"Tomb","deleted":true},)"
      R"({"id":"c-3","title":"Tre","due":"bad","localRev":99}]})";
  CompanionTodoPush push;
  PlannerJsonDecoder decoder(push);
  decoder.feed(body.data(), body.size());
  CHECK(decoder.finish());
  CHECK(push.storeId == "abcd" && push.hasBaseline && push.baseline == 12);
  CHECK(push.items.size() == 2);
  CHECK(push.items[0].id == "c-1" && push.items[0].done && push.items[0].due == 20261120);
  CHECK(push.items[1].id == "c-3" && push.items[1].due == 0 && push.items[1].localRev == 0);
  CHECK(decoder.skippedItems() == 1);
}

void testMalformedJson() {
  TodoList out;
  CHECK(!decode(std::string(R"({"items":[{"id":"a","title":"b"})"), out));  // truncated
  CHECK(!decode(std::string(R"([1,2,3])"), out));                           // wrong root
  CHECK(!decode(std::string(""), out));
}

void testCalendarRoundTrip() {
  const std::string body =
      R"({"generatedAt":1762790400,"utcOffsetMin":60,"rangeStart":"2026-11-09","rangeEnd":"2026-12-10",)"
      R"("events":[)"
      R"({"title":"Riunione","location":"Ufficio","calendar":"Lavoro","startDate":"2026-11-12","startMin":600,"endDate":"2026-11-12","endMin":660},)"
      R"({"title":"Ferie","startDate":"2026-11-12","endDate":"2026-11-14","allDay":true},)"
      R"({"title":"Dentista","startDate":"2026-11-10","startMin":540},)"
      R"({"title":"","startDate":"2026-11-10"},)"
      R"({"title":"Senza data"}]})";
  Calendar cal;
  CHECK(decode(body, cal, 5));
  CHECK(cal.utcOffsetMin == 60 && cal.rangeStart == 20261109 && cal.rangeEnd == 20261210);
  CHECK(cal.events.size() == 3);
  CHECK(cal.events[0].title == "Dentista" && cal.events[0].endMin == 540);  // sorted by date
  CHECK(cal.events[1].title == "Ferie" && cal.events[1].allDay && cal.events[1].endDate == 20261114);
  CHECK(cal.events[2].title == "Riunione" && cal.events[2].location == "Ufficio");

  Calendar back;
  CHECK(decode(encode(cal), back));
  CHECK(back.events.size() == 3 && back.events[2].calendar == "Lavoro" && back.generatedAt == 1762790400);
}

void testCalendarLimit() {
  std::string body = R"({"events":[)";
  for (size_t i = 0; i < kMaxEvents + 5; i++) {
    if (i) body += ",";
    body += R"({"title":"e","startDate":"2026-11-10"})";
  }
  body += "]}";
  Calendar cal;
  PlannerJsonDecoder decoder(cal);
  decoder.feed(body.data(), body.size());
  CHECK(decoder.finish());
  CHECK(cal.events.size() == kMaxEvents && decoder.skippedItems() == 5);
}
}  // namespace

int main() {
  testDates();
  testUtf8Helpers();
  testLocalEdits();
  testMergeCompanionWins();
  testMergeKeepsUnseenDeviceChanges();
  testMergeTombstones();
  testMergeUnknownClock();
  testMergeInvalidBaseline();
  testTodoLimit();
  testTodoRoundTrip();
  testTodoPushDecode();
  testMalformedJson();
  testCalendarRoundTrip();
  testCalendarLimit();
  std::printf("planner tests: %d checks passed\n", checks);
  return 0;
}
