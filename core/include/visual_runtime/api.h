#pragma once
#include <cstdint>

struct VRTState {
  void *runtime;
};

enum class VRTSurfaceKind : uint32_t {
  None = 0,
  MacOSMetalLayer = 1,
  LinuxXcbWindow = 2,
  LinuxWaylandSurface = 3,
};

// Surface size in both drawable pixels and display-independent screen units.
// Screen units are used by product-shaped view changes; pixel units are used
// for graphics API drawable/swapchain sizing. On 1:1 platforms these values are
// the same.
struct VRTSurfaceMetrics {
  uint32_t pixel_width;
  uint32_t pixel_height;
  double screen_width;
  double screen_height;
};

#ifdef __cplusplus
namespace visual_runtime {
constexpr VRTSurfaceMetrics metrics_1x(uint32_t width, uint32_t height) {
  return VRTSurfaceMetrics{width, height, static_cast<double>(width),
                           static_cast<double>(height)};
}
} // namespace visual_runtime
#endif

// Describes the native rendering surface passed to the visual runtime on init.
// Handle fields are interpreted according to kind; null/zero for headless
// hosts.
struct VRTSurfaceDescriptor {
  VRTSurfaceKind kind;
  void *display_handle;
  uintptr_t surface_handle;
  VRTSurfaceMetrics metrics;
};

enum VRTViewChangeFlags : uint32_t {
  VRTViewChange_None = 0,
  VRTViewChange_Pan = 1u << 0,
  VRTViewChange_Zoom = 1u << 1,
};

// Product-shaped request to change the retained view intention. Screen values
// are display-independent surface-local units with a top-left origin: +X moves
// right and +Y moves down. Pan uses drag/content semantics. Zoom is a
// logarithmic scale delta anchored at a screen-space point. When pan and zoom
// are both present, the runtime applies anchored zoom first, then pan. Reserved
// must be zero.
struct VRTViewChange {
  uint32_t flags;
  uint32_t reserved;

  double pan_x_screen;
  double pan_y_screen;

  double zoom_delta_log_scale;
  double zoom_anchor_x_screen;
  double zoom_anchor_y_screen;
};

constexpr uint32_t VISUAL_RUNTIME_API_VERSION = 5;

struct VRTAPI {
  uint32_t abi_version;
  uint32_t struct_size;
  const char *backend_name;

  void (*init)(VRTState *, VRTSurfaceDescriptor *);
  void (*resize)(VRTState *, const VRTSurfaceMetrics *);
  void (*change_view)(VRTState *, const VRTViewChange *);
  void (*update)(VRTState *, float);
  void (*shutdown)(VRTState *);
};

extern "C" {
const VRTAPI *visual_runtime_get_api();
}
