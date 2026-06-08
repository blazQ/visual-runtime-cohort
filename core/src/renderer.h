#pragma once

#include "frame_config.h"
#include "visual_runtime/api.h"

#include <cstddef>
#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <memory>

struct RendererBackend;

namespace renderer {

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

} // namespace renderer

struct Renderer {
  Renderer();
  ~Renderer();

  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;
  Renderer(Renderer &&) noexcept;
  Renderer &operator=(Renderer &&) noexcept;

  bool init(VRTSurfaceDescriptor *surface);
  void resize(const VRTSurfaceMetrics *metrics);
  void set_frame_config(const FrameConfig &frame_config);
  renderer::DrawableHandle create_drawable(const renderer::DrawableDesc &desc);
  void destroy_drawable(renderer::DrawableHandle handle);
  bool begin_frame(float t);
  void draw(renderer::DrawableHandle handle);
  void end_frame();
  void render_frame(float t);
  void shutdown();

private:
  std::unique_ptr<RendererBackend> backend_;
};
