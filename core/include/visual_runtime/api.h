#pragma once
#include "visual_runtime/types.h"
#include <cstdint>

// Opaque per-instance runtime handle owned by the visual runtime API.
// Hosts allocate this small carrier, but they must not inspect runtime.
struct VRTState {
  void *runtime;
};

constexpr uint32_t VISUAL_RUNTIME_API_VERSION = 7;

struct VRTAPI {
  uint32_t abi_version;
  uint32_t struct_size;
  const char *backend_name;

  void (*init)(VRTState *, VRTSurfaceDescriptor *);
  void (*resize)(VRTState *, const VRTSurfaceMetrics *);
  void (*set_scene_settings)(VRTState *, const VRTSceneSettings *);
  void (*change_view)(VRTState *, const VRTViewChange *);
  bool (*screen_to_world)(VRTState *, const VRTScreenPoint *, VRTWorldPoint *);
  void (*upsert_shape)(VRTState *, const VRTShapeDescriptor *);
  void (*upsert_image)(VRTState *, const VRTImageDescriptor *);
  VRTId (*hit_test)(VRTState *, const VRTScreenPoint *);
  void (*update)(VRTState *, float);
  void (*shutdown)(VRTState *);
};

extern "C" {
const VRTAPI *visual_runtime_get_api();
}
