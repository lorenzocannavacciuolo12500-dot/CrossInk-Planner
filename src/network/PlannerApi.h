#pragma once

class WebServer;

/**
 * HTTP endpoints used by the Planner companion app (to-do sync, calendar
 * snapshot, clock). Registered on CrossPointWebServer while File Transfer is
 * active. Protocol: docs/planner-sync.md.
 */
namespace PlannerApi {
void registerRoutes(WebServer& server);
}
