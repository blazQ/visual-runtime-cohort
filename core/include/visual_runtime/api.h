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

// Surface size in both drawable pixels and display-independent screen units.
// Screen units are used by product-shaped view changes; pixel units are used
// for graphics API drawable/swapchain sizing. On 1:1 platforms these values are
// the same.
struct VisualRuntimeSurfaceMetrics {
  uint32_t pixel_width;
  uint32_t pixel_height;
  double screen_width;
  double screen_height;
};

#ifdef __cplusplus
namespace visual_runtime {
constexpr VisualRuntimeSurfaceMetrics metrics_1x(uint32_t width,
                                                 uint32_t height) {
  return VisualRuntimeSurfaceMetrics{width, height, static_cast<double>(width),
                                     static_cast<double>(height)};
}
} // namespace visual_runtime
#endif

// Describes the native rendering surface passed to the visual runtime on init.
// Handle fields are interpreted according to kind; null/zero for headless
// hosts.
struct SurfaceDescriptor {
  SurfaceKind kind;
  void *display_handle;
  uintptr_t surface_handle;
  VisualRuntimeSurfaceMetrics metrics;
};

enum VisualRuntimeViewChangeFlags : uint32_t {
  VisualRuntimeViewChange_None = 0,
  VisualRuntimeViewChange_Pan = 1u << 0,
  VisualRuntimeViewChange_Zoom = 1u << 1,
};

// Product-shaped request to change the retained view intention. Screen values
// are display-independent surface-local units with a top-left origin: +X moves
// right and +Y moves down. Pan uses drag/content semantics. Zoom is a
// logarithmic scale delta anchored at a screen-space point. When pan and zoom
// are both present, the runtime applies anchored zoom first, then pan. Reserved
// must be zero.
struct VisualRuntimeViewChange {
  uint32_t flags;
  uint32_t reserved;

  double pan_x_screen;
  double pan_y_screen;

  double zoom_delta_log_scale;
  double zoom_anchor_x_screen;
  double zoom_anchor_y_screen;
};

constexpr uint32_t VISUAL_RUNTIME_API_VERSION = 5;

struct VisualRuntimeAPI {
  uint32_t abi_version;
  uint32_t struct_size;
  const char *backend_name;

  void (*init)(VisualRuntimeState *, SurfaceDescriptor *);
  void (*resize)(VisualRuntimeState *, const VisualRuntimeSurfaceMetrics *);
  void (*change_view)(VisualRuntimeState *, const VisualRuntimeViewChange *);
  void (*update)(VisualRuntimeState *, float);
  void (*shutdown)(VisualRuntimeState *);
};

extern "C" {
const VisualRuntimeAPI *visual_runtime_get_api();
}
