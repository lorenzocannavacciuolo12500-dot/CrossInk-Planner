#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "PlannerTypes.h"

class StreamingJsonParser;

// Streaming JSON encode/decode for Planner files and HTTP payloads.
// Decoding uses lib/JsonParser's StreamingJsonParser, so a document never has
// to be held in RAM as a tree: items are materialized one at a time straight
// into the target vectors (bounded by kMaxTodos / kMaxEvents).
namespace planner {

// Output sink: receives encoded bytes in small pieces.
struct JsonSink {
  void* ctx = nullptr;
  void (*write)(void* ctx, const char* data, size_t len) = nullptr;
};

enum class TodoJsonMode : uint8_t {
  File,  // everything, including localRev (todos.json). Not "Storage": HalStorage.h #defines that name.
  Api,   // what the companion sees (no localRev)
};

void writeTodoListJson(const TodoList& list, TodoJsonMode mode, const JsonSink& sink);
void writeCalendarJson(const Calendar& calendar, const JsonSink& sink);

// What the companion sends with POST /api/planner/todos.
struct CompanionTodoPush {
  std::string storeId;
  uint32_t baseline = 0;
  bool hasBaseline = false;
  // Names of the lists the push mentions; items point into this by index.
  std::vector<std::string> lists;
  std::vector<TodoItem> items;
};

// Incremental decoder. Call feed() with consecutive chunks, then finish().
class PlannerJsonDecoder {
 public:
  enum class Kind : uint8_t { TodoFile, TodoPush, Calendar };

  // The target is cleared and filled in place. The decoder registers itself
  // as the parser context, so it is neither copyable nor movable.
  explicit PlannerJsonDecoder(TodoList& out);
  explicit PlannerJsonDecoder(CompanionTodoPush& out);
  explicit PlannerJsonDecoder(Calendar& out);

  PlannerJsonDecoder(PlannerJsonDecoder&&) = delete;
  PlannerJsonDecoder& operator=(PlannerJsonDecoder&&) = delete;
  PlannerJsonDecoder(const PlannerJsonDecoder&) = delete;
  PlannerJsonDecoder& operator=(const PlannerJsonDecoder&) = delete;
  ~PlannerJsonDecoder();

  void feed(const char* data, size_t len);
  // Returns false on malformed JSON or a document of the wrong shape.
  // On success the target is fully populated (and calendar events are sorted).
  bool finish();

  // Number of array items that were skipped (invalid or over the limit).
  size_t skippedItems() const { return skipped; }

  // Callbacks used by the parser trampolines; not part of the public contract.
  void onKey(const char* key, size_t len);
  void onString(const char* value, size_t len);
  void onNumber(const char* value, size_t len);
  void onBool(bool value);
  void onNull();
  void onObjectStart();
  void onObjectEnd();
  void onArrayStart();
  void onArrayEnd();

 private:
  void init();
  void handleTopScalar(const char* str, size_t len, int64_t num, bool isNum, bool isBool, bool boolValue);
  void handleItemScalar(const char* str, size_t len, int64_t num, bool isNum, bool isBool, bool boolValue);
  void beginItem();
  void endItem();

  Kind kind;
  TodoList* todoFile = nullptr;
  CompanionTodoPush* todoPush = nullptr;
  Calendar* calendar = nullptr;

  StreamingJsonParser* parser = nullptr;  // owned; heap because it carries a 512-byte token buffer
  int depth = 0;
  bool sawRoot = false;
  bool inItemsArray = false;
  bool inItem = false;
  bool badShape = false;
  bool badValue = false;
  int itemsArrayDepth = -1;
  char key[24] = {};

  TodoItem pendingTodo;
  // Name of the list the current task names; resolved into an index at endItem().
  std::string pendingListName;
  CalendarEvent pendingEvent;
  bool pendingValid = true;
  size_t skipped = 0;
};

// Decodes JSON \uXXXX escapes (as passed through by StreamingJsonParser) to UTF-8.
void decodeUnicodeEscapes(std::string& s);

}  // namespace planner
