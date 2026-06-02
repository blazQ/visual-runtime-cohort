#pragma once

#include "frame_config.h"
#include "visual_runtime/api.h"

#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>

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
    glm::mat4 aspect_matrix{1.0f};

    if (metrics_.pixel_width > 0 && metrics_.pixel_height > 0) {
      const float aspect = static_cast<float>(metrics_.pixel_width) /
                           static_cast<float>(metrics_.pixel_height);

      if (aspect >= 1.0f) {
        aspect_matrix[0][0] = 1.0f / aspect;
      } else {
        aspect_matrix[1][1] = aspect;
      }
    }

    config.view_proj_transform = glm::mat4(view_matrix_) * aspect_matrix;
    return config;
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

  void apply_zoom(const VRTViewChange &change) {
    const double scale = std::exp(change.zoom_delta_log_scale);
    const double anchor_x =
        (2.0 * change.zoom_anchor_x_screen / metrics_.screen_width) - 1.0;
    const double anchor_y =
        1.0 - (2.0 * change.zoom_anchor_y_screen / metrics_.screen_height);

    view_matrix_ =
        glm::translate(glm::dmat4(1.0), glm::dvec3(anchor_x, anchor_y, 0.0)) *
        glm::scale(glm::dmat4(1.0), glm::dvec3(scale, scale, 1.0)) *
        glm::translate(glm::dmat4(1.0), glm::dvec3(-anchor_x, -anchor_y, 0.0)) *
        view_matrix_;
  }

  void apply_pan(const VRTViewChange &change) {
    const double ndc_x = 2.0 * change.pan_x_screen / metrics_.screen_width;
    const double ndc_y = -2.0 * change.pan_y_screen / metrics_.screen_height;

    view_matrix_ =
        glm::translate(glm::dmat4(1.0), glm::dvec3(ndc_x, ndc_y, 0.0)) *
        view_matrix_;
  }

  VRTSurfaceMetrics metrics_{};
  glm::dmat4 view_matrix_{1.0};
};
