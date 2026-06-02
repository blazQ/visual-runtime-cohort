#include "VisualRuntimeHost.h"

#include "visual_runtime_module.h"

#include <cstdio>
#include <utility>

void VisualRuntimeModuleDeleter::operator()(VisualRuntimeModule *module) const {
  delete module;
}

VisualRuntimeHost::VisualRuntimeHost(std::string lib_path) {
  VisualRuntimeModule module = VisualRuntimeModule::open(lib_path.c_str());
  if (!module)
    return;

  module_.reset(new VisualRuntimeModule(std::move(module)));
}

bool VisualRuntimeHost::valid() const {
  return module_ && static_cast<bool>(*module_);
}

std::string VisualRuntimeHost::backendName() const {
  if (!module_)
    return "Unknown";

  return module_->backendName();
}

void VisualRuntimeHost::attachSurface(
    void *native_surface, const VisualRuntimeSurfaceMetrics &metrics) {
  if (!module_)
    return;

  SurfaceDescriptor surface{
      SurfaceKind::MacOSMetalLayer,
      nullptr,
      reinterpret_cast<uintptr_t>(native_surface),
      metrics,
  };
  module_->attachSurface(surface);
}

void VisualRuntimeHost::resize(const VisualRuntimeSurfaceMetrics &metrics) {
  if (!module_)
    return;

  module_->resize(metrics);
}

void VisualRuntimeHost::changeView(const VisualRuntimeViewChange &change) {
  if (!module_)
    return;

  module_->changeView(change);
}

void VisualRuntimeHost::tick(float dt) {
  if (!module_)
    return;

  if (module_->reloadIfChanged()) {
    std::printf("[host] reloaded (frame %llu)\n", module_->frameCount());
  }
  module_->tick(dt);
}

bool VisualRuntimeHost::reload() {
  if (!module_)
    return false;

  bool ok = module_->reload();
  if (ok) {
    std::printf("[host] manually reloaded (frame %llu)\n",
                module_->frameCount());
  }
  return ok;
}
