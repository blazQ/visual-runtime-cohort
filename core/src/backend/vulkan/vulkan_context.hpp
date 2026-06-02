#pragma once

#include "renderer.h"
#include "vulkan_utils.hpp"

#include <vulkan/vulkan.h>

#include <vector>

namespace visual_runtime::vulkan {

struct QueueFamilies {
  uint32_t graphics = invalid_queue_family;
  uint32_t present = invalid_queue_family;
};

class VulkanContext {
public:
  bool init(VRTSurfaceDescriptor *surface);
  void shutdown();

  VkInstance instance() const { return instance_; }
  VkSurfaceKHR surface() const { return surface_; }
  VkPhysicalDevice physical_device() const { return physical_device_; }
  VkDevice device() const { return device_; }
  VkQueue graphics_queue() const { return graphics_queue_; }
  VkQueue present_queue() const { return present_queue_; }
  const QueueFamilies &queue_families() const { return queue_families_; }
  const std::vector<const char *> &device_extensions() const {
    return device_extensions_;
  }

private:
  bool create_instance(VRTSurfaceKind surface_kind);
  bool create_surface(const VRTSurfaceDescriptor &surface);
  bool pick_physical_device();
  bool physical_device_suitable(VkPhysicalDevice physical_device,
                                QueueFamilies &families,
                                std::vector<const char *> &device_extensions);
  bool create_device();

  VkInstance instance_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue graphics_queue_ = VK_NULL_HANDLE;
  VkQueue present_queue_ = VK_NULL_HANDLE;
  QueueFamilies queue_families_{};
  std::vector<const char *> device_extensions_{};
};

bool queue_families_complete(const QueueFamilies &families);

} // namespace visual_runtime::vulkan
