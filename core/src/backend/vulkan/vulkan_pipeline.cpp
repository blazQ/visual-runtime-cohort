#include "vulkan_pipeline.hpp"

#include "vulkan_utils.hpp"

#include <array>
#include <vector>

namespace visual_runtime::vulkan {

bool VulkanPipeline::create(const VulkanContext &context,
                            const VulkanPipelineConfig &config) {
  destroy(context);

  VkShaderModule vertex_shader = VK_NULL_HANDLE;
  VkShaderModule fragment_shader = VK_NULL_HANDLE;
  if (!create_shader_module(context, config.vertex_shader_path,
                            vertex_shader) ||
      !create_shader_module(context, config.fragment_shader_path,
                            fragment_shader)) {
    if (vertex_shader != VK_NULL_HANDLE) {
      vkDestroyShaderModule(context.device(), vertex_shader, nullptr);
    }
    if (fragment_shader != VK_NULL_HANDLE) {
      vkDestroyShaderModule(context.device(), fragment_shader, nullptr);
    }
    return false;
  }

  VkPipelineShaderStageCreateInfo vertex_stage{};
  vertex_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertex_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertex_stage.module = vertex_shader;
  vertex_stage.pName = "main";

  VkPipelineShaderStageCreateInfo fragment_stage{};
  fragment_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragment_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragment_stage.module = fragment_shader;
  fragment_stage.pName = "main";

  std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{
      vertex_stage,
      fragment_stage,
  };

  VkVertexInputBindingDescription vertex_binding{};
  vertex_binding.binding = 0;
  vertex_binding.stride = config.vertex_stride;
  vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::array<VkVertexInputAttributeDescription, 2> vertex_attributes{};
  vertex_attributes[0].location = 0;
  vertex_attributes[0].binding = 0;
  vertex_attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
  vertex_attributes[0].offset = config.position_offset;
  vertex_attributes[1].location = 1;
  vertex_attributes[1].binding = 0;
  vertex_attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  vertex_attributes[1].offset = config.color_offset;

  VkPipelineVertexInputStateCreateInfo vertex_input{};
  vertex_input.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input.vertexBindingDescriptionCount = 1;
  vertex_input.pVertexBindingDescriptions = &vertex_binding;
  vertex_input.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(vertex_attributes.size());
  vertex_input.pVertexAttributeDescriptions = vertex_attributes.data();

  VkPipelineInputAssemblyStateCreateInfo input_assembly{};
  input_assembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewport_state{};
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizer.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachment{};
  color_blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo color_blending{};
  color_blending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blending.attachmentCount = 1;
  color_blending.pAttachments = &color_blend_attachment;

  std::array<VkDynamicState, 2> dynamic_states{
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
  };

  VkPipelineDynamicStateCreateInfo dynamic_state{};
  dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state.dynamicStateCount =
      static_cast<uint32_t>(dynamic_states.size());
  dynamic_state.pDynamicStates = dynamic_states.data();

  VkPipelineLayoutCreateInfo layout_info{};
  layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layout_info.setLayoutCount = 1;
  layout_info.pSetLayouts = &config.descriptor_set_layout;

  if (!check_vk(vkCreatePipelineLayout(context.device(), &layout_info, nullptr,
                                       &pipeline_layout_),
                "failed to create Vulkan pipeline layout")) {
    vkDestroyShaderModule(context.device(), vertex_shader, nullptr);
    vkDestroyShaderModule(context.device(), fragment_shader, nullptr);
    return false;
  }

  VkPipelineRenderingCreateInfo rendering_info{};
  rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  rendering_info.colorAttachmentCount = 1;
  rendering_info.pColorAttachmentFormats = &config.color_format;

  VkGraphicsPipelineCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.pNext = &rendering_info;
  pipeline_info.stageCount = static_cast<uint32_t>(shader_stages.size());
  pipeline_info.pStages = shader_stages.data();
  pipeline_info.pVertexInputState = &vertex_input;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &rasterizer;
  pipeline_info.pMultisampleState = &multisampling;
  pipeline_info.pColorBlendState = &color_blending;
  pipeline_info.pDynamicState = &dynamic_state;
  pipeline_info.layout = pipeline_layout_;

  const bool created =
      check_vk(vkCreateGraphicsPipelines(context.device(), VK_NULL_HANDLE, 1,
                                         &pipeline_info, nullptr, &pipeline_),
               "failed to create Vulkan graphics pipeline");
  if (!created) {
    destroy(context);
  }

  vkDestroyShaderModule(context.device(), vertex_shader, nullptr);
  vkDestroyShaderModule(context.device(), fragment_shader, nullptr);
  return created;
}

void VulkanPipeline::destroy(const VulkanContext &context) {
  if (pipeline_ != VK_NULL_HANDLE) {
    vkDestroyPipeline(context.device(), pipeline_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
  }
  if (pipeline_layout_ != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(context.device(), pipeline_layout_, nullptr);
    pipeline_layout_ = VK_NULL_HANDLE;
  }
}

bool VulkanPipeline::create_shader_module(const VulkanContext &context,
                                          const char *path,
                                          VkShaderModule &shader_module) {
  std::vector<char> bytes;
  if (!read_binary_file(path, bytes)) {
    return false;
  }

  VkShaderModuleCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  create_info.codeSize = bytes.size();
  create_info.pCode = reinterpret_cast<const uint32_t *>(bytes.data());

  return check_vk(vkCreateShaderModule(context.device(), &create_info, nullptr,
                                       &shader_module),
                  "failed to create Vulkan shader module");
}

} // namespace visual_runtime::vulkan
