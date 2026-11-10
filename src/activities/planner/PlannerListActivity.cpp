#include "PlannerListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/CompactHeader.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;
}

PlannerListActivity::PlannerListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity(name, renderer, mappedInput), uiTarget(makeUiTarget(renderer)), app(uiTarget, uiTarget.deviceContext()) {}

const char* PlannerListActivity::confirmHint() const { return tr(STR_SELECT); }

void PlannerListActivity::onEnter() {
  Activity::onEnter();
  uiReady = false;
  holdHandled = false;
  visibleRows = 1;
  applySharedUiTheme(app, uiTarget);
  app.on(ACTION_ROW, &PlannerListActivity::onRowEvent, this);
  app.setScreen(&PlannerListActivity::listScreen, this);
  clampSelection();
  requestUpdate();
}

void PlannerListActivity::clampSelection() {
  const int count = itemCount();
  if (selectedIndex >= count) selectedIndex = count > 0 ? count - 1 : 0;
  if (selectedIndex < 0) selectedIndex = 0;
  topIndex = followListSelection(selectedIndex, topIndex, visibleRows, count);
}

void PlannerListActivity::loop() {
  if (TouchHeaderBackButton::wasTapped(mappedInput, TouchHeaderBackButton::compactHeaderRect(renderer)) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  const int count = itemCount();

  if (supportsLongPress() && count > 0 && !holdHandled &&
      mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() >= kHoldMs) {
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
    const int next =
        scrollListBy(topIndex, swipe == MappedInputManager::SwipeDir::Up ? visibleRows : -visibleRows, visibleRows, count);
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
  buttonNavigator.onNextRelease(
      [this, count, &moveSelection] { moveSelection(ButtonNavigator::nextIndex(selectedIndex, count)); });
  buttonNavigator.onPreviousRelease(
      [this, count, &moveSelection] { moveSelection(ButtonNavigator::previousIndex(selectedIndex, count)); });
  buttonNavigator.onNextContinuous([this, count, &moveSelection] {
    moveSelection(ButtonNavigator::nextPageIndex(selectedIndex, count, visibleRows));
  });
  buttonNavigator.onPreviousContinuous([this, count, &moveSelection] {
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

  fui::ListProps props;
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.selectedIndex = static_cast<int16_t>(selectedIndex);
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
  renderer.clearScreen();
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::drawCompact(renderer, screenTitle());
  } else {
    CompactHeader::drawTitle(renderer, screenTitle(), true);
  }
  uiReady = false;
  app.render();
  uiReady = true;

  const auto labels =
      mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), confirmHint(), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
