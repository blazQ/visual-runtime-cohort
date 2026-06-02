#include "renderer.h"
#include "visual_runtime/api.h"

#include <cstdio>

static const char *VERSION = "v6"; // change this to demonstrate hot reload

static Renderer g_renderer;

static void visual_runtime_init_impl(VisualRuntimeState *state,
                                     SurfaceDescriptor *surface) {
  state->frame_count = 0;
  state->elapsed_time = 0.0f;
  g_renderer.init(surface);
  std::printf("[visual-runtime %s] init\n", VERSION);
  std::fflush(stdout);
}

static void visual_runtime_resize_impl(VisualRuntimeState *, uint32_t width,
                                       uint32_t height) {
  g_renderer.resize(width, height);
}

static void visual_runtime_update_impl(VisualRuntimeState *state, float dt) {
  state->frame_count++;
  state->elapsed_time += dt;
  g_renderer.render_frame(state->elapsed_time);
}

static void visual_runtime_shutdown_impl(VisualRuntimeState *) {
  g_renderer.shutdown();
}

static void visual_runtime_set_background_color_impl(VisualRuntimeState *,
                                                     const Color *color) {
  if(color){
    g_renderer.set_background_color(color->r, color->g, color->b);
  }                                              
}

static void visual_runtime_set_view_impl(VisualRuntimeState *,
                                         const ViewState *view) {
  if (view) {
    g_renderer.set_view(view->pan_x, view->pan_y, view->zoom);
  }
}

static void visual_runtime_draw_rect_impl(VisualRuntimeState *,
                                         const Rectangle *rect) {
  if (rect) {
    g_renderer.draw_rect(rect->top_left_corner_x, rect->top_left_corner_y, rect->width, rect->height, rect->color.r, rect->color.g, rect->color.b);
  }
}

extern "C" {

const VisualRuntimeAPI *visual_runtime_get_api() {
  static const VisualRuntimeAPI api{
      VISUAL_RUNTIME_API_VERSION,   sizeof(VisualRuntimeAPI),
      VISUAL_RUNTIME_BACKEND_NAME,  visual_runtime_init_impl,
      visual_runtime_resize_impl,   visual_runtime_update_impl,
      visual_runtime_shutdown_impl,
      visual_runtime_set_background_color_impl,
      visual_runtime_set_view_impl,
      visual_runtime_draw_rect_impl,
  };
  return &api;
}

} // extern "C"
