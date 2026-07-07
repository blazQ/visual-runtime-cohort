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
#include <vector>

namespace {

// Which item a left-click acts on, chosen with the number/letter keys.
enum class Tool { PlaceRectangle, PlaceCircle, PlaceRoundedBox, PlaceImage, Select };

struct AppState {
  VisualRuntimeModule *runtime = nullptr;
  Tool tool = Tool::PlaceCircle;
  VRTId next_item_id = 1; // unique across shapes and images
  VRTId selected_id = kInvalidId;
  bool dragging = false;
  bool panning = false;
  double last_pan_x_screen = 0.0;
  double last_pan_y_screen = 0.0;
};

constexpr double kZoomPerScrollStep = 0.15;
constexpr double kPlacedSize = 0.2;
constexpr VRTColorRGBA kPlacedColor{0.20f, 0.60f, 0.95f, 1.0f};
constexpr uint32_t kImageSize = 64;
constexpr uint32_t kImageCell = 8;

void glfw_error(int code, const char *description) {
  std::fprintf(stderr, "[glfw-minimal] GLFW error %d: %s\n", code,
               description ? description : "unknown");
}

bool env_set(const char *name) {
  const char *value = std::getenv(name);
  return value && value[0] != '\0';
}

// Pixel size drives the swapchain; logical screen size drives interaction.
VRTSurfaceMetrics surface_metrics(GLFWwindow *window) {
  int pixel_width = 0;
  int pixel_height = 0;
  glfwGetFramebufferSize(window, &pixel_width, &pixel_height);

  int screen_width = 0;
  int screen_height = 0;
  glfwGetWindowSize(window, &screen_width, &screen_height);

  return VRTSurfaceMetrics{
      static_cast<uint32_t>(pixel_width),
      static_cast<uint32_t>(pixel_height),
      static_cast<double>(screen_width),
      static_cast<double>(screen_height),
  };
}

VRTScreenPoint cursor_point(GLFWwindow *window) {
  double x = 0.0;
  double y = 0.0;
  glfwGetCursorPos(window, &x, &y);
  return VRTScreenPoint{x, y};
}

std::vector<uint8_t> make_checkerboard(uint32_t size, uint32_t cell) {
  std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
  for (uint32_t y = 0; y < size; ++y) {
    for (uint32_t x = 0; x < size; ++x) {
      const bool lit = ((x / cell) + (y / cell)) % 2 == 0;
      uint8_t *p = &pixels[(static_cast<size_t>(y) * size + x) * 4];
      p[0] = lit ? 235 : 40;
      p[1] = lit ? 110 : 40;
      p[2] = lit ? 45 : 40;
      p[3] = 255;
    }
  }
  return pixels;
}

VRTShapeKind shape_kind_for(Tool tool) {
  switch (tool) {
  case Tool::PlaceCircle:     return VRTShapeKind::Circle;
  case Tool::PlaceRoundedBox: return VRTShapeKind::RoundedBox;
  case Tool::PlaceRectangle:
  default:                    return VRTShapeKind::Rectangle;
  }
}


// Drop a new shape or image at the cursor for the active place tool.
void place_item(AppState &state, VRTScreenPoint at) {
  VRTWorldPoint world{};
  if (!state.runtime->screenToWorld(at, world)) {
    return;
  }
  const VRTVec2 center{world.x_world, world.y_world};
  const VRTVec2 size{kPlacedSize, kPlacedSize};

  if (state.tool == Tool::PlaceImage) {
    const std::vector<uint8_t> pixels = make_checkerboard(kImageSize, kImageCell);
    state.runtime->upsertImage(VRTImageDescriptor{
        state.next_item_id++, 0, center, size,
        VRTPixelBuffer{pixels.data(), kImageSize, kImageSize,
                       VRTPixelFormat::RGBA8Srgb}});
    return;
  }

  const VRTShapeKind kind = shape_kind_for(state.tool);

  state.runtime->upsertShape(VRTShapeDescriptor{
      state.next_item_id++, kind, 0, center, size, kPlacedColor});
}

// Select whatever is under the cursor (a miss clears selection) and grab it.
void select_at(AppState &state, VRTScreenPoint at) {
  state.selected_id = state.runtime->hit_test(at);
  state.runtime->set_selection(state.selected_id);
  state.dragging = state.selected_id != kInvalidId;
}

void framebuffer_resized(GLFWwindow *window, int, int) {
  auto *state = static_cast<AppState *>(glfwGetWindowUserPointer(window));
  if (state && state->runtime) {
    state->runtime->resize(surface_metrics(window));
  }
}

// Left button places/selects and drives item drags; right/middle drives panning.
void mouse_button(GLFWwindow *window, int button, int action, int /*mods*/) {
  auto *state = static_cast<AppState *>(glfwGetWindowUserPointer(window));
  if (!state || !state->runtime) {
    return;
  }

  if (button == GLFW_MOUSE_BUTTON_LEFT) {
    if (action == GLFW_PRESS) {
      const VRTScreenPoint at = cursor_point(window);
      if (state->tool == Tool::Select) {
        select_at(*state, at);
      } else {
        place_item(*state, at);
      }
    } else if (action == GLFW_RELEASE) {
      state->dragging = false;
    }
    return;
  }

  if (button == GLFW_MOUSE_BUTTON_RIGHT || button == GLFW_MOUSE_BUTTON_MIDDLE) {
    if (action == GLFW_PRESS) {
      state->panning = true;
      glfwGetCursorPos(window, &state->last_pan_x_screen,
                       &state->last_pan_y_screen);
    } else if (action == GLFW_RELEASE) {
      state->panning = false;
    }
  }
}

// A drag moves the selected item; a pan drag moves the view.
void cursor_moved(GLFWwindow *window, double x_screen, double y_screen) {
  auto *state = static_cast<AppState *>(glfwGetWindowUserPointer(window));
  if (!state || !state->runtime) {
    return;
  }

  if (state->dragging) {
    VRTWorldPoint world{};
    if (state->runtime->screenToWorld(VRTScreenPoint{x_screen, y_screen}, world)) {
      state->runtime->move_item(state->selected_id, world);
    }
    return;
  }

  if (!state->panning) {
    return;
  }

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
  const VRTScreenPoint at = cursor_point(window);

  VRTViewChange change{};
  change.flags = VRTViewChange_Zoom;
  change.zoom_delta_log_scale = y_offset * kZoomPerScrollStep;
  change.zoom_anchor_x_screen = at.x_screen;
  change.zoom_anchor_y_screen = at.y_screen;
  state->runtime->changeView(change);
}

// 1/2/3 pick a place tool, S the select tool.
void key_pressed(GLFWwindow *window, int key, int /*scancode*/, int action,
                 int /*mods*/) {
  if (action != GLFW_PRESS) {
    return;
  }
  auto *state = static_cast<AppState *>(glfwGetWindowUserPointer(window));
  if (!state) {
    return;
  }
  switch (key) {
  case GLFW_KEY_1: state->tool = Tool::PlaceRectangle; break;
  case GLFW_KEY_2: state->tool = Tool::PlaceCircle;    break;
  case GLFW_KEY_3: state->tool = Tool::PlaceRoundedBox; break;
  case GLFW_KEY_4: state->tool = Tool::PlaceImage;     break;
  case GLFW_KEY_S: state->tool = Tool::Select;         break;
  }
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

  VRTSurfaceDescriptor surface{
      VRTSurfaceKind::LinuxWaylandSurface,
      display,
      reinterpret_cast<uintptr_t>(wayland_surface),
      surface_metrics(window),
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

  VRTSurfaceDescriptor surface{
      VRTSurfaceKind::LinuxXcbWindow,
      connection,
      static_cast<uintptr_t>(x11_window),
      surface_metrics(window),
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
  glfwSetKeyCallback(window, key_pressed);

  if (!attach_surface(window, runtime)) {
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }

  runtime.setSceneSettings(
      VRTSceneSettings{VRTColorRGBA{0.09f, 0.10f, 0.12f, 1.0f}});

  using clock = std::chrono::steady_clock;
  auto last = clock::now();

  while (!glfwWindowShouldClose(window)) {
    runtime.reloadIfChanged();

    const auto now = clock::now();
    const float dt = std::chrono::duration<float>(now - last).count();
    last = now;

    runtime.tick(dt);
    glfwPollEvents();
  }

  runtime.shutdown();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
