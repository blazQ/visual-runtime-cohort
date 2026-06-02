#include "visual_runtime/api.h"
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

// TODO: Move View, Background Color, Rects (which will become Shapes) in the runtime.
// Last_mouse coordinates and panning remain application_side concerns

struct AppState {
  VisualRuntimeModule *runtime = nullptr;
  ViewState view{0.0f, 0.0f, 1.0f};
  Color background{0.0f, 0.0f, 0.15f};
  std::vector<Rectangle> rects;
  double last_mouse_x = 0.0;
  double last_mouse_y = 0.0;
  bool panning = false;
};

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

  state->runtime->resize(static_cast<uint32_t>(width),
                         static_cast<uint32_t>(height));
  std::fprintf(stderr, "[glfw-minimal] resized to %dx%d\n", width, height);
}

void on_scroll(GLFWwindow *window, double /*dx*/, double dy) {
  auto *state = static_cast<AppState *>(glfwGetWindowUserPointer(window));
  if (!state || !state->runtime) return;

  state->view.zoom *= (dy > 0) ? 1.1f : (1.0f / 1.1f);
}

void on_mouse_button(GLFWwindow *window, int button, int action, int /*mods*/) {
  if (button != GLFW_MOUSE_BUTTON_LEFT) return;
  auto *state = static_cast<AppState *>(glfwGetWindowUserPointer(window));
  if (!state) return;
  state->panning = (action == GLFW_PRESS);
  if (state->panning)
    glfwGetCursorPos(window, &state->last_mouse_x, &state->last_mouse_y);
}

void on_cursor_pos(GLFWwindow *window, double x, double y) {
  auto *state = static_cast<AppState *>(glfwGetWindowUserPointer(window));
  if (!state || !state->runtime) return;

  if(state->panning){
      int w, h;
      glfwGetWindowSize(window, &w, &h);
      const float ref = static_cast<float>(std::min(w, h));

      // Convert pixel delta → canvas units: NDC range is 2 (-1..1), ref is the
      // shorter dimension (matches aspect-correction in the shader).
      const double dx = x - state->last_mouse_x;
      const double dy = y - state->last_mouse_y;
      state->view.pan_x -= static_cast<float>(2.0 * dx / ref) / state->view.zoom;
      state->view.pan_y += static_cast<float>(2.0 * dy / ref) / state->view.zoom;
  }

  state->last_mouse_x = x;
  state->last_mouse_y = y;
}

void on_key(GLFWwindow *window, int key, int /*scancode*/, int action, int /*mods*/){
  if (key != GLFW_KEY_P || action != GLFW_PRESS) return;
  auto *state = static_cast<AppState *>(glfwGetWindowUserPointer(window));
  if (!state || !state->runtime) return;


  int ww, wh;
  glfwGetWindowSize(window, &ww, &wh);

  float nx = (2.0f * state->last_mouse_x / ww) - 1.0f;
  float ny = (2.0f * state->last_mouse_y / wh) - 1.0f;

  float aspect = float(ww) / float(wh);
  float sx = (aspect >= 1.0f) ? (1.0f / aspect) : 1.0f;
  float sy = (aspect >= 1.0f) ? -1.0f : -aspect;

  float wx = (nx / sx) / state->view.zoom + state->view.pan_x;
  float wy = (ny / sy) / state->view.zoom + state->view.pan_y;

  Rectangle rect{wx - 0.1f, wy + 0.05f, 0.2f, 0.1f, {1.0f, 0.5f, 0.0f}};
  state->rects.push_back(rect);
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

  SurfaceDescriptor surface{
      SurfaceKind::LinuxWaylandSurface,
      display,
      reinterpret_cast<uintptr_t>(wayland_surface),
      static_cast<uint32_t>(width),
      static_cast<uint32_t>(height),
  };
  std::fprintf(stderr,
               "[glfw-minimal] attaching LinuxWaylandSurface surface (%ux%u)\n",
               surface.width, surface.height);
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

  SurfaceDescriptor surface{
      SurfaceKind::LinuxXcbWindow,        connection,
      static_cast<uintptr_t>(x11_window), static_cast<uint32_t>(width),
      static_cast<uint32_t>(height),
  };
  std::fprintf(stderr,
               "[glfw-minimal] attaching LinuxXcbWindow surface (%ux%u)\n",
               surface.width, surface.height);
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
  glfwSetScrollCallback(window, on_scroll);
  glfwSetMouseButtonCallback(window, on_mouse_button);
  glfwSetCursorPosCallback(window, on_cursor_pos);
  glfwSetKeyCallback(window, on_key);

  if (!attach_surface(window, runtime)) {
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }
  using clock = std::chrono::steady_clock;
  auto last = clock::now();

  while (!glfwWindowShouldClose(window)) {
    if (runtime.reloadIfChanged()) {
      std::printf("[host] reloaded (frame %lu)\n", runtime.frameCount());
    }

    auto now = clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;

    const float pan_speed = 0.8f;  // canvas units per second — tune to taste
    if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) state.view.pan_x -= pan_speed * dt / state.view.zoom;
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) state.view.pan_x += pan_speed * dt / state.view.zoom;
    if (glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS) state.view.pan_y += pan_speed * dt / state.view.zoom;
    if (glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS) state.view.pan_y -= pan_speed * dt / state.view.zoom;
    runtime.setView(state.view);
    runtime.setBackgroundColor(state.background);
  
    for (const auto &rect: state.rects){
      runtime.drawRect(rect);
    }

    runtime.tick(dt);
    glfwPollEvents();
  }

  std::printf("[glfw-minimal] exiting after %lu frames\n",
              runtime.frameCount());
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
