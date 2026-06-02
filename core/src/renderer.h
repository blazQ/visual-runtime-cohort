#pragma once

#include "visual_runtime/api.h"

#include <cstdint>
#include <memory>

struct RendererBackend;

struct Renderer {
  Renderer();
  ~Renderer();

  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;
  Renderer(Renderer &&) noexcept;
  Renderer &operator=(Renderer &&) noexcept;

  bool init(SurfaceDescriptor *surface);
  void resize(uint32_t width, uint32_t height);
  void render_frame(float t);
  void shutdown();
  void set_background_color(float r, float g, float b);
  void set_view(float pan_x, float pan_y, float zoom);
  void draw_rect(float top_left_x, float top_left_y, float width, float height, float r, float g, float b);

private:
  std::unique_ptr<RendererBackend> backend_;
};
