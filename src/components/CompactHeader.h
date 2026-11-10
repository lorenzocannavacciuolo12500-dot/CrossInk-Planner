#pragma once

class GfxRenderer;
struct ThemeMetrics;

namespace CompactHeader {
int height(const ThemeMetrics& metrics);
int headerBottomY(const ThemeMetrics& metrics);
int contentTop(const ThemeMetrics& metrics);
void drawTitle(const GfxRenderer& renderer, const char* title, bool showDate = false);
// Same header with a short status text on the right instead of the date.
void drawTitleWithStatus(const GfxRenderer& renderer, const char* title, const char* rightText);
}  // namespace CompactHeader
