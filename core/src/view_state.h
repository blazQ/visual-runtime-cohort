#pragma once

#include "frame_config.h"
#include "visual_runtime/api.h"

#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

class ViewState {
public:
  bool resize(const VRTSurfaceMetrics &metrics) {
    if (metrics_equal(metrics_, metrics)) {
      return false;
    }

    metrics_ = metrics;
    return true;
  }

  bool change_view(const VRTViewChange &change) {
    if (change.reserved != 0 || !has_screen_metrics()) {
      return false;
    }

    bool changed = false;

    if ((change.flags & VRTViewChange_Zoom) != 0) {
      apply_zoom(change);
      changed = true;
    }

    if ((change.flags & VRTViewChange_Pan) != 0) {
      apply_pan(change);
      changed = true;
    }

    return changed;
  }

  FrameConfig frame_config() const {
    FrameConfig config{};
    config.view_proj_transform = glm::mat4(view_projection_matrix());
    return config;
  }

  bool screen_to_world(const VRTScreenPoint &screen,
                       VRTWorldPoint &world) const {
    if (!has_screen_metrics()) {
      return false;
    }

    const glm::dvec2 ndc = screen_point_to_ndc(screen);
    const glm::dmat4 inverse_view_projection =
        glm::inverse(view_projection_matrix());
    const glm::dvec4 world_position =
        inverse_view_projection * glm::dvec4(ndc.x, ndc.y, 0.0, 1.0);

    world = VRTWorldPoint{
        world_position.x,
        world_position.y,
    };
    return true;
  }

private:
  static bool metrics_equal(const VRTSurfaceMetrics &lhs,
                            const VRTSurfaceMetrics &rhs) {
    return lhs.pixel_width == rhs.pixel_width &&
           lhs.pixel_height == rhs.pixel_height &&
           lhs.screen_width == rhs.screen_width &&
           lhs.screen_height == rhs.screen_height;
  }

  bool has_screen_metrics() const {
    return metrics_.screen_width > 0.0 && metrics_.screen_height > 0.0;
  }

  glm::dvec2 screen_point_to_ndc(const VRTScreenPoint &screen) const {
    return glm::dvec2{
        (2.0 * screen.x_screen / metrics_.screen_width) - 1.0,
        1.0 - (2.0 * screen.y_screen / metrics_.screen_height),
    };
  }

  glm::dvec2 screen_delta_to_ndc(double x_screen, double y_screen) const {
    return glm::dvec2{
        2.0 * x_screen / metrics_.screen_width,
        -2.0 * y_screen / metrics_.screen_height,
    };
  }

  glm::dmat4 view_projection_matrix() const {
    return view_matrix_ * aspect_matrix();
  }

  glm::dmat4 aspect_matrix() const {
    glm::dmat4 matrix{1.0};

    if (metrics_.pixel_width > 0 && metrics_.pixel_height > 0) {
      const double aspect = static_cast<double>(metrics_.pixel_width) /
                            static_cast<double>(metrics_.pixel_height);

      if (aspect >= 1.0) {
        matrix[0][0] = 1.0 / aspect;
      } else {
        matrix[1][1] = aspect;
      }
    }

    return matrix;
  }

  void apply_zoom(const VRTViewChange &change) {
    const double scale = std::exp(change.zoom_delta_log_scale);
    const glm::dvec2 anchor = screen_point_to_ndc(VRTScreenPoint{
        change.zoom_anchor_x_screen,
        change.zoom_anchor_y_screen,
    });

    view_matrix_ =
        glm::translate(glm::dmat4(1.0), glm::dvec3(anchor, 0.0)) *
        glm::scale(glm::dmat4(1.0), glm::dvec3(scale, scale, 1.0)) *
        glm::translate(glm::dmat4(1.0), glm::dvec3(-anchor, 0.0)) *
        view_matrix_;
  }

  void apply_pan(const VRTViewChange &change) {
    const glm::dvec2 delta =
        screen_delta_to_ndc(change.pan_x_screen, change.pan_y_screen);

    view_matrix_ =
        glm::translate(glm::dmat4(1.0), glm::dvec3(delta, 0.0)) * view_matrix_;
  }

  VRTSurfaceMetrics metrics_{};
  glm::dmat4 view_matrix_{1.0};
};
