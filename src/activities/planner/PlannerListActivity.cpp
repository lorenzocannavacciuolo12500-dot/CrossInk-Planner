#include "PlannerListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <array>
#include <cstdio>

#include "MappedInputManager.h"
#include "PlannerStore.h"
#include "SilentRestart.h"
#include "components/CompactHeader.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;
}

PlannerListActivity::PlannerListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity(name, renderer, mappedInput),
      uiTarget(makeUiTarget(renderer)),
      app(uiTarget, uiTarget.deviceContext()) {}

const char* PlannerListActivity::confirmHint() const { return tr(STR_SELECT); }

void PlannerListActivity::onEnter() {
  Activity::onEnter();
  uiReady = false;
  firstPaintDone = false;
  holdHandled = false;
  sideNextSeen = false;
  sidePreviousSeen = false;
  visibleRows = 1;
  applySharedUiTheme(app, uiTarget);
  app.on(ACTION_ROW, &PlannerListActivity::onRowEvent, this);
  app.setScreen(&PlannerListActivity::listScreen, this);
  clampSelection();

  PlannerPageDots::Page page;
  syncEnabled = pageDot(page) && page != PlannerPageDots::Page::Home;
  if (syncEnabled) {
    PlannerStore::setActivePage(page == PlannerPageDots::Page::Todo ? PlannerStore::ResumePage::Todo
                                                                    : PlannerStore::ResumePage::Calendar);
    seenExternalChanges = PlannerStore::externalChangeCount();
    sync.start(renderer);
    activeScreen = this;
  }
  sleepFrame = false;
  updateSyncLabel();
  requestUpdate();
}

PlannerListActivity* PlannerListActivity::activeScreen = nullptr;
bool PlannerListActivity::sleepFrameReady = false;

void PlannerListActivity::prepareSleepFrame() {
  if (!activeScreen) return;
  RenderLock lock;
  activeScreen->sleepFrame = true;  // later renders stay clean and off-panel too
  activeScreen->drawFrame();
  sleepFrameReady = true;
}

bool PlannerListActivity::takeSleepFrame() {
  const bool ready = sleepFrameReady;
  sleepFrameReady = false;
  return ready;
}

void PlannerListActivity::onExit() {
  if (activeScreen == this) activeScreen = nullptr;
  if (syncEnabled) {
    PlannerStore::setActivePage(PlannerStore::ResumePage::None);
    syncEnabled = false;
    // Same policy as File Transfer: reboot (straight to Home, no splash) when
    // the Wi-Fi session left the heap fragmented. A no-op while going to sleep.
    if (sync.stop()) silentRestart();
  }
  Activity::onExit();
}

void PlannerListActivity::updateSyncLabel() {
  char next[sizeof(syncLabel)] = {};
  if (syncEnabled) {
    switch (sync.state()) {
      case PlannerBackgroundSync::State::Off:
        break;
      case PlannerBackgroundSync::State::Waiting:
      case PlannerBackgroundSync::State::Connecting:
        std::snprintf(next, sizeof(next), "%s", tr(STR_PLANNER_SYNC_CONNECTING));
        break;
      case PlannerBackgroundSync::State::Online:
        std::snprintf(next, sizeof(next), tr(STR_PLANNER_SYNC_ONLINE_FORMAT), sync.ip());
        break;
      case PlannerBackgroundSync::State::NoSavedNetwork:
        std::snprintf(next, sizeof(next), "%s", tr(STR_PLANNER_SYNC_NO_NETWORK));
        break;
      case PlannerBackgroundSync::State::Unavailable:
        std::snprintf(next, sizeof(next), "%s", tr(STR_PLANNER_SYNC_OFFLINE));
        break;
    }
  }
  RenderLock lock;
  std::snprintf(syncLabel, sizeof(syncLabel), "%s", next);
}

void PlannerListActivity::pollSync() {
  if (!syncEnabled) return;
  if (sync.poll()) {
    updateSyncLabel();
    requestUpdate();
  }
  const uint32_t changes = PlannerStore::externalChangeCount();
  if (changes != seenExternalChanges) {
    seenExternalChanges = changes;
    {
      // The render task reads the same lists: swap the data in under its lock.
      RenderLock lock;
      reloadData();
      clampSelection();
    }
    requestUpdate();
  }
}

void PlannerListActivity::clampSelection() {
  const int count = itemCount();
  if (selectedIndex >= count) selectedIndex = count > 0 ? count - 1 : 0;
  if (selectedIndex < 0) selectedIndex = 0;
  topIndex = followListSelection(selectedIndex, topIndex, visibleRows, count);
}

bool PlannerListActivity::handleSideButtons() {
  // Act on release, and only for presses that started on this screen, so the
  // release of the press that opened it (or a chord) is not acted on again.
  using Button = MappedInputManager::Button;
  if (mappedInput.wasPressed(Button::Down)) sideNextSeen = true;
  if (mappedInput.wasPressed(Button::Up)) sidePreviousSeen = true;
  if (mappedInput.wasReleased(Button::Down)) {
    const bool seen = sideNextSeen;
    sideNextSeen = false;
    if (seen && !mappedInput.isPressed(Button::Up)) onSideButton(true);
    return true;
  }
  if (mappedInput.wasReleased(Button::Up)) {
    const bool seen = sidePreviousSeen;
    sidePreviousSeen = false;
    if (seen && !mappedInput.isPressed(Button::Down)) onSideButton(false);
    return true;
  }
  return mappedInput.isPressed(Button::Down) || mappedInput.isPressed(Button::Up);
}

void PlannerListActivity::loop() {
  pollSync();

  if (TouchHeaderBackButton::wasTapped(mappedInput, TouchHeaderBackButton::compactHeaderRect(renderer)) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (handleSideButtons()) return;

  const int count = itemCount();

  if (supportsLongPress() && count > 0 && !holdHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= kHoldMs) {
    holdHandled = true;
    activate(selectedIndex, true);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (holdHandled) {
      holdHandled = false;
      return;
    }
    if (count > 0) activate(selectedIndex, false);
    return;
  }

  if (count == 0) return;

  if (uiReady) {
    const fui::InputSnapshot snap = touchSnapshotFrom(mappedInput);
    if (snap.touchPressed || snap.touchReleased) {
      const auto event = app.route(snap);
      if (app.invalidated()) requestUpdate();
      if (event) return;
    }
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    const int next = scrollListBy(topIndex, swipe == MappedInputManager::SwipeDir::Up ? visibleRows : -visibleRows,
                                  visibleRows, count);
    if (next != topIndex) {
      topIndex = next;
      requestUpdate();
    }
    return;
  }

  const auto moveSelection = [this, count](const int next) {
    selectedIndex = next;
    topIndex = followListSelection(selectedIndex, topIndex, visibleRows, count);
    requestUpdate();
  };
  // Front buttons only: the side buttons are reserved for page changes.
  constexpr std::array<MappedInputManager::Button, 2> kNext{MappedInputManager::Button::Right,
                                                            MappedInputManager::Button::Right};
  constexpr std::array<MappedInputManager::Button, 2> kPrevious{MappedInputManager::Button::Left,
                                                                MappedInputManager::Button::Left};
  buttonNavigator.onRelease(
      kNext, [this, count, &moveSelection] { moveSelection(ButtonNavigator::nextIndex(selectedIndex, count)); });
  buttonNavigator.onRelease(kPrevious, [this, count, &moveSelection] {
    moveSelection(ButtonNavigator::previousIndex(selectedIndex, count));
  });
  buttonNavigator.onContinuous(kNext, [this, count, &moveSelection] {
    moveSelection(ButtonNavigator::nextPageIndex(selectedIndex, count, visibleRows));
  });
  buttonNavigator.onContinuous(kPrevious, [this, count, &moveSelection] {
    moveSelection(ButtonNavigator::previousPageIndex(selectedIndex, count, visibleRows));
  });
}

void PlannerListActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<PlannerListActivity*>(user);
  if (event.value < 0 || event.value >= self->itemCount()) return;
  self->selectedIndex = event.value;
  self->app.clearTapFlash();
  self->activate(self->selectedIndex, event.longPress && self->supportsLongPress());
  self->requestUpdate();
}

void PlannerListActivity::listScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<PlannerListActivity*>(user)->buildListScreen(screen);
}

void PlannerListActivity::buildListScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(CompactHeader::contentTop(metrics)), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  const int count = itemCount();
  if (count == 0) {
    const char* text = emptyText();
    if (text) screen.centeredText(text, screen.theme().bodyText);
    return;
  }

  rows.clear();
  rows.reserve(static_cast<size_t>(count));
  fillItems(rows);
  for (size_t i = 0; i < rows.size(); i++) rows[i].actionValue = static_cast<int16_t>(i);
  if (sleepFrame) {
    size_t kept = 0;
    for (size_t i = 0; i < rows.size(); i++) {
      if (!isActionRow(static_cast<int>(i))) rows[kept++] = rows[i];
    }
    rows.resize(kept);
    if (rows.empty()) {
      const char* text = emptyText();
      screen.centeredText(text ? text : tr(STR_PLANNER_ALL_DONE), screen.theme().bodyText);
      return;
    }
  }

  fui::ListProps props;
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.selectedIndex = static_cast<int16_t>(sleepFrame ? -1 : selectedIndex);
  props.action = ACTION_ROW;
  props.inputMask =
      static_cast<uint16_t>(supportsLongPress() ? (fui::InputTouch | fui::InputLongPress) : fui::InputTouch);
  const auto fitted = configureUiList(props, screen.theme(), screen.body(), rowType());
  visibleRows = fitted > 0 ? fitted : 1;
  topIndex = scrollListBy(topIndex, 0, visibleRows, count);
  props.topIndex = static_cast<uint16_t>(topIndex);
  screen.list(props);
}

void PlannerListActivity::render(RenderLock&&) {
  drawFrame();
  if (sleepFrame) return;  // the sleep screen refreshes the panel
  renderer.displayBuffer(firstPaintDone ? HalDisplay::FAST_REFRESH : firstRefresh);
  firstPaintDone = true;
}

void PlannerListActivity::drawFrame() {
  PlannerPageDots::Page page;
  const bool showDots = pageDot(page) && !sleepFrame;
  // The page dots own the status-bar centre while this screen renders; the
  // sleep frame keeps only the clock / date of the status bar.
  const PlannerPageDots::CenterReservation dotsCenter(showDots);
  const PlannerPageDots::ClockOnlyStatusBar clockOnly(sleepFrame);
  renderer.clearScreen();
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::drawCompact(renderer, screenTitle());
  } else {
    // While syncing in the background the status replaces the date.
    if (syncLabel[0] != '\0' && !sleepFrame) {
      CompactHeader::drawTitleWithStatus(renderer, screenTitle(), syncLabel);
    } else {
      CompactHeader::drawTitle(renderer, screenTitle(), true);
    }
  }
  if (showDots) PlannerPageDots::draw(renderer, page);
  uiReady = false;
  app.render();
  uiReady = true;
  if (sleepFrame) return;

  const auto labels =
      mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), confirmHint(), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
