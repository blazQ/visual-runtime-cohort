#pragma once

#include "frame_config.h"
#include "visual_runtime/api.h"
#include "visual_runtime/types.h"

#include <cstddef>
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <memory>

struct RendererBackend;

struct Renderer {
  struct Vertex {
    glm::vec2 position{};
    glm::vec4 color{};
  };

  // Zero is invalid so default construction can represent "no drawable".
  struct DrawableHandle {
    uint32_t value = 0;
  };

  struct DrawableDesc {
    const Vertex *vertices = nullptr;
    size_t vertex_count = 0;
  };

  struct DrawableState {
    glm::mat4 model_transform{1.0f};
    glm::vec4 color{1.0f};
    VRTShapeKind kind{VRTShapeKind::Rectangle};
  };

  Renderer();
  ~Renderer();

  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;
  Renderer(Renderer &&) noexcept;
  Renderer &operator=(Renderer &&) noexcept;

  bool init(VRTSurfaceDescriptor *surface);
  void resize(const VRTSurfaceMetrics *metrics);
  void set_frame_config(const FrameConfig &frame_config);
  DrawableHandle create_drawable(const DrawableDesc &desc);
  void update_drawable(DrawableHandle handle, const DrawableState &state);
  void destroy_drawable(DrawableHandle handle);
  bool begin_frame(float t);
  void draw(DrawableHandle handle);
  void end_frame();
  void render_frame(float t);
  void shutdown();

private:
  std::unique_ptr<RendererBackend> backend_;
};
