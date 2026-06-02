#include "renderer.h"
#include "vulkan_context.hpp"
#include "vulkan_frame_resources.hpp"
#include "vulkan_pipeline.hpp"
#include "vulkan_utils.hpp"

#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vulkan/vulkan_core.h>

namespace {

using visual_runtime::vulkan::check_vk;
using visual_runtime::vulkan::find_memory_type;
using visual_runtime::vulkan::invalid_queue_family;
using visual_runtime::vulkan::print_vk_error;
using visual_runtime::vulkan::VulkanContext;
using visual_runtime::vulkan::VulkanFrameResources;
using visual_runtime::vulkan::VulkanPipeline;
using visual_runtime::vulkan::VulkanPipelineConfig;

struct Vertex {
  float position[2];
  float color[3];
};

struct FrameUniforms {
  glm::mat4 matrix{1.0f};
};

} // namespace

struct RendererBackend {
  bool init(SurfaceDescriptor *surface);
  void resize(uint32_t width, uint32_t height);
  void render_frame(float t);
  void shutdown();
  void set_background_color(float r, float g, float b);
  void set_view(float pan_x, float pan_y, float zoom);
  void draw_rect(float top_left_corner_x, float top_left_corner_y, float width, float height, float r, float g, float b);

private:
  bool create_command_pool();
  bool create_sync_objects();
  bool create_frame_resources();
  bool frame_resources_ready() const;
  void recreate_frame_resources();
  bool build_geometry();
  bool build_uniforms();
  bool create_descriptor_layout();
  bool create_descriptor_pool();
  bool create_descriptor_set();
  bool build_pipeline();
  bool create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags properties, VkBuffer &buffer,
                     VkDeviceMemory &memory);
  bool record_clear_commands(uint32_t image_index);
  void update_frame_uniforms();
  void destroy_pipeline();
  void destroy_swapchain();
  void destroy_buffer(VkBuffer &buffer, VkDeviceMemory &memory);

  VulkanContext context_;
  VulkanFrameResources frame_resources_{};
  VkCommandPool command_pool_ = VK_NULL_HANDLE;
  VkSemaphore image_available_ = VK_NULL_HANDLE;
  VkSemaphore render_finished_ = VK_NULL_HANDLE;
  VkFence frame_in_flight_ = VK_NULL_HANDLE;
  VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory vertex_buffer_memory_ = VK_NULL_HANDLE;
  uint32_t vertex_count_ = 0;
  VkBuffer frame_uniform_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory frame_uniform_buffer_memory_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
  VulkanPipeline pipeline_{};
  uint32_t render_width_ = 0;
  uint32_t render_height_ = 0;

  float bg_r_ = 0.0f;
  float bg_g_ = 0.0f;
  float bg_b_ = 0.0f;

  float pan_x_ = 0.0f;
  float pan_y_ = 0.0f;
  float zoom_ = 1.0f;

  static constexpr uint32_t MAX_RECTS = 128;
  Vertex staged_vertices_[MAX_RECTS * 6]; // 6 Vertices make a quad
  uint32_t staged_vertex_count_ = 0;
};

Renderer::Renderer() = default;
Renderer::~Renderer() = default;
Renderer::Renderer(Renderer &&) noexcept = default;
Renderer &Renderer::operator=(Renderer &&) noexcept = default;

bool Renderer::init(SurfaceDescriptor *surface) {
  if (!backend_) {
    backend_ = std::make_unique<RendererBackend>();
  }
  return backend_->init(surface);
}

void Renderer::resize(uint32_t width, uint32_t height) {
  if (backend_) {
    backend_->resize(width, height);
  }
}

void Renderer::render_frame(float t) {
  if (backend_) {
    backend_->render_frame(t);
  }
}

void Renderer::shutdown() {
  if (backend_) {
    backend_->shutdown();
    backend_.reset();
  }
}

void Renderer::set_background_color(float r, float g, float b) {
  if (backend_) {
    backend_->set_background_color(r, g, b);
  }
}

void Renderer::set_view(float pan_x, float pan_y, float zoom) {
  if (backend_) {
    backend_->set_view(pan_x, pan_y, zoom);
  }
}

void Renderer::draw_rect(float top_left_corner_x, float top_left_corner_y, float width, float height, float r, float g, float b){
  if (backend_) {
    backend_->draw_rect(top_left_corner_x, top_left_corner_y, width, height, r, g, b);
  }
}

bool RendererBackend::init(SurfaceDescriptor *surface) {
  shutdown();

  if (!context_.init(surface) || !create_command_pool() ||
      !create_sync_objects() || !create_descriptor_layout() ||
      !build_geometry() || !build_uniforms() || !create_descriptor_pool() ||
      !create_descriptor_set()) {
    shutdown();
    return false;
  }

  resize(surface->width, surface->height);

  return true;
}

void RendererBackend::resize(uint32_t width, uint32_t height) {
  if (render_width_ == width && render_height_ == height) {
    return;
  }

  render_width_ = width;
  render_height_ = height;
  update_frame_uniforms();

  if (context_.device() == VK_NULL_HANDLE || width == 0 || height == 0) {
    return;
  }

  if (!frame_resources_ready() || frame_resources_.extent().width != width ||
      frame_resources_.extent().height != height) {
    if (frame_resources_.swapchain() == VK_NULL_HANDLE) {
      create_frame_resources();
    } else {
      recreate_frame_resources();
    }
  }
}

void RendererBackend::render_frame(float t) {
  (void)t;
  if (render_width_ == 0 || render_height_ == 0 || !frame_resources_ready()) {
    return;
  }

  if (!check_vk(vkWaitForFences(context_.device(), 1, &frame_in_flight_,
                                VK_TRUE, UINT64_MAX),
                "failed to wait for Vulkan frame fence")) {
    return;
  }

  const VkDeviceSize upload_size = sizeof(Vertex) * staged_vertex_count_;
  if (upload_size > 0) {
    void *data = nullptr;
    vkMapMemory(context_.device(), vertex_buffer_memory_, 0, upload_size, 0, &data);
    std::memcpy(data, staged_vertices_, upload_size);
    vkUnmapMemory(context_.device(), vertex_buffer_memory_);
  }

  vertex_count_ = staged_vertex_count_;
  staged_vertex_count_ = 0; // every frame we reset and re-draw

  uint32_t image_index = 0;
  VkSwapchainKHR swapchain = frame_resources_.swapchain();
  VkResult acquire_result =
      vkAcquireNextImageKHR(context_.device(), swapchain, UINT64_MAX,
                            image_available_, VK_NULL_HANDLE, &image_index);
  if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
    recreate_frame_resources();
    return;
  }
  if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR) {
    print_vk_error("failed to acquire Vulkan swapchain image", acquire_result);
    return;
  }
  const bool should_recreate_after_present =
      acquire_result == VK_SUBOPTIMAL_KHR;

  if (!record_clear_commands(image_index)) {
    return;
  }
  if (!check_vk(vkResetFences(context_.device(), 1, &frame_in_flight_),
                "failed to reset Vulkan frame fence")) {
    return;
  }

  VkPipelineStageFlags wait_stage =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = &image_available_;
  submit_info.pWaitDstStageMask = &wait_stage;
  submit_info.commandBufferCount = 1;
  const auto &command_buffers = frame_resources_.command_buffers();
  submit_info.pCommandBuffers = &command_buffers[image_index];
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = &render_finished_;

  if (!check_vk(vkQueueSubmit(context_.graphics_queue(), 1, &submit_info,
                              frame_in_flight_),
                "failed to submit Vulkan clear commands")) {
    return;
  }

  VkPresentInfoKHR present_info{};
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = &render_finished_;
  present_info.swapchainCount = 1;
  present_info.pSwapchains = &swapchain;
  present_info.pImageIndices = &image_index;

  VkResult present_result =
      vkQueuePresentKHR(context_.present_queue(), &present_info);
  if (present_result == VK_ERROR_OUT_OF_DATE_KHR ||
      present_result == VK_SUBOPTIMAL_KHR || should_recreate_after_present) {
    recreate_frame_resources();
  } else if (present_result != VK_SUCCESS) {
    print_vk_error("failed to present Vulkan swapchain image", present_result);
  }
}

void RendererBackend::shutdown() {
  if (context_.device() != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(context_.device());
    destroy_pipeline();
    if (descriptor_pool_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(context_.device(), descriptor_pool_, nullptr);
      descriptor_pool_ = VK_NULL_HANDLE;
      descriptor_set_ = VK_NULL_HANDLE;
    }
    if (descriptor_set_layout_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(context_.device(), descriptor_set_layout_,
                                   nullptr);
      descriptor_set_layout_ = VK_NULL_HANDLE;
    }
    destroy_buffer(frame_uniform_buffer_, frame_uniform_buffer_memory_);
    destroy_buffer(vertex_buffer_, vertex_buffer_memory_);
    vertex_count_ = 0;
    destroy_swapchain();
    if (frame_in_flight_ != VK_NULL_HANDLE) {
      vkDestroyFence(context_.device(), frame_in_flight_, nullptr);
      frame_in_flight_ = VK_NULL_HANDLE;
    }
    if (render_finished_ != VK_NULL_HANDLE) {
      vkDestroySemaphore(context_.device(), render_finished_, nullptr);
      render_finished_ = VK_NULL_HANDLE;
    }
    if (image_available_ != VK_NULL_HANDLE) {
      vkDestroySemaphore(context_.device(), image_available_, nullptr);
      image_available_ = VK_NULL_HANDLE;
    }
    if (command_pool_ != VK_NULL_HANDLE) {
      vkDestroyCommandPool(context_.device(), command_pool_, nullptr);
      command_pool_ = VK_NULL_HANDLE;
    }
  }
  context_.shutdown();
  render_width_ = 0;
  render_height_ = 0;
}

void RendererBackend::set_background_color(float r, float g, float b) {
  bg_r_ = r;
  bg_g_ = g;
  bg_b_ = b;
}

void RendererBackend::set_view(float pan_x, float pan_y, float zoom) {
  pan_x_ = pan_x;
  pan_y_ = pan_y;
  zoom_ = zoom;
  update_frame_uniforms();
}

void RendererBackend::draw_rect(float x, float y, float w, float h,
                                float r, float g, float b) {
  if (staged_vertex_count_ + 6 > MAX_RECTS * 6) return;

  Vertex *v = &staged_vertices_[staged_vertex_count_];
  v[0] = {{x,     y    }, {r, g, b}};
  v[1] = {{x + w, y    }, {r, g, b}};
  v[2] = {{x + w, y - h}, {r, g, b}};
  v[3] = {{x,     y    }, {r, g, b}};
  v[4] = {{x + w, y - h}, {r, g, b}};
  v[5] = {{x,     y - h}, {r, g, b}};
  staged_vertex_count_ += 6;
}


bool RendererBackend::create_command_pool() {
  VkCommandPoolCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  create_info.queueFamilyIndex = context_.queue_families().graphics;

  return check_vk(vkCreateCommandPool(context_.device(), &create_info, nullptr,
                                      &command_pool_),
                  "failed to create Vulkan command pool");
}

bool RendererBackend::create_sync_objects() {
  VkSemaphoreCreateInfo semaphore_info{};
  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fence_info{};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  return check_vk(vkCreateSemaphore(context_.device(), &semaphore_info, nullptr,
                                    &image_available_),
                  "failed to create Vulkan image-available semaphore") &&
         check_vk(vkCreateSemaphore(context_.device(), &semaphore_info, nullptr,
                                    &render_finished_),
                  "failed to create Vulkan render-finished semaphore") &&
         check_vk(vkCreateFence(context_.device(), &fence_info, nullptr,
                                &frame_in_flight_),
                  "failed to create Vulkan frame fence");
}

bool RendererBackend::create_frame_resources() {
  return frame_resources_.create_frame_resources(
      context_, command_pool_, render_width_, render_height_,
      [this] { return build_pipeline(); }, [this] { destroy_pipeline(); });
}

bool RendererBackend::frame_resources_ready() const {
  return frame_resources_.frame_resources_ready() &&
         pipeline_.layout() != VK_NULL_HANDLE &&
         pipeline_.pipeline() != VK_NULL_HANDLE;
}

void RendererBackend::recreate_frame_resources() {
  frame_resources_.recreate_frame_resources(
      context_, command_pool_, render_width_, render_height_,
      [this] { return build_pipeline(); }, [this] { destroy_pipeline(); });
}

bool RendererBackend::build_geometry() {
  const VkDeviceSize buffer_size = sizeof(Vertex) * MAX_RECTS * 6; // Pre-allocate based on max number of vertices

  return create_buffer(buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     vertex_buffer_, vertex_buffer_memory_);
}

bool RendererBackend::build_uniforms() {
  if (!create_buffer(sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     frame_uniform_buffer_, frame_uniform_buffer_memory_)) {
    return false;
  }

  update_frame_uniforms();
  return true;
}

bool RendererBackend::create_descriptor_layout() {
  VkDescriptorSetLayoutBinding frame_uniform_binding{};
  frame_uniform_binding.binding = 0;
  frame_uniform_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  frame_uniform_binding.descriptorCount = 1;
  frame_uniform_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  VkDescriptorSetLayoutCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  create_info.bindingCount = 1;
  create_info.pBindings = &frame_uniform_binding;

  return check_vk(vkCreateDescriptorSetLayout(context_.device(), &create_info,
                                              nullptr, &descriptor_set_layout_),
                  "failed to create Vulkan descriptor set layout");
}

bool RendererBackend::create_descriptor_pool() {
  VkDescriptorPoolSize pool_size{};
  pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  pool_size.descriptorCount = 1;

  VkDescriptorPoolCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  create_info.poolSizeCount = 1;
  create_info.pPoolSizes = &pool_size;
  create_info.maxSets = 1;

  return check_vk(vkCreateDescriptorPool(context_.device(), &create_info,
                                         nullptr, &descriptor_pool_),
                  "failed to create Vulkan descriptor pool");
}

bool RendererBackend::create_descriptor_set() {
  VkDescriptorSetAllocateInfo allocate_info{};
  allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocate_info.descriptorPool = descriptor_pool_;
  allocate_info.descriptorSetCount = 1;
  allocate_info.pSetLayouts = &descriptor_set_layout_;

  if (!check_vk(vkAllocateDescriptorSets(context_.device(), &allocate_info,
                                         &descriptor_set_),
                "failed to allocate Vulkan descriptor set")) {
    return false;
  }

  VkDescriptorBufferInfo buffer_info{};
  buffer_info.buffer = frame_uniform_buffer_;
  buffer_info.offset = 0;
  buffer_info.range = sizeof(FrameUniforms);

  VkWriteDescriptorSet descriptor_write{};
  descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptor_write.dstSet = descriptor_set_;
  descriptor_write.dstBinding = 0;
  descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptor_write.descriptorCount = 1;
  descriptor_write.pBufferInfo = &buffer_info;

  vkUpdateDescriptorSets(context_.device(), 1, &descriptor_write, 0, nullptr);
  return true;
}

bool RendererBackend::build_pipeline() {
  VulkanPipelineConfig config{};
  config.descriptor_set_layout = descriptor_set_layout_;
  config.color_format = frame_resources_.format();
  config.vertex_shader_path = VRT_VULKAN_VERTEX_SPV_PATH;
  config.fragment_shader_path = VRT_VULKAN_FRAGMENT_SPV_PATH;
  config.vertex_stride = sizeof(Vertex);
  config.position_offset = offsetof(Vertex, position);
  config.color_offset = offsetof(Vertex, color);

  return pipeline_.create(context_, config);
}

bool RendererBackend::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                    VkMemoryPropertyFlags properties,
                                    VkBuffer &buffer, VkDeviceMemory &memory) {
  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = size;
  buffer_info.usage = usage;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (!check_vk(
          vkCreateBuffer(context_.device(), &buffer_info, nullptr, &buffer),
          "failed to create Vulkan buffer")) {
    return false;
  }

  VkMemoryRequirements memory_requirements{};
  vkGetBufferMemoryRequirements(context_.device(), buffer,
                                &memory_requirements);

  const uint32_t memory_type =
      find_memory_type(context_.physical_device(),
                       memory_requirements.memoryTypeBits, properties);
  if (memory_type == invalid_queue_family) {
    std::fprintf(stderr,
                 "[renderer] failed to find suitable Vulkan buffer memory\n");
    return false;
  }

  VkMemoryAllocateInfo allocate_info{};
  allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocate_info.allocationSize = memory_requirements.size;
  allocate_info.memoryTypeIndex = memory_type;

  if (!check_vk(
          vkAllocateMemory(context_.device(), &allocate_info, nullptr, &memory),
          "failed to allocate Vulkan buffer memory")) {
    return false;
  }

  return check_vk(vkBindBufferMemory(context_.device(), buffer, memory, 0),
                  "failed to bind Vulkan buffer memory");
}

bool RendererBackend::record_clear_commands(uint32_t image_index) {
  const auto &command_buffers = frame_resources_.command_buffers();
  const auto &swapchain_images = frame_resources_.images();
  const auto &swapchain_image_views = frame_resources_.image_views();
  const VkExtent2D swapchain_extent = frame_resources_.extent();
  VkCommandBuffer command_buffer = command_buffers[image_index];

  if (!check_vk(vkResetCommandBuffer(command_buffer, 0),
                "failed to reset Vulkan command buffer")) {
    return false;
  }

  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  if (!check_vk(vkBeginCommandBuffer(command_buffer, &begin_info),
                "failed to begin Vulkan command buffer")) {
    return false;
  }

  VkImageMemoryBarrier2 before_clear{};
  before_clear.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  before_clear.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
  before_clear.srcAccessMask = VK_ACCESS_2_NONE;
  before_clear.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  before_clear.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  before_clear.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  before_clear.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  before_clear.image = swapchain_images[image_index];
  before_clear.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  before_clear.subresourceRange.baseMipLevel = 0;
  before_clear.subresourceRange.levelCount = 1;
  before_clear.subresourceRange.baseArrayLayer = 0;
  before_clear.subresourceRange.layerCount = 1;

  VkDependencyInfo before_dependency{};
  before_dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  before_dependency.imageMemoryBarrierCount = 1;
  before_dependency.pImageMemoryBarriers = &before_clear;
  vkCmdPipelineBarrier2(command_buffer, &before_dependency);

  VkRenderingAttachmentInfo color_attachment{};
  color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  color_attachment.imageView = swapchain_image_views[image_index];
  color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.clearValue.color = {{bg_r_, bg_g_, bg_b_, 1.0f}};

  VkRenderingInfo rendering_info{};
  rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  rendering_info.renderArea.offset = {0, 0};
  rendering_info.renderArea.extent = swapchain_extent;
  rendering_info.layerCount = 1;
  rendering_info.colorAttachmentCount = 1;
  rendering_info.pColorAttachments = &color_attachment;

  vkCmdBeginRendering(command_buffer, &rendering_info);

  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(swapchain_extent.width);
  viewport.height = static_cast<float>(swapchain_extent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(command_buffer, 0, 1, &viewport);

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = swapchain_extent;
  vkCmdSetScissor(command_buffer, 0, 1, &scissor);

  VkDeviceSize vertex_offset = 0;
  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline_.pipeline());
  vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline_.layout(), 0, 1, &descriptor_set_, 0,
                          nullptr);
  vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer_, &vertex_offset);
  vkCmdDraw(command_buffer, vertex_count_, 1, 0, 0);

  vkCmdEndRendering(command_buffer);

  VkImageMemoryBarrier2 before_present{};
  before_present.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  before_present.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  before_present.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  before_present.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
  before_present.dstAccessMask = VK_ACCESS_2_NONE;
  before_present.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  before_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  before_present.image = swapchain_images[image_index];
  before_present.subresourceRange = before_clear.subresourceRange;

  VkDependencyInfo present_dependency{};
  present_dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  present_dependency.imageMemoryBarrierCount = 1;
  present_dependency.pImageMemoryBarriers = &before_present;
  vkCmdPipelineBarrier2(command_buffer, &present_dependency);

  return check_vk(vkEndCommandBuffer(command_buffer),
                  "failed to record Vulkan command buffer");
}

void RendererBackend::update_frame_uniforms() {
  if (frame_uniform_buffer_memory_ == VK_NULL_HANDLE) {
    return;
  }

  FrameUniforms uniforms{};

  if (render_width_ > 0 && render_height_ > 0) {
    const float aspect =
        static_cast<float>(render_width_) / static_cast<float>(render_height_);

    // Aspect correction + Vulkan y-flip (NDC y points down)
    const float sx = (aspect >= 1.0f) ? (1.0f / aspect) : 1.0f;
    const float sy = (aspect >= 1.0f) ? -1.0f : -aspect;

    uniforms.matrix = glm::scale(glm::mat4{1.0f}, glm::vec3(sx, sy, 1.0f));
    uniforms.matrix = glm::scale(uniforms.matrix, glm::vec3(zoom_, zoom_, 1.0f));
    uniforms.matrix = glm::translate(uniforms.matrix, glm::vec3(-pan_x_, -pan_y_, 0.0f));
  }

  void *data = nullptr;
  if (!check_vk(vkMapMemory(context_.device(), frame_uniform_buffer_memory_, 0,
                            sizeof(FrameUniforms), 0, &data),
                "failed to map Vulkan frame uniform buffer")) {
    return;
  }
  std::memcpy(data, &uniforms, sizeof(uniforms));
  vkUnmapMemory(context_.device(), frame_uniform_buffer_memory_);
}


void RendererBackend::destroy_pipeline() { pipeline_.destroy(context_); }

void RendererBackend::destroy_swapchain() {
  frame_resources_.destroy_swapchain(context_, command_pool_);
}

void RendererBackend::destroy_buffer(VkBuffer &buffer, VkDeviceMemory &memory) {
  if (buffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(context_.device(), buffer, nullptr);
    buffer = VK_NULL_HANDLE;
  }
  if (memory != VK_NULL_HANDLE) {
    vkFreeMemory(context_.device(), memory, nullptr);
    memory = VK_NULL_HANDLE;
  }
}
