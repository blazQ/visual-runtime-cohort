#include "visual_runtime_module.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(__linux__)
#include <GLFW/glfw3native.h>
#if defined(VRT_GLFW_HAS_NATIVE_X11)
#include <X11/Xlib-xcb.h>
#include <xcb/xcb.h>
#endif
#endif

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace {

struct AppState {
  VisualRuntimeModule *runtime = nullptr;
  bool panning = false;
  double last_pan_x_screen = 0.0;
  double last_pan_y_screen = 0.0;
  VRTId next_shape_id = 1;
};

// Zoom sensitivity applied per scroll notch, in log-scale units.
constexpr double kZoomLogScalePerScrollStep = 0.15;
// Side length of a placed rectangle, in world units.
constexpr double kPlacedShapeSizeWorld = 0.2;

void glfw_error(int code, const char *description) {
  std::fprintf(stderr, "[glfw-minimal] GLFW error %d: %s\n", code,
               description ? description : "unknown");
}

bool env_set(const char *name) {
  const char *value = std::getenv(name);
  return value && value[0] != '\0';
}

void framebuffer_resized(GLFWwindow *window, int width, int height) {
  auto *state = static_cast<AppState *>(glfwGetWindowUserPointer(window));
  if (!state || !state->runtime) {
    return;
  }

  state->runtime->resize(visual_runtime::metrics_1x(
      static_cast<uint32_t>(width), static_cast<uint32_t>(height)));
  std::fprintf(stderr, "[glfw-minimal] resized to %dx%d\n", width, height);
}

// The runtime's screen space is the framebuffer-pixel space (see the metrics we
// hand it on attach/resize), but GLFW cursor positions are in window
// coordinates. On scaled displays those differ, so map the cursor into
// framebuffer pixels before handing points to the runtime.
void cursor_screen_point(GLFWwindow *window, double &x_screen,
                         double &y_screen) {
  double cursor_x = 0.0;
  double cursor_y = 0.0;
  glfwGetCursorPos(window, &cursor_x, &cursor_y);

  int window_width = 0;
  int window_height = 0;
  glfwGetWindowSize(window, &window_width, &window_height);

  int framebuffer_width = 0;
  int framebuffer_height = 0;
  glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);

  const double scale_x = window_width > 0
                             ? static_cast<double>(framebuffer_width) /
                                   static_cast<double>(window_width)
                             : 1.0;
  const double scale_y = window_height > 0
                             ? static_cast<double>(framebuffer_height) /
                                   static_cast<double>(window_height)
                             : 1.0;
  x_screen = cursor_x * scale_x;
  y_screen = cursor_y * scale_y;
}

// Left click places a rectangle at the cursor; right/middle drag pans.
void mouse_button(GLFWwindow *window, int button, int action, int /*mods*/) {
  auto *state = static_cast<AppState *>(glfwGetWindowUserPointer(window));
  if (!state || !state->runtime) {
    return;
  }

  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
    double x_screen = 0.0;
    double y_screen = 0.0;
    cursor_screen_point(window, x_screen, y_screen);

    VRTWorldPoint world{};
    if (!state->runtime->screenToWorld(VRTScreenPoint{x_screen, y_screen},
                                       world)) {
      return;
    }

    state->runtime->upsertShape(VRTShapeDescriptor{
        state->next_shape_id++,
        VRTShapeKind::Circle,
        /*reserved=*/0,
        VRTVec2{world.x_world, world.y_world},
        VRTVec2{kPlacedShapeSizeWorld, kPlacedShapeSizeWorld},
        VRTColorRGBA{0.20f, 0.60f, 0.95f, 1.0f},
    });
    return;
  }

  if (button == GLFW_MOUSE_BUTTON_RIGHT || button == GLFW_MOUSE_BUTTON_MIDDLE) {
    if (action == GLFW_PRESS) {
      state->panning = true;
      cursor_screen_point(window, state->last_pan_x_screen,
                          state->last_pan_y_screen);
    } else if (action == GLFW_RELEASE) {
      state->panning = false;
    }
  }
}

// While a pan drag is active, translate cursor motion into a view pan.
void cursor_moved(GLFWwindow *window, double /*x*/, double /*y*/) {
  auto *state = static_cast<AppState *>(glfwGetWindowUserPointer(window));
  if (!state || !state->runtime || !state->panning) {
    return;
  }

  double x_screen = 0.0;
  double y_screen = 0.0;
  cursor_screen_point(window, x_screen, y_screen);
  const double dx = x_screen - state->last_pan_x_screen;
  const double dy = y_screen - state->last_pan_y_screen;
  state->last_pan_x_screen = x_screen;
  state->last_pan_y_screen = y_screen;
  if (dx == 0.0 && dy == 0.0) {
    return;
  }

  VRTViewChange change{};
  change.flags = VRTViewChange_Pan;
  change.pan_x_screen = dx;
  change.pan_y_screen = dy;
  state->runtime->changeView(change);
}

// Scroll zooms about the cursor: scroll up (positive) zooms in.
void scrolled(GLFWwindow *window, double /*x_offset*/, double y_offset) {
  auto *state = static_cast<AppState *>(glfwGetWindowUserPointer(window));
  if (!state || !state->runtime || y_offset == 0.0) {
    return;
  }

  double x_screen = 0.0;
  double y_screen = 0.0;
  cursor_screen_point(window, x_screen, y_screen);

  VRTViewChange change{};
  change.flags = VRTViewChange_Zoom;
  change.zoom_delta_log_scale = y_offset * kZoomLogScalePerScrollStep;
  change.zoom_anchor_x_screen = x_screen;
  change.zoom_anchor_y_screen = y_screen;
  state->runtime->changeView(change);
}

#if defined(__linux__)
#if defined(VRT_GLFW_HAS_NATIVE_WAYLAND)
bool attach_wayland_surface(GLFWwindow *window, VisualRuntimeModule &runtime) {
  wl_display *display = glfwGetWaylandDisplay();
  wl_surface *wayland_surface = glfwGetWaylandWindow(window);
  if (!display || !wayland_surface) {
    std::fprintf(stderr,
                 "[glfw-minimal] failed to get Wayland surface handles\n");
    return false;
  }

  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window, &width, &height);

  VRTSurfaceDescriptor surface{
      VRTSurfaceKind::LinuxWaylandSurface,
      display,
      reinterpret_cast<uintptr_t>(wayland_surface),
      visual_runtime::metrics_1x(static_cast<uint32_t>(width),
                                 static_cast<uint32_t>(height)),
  };
  std::fprintf(stderr,
               "[glfw-minimal] attaching LinuxWaylandSurface surface (%ux%u)\n",
               surface.metrics.pixel_width, surface.metrics.pixel_height);
  runtime.attachSurface(surface);
  return true;
}
#endif

#if defined(VRT_GLFW_HAS_NATIVE_X11)
bool attach_xcb_surface(GLFWwindow *window, VisualRuntimeModule &runtime) {
  Display *display = glfwGetX11Display();
  if (!display) {
    std::fprintf(stderr, "[glfw-minimal] failed to get XCB surface handles\n");
    return false;
  }

  Window x11_window = glfwGetX11Window(window);
  xcb_connection_t *connection = XGetXCBConnection(display);
  if (x11_window == 0 || !connection || xcb_connection_has_error(connection)) {
    std::fprintf(stderr, "[glfw-minimal] failed to get XCB surface handles\n");
    return false;
  }

  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window, &width, &height);

  VRTSurfaceDescriptor surface{
      VRTSurfaceKind::LinuxXcbWindow,
      connection,
      static_cast<uintptr_t>(x11_window),
      visual_runtime::metrics_1x(static_cast<uint32_t>(width),
                                 static_cast<uint32_t>(height)),
  };
  std::fprintf(stderr,
               "[glfw-minimal] attaching LinuxXcbWindow surface (%ux%u)\n",
               surface.metrics.pixel_width, surface.metrics.pixel_height);
  runtime.attachSurface(surface);
  return true;
}
#endif

bool attach_surface(GLFWwindow *window, VisualRuntimeModule &runtime) {
#if defined(VRT_GLFW_HAS_PLATFORM_API)
  const int platform = glfwGetPlatform();
#if defined(VRT_GLFW_HAS_NATIVE_WAYLAND)
  if (platform == GLFW_PLATFORM_WAYLAND) {
    return attach_wayland_surface(window, runtime);
  }
#endif

#if defined(VRT_GLFW_HAS_NATIVE_X11)
  if (platform == GLFW_PLATFORM_X11) {
    return attach_xcb_surface(window, runtime);
  }
#endif

  std::fprintf(stderr, "[glfw-minimal] unsupported GLFW platform: %d\n",
               platform);
  return false;
#else
  const bool has_wayland_display = env_set("WAYLAND_DISPLAY");
  const bool has_x11_display = env_set("DISPLAY");
#if defined(VRT_GLFW_HAS_NATIVE_WAYLAND)
  if (has_wayland_display && attach_wayland_surface(window, runtime)) {
    return true;
  }
#endif

#if defined(VRT_GLFW_HAS_NATIVE_X11)
  if (has_x11_display && attach_xcb_surface(window, runtime)) {
    return true;
  }
#endif

#if defined(VRT_GLFW_HAS_NATIVE_WAYLAND)
  if (!has_wayland_display && attach_wayland_surface(window, runtime)) {
    return true;
  }
#endif

#if defined(VRT_GLFW_HAS_NATIVE_X11)
  if (!has_x11_display && attach_xcb_surface(window, runtime)) {
    return true;
  }
#endif

  std::fprintf(
      stderr,
      "[glfw-minimal] could not attach Wayland or XCB surface handles\n");
  return false;
#endif
}
#else
bool attach_surface(GLFWwindow *window, VisualRuntimeModule &runtime) {
  (void)window;
  (void)runtime;
  std::fprintf(stderr,
               "[glfw-minimal] native surface handles are not implemented for "
               "this platform\n");
  return false;
}
#endif

} // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  glfwSetErrorCallback(glfw_error);

  if (!glfwInit()) {
    return 1;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow *window =
      glfwCreateWindow(1280, 720, "Visual Runtime", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }

  VisualRuntimeModule runtime =
      VisualRuntimeModule::open(VISUAL_RUNTIME_LIB_PATH);
  if (!runtime) {
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }

  AppState state{&runtime};
  glfwSetWindowUserPointer(window, &state);
  glfwSetFramebufferSizeCallback(window, framebuffer_resized);
  glfwSetMouseButtonCallback(window, mouse_button);
  glfwSetCursorPosCallback(window, cursor_moved);
  glfwSetScrollCallback(window, scrolled);

  if (!attach_surface(window, runtime)) {
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }

  // Start with an empty canvas on a dark background; the scene is built by
  // interaction: left-click to place a rectangle, scroll to zoom, right/middle
  // drag to pan.
  runtime.setSceneSettings(
      VRTSceneSettings{VRTColorRGBA{0.09f, 0.10f, 0.12f, 1.0f}});

  using clock = std::chrono::steady_clock;
  auto last = clock::now();

  while (!glfwWindowShouldClose(window)) {
    if (runtime.reloadIfChanged()) {
      std::printf("[host] reloaded\n");
    }

    auto now = clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;

    runtime.tick(dt);
    glfwPollEvents();
  }

  std::printf("[glfw-minimal] exiting\n");

  runtime.shutdown();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
