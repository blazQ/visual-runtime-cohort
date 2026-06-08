#include "renderer.h"
#include "view_state.h"
#include "visual_runtime/api.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <vector>

namespace {

glm::vec4 color_to_vec4(const VRTColorRGBA &color) {
  return glm::vec4{color.r, color.g, color.b, color.a};
}

bool color_equal(const VRTColorRGBA &lhs, const VRTColorRGBA &rhs) {
  return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

bool scene_settings_equal(const VRTSceneSettings &lhs,
                          const VRTSceneSettings &rhs) {
  return color_equal(lhs.background_color, rhs.background_color);
}

std::array<renderer::Vertex, 6>
rectangle_vertices(const VRTShapeDescriptor &shape) {
  const glm::vec2 center{
      static_cast<float>(shape.center_world.x),
      static_cast<float>(shape.center_world.y),
  };
  const glm::vec2 half_size{
      static_cast<float>(shape.size_world.x * 0.5),
      static_cast<float>(shape.size_world.y * 0.5),
  };
  const glm::vec2 min = center - half_size;
  const glm::vec2 max = center + half_size;
  const glm::vec4 color = color_to_vec4(shape.color);

  return {{
      renderer::Vertex{min, color},
      renderer::Vertex{glm::vec2{max.x, min.y}, color},
      renderer::Vertex{max, color},
      renderer::Vertex{min, color},
      renderer::Vertex{max, color},
      renderer::Vertex{glm::vec2{min.x, max.y}, color},
  }};
}

class ShapeStore {
public:
  void upsert(const VRTShapeDescriptor &shape, Renderer &renderer) {
    if (shape.id == 0) {
      return;
    }
    auto vertices = rectangle_vertices(shape);
    const renderer::DrawableDesc drawable_desc{
        vertices.data(),
        vertices.size(),
    };

    auto existing_shape =
        std::find_if(shapes_.begin(), shapes_.end(),
                     [id = shape.id](const ShapeRecord &record) {
                       return record.descriptor.id == id;
                     });
    if (existing_shape != shapes_.end()) {
      renderer.destroy_drawable(existing_shape->drawable);
      existing_shape->descriptor = shape;
      existing_shape->drawable = renderer.create_drawable(drawable_desc);
      return;
    }

    shapes_.push_back(ShapeRecord{
        shape,
        renderer.create_drawable(drawable_desc),
    });
  }

  void draw(Renderer &renderer) const {
    for (const auto &shape : shapes_) {
      renderer.draw(shape.drawable);
    }
  }

  void destroy_drawables(Renderer &renderer) {
    for (const auto &shape : shapes_) {
      renderer.destroy_drawable(shape.drawable);
    }
    shapes_.clear();
  }

private:
  struct ShapeRecord {
    VRTShapeDescriptor descriptor{};
    renderer::DrawableHandle drawable{};
  };

  std::vector<ShapeRecord> shapes_;
};

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

  ~VisualRuntime() {
    shapes_.destroy_drawables(renderer_);
    renderer_.shutdown();
  }

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

  void set_scene_settings(const VRTSceneSettings &settings) {
    if (scene_settings_equal(scene_settings_, settings)) {
      return;
    }
    scene_settings_ = settings;
    sync_frame_config();
  }

  void upsert_shape(const VRTShapeDescriptor &shape) {
    shapes_.upsert(shape, renderer_);
  }

  void update(float dt) {
    elapsed_time_ += dt;
    if (renderer_.begin_frame(elapsed_time_)) {
      shapes_.draw(renderer_);
      renderer_.end_frame();
    }
  }

private:
  void sync_frame_config() {
    FrameConfig frame_config = view_state_.frame_config();
    frame_config.clear_color = color_to_vec4(scene_settings_.background_color);
    renderer_.set_frame_config(frame_config);
  }

  VRTSceneSettings scene_settings_{
      VRTColorRGBA{0.0f, 0.0f, 0.0f, 1.0f},
  };
  ShapeStore shapes_;
  ViewState view_state_;
  Renderer renderer_;
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

// Forward app-owned scene settings across the C boundary.
void set_scene_settings(VRTState *state, const VRTSceneSettings *settings) {
  if (auto *rt = runtime(state); rt && settings) {
    rt->set_scene_settings(*settings);
  }
}

// Forward product-shaped view changes across the C boundary.
void change_view(VRTState *state, const VRTViewChange *change) {
  if (auto *rt = runtime(state); rt && change) {
    rt->change_view(*change);
  }
}

// Forward product-shaped scene shape updates across the C boundary.
void upsert_shape(VRTState *state, const VRTShapeDescriptor *shape) {
  if (auto *rt = runtime(state); rt && shape) {
    rt->upsert_shape(*shape);
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
      api_callbacks::resize,       api_callbacks::set_scene_settings,
      api_callbacks::change_view,  api_callbacks::upsert_shape,
      api_callbacks::update,       api_callbacks::shutdown,
  };
  return &api;
}

} // extern "C"
