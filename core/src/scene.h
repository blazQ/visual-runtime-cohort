#pragma once

#include "visual_runtime/types.h"

#include <algorithm>
#include <glm/vec2.hpp>
#include <vector>

// Axis-aligned world-space bounds for a shape: center and size in world units.
struct ShapeBounds {
  glm::dvec2 center_world{};
  glm::dvec2 size_world{};

  bool operator==(const ShapeBounds &other) const {
    return center_world == other.center_world && size_world == other.size_world;
  }
};

// Internal Shape: the visual runtime's C++ representation of one retained shape
// after it crosses the C ABI boundary. Content only — no renderer state.
struct Shape {
  VRTId id = 0;
  VRTShapeKind kind = VRTShapeKind::Rectangle;
  ShapeBounds bounds{};
  VRTColorRGBA color{};

  bool operator==(const Shape &other) const {
    return id == other.id && kind == other.kind && bounds == other.bounds &&
           color == other.color;
  }
};

// Scene: the visual runtime's retained model of visual content and settings.
// It knows what is in the scene and whether an upsert changed anything; it does
// not know about the renderer, drawable handles, or math-vector colors.
class Scene {
public:
  enum class Change { Unchanged, Created, Updated };

  // Create or replace one retained shape by product-owned identity. Reports
  // whether (and how) the content changed so the caller can reconcile renderer
  // state without re-deriving it.
  Change upsert_shape(const VRTShapeDescriptor &descriptor) {
    if (descriptor.id == 0 || descriptor.reserved != 0) {
      return Change::Unchanged;
    }

    const Shape next_shape = shape_from_descriptor(descriptor);
    auto existing = std::find_if(
        shapes_.begin(), shapes_.end(),
        [id = next_shape.id](const Shape &shape) { return shape.id == id; });

    if (existing != shapes_.end()) {
      if (*existing == next_shape) {
        return Change::Unchanged;
      }
      *existing = next_shape;
      return Change::Updated;
    }

    shapes_.push_back(next_shape);
    return Change::Created;
  }

  // Returns whether the settings actually changed — same idiom as
  // ViewState::resize, so the caller knows when to re-derive FrameConfig.
  bool set_scene_settings(const VRTSceneSettings &settings) {
    if (scene_settings_ == settings) {
      return false;
    }
    scene_settings_ = settings;
    return true;
  }

  // Retained shapes in insertion order — the draw-order authority.
  const std::vector<Shape> &shapes() const { return shapes_; }

  const Shape *find_shape(VRTId id) const {
    auto it = std::find_if(shapes_.begin(), shapes_.end(),
                           [id](const Shape &shape) { return shape.id == id; });
    return it == shapes_.end() ? nullptr : &*it;
  }

  const VRTColorRGBA &background_color() const {
    return scene_settings_.background_color;
  }

private:
  static Shape shape_from_descriptor(const VRTShapeDescriptor &descriptor) {
    return Shape{
        descriptor.id,
        descriptor.kind,
        ShapeBounds{
            glm::dvec2{descriptor.center_world.x, descriptor.center_world.y},
            glm::dvec2{descriptor.size_world.x, descriptor.size_world.y},
        },
        descriptor.color,
    };
  }

  VRTSceneSettings scene_settings_{
      VRTColorRGBA{0.0f, 0.0f, 0.0f, 1.0f}, // Scene Settings Default
  };
  std::vector<Shape> shapes_;
};
