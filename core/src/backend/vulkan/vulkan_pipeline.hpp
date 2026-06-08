#pragma once

#include "vulkan_context.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace visual_runtime::vulkan {

struct VulkanPipelineConfig {
  VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
  VkFormat color_format = VK_FORMAT_UNDEFINED;
  const char *vertex_shader_path = nullptr;
  const char *fragment_shader_path = nullptr;
  uint32_t vertex_stride = 0;
  uint32_t position_offset = 0;
  uint32_t vertex_push_constant_size = 0;
};

class VulkanPipeline {
public:
  bool create(const VulkanContext &context, const VulkanPipelineConfig &config);
  void destroy(const VulkanContext &context);

  VkPipelineLayout layout() const { return pipeline_layout_; }
  VkPipeline pipeline() const { return pipeline_; }

private:
  bool create_shader_module(const VulkanContext &context, const char *path,
                            VkShaderModule &shader_module);

  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace visual_runtime::vulkan
