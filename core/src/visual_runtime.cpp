#include "renderer.h"
#include "view_state.h"
#include "visual_runtime/api.h"

#include <cstdio>

namespace {

// Private C++ owner for one visual runtime instance. Think of
// this as the real runtime object: it owns renderer-facing state and is where
// runtime concepts such as scene, view, or camera state should live.
class VisualRuntime {
public:
  explicit VisualRuntime(VRTSurfaceDescriptor *surface) {
    if (surface) {
      view_state_.resize(surface->metrics);
    }
    renderer_.init(surface);
    sync_frame_config();
    std::printf("[visual-runtime] init\n");
    std::fflush(stdout);
  }

  ~VisualRuntime() { renderer_.shutdown(); }

  VisualRuntime(const VisualRuntime &) = delete;
  VisualRuntime &operator=(const VisualRuntime &) = delete;

  void resize(const VRTSurfaceMetrics &metrics) {
    const bool view_changed = view_state_.resize(metrics);
    renderer_.resize(&metrics);
    if (view_changed) {
      sync_frame_config();
    }
  }

  void change_view(const VRTViewChange &change) {
    if (view_state_.change_view(change)) {
      sync_frame_config();
    }
  }

  void update(float dt) {
    frame_count_ += 1;
    elapsed_time_ += dt;
    renderer_.render_frame(elapsed_time_);
  }

private:
  void sync_frame_config() {
    renderer_.set_frame_config(view_state_.frame_config());
  }

  ViewState view_state_;
  Renderer renderer_;
  uint64_t frame_count_ = 0;
  float elapsed_time_ = 0.0f;
};

// Recover the private C++ runtime from the opaque ABI carrier.
VisualRuntime *runtime(VRTState *state) {
  return state ? static_cast<VisualRuntime *>(state->runtime) : nullptr;
}

// Delete any runtime currently stored in the ABI carrier.
void destroy_runtime(VRTState *state) {
  if (!state) {
    return;
  }

  delete runtime(state);
  state->runtime = nullptr;
}

// Function-table callbacks for the C ABI. Add or change functions here when
// the public VRTAPI grows, then wire them into visual_runtime_get_api below.
namespace api_callbacks {

// Create a fresh runtime instance for this host-owned state carrier.
void init(VRTState *state, VRTSurfaceDescriptor *surface) {
  if (!state) {
    return;
  }

  destroy_runtime(state);
  state->runtime = new VisualRuntime(surface);
}

// Forward surface-size changes across the C boundary.
void resize(VRTState *state, const VRTSurfaceMetrics *metrics) {
  if (auto *rt = runtime(state); rt && metrics) {
    rt->resize(*metrics);
  }
}

// Forward product-shaped view changes across the C boundary.
void change_view(VRTState *state, const VRTViewChange *change) {
  if (auto *rt = runtime(state); rt && change) {
    rt->change_view(*change);
  }
}

// Advance and render one runtime tick.
void update(VRTState *state, float dt) {
  if (auto *rt = runtime(state)) {
    rt->update(dt);
  }
}

// Release the runtime instance owned by this state carrier.
void shutdown(VRTState *state) { destroy_runtime(state); }

} // namespace api_callbacks

} // namespace

extern "C" {

// Build the function table that the host loads. The static table points at the
// callback implementations above and remains valid for the runtime module's
// lifetime.
const VRTAPI *visual_runtime_get_api() {
  static const VRTAPI api{
      VISUAL_RUNTIME_API_VERSION,  sizeof(VRTAPI),
      VISUAL_RUNTIME_BACKEND_NAME, api_callbacks::init,
      api_callbacks::resize,       api_callbacks::change_view,
      api_callbacks::update,       api_callbacks::shutdown,
  };
  return &api;
}

} // extern "C"
