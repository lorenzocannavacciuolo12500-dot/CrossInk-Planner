#include "TodoListActivity.h"

#include <Arduino.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <PlannerTodos.h>

#include <cstdio>
#include <mutex>
#include <utility>

#include "PlannerFormat.h"
#include "PlannerStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"

namespace fui = freeink::ui;

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
  std::vector<uint16_t>().swap(order);
  std::vector<std::string>().swap(subtitles);
  PlannerListActivity::onExit();
}

void TodoListActivity::reload() {
  {
    std::lock_guard<std::mutex> guard(PlannerStore::lock());
    if (!PlannerStore::loadTodos(list)) LOG_ERR("PLN", "todos.json unreadable; starting an empty list");
  }
  refreshDerived();
}

void TodoListActivity::refreshDerived() {
  order = planner::visibleTodoOrder(list);
  completedCount = order.size() - planner::countOpenTodos(list);
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
  return selectedIndex < addRowIndex() ? tr(STR_PLANNER_TOGGLE) : tr(STR_SELECT);
}

void TodoListActivity::fillItems(std::vector<fui::ListItem>& items) {
  subtitles.clear();
  subtitles.reserve(order.size());

  for (const uint16_t idx : order) {
    const planner::TodoItem& task = list.items[idx];
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
    row.toggle = true;
    row.toggleChecked = task.done;
    items.push_back(row);
  }

  fui::ListItem add;
  add.label = tr(STR_PLANNER_ADD_TASK);
  add.subtitle = order.empty() ? nullptr : tr(STR_PLANNER_HOLD_TO_DELETE);
  items.push_back(add);

  if (completedCount > 0) {
    fui::ListItem clear;
    clear.label = deleteCompletedLabel;
    items.push_back(clear);
  }
}

void TodoListActivity::activate(const int index, const bool longPress) {
  if (index >= 0 && index < addRowIndex()) {
    const size_t itemIndex = order[static_cast<size_t>(index)];
    if (longPress) {
      confirmDeleteTask(itemIndex);
      return;
    }
    const bool done = !list.items[itemIndex].done;
    if (planner::setLocalTodoDone(list, itemIndex, done, PlannerStore::nowForEdits())) {
      // Keep the cursor on the same task even though it moves within the list.
      persist();
      for (size_t i = 0; i < order.size(); i++) {
        if (order[i] == itemIndex) {
          selectedIndex = static_cast<int>(i);
          break;
        }
      }
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
    if (planner::addLocalTodo(list, text->text, 0, PlannerStore::nowForEdits(), esp_random())) {
      persist();
      // Select the new task (open, undated: it sorts after dated open tasks).
      const size_t newIndex = list.items.size() - 1;
      for (size_t i = 0; i < order.size(); i++) {
        if (order[i] == newIndex) selectedIndex = static_cast<int>(i);
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
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(
      renderer, mappedInput, tr(STR_PLANNER_DELETE_COMPLETED_CONFIRM), deleteCompletedLabel);
  if (!confirmation) {
    LOG_ERR("PLN", "OOM: delete confirmation");
    return;
  }
  startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    if (planner::deleteCompletedLocalTodos(list, PlannerStore::nowForEdits()) > 0) persist();
  });
}
