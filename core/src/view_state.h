#pragma once

#include "frame_config.h"
#include "visual_runtime/api.h"

#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

class ViewState {
public:
  bool resize(const VRTSurfaceMetrics &metrics) {
    if (metrics_equal(metrics_, metrics)) {
      return false;
    }

    metrics_ = metrics;
    update_projection_matrices();
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

    const glm::dvec2 world_position =
        center_world_ + screen_point_to_world_offset(screen);

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

  double current_scale() const { return std::exp(log_scale_); }

  glm::dmat4 view_projection_matrix() const {
    return projection_matrix_ * view_matrix();
  }

  glm::dmat4 view_matrix() const {
    const double scale = current_scale();

    return glm::scale(glm::dmat4(1.0), glm::dvec3(scale, scale, 1.0)) *
           glm::translate(glm::dmat4(1.0), glm::dvec3(-center_world_, 0.0));
  }

  void apply_zoom(const VRTViewChange &change) {
    const VRTScreenPoint anchor_screen{
        change.zoom_anchor_x_screen,
        change.zoom_anchor_y_screen,
    };
    const glm::dvec2 anchor_world =
        center_world_ + screen_point_to_world_offset(anchor_screen);

    log_scale_ += change.zoom_delta_log_scale;
    center_world_ = anchor_world - screen_point_to_world_offset(anchor_screen);
  }

  void apply_pan(const VRTViewChange &change) {
    center_world_ -=
        screen_delta_to_world_offset(change.pan_x_screen, change.pan_y_screen);
  }

  glm::dvec2 screen_point_to_world_offset(const VRTScreenPoint &screen) const {
    return projection_inverse_transform(screen_point_to_ndc(screen)) /
           current_scale();
  }

  glm::dvec2 screen_delta_to_world_offset(double x_screen,
                                          double y_screen) const {
    return projection_inverse_transform(
               screen_delta_to_ndc(x_screen, y_screen)) /
           current_scale();
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

  glm::dvec2 projection_inverse_transform(const glm::dvec2 &ndc) const {
    const glm::dvec4 transformed =
        projection_inverse_matrix_ * glm::dvec4{ndc.x, ndc.y, 0.0, 1.0};
    return glm::dvec2{transformed.x, transformed.y};
  }

  void update_projection_matrices() {
    projection_matrix_ = glm::dmat4{1.0};
    projection_inverse_matrix_ = glm::dmat4{1.0};

    if (metrics_.pixel_width == 0 || metrics_.pixel_height == 0) {
      return;
    }

    const double aspect = static_cast<double>(metrics_.pixel_width) /
                          static_cast<double>(metrics_.pixel_height);
    if (aspect >= 1.0) {
      projection_matrix_[0][0] = 1.0 / aspect;
      projection_inverse_matrix_[0][0] = aspect;
      return;
    }

    projection_matrix_[1][1] = aspect;
    projection_inverse_matrix_[1][1] = 1.0 / aspect;
  }

private:
  VRTSurfaceMetrics metrics_{};
  glm::dmat4 projection_matrix_{1.0};
  glm::dmat4 projection_inverse_matrix_{1.0};
  glm::dvec2 center_world_{0.0, 0.0};
  double log_scale_ = 0.0;
};
