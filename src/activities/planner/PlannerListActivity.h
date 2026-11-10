#pragma once

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <atomic>
#include <vector>

#include "activities/Activity.h"
#include "components/PlannerPageDots.h"
#include "components/UIThemeTokens.h"
#include "util/ButtonNavigator.h"

/**
 * Shared list screen for the Planner (hub, to-do list, calendar agenda).
 * Owns FreeInkUI list rendering, button/touch navigation, paging and the
 * hold-Confirm gesture, so each Planner screen only describes its rows.
 *
 * The front buttons move the selection. The side buttons never do: they only
 * move between pages (Calendar <- Home -> To-Do List) through onSideButton().
 */
class PlannerListActivity : public Activity {
 public:
  PlannerListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 protected:
  using UiApp = freeink::ui::FreeInkApp<20, 4>;

  virtual int itemCount() const = 0;
  // Fill one ListItem per row. Strings must stay valid until render() returns.
  virtual void fillItems(std::vector<freeink::ui::ListItem>& items) = 0;
  // Confirm or tap on a row; longPress = held Confirm / long touch.
  virtual void activate(int index, bool longPress) = 0;
  virtual const char* screenTitle() const = 0;
  virtual const char* confirmHint() const;
  virtual const char* emptyText() const { return nullptr; }
  virtual UiListRowType rowType() const { return UiListRowType::WithSubtitle; }
  virtual bool supportsLongPress() const { return false; }
  // Side button released (next = Next Page side button, right edge on the X3).
  virtual void onSideButton(bool /*next*/) {}
  // Page dots shown in the status row, or false for screens outside the pager.
  virtual bool pageDot(PlannerPageDots::Page& /*page*/) const { return false; }

  // Keeps the selection inside the list after rows were added or removed.
  void clampSelection();

  int selectedIndex = 0;
  int topIndex = 0;
  int visibleRows = 1;

 private:
  static constexpr unsigned long kHoldMs = 800;

  ButtonNavigator buttonNavigator;
  bool holdHandled = false;
  bool sideNextSeen = false;
  bool sidePreviousSeen = false;
  bool handleSideButtons();
  freeink::ui::GfxRendererTarget uiTarget;  // must precede `app`: the app holds a reference to it
  UiApp app;
  std::atomic<bool> uiReady{false};
  std::vector<freeink::ui::ListItem> rows;  // reused between renders

  static void listScreen(UiApp::ScreenType& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildListScreen(UiApp::ScreenType& screen);
};
