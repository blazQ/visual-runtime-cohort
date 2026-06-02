#include "renderer.h"
#include "visual_runtime/api.h"

#include <cstdio>

static const char *VERSION = "v1"; // change this to demonstrate hot reload

class VisualRuntime {
public:
  explicit VisualRuntime(SurfaceDescriptor *surface) {
    renderer_.init(surface);
    std::printf("[visual-runtime %s] init\n", VERSION);
    std::fflush(stdout);
  }

  ~VisualRuntime() { renderer_.shutdown(); }

  VisualRuntime(const VisualRuntime &) = delete;
  VisualRuntime &operator=(const VisualRuntime &) = delete;

  void resize(const VisualRuntimeSurfaceMetrics &metrics) {
    renderer_.resize(&metrics);
  }

  void change_view(const VisualRuntimeViewChange &change) {
    renderer_.change_view(&change);
  }

  void update(float dt) {
    frame_count_ += 1;
    elapsed_time_ += dt;
    renderer_.render_frame(elapsed_time_);
  }

private:
  Renderer renderer_;
  uint64_t frame_count_ = 0;
  float elapsed_time_ = 0.0f;
};

static VisualRuntime *runtime(VisualRuntimeState *state) {
  return state ? static_cast<VisualRuntime *>(state->runtime) : nullptr;
}

static void visual_runtime_shutdown_impl(VisualRuntimeState *state);

static void visual_runtime_init_impl(VisualRuntimeState *state,
                                     SurfaceDescriptor *surface) {
  if (!state) {
    return;
  }

  visual_runtime_shutdown_impl(state);
  state->runtime = new VisualRuntime(surface);
}

static void
visual_runtime_resize_impl(VisualRuntimeState *state,
                           const VisualRuntimeSurfaceMetrics *metrics) {
  if (auto *rt = runtime(state); rt && metrics) {
    rt->resize(*metrics);
  }
}

static void
visual_runtime_change_view_impl(VisualRuntimeState *state,
                                const VisualRuntimeViewChange *change) {
  if (auto *rt = runtime(state); rt && change) {
    rt->change_view(*change);
  }
}

static void visual_runtime_update_impl(VisualRuntimeState *state, float dt) {
  if (auto *rt = runtime(state)) {
    rt->update(dt);
  }
}

static void visual_runtime_shutdown_impl(VisualRuntimeState *state) {
  if (!state) {
    return;
  }

  delete runtime(state);
  state->runtime = nullptr;
}

extern "C" {

const VisualRuntimeAPI *visual_runtime_get_api() {
  static const VisualRuntimeAPI api{
      VISUAL_RUNTIME_API_VERSION,  sizeof(VisualRuntimeAPI),
      VISUAL_RUNTIME_BACKEND_NAME, visual_runtime_init_impl,
      visual_runtime_resize_impl,  visual_runtime_change_view_impl,
      visual_runtime_update_impl,  visual_runtime_shutdown_impl,
  };
  return &api;
}

} // extern "C"
