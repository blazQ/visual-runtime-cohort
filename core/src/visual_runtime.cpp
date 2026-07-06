#include "renderer.h"
#include "scene.h"
#include "view_state.h"
#include "visual_runtime/api.h"
#include "visual_runtime/types.h"

#include <array>
#include <cstdio>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <utility>
#include <vector>

namespace {

// Renderer preparation: turning retained Scene content into renderer-facing
// drawable state. This is the reconciler's job, not the Scene's, so it lives
// here on the VisualRuntime side. Product vocabulary (colors, shape kinds) is
// translated into the renderer's own terms here, at the preparation seam.

glm::vec4 color_to_vec4(const VRTColorRGBA &color) {
  return glm::vec4{color.r, color.g, color.b, color.a};
}

Renderer::PrimitiveKind primitive_kind_for(VRTShapeKind kind) {
  switch (kind) {
  case VRTShapeKind::Circle:
    return Renderer::PrimitiveKind::Circle;
  case VRTShapeKind::Rectangle:
    return Renderer::PrimitiveKind::Rectangle;
  }
  return Renderer::PrimitiveKind::Rectangle;
}

Renderer::TextureFormat texture_format_for(VRTPixelFormat format){
  switch (format) {
    case VRTPixelFormat::RGBA8Srgb: return Renderer::TextureFormat::RGBA8Srgb;
    case VRTPixelFormat::RGBA8Unorm: return Renderer::TextureFormat::RGBA8Unorm;
  }
  return Renderer::TextureFormat::RGBA8Srgb;
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

glm::mat4 model_transform_for(const WorldBounds &bounds) { 
  return       glm::translate(glm::mat4{1.0f},
                     glm::vec3{static_cast<float>(bounds.center_world.x),
                               static_cast<float>(bounds.center_world.y),
                               0.0f}) *
          glm::scale(glm::mat4{1.0f},
                     glm::vec3{static_cast<float>(bounds.size_world.x),
                               static_cast<float>(bounds.size_world.y),
                               1.0f});
};


Renderer::DrawableState drawable_state_for(const Shape &shape) {
  return Renderer::DrawableState{model_transform_for(shape.bounds),
      color_to_vec4(shape.color),
      primitive_kind_for(shape.kind),
  };
}

Renderer::DrawableState image_state_for(const Image &image,
                                        Renderer::TextureHandle texture) {
  return Renderer::DrawableState{
      model_transform_for(image.bounds),
      glm::vec4{1.0f},                     // unused for images (a white tint)
      Renderer::PrimitiveKind::Rectangle,
      texture,
  };
}

// Selection box appearance.
constexpr glm::vec4 kSelectionBoxColor{1.0f, 0.82f, 0.25f, 1.0f};
// Box size relative to the item it frames,
constexpr double kSelectionBoxScale = 1.12;

Renderer::DrawableState selection_box_state_for(const WorldBounds &bounds) {
  const WorldBounds framed{bounds.center_world,
                           bounds.size_world * kSelectionBoxScale};
  return Renderer::DrawableState{
      model_transform_for(framed),
      kSelectionBoxColor,
      Renderer::PrimitiveKind::SelectionBox,
  };
}


struct ImageEntry {
  VRTId id;
  Renderer::DrawableHandle drawable;
  Renderer::TextureHandle texture;
};

// Private C++ owner for one visual runtime instance. It owns the Renderer and
// the Scene, and reconciles retained Scene content into renderer drawables.
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
    if (scene_.set_scene_settings(settings)) {
      sync_frame_config();
    }
  }

  void upsert_shape(const VRTShapeDescriptor &descriptor) {
    switch (scene_.upsert_shape(descriptor)) {
    case Scene::Change::Unchanged:
      return;
    case Scene::Change::Created: {
      const Renderer::DrawableDesc geometry{kQuadVertices.data(),
                                            kQuadVertices.size()};
      const Renderer::DrawableHandle handle = renderer_.create_drawable(geometry);
      shape_handles_.push_back({descriptor.id, handle});
      renderer_.update_drawable(
          handle, drawable_state_for(*scene_.find_shape(descriptor.id)));
      return;
    }
    case Scene::Change::Updated:
      if (Renderer::DrawableHandle *handle = find_handle(descriptor.id)) {
        renderer_.update_drawable(
            *handle, drawable_state_for(*scene_.find_shape(descriptor.id)));
      }
      return;
    }
  }

  void upsert_image(const VRTImageDescriptor &descriptor) {
    switch (scene_.upsert_image(descriptor)) {
    case Scene::Change::Unchanged:
      return;
    case Scene::Change::Created: {
      const Renderer::TextureDesc texture_desc{descriptor.pixels.pixels, 
        descriptor.pixels.width, 
        descriptor.pixels.height, 
        texture_format_for(descriptor.pixels.format)};
      const Renderer::TextureHandle texture_handle = renderer_.create_texture(texture_desc);

      const Renderer::DrawableDesc quad{kQuadVertices.data(), kQuadVertices.size()};
      const Renderer::DrawableHandle drawable_handle = renderer_.create_drawable(quad);
      
      image_handles_.push_back({descriptor.id, drawable_handle, texture_handle});
      renderer_.update_drawable(drawable_handle, image_state_for(*scene_.find_image(descriptor.id), texture_handle));
      return;
    }
    case Scene::Change::Updated:
      if (ImageEntry *entry = find_image_handle(descriptor.id)){
        renderer_.update_drawable(entry->drawable, image_state_for(*scene_.find_image(descriptor.id), entry->texture));
      }
      return;
    }
  }

  VRTId hit_test(const VRTScreenPoint &screen_point){
    VRTWorldPoint world{};
    if (!view_state_.screen_to_world(screen_point, world)) return kInvalidId;
    return scene_.item_at(glm::dvec2{world.x_world, world.y_world});
  }

  void set_selection(VRTId id) {
    selected_id_ = id;
  }

  void update(float dt) {
    elapsed_time_ += dt;
    if (renderer_.begin_frame(elapsed_time_)) {
      for (const auto &entry : shape_handles_) {
        renderer_.draw(entry.second);
      }

      for (const auto &entry : image_handles_){
        renderer_.draw(entry.drawable);
      }

      draw_selection_box();

      renderer_.end_frame();
    }
  }

private:
  // Transient chrome: resolve the app-owned selection to bounds each frame and
  // draw a box on top. An unknown or stale id resolves to nothing.
  void draw_selection_box() {
    if (selected_id_ == kInvalidId) {
      return;
    }
    const WorldBounds *bounds = scene_.bounds_of(selected_id_);
    if (!bounds) {
      return;
    }
    if (selection_box_.value == 0) {
      selection_box_ = renderer_.create_drawable(
          {kQuadVertices.data(), kQuadVertices.size()});
    }
    renderer_.update_drawable(selection_box_, selection_box_state_for(*bounds));
    renderer_.draw(selection_box_);
  }

  Renderer::DrawableHandle *find_handle(VRTId id) {
    for (auto &entry : shape_handles_) {
      if (entry.first == id) {
        return &entry.second;
      }
    }
    return nullptr;
  }

  ImageEntry *find_image_handle(VRTId id) {
    for (auto &entry : image_handles_) {
      if (entry.id == id) {
        return &entry;
      }
    }
    return nullptr;
  }

  void sync_frame_config() {
    FrameConfig frame_config = view_state_.frame_config();
    frame_config.clear_color = color_to_vec4(scene_.background_color());
    renderer_.set_frame_config(frame_config);
  }

  Scene scene_;
  ViewState view_state_;
  Renderer renderer_;
  std::vector<std::pair<VRTId, Renderer::DrawableHandle>> shape_handles_;
  std::vector<ImageEntry> image_handles_;
  Renderer::DrawableHandle selection_box_{};
  VRTId selected_id_ = kInvalidId;
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

void upsert_image(VRTState *state, const VRTImageDescriptor *image) {
  if (auto *rt = runtime(state); rt && image) {
    rt->upsert_image(*image);
  }
}

VRTId hit_test(VRTState *state, const VRTScreenPoint *point) {
  if (auto *rt = runtime(state); rt && point){
    return rt->hit_test(*point);
  }
  return kInvalidId;
}

void set_selection(VRTState *state, VRTId id) {
  if (auto *rt = runtime(state)) {
    rt->set_selection(id);
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
      api_callbacks::upsert_shape, api_callbacks::upsert_image,
      api_callbacks::hit_test,     api_callbacks::set_selection,
      api_callbacks::update,
      api_callbacks::shutdown,
  };
  return &api;
}

} // extern "C"
