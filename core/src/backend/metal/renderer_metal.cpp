#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include "renderer.h"

#include "Foundation/Foundation.hpp"
#include "Metal/Metal.hpp"
#include "QuartzCore/QuartzCore.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <glm/mat4x4.hpp>
#include <limits>
#include <vector>

namespace {

bool metrics_equal(const VRTSurfaceMetrics &lhs, const VRTSurfaceMetrics &rhs) {
  return lhs.pixel_width == rhs.pixel_width &&
         lhs.pixel_height == rhs.pixel_height &&
         lhs.screen_width == rhs.screen_width &&
         lhs.screen_height == rhs.screen_height;
}

struct FrameUniforms {
  glm::mat4 matrix{1.0f};
};

struct DrawableUniforms {
  glm::mat4 model_transform{1.0f};
  glm::vec4 color{1.0f};
};

struct Drawable {
  MTL::Buffer *vertex_buffer = nullptr;
  MTL::Buffer *state_buffer = nullptr;
  NS::UInteger vertex_count = 0;
};

void print_error(const char *context, NS::Error *error) {
  if (!error) {
    std::fprintf(stderr, "[renderer] %s\n", context);
    return;
  }

  NS::String *message = error->localizedDescription();
  std::fprintf(stderr, "[renderer] %s: %s\n", context,
               message ? message->utf8String() : "unknown error");
}

} // namespace

struct RendererBackend {
  bool init(VRTSurfaceDescriptor *surface);
  void resize(const VRTSurfaceMetrics *metrics);
  void set_frame_config(const FrameConfig &frame_config);
  renderer::DrawableHandle create_drawable(const renderer::DrawableDesc &desc);
  void update_drawable(renderer::DrawableHandle handle,
                       const renderer::DrawableState &state);
  void destroy_drawable(renderer::DrawableHandle handle);
  bool begin_frame(float t);
  void draw(renderer::DrawableHandle handle);
  void end_frame();
  void render_frame(float t);
  void shutdown();

private:
  bool build_pipeline();
  bool build_uniforms();
  void update_frame_uniforms(const FrameConfig &frame_config);
  Drawable *drawable_for(renderer::DrawableHandle handle);

  CA::MetalLayer *layer_ = nullptr;
  MTL::Device *device_ = nullptr;
  MTL::CommandQueue *queue_ = nullptr;
  MTL::Library *library_ = nullptr;
  MTL::RenderPipelineState *pipeline_ = nullptr;
  MTL::Buffer *frame_uniform_buffer_ = nullptr;
  NS::AutoreleasePool *frame_pool_ = nullptr;
  MTL::CommandBuffer *frame_command_ = nullptr;
  MTL::RenderCommandEncoder *frame_encoder_ = nullptr;
  CA::MetalDrawable *frame_drawable_ = nullptr;
  std::vector<Drawable> drawables_;
  VRTSurfaceMetrics metrics_{};
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

renderer::DrawableHandle
Renderer::create_drawable(const renderer::DrawableDesc &desc) {
  return backend_ ? backend_->create_drawable(desc)
                  : renderer::DrawableHandle{};
}

void Renderer::update_drawable(renderer::DrawableHandle handle,
                               const renderer::DrawableState &state) {
  if (backend_) {
    backend_->update_drawable(handle, state);
  }
}

void Renderer::destroy_drawable(renderer::DrawableHandle handle) {
  if (backend_) {
    backend_->destroy_drawable(handle);
  }
}

bool Renderer::begin_frame(float t) {
  return backend_ ? backend_->begin_frame(t) : false;
}

void Renderer::draw(renderer::DrawableHandle handle) {
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
  if (!surface || surface->kind != VRTSurfaceKind::MacOSMetalLayer ||
      surface->surface_handle == 0)
    return false;

  shutdown();

  layer_ = reinterpret_cast<CA::MetalLayer *>(surface->surface_handle);
  layer_->retain();

  device_ = MTL::CreateSystemDefaultDevice();
  if (!device_) {
    std::fprintf(stderr, "[renderer] failed to create Metal device\n");
    shutdown();
    return false;
  }

  queue_ = device_->newCommandQueue();
  if (!queue_) {
    std::fprintf(stderr, "[renderer] failed to create Metal command queue\n");
    shutdown();
    return false;
  }

  layer_->setDevice(device_);
  layer_->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
  layer_->setFramebufferOnly(true);

  if (!build_uniforms()) {
    shutdown();
    return false;
  }

  resize(&surface->metrics);

  if (!build_pipeline()) {
    shutdown();
    return false;
  }

  return true;
}

void RendererBackend::resize(const VRTSurfaceMetrics *metrics) {
  if (!metrics) {
    return;
  }
  if (metrics_equal(metrics_, *metrics)) {
    return;
  }

  metrics_ = *metrics;
}

void RendererBackend::set_frame_config(const FrameConfig &frame_config) {
  frame_config_ = frame_config;
  update_frame_uniforms(frame_config_);
}

renderer::DrawableHandle
RendererBackend::create_drawable(const renderer::DrawableDesc &desc) {
  if (!device_ || !desc.vertices || desc.vertex_count == 0) {
    return {};
  }

  const size_t byte_count = desc.vertex_count * sizeof(renderer::Vertex);
  MTL::Buffer *vertex_buffer = device_->newBuffer(
      desc.vertices, byte_count, MTL::ResourceStorageModeShared);
  if (!vertex_buffer) {
    std::fprintf(stderr,
                 "[renderer] failed to create drawable vertex buffer\n");
    return {};
  }

  MTL::Buffer *state_buffer = device_->newBuffer(
      sizeof(DrawableUniforms), MTL::ResourceStorageModeShared);
  if (!state_buffer) {
    vertex_buffer->release();
    std::fprintf(stderr, "[renderer] failed to create drawable state buffer\n");
    return {};
  }

  const Drawable drawable{
      vertex_buffer,
      state_buffer,
      static_cast<NS::UInteger>(desc.vertex_count),
  };
  auto empty_slot = std::find_if(
      drawables_.begin(), drawables_.end(),
      [](const Drawable &stored) { return stored.vertex_buffer == nullptr; });
  if (empty_slot != drawables_.end()) {
    *empty_slot = drawable;
    return renderer::DrawableHandle{
        static_cast<uint32_t>((empty_slot - drawables_.begin()) + 1),
    };
  }

  if (drawables_.size() >= std::numeric_limits<uint32_t>::max()) {
    vertex_buffer->release();
    state_buffer->release();
    return {};
  }

  drawables_.push_back(drawable);
  return renderer::DrawableHandle{
      static_cast<uint32_t>(drawables_.size()),
  };
}

void RendererBackend::update_drawable(renderer::DrawableHandle handle,
                                      const renderer::DrawableState &state) {
  Drawable *drawable = drawable_for(handle);
  if (!drawable || !drawable->state_buffer) {
    return;
  }

  // The renderer owns the upload cache; callers send a full instance snapshot.
  auto *contents =
      static_cast<DrawableUniforms *>(drawable->state_buffer->contents());
  contents->model_transform = state.model_transform;
  contents->color = state.color;
}

void RendererBackend::destroy_drawable(renderer::DrawableHandle handle) {
  Drawable *drawable = drawable_for(handle);
  if (!drawable) {
    return;
  }
  if (drawable->vertex_buffer) {
    drawable->vertex_buffer->release();
  }
  if (drawable->state_buffer) {
    drawable->state_buffer->release();
  }
  *drawable = {};
}

bool RendererBackend::begin_frame(float t) {
  if (!layer_ || !queue_ || !pipeline_ || !frame_uniform_buffer_) {
    return false;
  }

  (void)t;

  frame_pool_ = NS::AutoreleasePool::alloc()->init();

  CA::MetalDrawable *drawable = layer_->nextDrawable();
  if (!drawable) {
    frame_pool_->release();
    frame_pool_ = nullptr;
    return false;
  }
  frame_drawable_ = drawable;

  MTL::RenderPassDescriptor *pass =
      MTL::RenderPassDescriptor::renderPassDescriptor();
  auto *color = pass->colorAttachments()->object(0);
  color->setTexture(frame_drawable_->texture());
  color->setLoadAction(MTL::LoadActionClear);
  color->setStoreAction(MTL::StoreActionStore);
  const glm::vec4 &clear_color = frame_config_.clear_color;
  color->setClearColor(MTL::ClearColor(clear_color.r, clear_color.g,
                                       clear_color.b, clear_color.a));

  frame_command_ = queue_->commandBuffer();
  if (!frame_command_) {
    end_frame();
    return false;
  }

  frame_encoder_ = frame_command_->renderCommandEncoder(pass);
  if (!frame_encoder_) {
    end_frame();
    return false;
  }

  frame_encoder_->setViewport(MTL::Viewport{
      0.0,
      0.0,
      static_cast<double>(metrics_.pixel_width),
      static_cast<double>(metrics_.pixel_height),
      0.0,
      1.0,
  });
  frame_encoder_->setRenderPipelineState(pipeline_);
  frame_encoder_->setVertexBuffer(frame_uniform_buffer_, 0, 1);
  return true;
}

void RendererBackend::draw(renderer::DrawableHandle handle) {
  Drawable *drawable = drawable_for(handle);
  if (!frame_encoder_ || !drawable || !drawable->vertex_buffer ||
      drawable->vertex_count == 0) {
    return;
  }

  frame_encoder_->setVertexBuffer(drawable->vertex_buffer, 0, 0);
  frame_encoder_->setVertexBuffer(drawable->state_buffer, 0, 2);
  frame_encoder_->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0),
                                 drawable->vertex_count);
}

void RendererBackend::end_frame() {
  if (!frame_encoder_ || !frame_command_ || !frame_drawable_) {
    if (frame_pool_) {
      frame_pool_->release();
    }
    frame_pool_ = nullptr;
    frame_command_ = nullptr;
    frame_encoder_ = nullptr;
    frame_drawable_ = nullptr;
    return;
  }

  frame_encoder_->endEncoding();
  frame_command_->presentDrawable(frame_drawable_);
  frame_command_->commit();

  frame_pool_->release();
  frame_pool_ = nullptr;
  frame_command_ = nullptr;
  frame_encoder_ = nullptr;
  frame_drawable_ = nullptr;
}

void RendererBackend::render_frame(float t) {
  if (begin_frame(t)) {
    end_frame();
  }
}

bool RendererBackend::build_pipeline() {
  NS::Error *error = nullptr;
  NS::String *shader_path =
      NS::String::string(VRT_SHADER_LIB_PATH, NS::UTF8StringEncoding);
  library_ = device_->newLibrary(shader_path, &error);
  if (!library_) {
    print_error("failed to load Metal library " VRT_SHADER_LIB_PATH, error);
    return false;
  }

  MTL::Function *vertex_fn = library_->newFunction(MTLSTR("vertex_main"));
  MTL::Function *fragment_fn = library_->newFunction(MTLSTR("fragment_main"));
  if (!vertex_fn || !fragment_fn) {
    std::fprintf(stderr, "[renderer] failed to find vertex_main/fragment_main "
                         "in Metal library\n");
    if (vertex_fn)
      vertex_fn->release();
    if (fragment_fn)
      fragment_fn->release();
    return false;
  }

  MTL::RenderPipelineDescriptor *desc =
      MTL::RenderPipelineDescriptor::alloc()->init();
  desc->setVertexFunction(vertex_fn);
  desc->setFragmentFunction(fragment_fn);
  desc->colorAttachments()->object(0)->setPixelFormat(
      MTL::PixelFormatBGRA8Unorm);

  MTL::VertexDescriptor *vertex_desc = MTL::VertexDescriptor::alloc()->init();
  auto *position_attr = vertex_desc->attributes()->object(0);
  position_attr->setFormat(MTL::VertexFormatFloat2);
  position_attr->setOffset(offsetof(renderer::Vertex, position));
  position_attr->setBufferIndex(0);

  auto *vertex_layout = vertex_desc->layouts()->object(0);
  vertex_layout->setStride(sizeof(renderer::Vertex));
  vertex_layout->setStepFunction(MTL::VertexStepFunctionPerVertex);
  vertex_layout->setStepRate(1);

  desc->setVertexDescriptor(vertex_desc);

  pipeline_ = device_->newRenderPipelineState(desc, &error);

  vertex_desc->release();
  desc->release();
  vertex_fn->release();
  fragment_fn->release();

  if (!pipeline_) {
    print_error("failed to create render pipeline", error);
    return false;
  }

  return true;
}

bool RendererBackend::build_uniforms() {
  frame_uniform_buffer_ =
      device_->newBuffer(sizeof(FrameUniforms), MTL::ResourceStorageModeShared);
  if (!frame_uniform_buffer_) {
    std::fprintf(stderr, "[renderer] failed to create frame uniform buffer\n");
    return false;
  }

  return true;
}

void RendererBackend::update_frame_uniforms(const FrameConfig &frame_config) {
  if (!frame_uniform_buffer_) {
    return;
  }

  FrameUniforms uniforms{};
  uniforms.matrix = frame_config.view_proj_transform;

  auto *contents =
      static_cast<FrameUniforms *>(frame_uniform_buffer_->contents());
  *contents = uniforms;
}

Drawable *RendererBackend::drawable_for(renderer::DrawableHandle handle) {
  if (handle.value == 0 || handle.value > drawables_.size()) {
    return nullptr;
  }
  return &drawables_[handle.value - 1];
}

void RendererBackend::shutdown() {
  end_frame();
  if (frame_uniform_buffer_) {
    frame_uniform_buffer_->release();
    frame_uniform_buffer_ = nullptr;
  }
  for (auto &drawable : drawables_) {
    if (drawable.vertex_buffer) {
      drawable.vertex_buffer->release();
    }
    if (drawable.state_buffer) {
      drawable.state_buffer->release();
    }
  }
  drawables_.clear();
  if (pipeline_) {
    pipeline_->release();
    pipeline_ = nullptr;
  }
  if (library_) {
    library_->release();
    library_ = nullptr;
  }
  if (queue_) {
    queue_->release();
    queue_ = nullptr;
  }
  if (device_) {
    device_->release();
    device_ = nullptr;
  }
  if (layer_) {
    layer_->release();
    layer_ = nullptr;
  }
  metrics_ = {};
}
