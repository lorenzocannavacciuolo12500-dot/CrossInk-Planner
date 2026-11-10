#include "PlannerTodos.h"

#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <utility>

#include "PlannerDate.h"

namespace planner {

void truncateUtf8(std::string& s, const size_t maxBytes) {
  if (s.size() <= maxBytes) return;
  size_t cut = maxBytes;
  // Step back over UTF-8 continuation bytes (10xxxxxx) so the cut lands on a boundary.
  while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) cut--;
  s.resize(cut);
}

int findTodo(const TodoList& list, const std::string& id) {
  for (size_t i = 0; i < list.items.size(); i++) {
    if (list.items[i].id == id) return static_cast<int>(i);
  }
  return -1;
}

std::string makeDeviceTodoId(const std::string& storeId, const uint32_t revision, const uint32_t randomValue) {
  char buf[48];
  std::snprintf(buf, sizeof(buf), "d-%s-%08x-%08x", storeId.c_str(), static_cast<unsigned>(revision),
                static_cast<unsigned>(randomValue));
  std::string id(buf);
  truncateUtf8(id, kMaxIdBytes);
  return id;
}

namespace {
void stamp(TodoList& list, TodoItem& item, const int64_t now) {
  list.revision++;
  item.localRev = list.revision;
  item.updatedAt = now;
}

}  // namespace

void cleanText(std::string& s, const size_t maxBytes) {
  for (char& c : s) {
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
  }
  size_t start = 0;
  while (start < s.size() && s[start] == ' ') start++;
  size_t end = s.size();
  while (end > start && s[end - 1] == ' ') end--;
  if (start > 0 || end < s.size()) s = s.substr(start, end - start);
  truncateUtf8(s, maxBytes);
  while (!s.empty() && s.back() == ' ') s.pop_back();
}

bool sanitizeTodo(TodoItem& item) {
  truncateUtf8(item.id, kMaxIdBytes);
  cleanText(item.title, kMaxTitleBytes);
  if (item.due != 0 && !isValidYmd(item.due)) item.due = 0;
  if (item.updatedAt < 0) item.updatedAt = 0;
  return !item.id.empty() && !item.title.empty();
}

bool addLocalTodo(TodoList& list, std::string title, const int32_t due, const int64_t now,
                  const uint32_t randomValue) {
  TodoItem item;
  item.title = std::move(title);
  item.due = due;
  item.id = makeDeviceTodoId(list.storeId, list.revision + 1, randomValue);
  if (!sanitizeTodo(item)) return false;
  if (findTodo(list, item.id) >= 0) return false;  // astronomically unlikely, but never duplicate ids
  list.items.push_back(std::move(item));
  stamp(list, list.items.back(), now);
  enforceTodoLimit(list);
  return true;
}

bool setLocalTodoDone(TodoList& list, const size_t index, const bool done, const int64_t now) {
  if (index >= list.items.size()) return false;
  TodoItem& item = list.items[index];
  if (item.deleted || item.done == done) return false;
  item.done = done;
  stamp(list, item, now);
  return true;
}

bool deleteLocalTodo(TodoList& list, const size_t index, const int64_t now) {
  if (index >= list.items.size()) return false;
  TodoItem& item = list.items[index];
  if (item.deleted) return false;
  item.deleted = true;
  stamp(list, item, now);
  return true;
}

size_t deleteCompletedLocalTodos(TodoList& list, const int64_t now) {
  size_t count = 0;
  for (size_t i = 0; i < list.items.size(); i++) {
    if (list.items[i].done && !list.items[i].deleted && deleteLocalTodo(list, i, now)) count++;
  }
  return count;
}

size_t countOpenTodos(const TodoList& list) {
  size_t count = 0;
  for (const auto& item : list.items) {
    if (!item.deleted && !item.done) count++;
  }
  return count;
}

std::vector<uint16_t> visibleTodoOrder(const TodoList& list) {
  std::vector<uint16_t> order;
  order.reserve(list.items.size());
  for (size_t i = 0; i < list.items.size(); i++) {
    if (!list.items[i].deleted) order.push_back(static_cast<uint16_t>(i));
  }
  std::stable_sort(order.begin(), order.end(), [&list](const uint16_t a, const uint16_t b) {
    const TodoItem& x = list.items[a];
    const TodoItem& y = list.items[b];
    if (x.done != y.done) return !x.done;
    if (x.done) return false;  // completed: keep list order
    // Dated tasks first, earliest due first; undated tasks keep list order.
    const int32_t dx = x.due == 0 ? INT32_MAX : x.due;
    const int32_t dy = y.due == 0 ? INT32_MAX : y.due;
    return dx < dy;
  });
  return order;
}

void applyCompanionTodos(TodoList& list, std::vector<TodoItem>&& incoming, uint32_t baseline,
                         const bool baselineValid) {
  if (!baselineValid) baseline = 0;
  const auto unseenByCompanion = [baseline](const TodoItem& item) { return item.localRev > baseline; };

  std::unordered_map<std::string, size_t> localIndex;
  localIndex.reserve(list.items.size());
  for (size_t i = 0; i < list.items.size(); i++) localIndex.emplace(list.items[i].id, i);
  std::vector<bool> consumed(list.items.size(), false);

  std::vector<TodoItem> merged;
  merged.reserve(std::min(incoming.size() + list.items.size(), kMaxTodos + 1));

  std::unordered_map<std::string, bool> seenIncoming;
  seenIncoming.reserve(incoming.size());
  for (auto& in : incoming) {
    if (!sanitizeTodo(in)) continue;
    if (!seenIncoming.emplace(in.id, true).second) continue;  // duplicate id in payload: first wins
    in.deleted = false;
    in.localRev = 0;

    const auto it = localIndex.find(in.id);
    if (it != localIndex.end()) {
      consumed[it->second] = true;
      TodoItem& local = list.items[it->second];
      // An unseen device change wins when it is newer, or when its time is
      // unknown (edited while the clock was not set): it was made after the
      // companion's last look, so it is the best guess for the user's intent.
      if (unseenByCompanion(local) && (local.updatedAt == 0 || local.updatedAt > in.updatedAt)) {
        merged.push_back(std::move(local));  // may be a tombstone
        continue;
      }
    }
    merged.push_back(std::move(in));
  }

  // Device items the companion did not send: keep only changes it has not seen.
  for (size_t i = 0; i < list.items.size(); i++) {
    if (consumed[i]) continue;
    if (unseenByCompanion(list.items[i])) merged.push_back(std::move(list.items[i]));
  }

  list.items = std::move(merged);
  enforceTodoLimit(list);
}

size_t enforceTodoLimit(TodoList& list) {
  size_t dropped = 0;
  const auto dropFirstMatching = [&list, &dropped](auto predicate) {
    while (list.items.size() > kMaxTodos) {
      // Oldest = smallest updatedAt; scan once per removal (lists are small).
      int victim = -1;
      for (size_t i = 0; i < list.items.size(); i++) {
        if (!predicate(list.items[i])) continue;
        if (victim < 0 || list.items[i].updatedAt < list.items[static_cast<size_t>(victim)].updatedAt) {
          victim = static_cast<int>(i);
        }
      }
      if (victim < 0) return;
      list.items.erase(list.items.begin() + victim);
      dropped++;
    }
  };
  dropFirstMatching([](const TodoItem& t) { return t.deleted; });
  dropFirstMatching([](const TodoItem& t) { return t.done; });
  dropFirstMatching([](const TodoItem&) { return true; });
  return dropped;
}

}  // namespace planner
