#include "TodoListActivity.h"

#include <Arduino.h>
#include <FreeInkUIIcon.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <PlannerTodos.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "PlannerFormat.h"
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
  size_t visible = 0;
  for (const auto& section : planner::visibleTodoSections(list)) {
    visible += section.items.size();
    if (section.listIndex != planner::kNoList) {
      if (section.items.empty()) continue;  // an empty list is not worth a row
      rows.push_back(Row{0, section.listIndex, true});
      if (isCollapsed(section.listIndex)) continue;
    }
    for (const uint16_t item : section.items) {
      rows.push_back(Row{item, section.listIndex, false});
    }
  }
  completedCount = visible - planner::countOpenTodos(list);
  todayYmd = PlannerFormat::today();
  std::snprintf(deleteCompletedLabel, sizeof(deleteCompletedLabel), tr(STR_PLANNER_DELETE_COMPLETED_FORMAT),
                static_cast<unsigned>(completedCount));
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
      unsigned count = 0;
      for (const Row& other : rows) {
        if (!other.heading && other.listIndex == row.listIndex) count++;
      }
      if (isCollapsed(row.listIndex)) {
        count = 0;
        for (const auto& item : list.items) {
          if (!item.deleted && item.listIndex == row.listIndex) count++;
        }
      }
      char text[48];
      std::snprintf(text, sizeof(text), tr(STR_PLANNER_LIST_TASKS_FORMAT), count);
      std::string subtitle = text;
      if (isCollapsed(row.listIndex)) {
        subtitle += " - ";
        subtitle += tr(STR_PLANNER_LIST_CLOSED);
      }
      subtitles.push_back(std::move(subtitle));
      headings.push_back(list.lists[row.listIndex]);

      fui::ListItem heading;
      heading.label = headings.back().c_str();
      heading.subtitle = subtitles.back().c_str();
      items.push_back(heading);
      continue;
    }

    const planner::TodoItem& task = list.items[row.item];
    std::string subtitle;
    if (task.due != 0) {
      char day[32];
      char text[64];
      PlannerFormat::dayLabel(task.due, todayYmd, day, sizeof(day));
      const bool overdue = !task.done && todayYmd != 0 && task.due < todayYmd;
      std::snprintf(text, sizeof(text), overdue ? tr(STR_PLANNER_OVERDUE_FORMAT) : tr(STR_PLANNER_DUE_FORMAT), day);
      subtitle = text;
    } else if (task.done) {
      subtitle = tr(STR_PLANNER_DONE);
    }
    subtitles.push_back(std::move(subtitle));

    fui::ListItem row;
    row.label = task.title.c_str();
    row.subtitle = subtitles.back().empty() ? nullptr : subtitles.back().c_str();
    // Square checkbox in front of the title, filled with a check mark when done.
    row.icon = fui::bitmapFromIcon(task.done ? icon_planner_checkbox_checked_32 : icon_planner_checkbox_empty_32);
    items.push_back(row);
  }

  fui::ListItem add;
  add.label = tr(STR_PLANNER_ADD_TASK);
  add.subtitle = rows.empty() ? nullptr : tr(STR_PLANNER_HOLD_TO_DELETE);
  items.push_back(add);

  if (completedCount > 0) {
    fui::ListItem clear;
    clear.label = deleteCompletedLabel;
    items.push_back(clear);
  }
}

void TodoListActivity::activate(const int index, const bool longPress) {
  if (index >= 0 && index < addRowIndex()) {
    const Row& row = rows[static_cast<size_t>(index)];
    if (row.heading) {
      if (longPress) return;
      toggleCollapsed(row.listIndex);
      refreshDerived();
      clampSelection();
      requestUpdate();
      return;
    }
    const size_t itemIndex = row.item;
    if (longPress) {
      confirmDeleteTask(itemIndex);
      return;
    }
    const bool done = !list.items[itemIndex].done;
    if (planner::setLocalTodoDone(list, itemIndex, done, PlannerStore::nowForEdits())) {
      // The checked task drops to the completed ones, but the cursor stays on
      // the row it was on, so a list can be ticked off from the top down.
      persist();
      clampSelection();
    }
    return;
  }
  if (longPress) return;
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

void TodoListActivity::confirmDeleteTask(const size_t itemIndex) {
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_PLANNER_DELETE_TASK),
                                                              list.items[itemIndex].title, true);
  if (!confirmation) {
    LOG_ERR("PLN", "OOM: delete confirmation");
    return;
  }
  const std::string id = list.items[itemIndex].id;
  startActivityForResult(std::move(confirmation), [this, id](const ActivityResult& result) {
    if (result.isCancelled) return;
    const int idx = planner::findTodo(list, id);
    if (idx >= 0 && planner::deleteLocalTodo(list, static_cast<size_t>(idx), PlannerStore::nowForEdits())) persist();
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

void TodoListActivity::onSideButton(const bool next) {
  // Calendar <- Home -> To-Do List: Previous Page returns to Home.
  if (!next) onGoHome();
}
