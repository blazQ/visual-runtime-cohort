#include "renderer.h"
#include "view_state.h"
#include "visual_runtime/api.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vector>

namespace {

glm::vec4 color_to_vec4(const VRTColorRGBA &color) {
  return glm::vec4{color.r, color.g, color.b, color.a};
}

bool color_equal(const VRTColorRGBA &lhs, const VRTColorRGBA &rhs) {
  return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

bool vec2_equal(const glm::dvec2 &lhs, const glm::dvec2 &rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y;
}

bool color_equal(const glm::vec4 &lhs, const glm::vec4 &rhs) {
  return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

bool scene_settings_equal(const VRTSceneSettings &lhs,
                          const VRTSceneSettings &rhs) {
  return color_equal(lhs.background_color, rhs.background_color);
}

// These are scene concepts kept local to VisualRuntime for now. When the
// runtime grows a dedicated Scene object, these structs should move with it.
struct ShapeBounds {
  glm::dvec2 center_world{};
  glm::dvec2 size_world{};
};

struct Shape {
  VRTId id = 0;
  VRTShapeKind kind = VRTShapeKind::Rectangle;
  ShapeBounds bounds{};
  glm::vec4 color{};
};

struct ShapeDrawable {
  Shape shape{};
  Renderer::DrawableHandle handle{};
};

Shape shape_from_descriptor(const VRTShapeDescriptor &descriptor) {
  return Shape{
      descriptor.id,
      descriptor.kind,
      ShapeBounds{
          glm::dvec2{descriptor.center_world.x, descriptor.center_world.y},
          glm::dvec2{descriptor.size_world.x, descriptor.size_world.y},
      },
      color_to_vec4(descriptor.color),
  };
}

bool shape_bounds_equal(const ShapeBounds &lhs, const ShapeBounds &rhs) {
  return vec2_equal(lhs.center_world, rhs.center_world) &&
         vec2_equal(lhs.size_world, rhs.size_world);
}

bool shape_equal(const Shape &lhs, const Shape &rhs) {
  return lhs.id == rhs.id && lhs.kind == rhs.kind &&
         shape_bounds_equal(lhs.bounds, rhs.bounds) &&
         color_equal(lhs.color, rhs.color);
}

constexpr glm::vec4 kPlaceholderColor{1.0f};

// Shared local geometry; rectangle placement and color live in drawable state.
constexpr std::array<Renderer::Vertex, 6> kQuadVertices{{
    Renderer::Vertex{glm::vec2{-0.5f, -0.5f}, kPlaceholderColor},
    Renderer::Vertex{glm::vec2{0.5f, -0.5f}, kPlaceholderColor},
    Renderer::Vertex{glm::vec2{0.5f, 0.5f}, kPlaceholderColor},
    Renderer::Vertex{glm::vec2{-0.5f, -0.5f}, kPlaceholderColor},
    Renderer::Vertex{glm::vec2{0.5f, 0.5f}, kPlaceholderColor},
    Renderer::Vertex{glm::vec2{-0.5f, 0.5f}, kPlaceholderColor},
}};

Renderer::DrawableState drawable_state_for(const Shape &shape) {
  return Renderer::DrawableState{
      glm::translate(glm::mat4{1.0f},
                     glm::vec3{static_cast<float>(shape.bounds.center_world.x),
                               static_cast<float>(shape.bounds.center_world.y),
                               0.0f}) *
          glm::scale(glm::mat4{1.0f},
                     glm::vec3{static_cast<float>(shape.bounds.size_world.x),
                               static_cast<float>(shape.bounds.size_world.y),
                               1.0f}),
      shape.color,
      shape.kind
  };
}

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
  }

  ~VisualRuntime() {
    destroy_shape_drawables();
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

  bool screen_to_world(const VRTScreenPoint &screen, VRTWorldPoint &world) {
    return view_state_.screen_to_world(screen, world);
  }

  void set_scene_settings(const VRTSceneSettings &settings) {
    if (scene_settings_equal(scene_settings_, settings)) {
      return;
    }
    scene_settings_ = settings;
    sync_frame_config();
  }

  void upsert_shape(const VRTShapeDescriptor &shape) {
    if (shape.id == 0 || shape.reserved != 0) {
      return;
    }

    const Shape next_shape = shape_from_descriptor(shape);
    auto existing_shape =
        std::find_if(shape_drawables_.begin(), shape_drawables_.end(),
                     [id = next_shape.id](const ShapeDrawable &drawable) {
                       return drawable.shape.id == id;
                     });

    if (existing_shape != shape_drawables_.end()) {
      if (shape_equal(existing_shape->shape, next_shape)) {
        return;
      }
      existing_shape->shape = next_shape;
      renderer_.update_drawable(existing_shape->handle,
                                drawable_state_for(existing_shape->shape));
      return;
    }

    const Renderer::DrawableDesc drawable_desc{
        kQuadVertices.data(),
        kQuadVertices.size(),
    };
    auto handle = renderer_.create_drawable(drawable_desc);
    renderer_.update_drawable(handle, drawable_state_for(next_shape));
    shape_drawables_.push_back(ShapeDrawable{
        next_shape,
        handle,
    });
  }

  void update(float dt) {
    elapsed_time_ += dt;
    if (renderer_.begin_frame(elapsed_time_)) {
      for (const auto &drawable : shape_drawables_) {
        renderer_.draw(drawable.handle);
      }
      renderer_.end_frame();
    }
  }

private:
  void destroy_shape_drawables() {
    for (const auto &drawable : shape_drawables_) {
      renderer_.destroy_drawable(drawable.handle);
    }
    shape_drawables_.clear();
  }

  void sync_frame_config() {
    FrameConfig frame_config = view_state_.frame_config();
    frame_config.clear_color = color_to_vec4(scene_settings_.background_color);
    renderer_.set_frame_config(frame_config);
  }

  VRTSceneSettings scene_settings_{
      VRTColorRGBA{0.0f, 0.0f, 0.0f, 1.0f},
  };
  std::vector<ShapeDrawable> shape_drawables_;
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

// Map a surface-local screen point into the runtime's retained world space.
bool screen_to_world(VRTState *state, const VRTScreenPoint *screen,
                     VRTWorldPoint *world) {
  if (auto *rt = runtime(state); rt && screen && world) {
    return rt->screen_to_world(*screen, *world);
  }
  return false;
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
      api_callbacks::change_view,  api_callbacks::screen_to_world,
      api_callbacks::upsert_shape, api_callbacks::update,
      api_callbacks::shutdown,
  };
  return &api;
}

} // extern "C"
