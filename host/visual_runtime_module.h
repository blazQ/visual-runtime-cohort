#pragma once

#include "dynamic_library.h"
#include "visual_runtime/types.h"
#include "visual_runtime_api.h"

#include <utility>

struct VisualRuntimeModule {
  VisualRuntimeModule(const VisualRuntimeModule &) = delete;
  VisualRuntimeModule &operator=(const VisualRuntimeModule &) = delete;
  VisualRuntimeModule(VisualRuntimeModule &&other) noexcept
      : lib_(std::move(other.lib_)), api_(other.api_),
        api_bound_(other.api_bound_), state_(other.state_),
        surface_(other.surface_), has_surface_(other.has_surface_),
        initialized_(other.initialized_) {
    other.initialized_ = false;
    other.api_bound_ = false;
  }
  ~VisualRuntimeModule() { shutdown(); }

  static VisualRuntimeModule open(const char *lib_path) {
    return VisualRuntimeModule(DynamicLibrary::open(lib_path));
  }

  void attachSurface(VRTSurfaceDescriptor surface) {
    surface_ = surface;
    has_surface_ = true;
    init();
  }

  void resize(VRTSurfaceMetrics metrics) {
    surface_.metrics = metrics;
    if (initialized_ && api_.resize)
      api_.resize(&state_, &surface_.metrics);
  }

  void setSceneSettings(const VRTSceneSettings &settings) const {
    if (initialized_ && api_.set_scene_settings)
      api_.set_scene_settings(&state_, &settings);
  }

  void changeView(const VRTViewChange &change) const {
    if (initialized_ && api_.change_view)
      api_.change_view(&state_, &change);
  }

  bool screenToWorld(const VRTScreenPoint &screen,
                     VRTWorldPoint &world) const {
    if (!initialized_ || !api_.screen_to_world)
      return false;
    return api_.screen_to_world(&state_, &screen, &world);
  }

  void upsertShape(const VRTShapeDescriptor &shape) const {
    if (initialized_ && api_.upsert_shape)
      api_.upsert_shape(&state_, &shape);
  }

  void upsertImage(const VRTImageDescriptor &image) const {
    if (initialized_ && api_.upsert_image)
      api_.upsert_image(&state_, &image);
  }

  VRTId hit_test(const VRTScreenPoint &screen) const {
    if (initialized_ && api_.hit_test)
      return api_.hit_test(&state_, &screen);
    return kInvalidId;
  }

  void set_selection(VRTId id) const {
    if (initialized_ && api_.set_selection)
      api_.set_selection(&state_, id);
  }

  bool reloadIfChanged() {
    if (!lib_.changed())
      return false;
    return reload();
  }

  bool reload() {
    shutdown();
    if (!lib_.reload())
      return false;

    bindApi();
    init();
    return true;
  }

  void tick(float dt) const {
    if (!initialized_ || !api_.update)
      return;
    api_.update(&state_, dt);
  }

  void shutdown() const {
    if (!initialized_)
      return;
    if (api_.shutdown)
      api_.shutdown(&state_);
    initialized_ = false;
  }

  const char *backendName() const {
    return api_.backend_name ? api_.backend_name : "Unknown";
  }

  explicit operator bool() const { return bool(lib_) && api_bound_; }

private:
  explicit VisualRuntimeModule(DynamicLibrary lib) : lib_(std::move(lib)) {
    if (lib_) {
      bindApi();
      init();
    }
  }

  void bindApi() {
    api_ = {};
    api_bound_ = false;

    auto get_api = reinterpret_cast<VisualRuntimeGetAPIFn>(
        lib_.sym("visual_runtime_get_api"));
    if (!get_api)
      return;

    const VRTAPI *loaded_api = get_api();
    if (!loaded_api) {
      std::fprintf(
          stderr,
          "[visual_runtime_module] visual_runtime_get_api returned null\n");
      return;
    }
    if (!visual_runtime_api_valid(*loaded_api))
      return;

    api_ = *loaded_api;
    api_bound_ = true;
  }

  void init() const {
    if (!api_.init)
      return;
    api_.init(&state_, has_surface_ ? &surface_ : nullptr);
    initialized_ = true;
  }

  DynamicLibrary lib_;
  VRTAPI api_;
  bool api_bound_ = false;
  mutable VRTState state_{};
  mutable VRTSurfaceDescriptor surface_{};
  bool has_surface_ = false;
  mutable bool initialized_ = false;
};
