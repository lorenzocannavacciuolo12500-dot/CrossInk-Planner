#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "PlannerTypes.h"

// To-do list operations made on the device, and the merge applied when the
// companion app pushes its list. See docs/planner-sync.md ("Merge rules").
namespace planner {

// Returns the index of the item with this id (tombstones included), or -1.
int findTodo(const TodoList& list, const std::string& id);

// Creates a device-side id such as "d-5f3a9c21-0000000e-1a2b3c4d".
std::string makeDeviceTodoId(const std::string& storeId, uint32_t revision, uint32_t randomValue);

// Local edits. `now` is unix seconds (0 when the clock is not valid).
// Each successful edit bumps list.revision and stamps the item's localRev.
bool addLocalTodo(TodoList& list, std::string title, int32_t due, int64_t now, uint32_t randomValue);
bool setLocalTodoDone(TodoList& list, size_t index, bool done, int64_t now);
bool deleteLocalTodo(TodoList& list, size_t index, int64_t now);
// Deletes every visible completed task; returns how many were deleted.
size_t deleteCompletedLocalTodos(TodoList& list, int64_t now);

size_t countOpenTodos(const TodoList& list);

// Indices of non-deleted items in display order: open tasks first (dated ones by
// due date, then undated), then completed ones; ties keep list order.
std::vector<uint16_t> visibleTodoOrder(const TodoList& list);

// One block of the To-Do screen: the tasks with no list, then one block per
// list, like the companion app. `listIndex` is kNoList for the first block.
struct TodoSection {
  uint8_t listIndex = kNoList;
  std::vector<uint16_t> items;
};

// The visible tasks grouped by list. Inside a block the order follows the
// companion app's positions when it sent any, otherwise the order the tasks
// were made in. Completed tasks keep their place.
std::vector<TodoSection> visibleTodoSections(const TodoList& list);

// Index of `name` in `lists`, adding it when it is new. Returns kNoList for an
// empty name or when there is no room left.
uint8_t internListName(std::vector<std::string>& lists, std::string name);
inline uint8_t internTodoList(TodoList& list, std::string name) { return internListName(list.lists, std::move(name)); }

// Applies a full list pushed by the companion. `incoming` items carry no
// tombstones and no localRev. `baselineValid` is false when the companion's
// storeId does not match (all device changes are then treated as unseen).
// `incomingLists` is the push's own name table: the indices on `incoming` are
// translated into the device's table. Pass nullptr when there is none.
void applyCompanionTodos(TodoList& list, std::vector<TodoItem>&& incoming, uint32_t baseline, bool baselineValid,
                         const std::vector<std::string>* incomingLists = nullptr);

// Keeps the list within kMaxTodos, dropping tombstones first, then the oldest
// completed tasks, then the oldest open ones. Returns how many were dropped.
size_t enforceTodoLimit(TodoList& list);

// Normalizes a parsed item: trims field sizes. Returns false when unusable
// (empty id or empty title).
bool sanitizeTodo(TodoItem& item);

}  // namespace planner
