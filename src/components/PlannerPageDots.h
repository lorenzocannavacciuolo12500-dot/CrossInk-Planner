#pragma once

#include <cstdint>

class GfxRenderer;

/**
 * Three page dots at the top of the screen for the side-button pages:
 * Calendar <- Home -> To-Do List. The dot of the current page is larger.
 */
namespace PlannerPageDots {
enum class Page : uint8_t { Calendar = 0, Home = 1, Todo = 2 };

void draw(const GfxRenderer& renderer, Page current);
}  // namespace PlannerPageDots
