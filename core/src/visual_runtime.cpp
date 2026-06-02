#include "renderer.h"
#include "visual_runtime/api.h"

#include <cstdio>

static const char *VERSION = "v1"; // change this to demonstrate hot reload

static Renderer g_renderer;

static void visual_runtime_init_impl(VisualRuntimeState *state,
                                     SurfaceDescriptor *surface) {
  state->frame_count = 0;
  state->elapsed_time = 0.0f;
  g_renderer.init(surface);
  std::printf("[visual-runtime %s] init\n", VERSION);
  std::fflush(stdout);
}

static void visual_runtime_resize_impl(
    VisualRuntimeState *, const VisualRuntimeSurfaceMetrics *metrics) {
  g_renderer.resize(metrics);
}

static void visual_runtime_change_view_impl(VisualRuntimeState *,
                                            const VisualRuntimeViewChange *change) {
  g_renderer.change_view(change);
}

static void visual_runtime_update_impl(VisualRuntimeState *state, float dt) {
  state->frame_count++;
  state->elapsed_time += dt;
  g_renderer.render_frame(state->elapsed_time);
}

static void visual_runtime_shutdown_impl(VisualRuntimeState *) {
  g_renderer.shutdown();
}

extern "C" {

const VisualRuntimeAPI *visual_runtime_get_api() {
  static const VisualRuntimeAPI api{
      VISUAL_RUNTIME_API_VERSION,   sizeof(VisualRuntimeAPI),
      VISUAL_RUNTIME_BACKEND_NAME,       visual_runtime_init_impl,
      visual_runtime_resize_impl,        visual_runtime_change_view_impl,
      visual_runtime_update_impl,        visual_runtime_shutdown_impl,
  };
  return &api;
}

} // extern "C"
