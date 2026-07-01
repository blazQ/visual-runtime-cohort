#include "renderer.h"
#include "visual_runtime/types.h"
#include "vulkan_context.hpp"
#include "vulkan_frame_resources.hpp"
#include "vulkan_pipeline.hpp"
#include "vulkan_utils.hpp"

#include <glm/mat4x4.hpp>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using visual_runtime::vulkan::check_vk;
using visual_runtime::vulkan::find_memory_type;
using visual_runtime::vulkan::invalid_queue_family;
using visual_runtime::vulkan::print_vk_error;
using visual_runtime::vulkan::VulkanContext;
using visual_runtime::vulkan::VulkanFrameResources;
using visual_runtime::vulkan::VulkanPipeline;
using visual_runtime::vulkan::VulkanPipelineConfig;

using DrawableDesc = Renderer::DrawableDesc;
using DrawableHandle = Renderer::DrawableHandle;
using DrawableState = Renderer::DrawableState;
using Vertex = Renderer::Vertex;

struct FrameUniforms {
  glm::mat4 matrix{1.0f};
};

struct DrawableUniforms {
  glm::mat4 model_transform{1.0f};
  glm::vec4 color{1.0f};
  Renderer::PrimitiveKind shape_kind = Renderer::PrimitiveKind::Rectangle;
};

struct Drawable {
  VkBuffer vertex_buffer = VK_NULL_HANDLE;
  VkDeviceMemory vertex_buffer_memory = VK_NULL_HANDLE;
  VkBuffer state_buffer = VK_NULL_HANDLE;
  VkDeviceMemory state_buffer_memory = VK_NULL_HANDLE;
  uint32_t vertex_count = 0;
};

struct DrawPush {
  uint32_t drawable_index = 0;
};

struct ActiveFrame {
  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  uint32_t image_index = 0;
  bool recreate_after_present = false;

  bool active() const { return command_buffer != VK_NULL_HANDLE; }
  void reset() {
    command_buffer = VK_NULL_HANDLE;
    swapchain = VK_NULL_HANDLE;
    image_index = 0;
    recreate_after_present = false;
  }
};

constexpr uint32_t kMaxDrawables = 1024;
constexpr uint32_t kFrameDescriptorBinding = 0;
constexpr uint32_t kDrawableDescriptorBinding = 1;

} // namespace

struct RendererBackend {
  bool init(VRTSurfaceDescriptor *surface);
  void resize(const VRTSurfaceMetrics *metrics);
  void set_frame_config(const FrameConfig &frame_config);
  DrawableHandle create_drawable(const DrawableDesc &desc);
  void update_drawable(DrawableHandle handle, const DrawableState &state);
  void destroy_drawable(DrawableHandle handle);
  bool begin_frame(float t);
  void draw(DrawableHandle handle);
  void end_frame();
  void render_frame(float t);
  void shutdown();

private:
  bool create_command_pool();
  bool create_sync_objects();
  bool create_frame_resources();
  bool create_render_finished_semaphores();
  bool frame_resources_ready() const;
  void recreate_frame_resources();
  bool build_uniforms();
  bool create_descriptor_layout();
  bool create_descriptor_pool();
  bool create_frame_descriptor();
  bool build_pipeline();
  bool create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags properties, VkBuffer &buffer,
                     VkDeviceMemory &memory);
  bool write_buffer(VkDeviceMemory memory, const void *contents,
                    VkDeviceSize size, const char *context);
  void write_drawable_descriptor(uint32_t drawable_index,
                                 const Drawable &drawable);
  void update_frame_uniforms(const FrameConfig &frame_config);
  Drawable *drawable_for(DrawableHandle handle);
  void destroy_render_finished_semaphores();
  void destroy_pipeline();
  void destroy_swapchain();
  void destroy_buffer(VkBuffer &buffer, VkDeviceMemory &memory);

  VulkanContext context_;
  VulkanFrameResources frame_resources_{};
  VkCommandPool command_pool_ = VK_NULL_HANDLE;
  VkSemaphore image_available_ = VK_NULL_HANDLE;
  std::vector<VkSemaphore> render_finished_semaphores_{};
  VkFence frame_in_flight_ = VK_NULL_HANDLE;
  VkBuffer frame_uniform_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory frame_uniform_buffer_memory_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
  VulkanPipeline pipeline_{};
  std::vector<Drawable> drawables_;
  ActiveFrame active_frame_{};
  uint32_t render_width_ = 0;
  uint32_t render_height_ = 0;
  FrameConfig frame_config_{};
};

Renderer::Renderer() = default;
Renderer::~Renderer() = default;
Renderer::Renderer(Renderer &&) noexcept = default;
Renderer &Renderer::operator=(Renderer &&) noexcept = default;

bool Renderer::init(VRTSurfaceDescriptor *surface) {
  if (!backend_) {
    backend_ = std::make_unique<RendererBackend>();
  }
  return backend_->init(surface);
}

void Renderer::resize(const VRTSurfaceMetrics *metrics) {
  if (backend_) {
    backend_->resize(metrics);
  }
}

void Renderer::set_frame_config(const FrameConfig &frame_config) {
  if (backend_) {
    backend_->set_frame_config(frame_config);
  }
}

Renderer::DrawableHandle Renderer::create_drawable(const DrawableDesc &desc) {
  return backend_ ? backend_->create_drawable(desc) : DrawableHandle{};
}

void Renderer::update_drawable(DrawableHandle handle,
                               const DrawableState &state) {
  if (backend_) {
    backend_->update_drawable(handle, state);
  }
}

void Renderer::destroy_drawable(DrawableHandle handle) {
  if (backend_) {
    backend_->destroy_drawable(handle);
  }
}

bool Renderer::begin_frame(float t) {
  return backend_ ? backend_->begin_frame(t) : false;
}

void Renderer::draw(DrawableHandle handle) {
  if (backend_) {
    backend_->draw(handle);
  }
}

void Renderer::end_frame() {
  if (backend_) {
    backend_->end_frame();
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

bool RendererBackend::init(VRTSurfaceDescriptor *surface) {
  shutdown();

  if (!context_.init(surface) || !create_command_pool() ||
      !create_sync_objects() || !create_descriptor_layout() ||
      !build_uniforms() || !create_descriptor_pool() ||
      !create_frame_descriptor()) {
    shutdown();
    return false;
  }

  resize(&surface->metrics);

  return true;
}

void RendererBackend::resize(const VRTSurfaceMetrics *metrics) {
  if (!metrics) {
    return;
  }

  const uint32_t width = metrics->pixel_width;
  const uint32_t height = metrics->pixel_height;
  if (render_width_ == width && render_height_ == height) {
    return;
  }

  render_width_ = width;
  render_height_ = height;
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

void RendererBackend::set_frame_config(const FrameConfig &frame_config) {
  frame_config_ = frame_config;
  update_frame_uniforms(frame_config_);
}

DrawableHandle RendererBackend::create_drawable(const DrawableDesc &desc) {
  if (context_.device() == VK_NULL_HANDLE || !desc.vertices ||
      desc.vertex_count == 0) {
    return {};
  }

  auto empty_slot = std::find_if(
      drawables_.begin(), drawables_.end(), [](const Drawable &stored) {
        return stored.vertex_buffer == VK_NULL_HANDLE;
      });
  const bool reusing_slot = empty_slot != drawables_.end();
  if (!reusing_slot && drawables_.size() >= kMaxDrawables) {
    return {};
  }

  const VkDeviceSize vertex_byte_count = desc.vertex_count * sizeof(Vertex);
  Drawable drawable{};
  if (!create_buffer(vertex_byte_count, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     drawable.vertex_buffer, drawable.vertex_buffer_memory) ||
      !write_buffer(drawable.vertex_buffer_memory, desc.vertices,
                    vertex_byte_count, "failed to map Vulkan vertex buffer") ||
      !create_buffer(sizeof(DrawableUniforms),
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     drawable.state_buffer, drawable.state_buffer_memory)) {
    destroy_buffer(drawable.vertex_buffer, drawable.vertex_buffer_memory);
    destroy_buffer(drawable.state_buffer, drawable.state_buffer_memory);
    return {};
  }
  drawable.vertex_count = static_cast<uint32_t>(desc.vertex_count);

  if (reusing_slot) {
    *empty_slot = drawable;
    const uint32_t index =
        static_cast<uint32_t>(empty_slot - drawables_.begin());
    write_drawable_descriptor(index, drawable);
    return DrawableHandle{index + 1};
  }

  drawables_.push_back(drawable);
  const uint32_t index = static_cast<uint32_t>(drawables_.size() - 1);
  write_drawable_descriptor(index, drawable);
  return DrawableHandle{index + 1};
}

void RendererBackend::update_drawable(DrawableHandle handle,
                                      const DrawableState &state) {
  Drawable *drawable = drawable_for(handle);
  if (!drawable || drawable->state_buffer_memory == VK_NULL_HANDLE) {
    return;
  }

  const DrawableUniforms uniforms{
      state.model_transform,
      state.color,
      state.kind,
  };
  write_buffer(drawable->state_buffer_memory, &uniforms, sizeof(uniforms),
               "failed to map Vulkan drawable state buffer");
}

void RendererBackend::destroy_drawable(DrawableHandle handle) {
  Drawable *drawable = drawable_for(handle);
  if (!drawable) {
    return;
  }

  destroy_buffer(drawable->vertex_buffer, drawable->vertex_buffer_memory);
  destroy_buffer(drawable->state_buffer, drawable->state_buffer_memory);
  drawable->vertex_count = 0;
}

bool RendererBackend::begin_frame(float t) {
  (void)t;
  if (render_width_ == 0 || render_height_ == 0 || !frame_resources_ready()) {
    return false;
  }

  if (!check_vk(vkWaitForFences(context_.device(), 1, &frame_in_flight_,
                                VK_TRUE, UINT64_MAX),
                "failed to wait for Vulkan frame fence")) {
    return false;
  }

  active_frame_.swapchain = frame_resources_.swapchain();
  VkResult acquire_result = vkAcquireNextImageKHR(
      context_.device(), active_frame_.swapchain, UINT64_MAX, image_available_,
      VK_NULL_HANDLE, &active_frame_.image_index);
  if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
    recreate_frame_resources();
    return false;
  }
  if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR) {
    print_vk_error("failed to acquire Vulkan swapchain image", acquire_result);
    return false;
  }
  active_frame_.recreate_after_present = acquire_result == VK_SUBOPTIMAL_KHR;

  const auto &command_buffers = frame_resources_.command_buffers();
  const auto &swapchain_images = frame_resources_.images();
  const auto &swapchain_image_views = frame_resources_.image_views();
  const VkExtent2D swapchain_extent = frame_resources_.extent();
  active_frame_.command_buffer = command_buffers[active_frame_.image_index];

  if (!check_vk(vkResetCommandBuffer(active_frame_.command_buffer, 0),
                "failed to reset Vulkan command buffer")) {
    active_frame_.reset();
    return false;
  }

  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  if (!check_vk(vkBeginCommandBuffer(active_frame_.command_buffer, &begin_info),
                "failed to begin Vulkan command buffer")) {
    active_frame_.reset();
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
  before_clear.image = swapchain_images[active_frame_.image_index];
  before_clear.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  before_clear.subresourceRange.baseMipLevel = 0;
  before_clear.subresourceRange.levelCount = 1;
  before_clear.subresourceRange.baseArrayLayer = 0;
  before_clear.subresourceRange.layerCount = 1;

  VkDependencyInfo before_dependency{};
  before_dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  before_dependency.imageMemoryBarrierCount = 1;
  before_dependency.pImageMemoryBarriers = &before_clear;
  vkCmdPipelineBarrier2(active_frame_.command_buffer, &before_dependency);

  VkRenderingAttachmentInfo color_attachment{};
  color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  color_attachment.imageView = swapchain_image_views[active_frame_.image_index];
  color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  const glm::vec4 &clear_color = frame_config_.clear_color;
  color_attachment.clearValue.color = {
      {clear_color.r, clear_color.g, clear_color.b, clear_color.a}};

  VkRenderingInfo rendering_info{};
  rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  rendering_info.renderArea.offset = {0, 0};
  rendering_info.renderArea.extent = swapchain_extent;
  rendering_info.layerCount = 1;
  rendering_info.colorAttachmentCount = 1;
  rendering_info.pColorAttachments = &color_attachment;

  vkCmdBeginRendering(active_frame_.command_buffer, &rendering_info);

  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = static_cast<float>(swapchain_extent.height);
  viewport.width = static_cast<float>(swapchain_extent.width);
  viewport.height = -static_cast<float>(swapchain_extent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(active_frame_.command_buffer, 0, 1, &viewport);

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = swapchain_extent;
  vkCmdSetScissor(active_frame_.command_buffer, 0, 1, &scissor);

  vkCmdBindPipeline(active_frame_.command_buffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.pipeline());
  vkCmdBindDescriptorSets(active_frame_.command_buffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.layout(),
                          0, 1, &descriptor_set_, 0, nullptr);

  return true;
}

void RendererBackend::draw(DrawableHandle handle) {
  Drawable *drawable = drawable_for(handle);
  if (!active_frame_.active() || !drawable ||
      drawable->vertex_buffer == VK_NULL_HANDLE ||
      drawable->vertex_count == 0) {
    return;
  }

  VkDeviceSize vertex_offset = 0;
  vkCmdBindVertexBuffers(active_frame_.command_buffer, 0, 1,
                         &drawable->vertex_buffer, &vertex_offset);
  const DrawPush push{handle.value - 1};
  vkCmdPushConstants(active_frame_.command_buffer, pipeline_.layout(),
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
  vkCmdDraw(active_frame_.command_buffer, drawable->vertex_count, 1, 0, 0);
}

void RendererBackend::end_frame() {
  if (!active_frame_.active()) {
    return;
  }

  vkCmdEndRendering(active_frame_.command_buffer);

  const auto &swapchain_images = frame_resources_.images();
  VkImageMemoryBarrier2 before_present{};
  before_present.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  before_present.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  before_present.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  before_present.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
  before_present.dstAccessMask = VK_ACCESS_2_NONE;
  before_present.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  before_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  before_present.image = swapchain_images[active_frame_.image_index];
  before_present.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  before_present.subresourceRange.baseMipLevel = 0;
  before_present.subresourceRange.levelCount = 1;
  before_present.subresourceRange.baseArrayLayer = 0;
  before_present.subresourceRange.layerCount = 1;

  VkDependencyInfo present_dependency{};
  present_dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  present_dependency.imageMemoryBarrierCount = 1;
  present_dependency.pImageMemoryBarriers = &before_present;
  vkCmdPipelineBarrier2(active_frame_.command_buffer, &present_dependency);

  if (!check_vk(vkEndCommandBuffer(active_frame_.command_buffer),
                "failed to record Vulkan command buffer")) {
    active_frame_.reset();
    return;
  }

  if (!check_vk(vkResetFences(context_.device(), 1, &frame_in_flight_),
                "failed to reset Vulkan frame fence")) {
    active_frame_.reset();
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
  submit_info.pCommandBuffers = &active_frame_.command_buffer;
  submit_info.signalSemaphoreCount = 1;
  VkSemaphore render_finished =
      render_finished_semaphores_[active_frame_.image_index];
  submit_info.pSignalSemaphores = &render_finished;

  if (!check_vk(vkQueueSubmit(context_.graphics_queue(), 1, &submit_info,
                              frame_in_flight_),
                "failed to submit Vulkan clear commands")) {
    active_frame_.reset();
    return;
  }

  VkPresentInfoKHR present_info{};
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = &render_finished;
  present_info.swapchainCount = 1;
  present_info.pSwapchains = &active_frame_.swapchain;
  present_info.pImageIndices = &active_frame_.image_index;

  VkResult present_result =
      vkQueuePresentKHR(context_.present_queue(), &present_info);
  if (present_result == VK_ERROR_OUT_OF_DATE_KHR ||
      present_result == VK_SUBOPTIMAL_KHR ||
      active_frame_.recreate_after_present) {
    recreate_frame_resources();
  } else if (present_result != VK_SUCCESS) {
    print_vk_error("failed to present Vulkan swapchain image", present_result);
  }

  active_frame_.reset();
}

void RendererBackend::render_frame(float t) {
  if (begin_frame(t)) {
    end_frame();
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
    for (auto &drawable : drawables_) {
      destroy_buffer(drawable.vertex_buffer, drawable.vertex_buffer_memory);
      destroy_buffer(drawable.state_buffer, drawable.state_buffer_memory);
      drawable.vertex_count = 0;
    }
    drawables_.clear();
    destroy_render_finished_semaphores();
    destroy_swapchain();
    if (frame_in_flight_ != VK_NULL_HANDLE) {
      vkDestroyFence(context_.device(), frame_in_flight_, nullptr);
      frame_in_flight_ = VK_NULL_HANDLE;
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
  active_frame_.reset();
  render_width_ = 0;
  render_height_ = 0;
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
         check_vk(vkCreateFence(context_.device(), &fence_info, nullptr,
                                &frame_in_flight_),
                  "failed to create Vulkan frame fence");
}

bool RendererBackend::create_frame_resources() {
  if (!frame_resources_.create_frame_resources(
          context_, command_pool_, render_width_, render_height_,
          [this] { return build_pipeline(); },
          [this] { destroy_pipeline(); })) {
    return false;
  }

  return create_render_finished_semaphores();
}

bool RendererBackend::create_render_finished_semaphores() {
  destroy_render_finished_semaphores();

  VkSemaphoreCreateInfo semaphore_info{};
  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  render_finished_semaphores_.resize(frame_resources_.images().size());
  for (VkSemaphore &semaphore : render_finished_semaphores_) {
    if (!check_vk(vkCreateSemaphore(context_.device(), &semaphore_info, nullptr,
                                    &semaphore),
                  "failed to create Vulkan render-finished semaphore")) {
      destroy_render_finished_semaphores();
      return false;
    }
  }

  return true;
}

bool RendererBackend::frame_resources_ready() const {
  return frame_resources_.frame_resources_ready() &&
         pipeline_.layout() != VK_NULL_HANDLE &&
         pipeline_.pipeline() != VK_NULL_HANDLE &&
         render_finished_semaphores_.size() == frame_resources_.images().size();
}

void RendererBackend::recreate_frame_resources() {
  if (context_.device() != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(context_.device());
  }
  destroy_render_finished_semaphores();
  frame_resources_.recreate_frame_resources(
      context_, command_pool_, render_width_, render_height_,
      [this] { return build_pipeline(); }, [this] { destroy_pipeline(); });
  if (frame_resources_.frame_resources_ready()) {
    create_render_finished_semaphores();
  }
}

bool RendererBackend::build_uniforms() {
  if (!create_buffer(sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     frame_uniform_buffer_, frame_uniform_buffer_memory_)) {
    return false;
  }

  return true;
}

bool RendererBackend::create_descriptor_layout() {
  std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
  bindings[0].binding = kFrameDescriptorBinding;
  bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  bindings[0].descriptorCount = 1;
  bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  bindings[1].binding = kDrawableDescriptorBinding;
  bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  bindings[1].descriptorCount = kMaxDrawables;
  bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  std::array<VkDescriptorBindingFlags, 2> binding_flags{};
  binding_flags[1] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                     VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

  VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info{};
  binding_flags_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
  binding_flags_info.bindingCount = static_cast<uint32_t>(binding_flags.size());
  binding_flags_info.pBindingFlags = binding_flags.data();

  VkDescriptorSetLayoutCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  create_info.pNext = &binding_flags_info;
  create_info.flags =
      VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
  create_info.bindingCount = static_cast<uint32_t>(bindings.size());
  create_info.pBindings = bindings.data();

  return check_vk(vkCreateDescriptorSetLayout(context_.device(), &create_info,
                                              nullptr, &descriptor_set_layout_),
                  "failed to create Vulkan descriptor set layout");
}

bool RendererBackend::create_descriptor_pool() {
  std::array<VkDescriptorPoolSize, 2> pool_sizes{};
  pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  pool_sizes[0].descriptorCount = 1;
  pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  pool_sizes[1].descriptorCount = kMaxDrawables;

  VkDescriptorPoolCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  create_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
  create_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
  create_info.pPoolSizes = pool_sizes.data();
  create_info.maxSets = 1;

  return check_vk(vkCreateDescriptorPool(context_.device(), &create_info,
                                         nullptr, &descriptor_pool_),
                  "failed to create Vulkan descriptor pool");
}

bool RendererBackend::create_frame_descriptor() {
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
  descriptor_write.dstBinding = kFrameDescriptorBinding;
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
  config.vertex_push_constant_size = sizeof(DrawPush);

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

bool RendererBackend::write_buffer(VkDeviceMemory memory, const void *contents,
                                   VkDeviceSize size, const char *context) {
  void *data = nullptr;
  if (!check_vk(vkMapMemory(context_.device(), memory, 0, size, 0, &data),
                context)) {
    return false;
  }
  std::memcpy(data, contents, static_cast<size_t>(size));
  vkUnmapMemory(context_.device(), memory);
  return true;
}

void RendererBackend::write_drawable_descriptor(uint32_t drawable_index,
                                                const Drawable &drawable) {
  if (descriptor_set_ == VK_NULL_HANDLE ||
      drawable.state_buffer == VK_NULL_HANDLE) {
    return;
  }

  VkDescriptorBufferInfo buffer_info{};
  buffer_info.buffer = drawable.state_buffer;
  buffer_info.offset = 0;
  buffer_info.range = sizeof(DrawableUniforms);

  VkWriteDescriptorSet descriptor_write{};
  descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptor_write.dstSet = descriptor_set_;
  descriptor_write.dstBinding = kDrawableDescriptorBinding;
  descriptor_write.dstArrayElement = drawable_index;
  descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  descriptor_write.descriptorCount = 1;
  descriptor_write.pBufferInfo = &buffer_info;

  vkUpdateDescriptorSets(context_.device(), 1, &descriptor_write, 0, nullptr);
}

void RendererBackend::update_frame_uniforms(const FrameConfig &frame_config) {
  if (frame_uniform_buffer_memory_ == VK_NULL_HANDLE) {
    return;
  }

  FrameUniforms uniforms{};
  uniforms.matrix = frame_config.view_proj_transform;

  write_buffer(frame_uniform_buffer_memory_, &uniforms, sizeof(uniforms),
               "failed to map Vulkan frame uniform buffer");
}

Drawable *RendererBackend::drawable_for(DrawableHandle handle) {
  if (handle.value == 0 || handle.value > drawables_.size()) {
    return nullptr;
  }
  return &drawables_[handle.value - 1];
}

void RendererBackend::destroy_render_finished_semaphores() {
  if (context_.device() == VK_NULL_HANDLE) {
    render_finished_semaphores_.clear();
    return;
  }

  for (VkSemaphore semaphore : render_finished_semaphores_) {
    if (semaphore != VK_NULL_HANDLE) {
      vkDestroySemaphore(context_.device(), semaphore, nullptr);
    }
  }
  render_finished_semaphores_.clear();
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
