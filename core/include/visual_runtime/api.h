#pragma once
#include <cstdint>

struct VisualRuntimeState {
  uint64_t frame_count;
  float elapsed_time;
};

enum class SurfaceKind : uint32_t {
  None = 0,
  MacOSMetalLayer = 1,
  LinuxXcbWindow = 2,
  LinuxWaylandSurface = 3,
};

// Describes the native rendering surface passed to the visual runtime on init.
// Handle fields are interpreted according to kind; null/zero for headless
// hosts.
struct SurfaceDescriptor {
  SurfaceKind kind;
  void *display_handle;
  uintptr_t surface_handle;
  uint32_t width;
  uint32_t height;
};

struct ViewState {
  float pan_x;
  float pan_y;
  float zoom;
};

struct Color {
  float r, g, b;
};

struct Rectangle {
  float top_left_corner_x;
  float top_left_corner_y;
  float width;
  float height;
  Color color;
};

constexpr uint32_t VISUAL_RUNTIME_API_VERSION = 6;

struct VisualRuntimeAPI {
  uint32_t abi_version;
  uint32_t struct_size;
  const char *backend_name;

  void (*init)(VisualRuntimeState *, SurfaceDescriptor *);
  void (*resize)(VisualRuntimeState *, uint32_t, uint32_t);
  void (*update)(VisualRuntimeState *, float);
  void (*shutdown)(VisualRuntimeState *);
  void (*set_background_color)(VisualRuntimeState *, const Color *);
  void (*set_view)(VisualRuntimeState *, const ViewState *);
  void (*draw_rectangle)(VisualRuntimeState *, const Rectangle *);
};

extern "C" {
const VisualRuntimeAPI *visual_runtime_get_api();
}
