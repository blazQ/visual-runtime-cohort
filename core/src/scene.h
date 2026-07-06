#pragma once

#include "visual_runtime/types.h"

#include <algorithm>
#include <glm/vec2.hpp>
#include <glm/common.hpp>
#include <vector>

// Axis-aligned world-space bounds for a shape: center and size in world units.
struct WorldBounds {
  glm::dvec2 center_world{};
  glm::dvec2 size_world{};

  bool operator==(const WorldBounds &other) const {
    return center_world == other.center_world && size_world == other.size_world;
  }

  bool contains(glm::dvec2 point) const {
    const glm::dvec2 d = glm::abs(point - center_world);
    return d.x <= size_world.x * 0.5 && d.y <= size_world.y * 0.5;
  }
};


// Internal Shape: the visual runtime's C++ representation of one retained shape
// after it crosses the C ABI boundary. Content only — no renderer state.
struct Shape {
  VRTId id = kInvalidId;
  VRTShapeKind kind = VRTShapeKind::Rectangle;
  WorldBounds bounds{};
  VRTColorRGBA color{};

  bool operator==(const Shape &other) const {
    return id == other.id && kind == other.kind && bounds == other.bounds &&
           color == other.color;
  }
};

struct Image {
  VRTId id = kInvalidId;
  WorldBounds bounds{};

  // For simplicity, on the Scene side, we don't care about the pixels of an image.
  // Two images are the same if they have the same id and the same bounds.
  // If in the future we want to introduce a way to change the pixels of an image from the host, maybe re-using ids, then this has to change.
  bool operator==(const Image &other) const {
    return id == other.id && bounds == other.bounds;
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
    return upsert(shapes_, descriptor, shape_from_descriptor);
  }

  Change upsert_image(const VRTImageDescriptor &descriptor) {
    return upsert(images_, descriptor, image_from_descriptor);
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

  const std::vector<Image> &images() const { return images_; }

  const Shape *find_shape(VRTId id) const {
    return find_by_id(shapes_, id);
  }

  const Image *find_image(VRTId id) const {
    return find_by_id(images_, id);
  }

  const WorldBounds *bounds_of(VRTId id) const {
    if (const Shape *shape = find_shape(id)) return &shape->bounds;
    if (const Image *image = find_image(id)) return &image->bounds;
    return nullptr;
  }

  bool move_item(VRTId id, glm::dvec2 center) {
    if (Shape *shape = find_by_id(shapes_, id)) {
      shape->bounds.center_world = center;
      return true;
    }
    if (Image *image = find_by_id(images_, id)) {
      image->bounds.center_world = center;
      return true;
    }
    return false;
  }

  const VRTColorRGBA &background_color() const {
    return scene_settings_.background_color;
  }

  VRTId item_at(glm::dvec2 world) const {
  // topmost first: images draw over shapes, later items over earlier — so
  // walk images reverse, then shapes reverse
    for (auto it = images_.rbegin(); it != images_.rend(); ++it)
      if (it->bounds.contains(world)) return it->id;
    for (auto it = shapes_.rbegin(); it != shapes_.rend(); ++it)
      if (it->bounds.contains(world)) return it->id;
    return kInvalidId;
  }


private:
  template <class T>
  static const T *find_by_id(const std::vector<T> &items, VRTId id) {
    auto it = std::find_if(items.begin(), items.end(), 
                                [id](const T &item) { return item.id == id; });
    return it == items.end() ? nullptr : &*it;
  }

  // Const-cast delegate trick from Effective C++:
  // 1. We can always add const
  // 2. Overloading calls the const version on a const reference to a mutable value
  // 3. The pointer we get can be safely const_casted because we know we own the mutable version
  // Might be a bit hacky, we could just create a non const clone of find_by_id withut being too clever.
  template <class T>
  static T *find_by_id(std::vector<T> &items, VRTId id) {
    const std::vector<T> &const_items = items;                 // bind as const
    return const_cast<T *>(find_by_id(const_items, id));       // reuse const overload
  }

  template <class T, class Descriptor>
  static Change upsert(std::vector<T> &items, const Descriptor &descriptor,
                       T (*make)(const Descriptor &)) {
    if (descriptor.id == kInvalidId || descriptor.reserved != 0) {
      return Change::Unchanged;
    }
    const T next = make(descriptor);
    if (T *existing = find_by_id(items, descriptor.id)) {
      if (*existing == next) return Change::Unchanged;
      *existing = next;
      return Change::Updated;
    }
    items.push_back(next);
    return Change::Created;
  }

  static WorldBounds bounds_from(const VRTVec2 &center, const VRTVec2 &size) {
    return WorldBounds{
        glm::dvec2{center.x, center.y},
        glm::dvec2{size.x,   size.y},
    };
  }

  static Shape shape_from_descriptor(const VRTShapeDescriptor &descriptor) {
    return Shape{
        descriptor.id,
        descriptor.kind,
        bounds_from(descriptor.center_world, descriptor.size_world),
        descriptor.color,
    };
  }

  static Image image_from_descriptor(const VRTImageDescriptor &descriptor) {
    return Image{
      descriptor.id,
      bounds_from(descriptor.center_world, descriptor.size_world)
    };
  };

  VRTSceneSettings scene_settings_{
      VRTColorRGBA{0.0f, 0.0f, 0.0f, 1.0f}, // Scene Settings Default
  };
  std::vector<Shape> shapes_;
  std::vector<Image> images_;
};
