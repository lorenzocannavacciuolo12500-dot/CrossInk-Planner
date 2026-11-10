#pragma once

class GfxRenderer;

// "Agenda" sleep screen: today's date, today's events from the last calendar
// snapshot and the open to-do tasks. Draws into the frame buffer only; the
// caller refreshes the panel.
namespace PlannerAgendaSleep {
void draw(const GfxRenderer& renderer);
}  // namespace PlannerAgendaSleep
