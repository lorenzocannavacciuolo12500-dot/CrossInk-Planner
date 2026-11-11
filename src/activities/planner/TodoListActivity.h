#pragma once

#include <PlannerTypes.h>

#include <cstdint>
#include <string>
#include <vector>

#include "PlannerListActivity.h"

/**
 * To-Do List. Tasks are grouped like the companion app: the ones in no list
 * first, then one block per list with its name as a row. Confirm on a list name
 * folds that list away (kept until the reader restarts).
 *
 * Confirm checks a task on or off; holding Confirm deletes it.
 * The Previous Page side button (left edge on the X3) goes back to Home.
 * The last rows add a task (on-screen keyboard) and clear completed tasks.
 * Every change is written to todos.json right away; the companion app syncs it
 * while this page (or Calendar, or File Transfer) is open.
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
  void onSideButton(bool next) override;
  void reloadData() override { reload(); }
  bool isActionRow(const int index) const override { return index >= addRowIndex(); }
  bool pageDot(PlannerPageDots::Page& page) const override {
    page = PlannerPageDots::Page::Todo;
    return true;
  }

 private:
  // One visible row: either a list name or a task.
  struct Row {
    uint16_t item = 0;  // index into list.items; unused on a heading
    uint8_t listIndex = planner::kNoList;
    bool heading = false;
  };

  planner::TodoList list;
  std::vector<Row> rows;  // display order, collapsed lists left out
  size_t completedCount = 0;
  int32_t todayYmd = 0;
  // Row text buffers; rebuilt on every render, reserved so pointers stay valid.
  std::vector<std::string> subtitles;
  std::vector<std::string> headings;  // list names, one per heading row
  char deleteCompletedLabel[48] = {};

  void reload();
  void refreshDerived();
  void persist();
  int addRowIndex() const { return static_cast<int>(rows.size()); }
  bool isCollapsed(uint8_t listIndex) const;
  void toggleCollapsed(uint8_t listIndex);
  int deleteCompletedRowIndex() const { return completedCount > 0 ? addRowIndex() + 1 : -1; }
  void openAddTask();
  void confirmDeleteTask(size_t itemIndex);
  void confirmDeleteCompleted();
};
