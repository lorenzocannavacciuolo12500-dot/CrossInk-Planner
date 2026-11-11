#include "TodoListActivity.h"

#include <Arduino.h>
#include <FreeInkUIIcon.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <PlannerTodos.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "PlannerStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/icons/plannerCheckboxIcons.h"
#include "platform/PlannerPlatform.h"

namespace fui = freeink::ui;

namespace {
// Lists the user folded away. Kept until the reader restarts: a fold is a
// glance-level choice, not worth a write to the SD card on every tap.
std::vector<std::string> collapsedLists;
}  // namespace

TodoListActivity::TodoListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : PlannerListActivity("PlannerTodo", renderer, mappedInput) {}

void TodoListActivity::onEnter() {
  reload();
  selectedIndex = 0;
  topIndex = 0;
  PlannerListActivity::onEnter();
}

void TodoListActivity::onExit() {
  // Nothing stays resident outside the screen (a full list can be ~30 KB).
  list = planner::TodoList{};
  std::vector<Row>().swap(rows);
  std::vector<std::string>().swap(subtitles);
  std::vector<std::string>().swap(headings);
  PlannerListActivity::onExit();
}

void TodoListActivity::reload() {
  {
    std::lock_guard<std::mutex> guard(PlannerStore::lock());
    if (!PlannerStore::loadTodos(list)) LOG_ERR("PLN", "todos.json unreadable; starting an empty list");
  }
  refreshDerived();
}

bool TodoListActivity::isCollapsed(const uint8_t listIndex) const {
  if (listIndex >= list.lists.size()) return false;
  return std::find(collapsedLists.begin(), collapsedLists.end(), list.lists[listIndex]) != collapsedLists.end();
}

void TodoListActivity::toggleCollapsed(const uint8_t listIndex) {
  if (listIndex >= list.lists.size()) return;
  const std::string& name = list.lists[listIndex];
  const auto at = std::find(collapsedLists.begin(), collapsedLists.end(), name);
  if (at != collapsedLists.end()) {
    collapsedLists.erase(at);
  } else {
    collapsedLists.push_back(name);
  }
}

void TodoListActivity::refreshDerived() {
  rows.clear();
  // Same page as the companion app: tasks in no list and whole lists, in the
  // order arranged there; a list's tasks sit under its name, set in a little.
  for (const auto& block : planner::visibleTodoLayout(list)) {
    if (block.listIndex == planner::kNoList) {
      rows.push_back(Row{block.items.front(), planner::kNoList, false});
      continue;
    }
    rows.push_back(Row{0, block.listIndex, true});
    if (isCollapsed(block.listIndex)) continue;
    const uint8_t listIndex = block.listIndex;
    std::transform(block.items.begin(), block.items.end(), std::back_inserter(rows),
                   [listIndex](const uint16_t item) { return Row{item, listIndex, false}; });
  }
  completedCount = planner::countDeletableCompletedTodos(list);
}

void TodoListActivity::persist() {
  bool ok;
  {
    std::lock_guard<std::mutex> guard(PlannerStore::lock());
    ok = PlannerStore::saveTodos(list);
  }
  if (!ok) LOG_ERR("PLN", "Could not save to-do list");
  refreshDerived();
  clampSelection();
  requestUpdate();
}

int TodoListActivity::itemCount() const { return addRowIndex() + 1 + (completedCount > 0 ? 1 : 0); }

const char* TodoListActivity::screenTitle() const { return tr(STR_PLANNER_TODO); }

const char* TodoListActivity::confirmHint() const {
  if (selectedIndex < 0 || selectedIndex >= addRowIndex()) return tr(STR_SELECT);
  const Row& row = rows[static_cast<size_t>(selectedIndex)];
  if (!row.heading) return tr(STR_PLANNER_TOGGLE);
  return isCollapsed(row.listIndex) ? tr(STR_PLANNER_LIST_OPEN) : tr(STR_PLANNER_LIST_CLOSE);
}

void TodoListActivity::fillItems(std::vector<fui::ListItem>& items) {
  subtitles.clear();
  subtitles.reserve(rows.size());
  headings.clear();
  headings.reserve(list.lists.size());

  for (const Row& row : rows) {
    if (row.heading) {
      // Under the name: what Confirm does to the list (no task counts).
      subtitles.push_back(isCollapsed(row.listIndex) ? std::string(tr(STR_PLANNER_LIST_OPEN))
                                                     : std::string(tr(STR_PLANNER_LIST_CLOSE)));
      headings.push_back(list.lists[row.listIndex]);

      fui::ListItem heading;
      heading.label = headings.back().c_str();
      heading.subtitle = subtitles.back().c_str();
      items.push_back(heading);
      continue;
    }

    const planner::TodoItem& task = list.items[row.item];
    // No due dates on the To-Do page any more (the app dropped them); a
    // completed task says so under its title, since the list cannot strike it.
    std::string subtitle = task.done ? std::string(tr(STR_PLANNER_DONE)) : std::string();
    subtitles.push_back(std::move(subtitle));

    fui::ListItem entry;
    entry.label = task.title.c_str();
    entry.subtitle = subtitles.back().empty() ? nullptr : subtitles.back().c_str();
    // Square checkbox in front of the title, filled with a check mark when
    // done; inside a list the box is set in from the list's name.
    const bool nested = row.listIndex != planner::kNoList;
    const freeink::Icon& box =
        task.done ? (nested ? icon_planner_checkbox_checked_indented_32 : icon_planner_checkbox_checked_32)
                  : (nested ? icon_planner_checkbox_empty_indented_32 : icon_planner_checkbox_empty_32);
    entry.icon = fui::bitmapFromIcon(box);
    items.push_back(entry);
  }

  fui::ListItem add;
  add.label = tr(STR_PLANNER_ADD_TASK);
  add.subtitle = nullptr;
  items.push_back(add);

  if (completedCount > 0) {
    fui::ListItem clear;
    clear.label = tr(STR_PLANNER_DELETE_COMPLETED);
    items.push_back(clear);
  }
}

void TodoListActivity::activate(const int index, const bool longPress) {
  if (index >= 0 && index < addRowIndex()) {
    const Row& row = rows[static_cast<size_t>(index)];
    if (row.heading) {
      if (longPress) {
        confirmDeleteList(row.listIndex);
        return;
      }
      toggleCollapsed(row.listIndex);
      refreshDerived();
      clampSelection();
      requestUpdate();
      return;
    }
    // Holding Confirm on a task is just a slow press.
    const size_t itemIndex = row.item;
    const bool done = !list.items[itemIndex].done;
    if (planner::setLocalTodoDone(list, itemIndex, done, PlannerStore::nowForEdits())) {
      // The task keeps its place and the cursor stays on it.
      persist();
      clampSelection();
    }
    return;
  }
  if (index == addRowIndex()) {
    openAddTask();
  } else if (index == deleteCompletedRowIndex()) {
    confirmDeleteCompleted();
  }
}

void TodoListActivity::openAddTask() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_PLANNER_NEW_TASK), "",
                                                           planner::kMaxTitleBytes, InputType::Text);
  if (!keyboard) {
    LOG_ERR("PLN", "OOM: keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto* text = std::get_if<KeyboardResult>(&result.data);
    if (!text) return;
    if (planner::addLocalTodo(list, text->text, 0, PlannerStore::nowForEdits(), planner_platform::random32())) {
      persist();
      // Select the new task (open, undated: it sorts after dated open tasks).
      const size_t newIndex = list.items.size() - 1;
      for (size_t i = 0; i < rows.size(); i++) {
        if (!rows[i].heading && rows[i].item == newIndex) selectedIndex = static_cast<int>(i);
      }
      clampSelection();
    }
  });
}

void TodoListActivity::confirmDeleteCompleted() {
  // The popup shows heading and body on one line: the question alone reads best.
  auto confirmation =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_PLANNER_DELETE_COMPLETED_CONFIRM), "");
  if (!confirmation) {
    LOG_ERR("PLN", "OOM: delete confirmation");
    return;
  }
  startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    if (planner::deleteCompletedLocalTodos(list, PlannerStore::nowForEdits()) > 0) persist();
  });
}

void TodoListActivity::confirmDeleteList(const uint8_t listIndex) {
  if (listIndex >= list.lists.size()) return;
  // Opened while Confirm is still held: its release must not answer the popup.
  auto confirmation =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_PLANNER_DELETE_LIST_CONFIRM), "", true);
  if (!confirmation) {
    LOG_ERR("PLN", "OOM: delete list confirmation");
    return;
  }
  const std::string name = list.lists[listIndex];
  startActivityForResult(std::move(confirmation), [this, name](const ActivityResult& result) {
    if (result.isCancelled) return;
    // Look the list up again by name: the table may have changed meanwhile.
    const auto at = std::find(list.lists.begin(), list.lists.end(), name);
    if (at == list.lists.end()) return;
    const auto index = static_cast<uint8_t>(std::distance(list.lists.begin(), at));
    if (planner::deleteLocalTodoList(list, index, PlannerStore::nowForEdits()) > 0) persist();
  });
}

void TodoListActivity::onSideButton(const bool next) {
  // Calendar <- Home -> To-Do List: Previous Page returns to Home.
  if (!next) onGoHome();
}
