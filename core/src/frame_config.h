#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

// Runtime-owned frame intent consumed by renderer backends.
//
// This stays inside the visual runtime rather than the public ABI: hosts send
// product-shaped view changes, and the runtime turns retained view state into
// the transform a backend should upload for the next frame.
struct FrameConfig {
  glm::mat4 view_proj_transform{1.0f};
  glm::vec4 clear_color{0.0f, 0.0f, 0.0f, 1.0f};
};
