#pragma once

#include <functional>

namespace yapper
{
// The GLFW + OpenGL + ImGui window. Calls buildFrame every frame to draw the actual UI.
class UI
{
public:
  int Run(const std::function<void()> &buildFrame);
};
} // namespace yapper
