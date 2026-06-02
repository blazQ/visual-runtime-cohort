#pragma once

#include "visual_runtime/api.h"

#include <cstdint>
#include <memory>
#include <string>

struct VisualRuntimeModule;

// Keeps VisualRuntimeModule forward-declared in this Swift-imported header;
// deletion is defined in VisualRuntimeHost.cpp where the full
// VisualRuntimeModule type is visible.
struct VisualRuntimeModuleDeleter {
  void operator()(VisualRuntimeModule *module) const;
};

class VisualRuntimeHost {
public:
  explicit VisualRuntimeHost(std::string lib_path);
  VisualRuntimeHost(const VisualRuntimeHost &) = delete;
  VisualRuntimeHost &operator=(const VisualRuntimeHost &) = delete;
  VisualRuntimeHost(VisualRuntimeHost &&) noexcept = default;
  VisualRuntimeHost &operator=(VisualRuntimeHost &&) noexcept = default;

  bool valid() const;
  void attachSurface(void *native_surface, const VRTSurfaceMetrics &metrics);
  void resize(const VRTSurfaceMetrics &metrics);
  void setSceneSettings(const VRTSceneSettings &settings);
  void changeView(const VRTViewChange &change);
  void tick(float dt);
  bool reload();
  std::string backendName() const;

private:
  std::unique_ptr<VisualRuntimeModule, VisualRuntimeModuleDeleter> module_;
};
