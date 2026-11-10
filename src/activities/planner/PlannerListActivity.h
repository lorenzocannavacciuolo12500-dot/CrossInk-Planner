#pragma once

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>
#include <HalDisplay.h>

#include <atomic>
#include <vector>

#include "activities/Activity.h"
#include "components/PlannerPageDots.h"
#include "components/UIThemeTokens.h"
#include "network/PlannerBackgroundSync.h"
#include "util/ButtonNavigator.h"

/**
 * Shared list screen for the Planner (hub, to-do list, calendar agenda).
 * Owns FreeInkUI list rendering, button/touch navigation, paging and the
 * hold-Confirm gesture, so each Planner screen only describes its rows.
 *
 * The front buttons move the selection. The side buttons never do: they only
 * move between pages (Calendar <- Home -> To-Do List) through onSideButton().
 *
 * Pager screens (those with page dots: To-Do List and Calendar) also join the
 * saved Wi-Fi in the background so the companion app can sync while they are
 * open; reloadData() runs when it changed something.
 */
class PlannerListActivity : public Activity {
 public:
  PlannerListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  // Refresh mode of the first paint (e.g. a cleaner one right after wake-up).
  void setFirstRefresh(HalDisplay::RefreshMode mode) { firstRefresh = mode; }

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
  // Reload from PlannerStore after the companion app changed the data.
  virtual void reloadData() {}

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

  HalDisplay::RefreshMode firstRefresh = HalDisplay::FAST_REFRESH;
  bool firstPaintDone = false;
  PlannerBackgroundSync sync;
  bool syncEnabled = false;
  uint32_t seenExternalChanges = 0;
  char syncLabel[40] = {};  // header status; written under RenderLock
  void pollSync();
  void updateSyncLabel();
  freeink::ui::GfxRendererTarget uiTarget;  // must precede `app`: the app holds a reference to it
  UiApp app;
  std::atomic<bool> uiReady{false};
  std::vector<freeink::ui::ListItem> rows;  // reused between renders

  static void listScreen(UiApp::ScreenType& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildListScreen(UiApp::ScreenType& screen);
};
