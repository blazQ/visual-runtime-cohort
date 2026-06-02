#pragma once

#include "frame_config.h"
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

  bool init(VRTSurfaceDescriptor *surface);
  void resize(const VRTSurfaceMetrics *metrics);
  void set_frame_config(const FrameConfig &frame_config);
  void render_frame(float t);
  void shutdown();

private:
  std::unique_ptr<RendererBackend> backend_;
};
