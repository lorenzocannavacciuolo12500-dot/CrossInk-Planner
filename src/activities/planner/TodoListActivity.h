#pragma once

#include <PlannerTypes.h>

#include <cstdint>
#include <string>
#include <vector>

#include "PlannerListActivity.h"

/**
 * Planner → To-do. Confirm checks a task on or off; holding Confirm deletes it.
 * The last rows add a task (on-screen keyboard) and clear completed tasks.
 * Every change is written to todos.json right away and synced with the
 * companion app the next time File Transfer is open.
 */
class TodoListActivity final : public PlannerListActivity {
 public:
  TodoListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 protected:
  int itemCount() const override;
  void fillItems(std::vector<freeink::ui::ListItem>& items) override;
  void activate(int index, bool longPress) override;
  const char* screenTitle() const override;
  const char* confirmHint() const override;
  bool supportsLongPress() const override { return true; }

 private:
  planner::TodoList list;
  std::vector<uint16_t> order;  // visible task indices into list.items, display order
  size_t completedCount = 0;
  int32_t todayYmd = 0;
  // Row text buffers; rebuilt on every render, reserved so pointers stay valid.
  std::vector<std::string> subtitles;
  char deleteCompletedLabel[48] = {};

  void reload();
  void refreshDerived();
  void persist();
  int addRowIndex() const { return static_cast<int>(order.size()); }
  int deleteCompletedRowIndex() const { return completedCount > 0 ? addRowIndex() + 1 : -1; }
  void openAddTask();
  void confirmDeleteTask(size_t itemIndex);
  void confirmDeleteCompleted();
};
