#pragma once
#include <cstdint>

// Native surface identity ----------------------------------------------------

enum class VRTSurfaceKind : uint32_t {
  None = 0,
  MacOSMetalLayer = 1,
  LinuxXcbWindow = 2,
  LinuxWaylandSurface = 3,
};

// Surface size ---------------------------------------------------------------

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

// Native surface handles -----------------------------------------------------

// Describes the native rendering surface passed to the visual runtime on init.
// Handle fields are interpreted according to kind; null/zero for headless
// hosts.
struct VRTSurfaceDescriptor {
  VRTSurfaceKind kind;
  void *display_handle;
  uintptr_t surface_handle;
  VRTSurfaceMetrics metrics;
};

// Basic value types ----------------------------------------------------------

// Product-owned stable identity for retained visual runtime objects. The
// visual runtime uses ids only to match later updates to earlier product
// objects; it does not allocate ids or interpret their meaning. Zero is
// reserved as an invalid id.
using VRTId = uint64_t;

struct VRTVec2 {
  double x;
  double y;
};

struct VRTScreenPoint {
  double x_screen;
  double y_screen;
};

struct VRTWorldPoint {
  double x_world;
  double y_world;
};

struct VRTColorRGBA {
  float r;
  float g;
  float b;
  float a;
};

// Scene shapes ---------------------------------------------------------------

enum class VRTShapeKind : uint32_t {
  Rectangle = 1,
};

// Product-shaped request to create or replace scene shape content. Positions
// and sizes are in world units and are independent from the current surface,
// pixel density, or view transform.
struct VRTShapeDescriptor {
  VRTId id;
  VRTShapeKind kind;
  uint32_t reserved;
  VRTVec2 center_world;
  VRTVec2 size_world;
  VRTColorRGBA color;
};

// Scene settings -------------------------------------------------------------

struct VRTSceneSettings {
  VRTColorRGBA background_color;
};

// View changes ---------------------------------------------------------------

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
