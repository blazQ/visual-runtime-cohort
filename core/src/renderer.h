#pragma once

#include "visual_runtime/api.h"

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
  void resize(const VisualRuntimeSurfaceMetrics *metrics);
  void change_view(const VisualRuntimeViewChange *change);
  void render_frame(float t);
  void shutdown();

private:
  std::unique_ptr<RendererBackend> backend_;
};
