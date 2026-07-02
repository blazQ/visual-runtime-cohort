#include "vulkan_context.hpp"

#include "vulkan_swapchain.hpp"

#if defined(__APPLE__)
#include <vulkan/vulkan_metal.h>
#endif
#if defined(__linux__)
#include <wayland-client.h>
#include <xcb/xcb.h>
#include <vulkan/vulkan_wayland.h>
#include <vulkan/vulkan_xcb.h>
#endif

#include <cstdio>
#include <cstring>
#include <utility>

namespace visual_runtime::vulkan {

namespace {

constexpr const char *kPortabilitySubsetExtensionName =
    "VK_KHR_portability_subset";

bool descriptor_indexing_features_supported(
    const VkPhysicalDeviceFeatures2 &features,
    const VkPhysicalDeviceDescriptorIndexingFeatures
        &descriptor_indexing_features) {
  return features.features.shaderStorageBufferArrayDynamicIndexing &&
         descriptor_indexing_features.runtimeDescriptorArray &&
         descriptor_indexing_features
             .shaderStorageBufferArrayNonUniformIndexing &&
         descriptor_indexing_features.descriptorBindingPartiallyBound &&
         descriptor_indexing_features
             .descriptorBindingStorageBufferUpdateAfterBind && 
             descriptor_indexing_features.shaderSampledImageArrayNonUniformIndexing && 
             descriptor_indexing_features.descriptorBindingSampledImageUpdateAfterBind;
}

QueueFamilies find_queue_families(VkPhysicalDevice physical_device,
                                  VkSurfaceKHR surface) {
  QueueFamilies families{};

  uint32_t family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count,
                                           nullptr);

  std::vector<VkQueueFamilyProperties> family_properties(family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count,
                                           family_properties.data());

  for (uint32_t i = 0; i < family_count; ++i) {
    const auto &family = family_properties[i];
    if (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      families.graphics = i;
    }

    VkBool32 present_supported = VK_FALSE;
    if (check_vk(vkGetPhysicalDeviceSurfaceSupportKHR(
                     physical_device, i, surface, &present_supported),
                 "failed to query Vulkan present support") &&
        present_supported) {
      families.present = i;
    }

    if (queue_families_complete(families)) {
      break;
    }
  }

  return families;
}

} // namespace

bool queue_families_complete(const QueueFamilies &families) {
  return families.graphics != invalid_queue_family &&
         families.present != invalid_queue_family;
}

bool VulkanContext::init(VRTSurfaceDescriptor *surface) {
  if (!surface || surface->kind == VRTSurfaceKind::None ||
      surface->surface_handle == 0) {
    return false;
  }
  if ((surface->kind == VRTSurfaceKind::LinuxXcbWindow ||
       surface->kind == VRTSurfaceKind::LinuxWaylandSurface) &&
      !surface->display_handle) {
    return false;
  }

  shutdown();

  if (!create_instance(surface->kind) || !create_surface(*surface) ||
      !pick_physical_device() || !create_device()) {
    shutdown();
    return false;
  }

  std::fprintf(stderr, "[renderer] Vulkan device and surface initialized\n");
  return true;
}

void VulkanContext::shutdown() {
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
  }
  graphics_queue_ = VK_NULL_HANDLE;
  present_queue_ = VK_NULL_HANDLE;
  physical_device_ = VK_NULL_HANDLE;
  queue_families_ = {};
  device_extensions_.clear();
  if (surface_ != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
  }
  if (instance_ != VK_NULL_HANDLE) {
    vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
  }
}

bool VulkanContext::create_instance(VRTSurfaceKind surface_kind) {
  std::vector<VkExtensionProperties> available_extensions;
  if (!enumerate_instance_extensions(available_extensions)) {
    return false;
  }

  std::vector<const char *> extensions{VK_KHR_SURFACE_EXTENSION_NAME};
#if defined(__APPLE__)
  if (surface_kind == VRTSurfaceKind::MacOSMetalLayer) {
    extensions.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
  }
#if defined(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)
  bool enable_portability_enumeration = extension_available(
      available_extensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
  if (enable_portability_enumeration) {
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
  }
#else
  bool enable_portability_enumeration = false;
#endif
#else
  bool enable_portability_enumeration = false;
#endif
#if defined(__linux__)
  if (surface_kind == VRTSurfaceKind::LinuxXcbWindow) {
    extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
  }
  if (surface_kind == VRTSurfaceKind::LinuxWaylandSurface) {
    extensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
  }
#endif

  if (!required_extensions_available(available_extensions, extensions.data(),
                                     static_cast<uint32_t>(extensions.size()),
                                     "instance")) {
    return false;
  }

  VkApplicationInfo app_info{};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = "Visual Runtime Cohort";
  app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  app_info.pEngineName = "Visual Runtime";
  app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
  app_info.apiVersion = VK_API_VERSION_1_3;

  VkInstanceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create_info.pApplicationInfo = &app_info;
  create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  create_info.ppEnabledExtensionNames = extensions.data();
  if (enable_portability_enumeration) {
    create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }

  return check_vk(vkCreateInstance(&create_info, nullptr, &instance_),
                  "failed to create Vulkan instance");
}

bool VulkanContext::create_surface(const VRTSurfaceDescriptor &surface) {
#if defined(__APPLE__)
  if (surface.kind == VRTSurfaceKind::MacOSMetalLayer) {
    auto create_metal_surface = reinterpret_cast<PFN_vkCreateMetalSurfaceEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateMetalSurfaceEXT"));
    if (!create_metal_surface) {
      std::fprintf(stderr,
                   "[renderer] failed to load vkCreateMetalSurfaceEXT\n");
      return false;
    }

    VkMetalSurfaceCreateInfoEXT create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    create_info.pLayer = reinterpret_cast<void *>(surface.surface_handle);

    return check_vk(
        create_metal_surface(instance_, &create_info, nullptr, &surface_),
        "failed to create Vulkan Metal surface");
  }
#endif
#if defined(__linux__)
  if (surface.kind == VRTSurfaceKind::LinuxXcbWindow) {
    auto create_xcb_surface = reinterpret_cast<PFN_vkCreateXcbSurfaceKHR>(
        vkGetInstanceProcAddr(instance_, "vkCreateXcbSurfaceKHR"));
    if (!create_xcb_surface) {
      std::fprintf(stderr, "[renderer] failed to load vkCreateXcbSurfaceKHR\n");
      return false;
    }

    VkXcbSurfaceCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    create_info.connection =
        reinterpret_cast<xcb_connection_t *>(surface.display_handle);
    create_info.window = static_cast<xcb_window_t>(surface.surface_handle);

    return check_vk(
        create_xcb_surface(instance_, &create_info, nullptr, &surface_),
        "failed to create Vulkan XCB surface");
  }
  if (surface.kind == VRTSurfaceKind::LinuxWaylandSurface) {
    auto create_wayland_surface =
        reinterpret_cast<PFN_vkCreateWaylandSurfaceKHR>(
            vkGetInstanceProcAddr(instance_, "vkCreateWaylandSurfaceKHR"));
    if (!create_wayland_surface) {
      std::fprintf(stderr,
                   "[renderer] failed to load vkCreateWaylandSurfaceKHR\n");
      return false;
    }

    VkWaylandSurfaceCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    create_info.display =
        reinterpret_cast<wl_display *>(surface.display_handle);
    create_info.surface =
        reinterpret_cast<wl_surface *>(surface.surface_handle);

    return check_vk(
        create_wayland_surface(instance_, &create_info, nullptr, &surface_),
        "failed to create Vulkan Wayland surface");
  }
#else
  (void)surface;
#endif
  std::fprintf(stderr,
               "[renderer] Vulkan surface creation is not implemented for this "
               "surface kind on this platform\n");
  return false;
}

bool VulkanContext::pick_physical_device() {
  uint32_t device_count = 0;
  if (!check_vk(vkEnumeratePhysicalDevices(instance_, &device_count, nullptr),
                "failed to count Vulkan physical devices")) {
    return false;
  }
  if (device_count == 0) {
    std::fprintf(stderr, "[renderer] no Vulkan physical devices found\n");
    return false;
  }

  std::vector<VkPhysicalDevice> devices(device_count);
  if (!check_vk(
          vkEnumeratePhysicalDevices(instance_, &device_count, devices.data()),
          "failed to enumerate Vulkan physical devices")) {
    return false;
  }

  for (VkPhysicalDevice device : devices) {
    QueueFamilies families{};
    std::vector<const char *> extensions;
    if (physical_device_suitable(device, families, extensions)) {
      physical_device_ = device;
      queue_families_ = families;
      device_extensions_ = std::move(extensions);

      VkPhysicalDeviceProperties properties{};
      vkGetPhysicalDeviceProperties(physical_device_, &properties);
      std::fprintf(stderr, "[renderer] selected Vulkan device: %s\n",
                   properties.deviceName);
      return true;
    }
  }

  std::fprintf(stderr, "[renderer] no suitable Vulkan physical device found\n");
  return false;
}

bool VulkanContext::physical_device_suitable(
    VkPhysicalDevice physical_device, QueueFamilies &families,
    std::vector<const char *> &device_extensions) {
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(physical_device, &properties);
  if (properties.apiVersion < VK_API_VERSION_1_3) {
    std::fprintf(stderr,
                 "[renderer] rejecting Vulkan device %s: Vulkan 1.3 is "
                 "required\n",
                 properties.deviceName);
    return false;
  }

  VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features{};
  descriptor_indexing_features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;

  VkPhysicalDeviceVulkan13Features vulkan13_features{};
  vulkan13_features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  vulkan13_features.pNext = &descriptor_indexing_features;

  VkPhysicalDeviceFeatures2 features{};
  features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  features.pNext = &vulkan13_features;
  vkGetPhysicalDeviceFeatures2(physical_device, &features);
  if (!vulkan13_features.dynamicRendering ||
      !vulkan13_features.synchronization2 ||
      !descriptor_indexing_features_supported(features,
                                              descriptor_indexing_features)) {
    std::fprintf(stderr,
                 "[renderer] rejecting Vulkan device %s: dynamic rendering, "
                 "synchronization2, and descriptor indexing are required\n",
                 properties.deviceName);
    return false;
  }

  std::vector<VkExtensionProperties> available_extensions;
  if (!enumerate_device_extensions(physical_device, available_extensions)) {
    return false;
  }

  device_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  if (extension_available(available_extensions,
                          kPortabilitySubsetExtensionName)) {
    device_extensions.push_back(kPortabilitySubsetExtensionName);
  }

  if (!required_extensions_available(
          available_extensions, device_extensions.data(),
          static_cast<uint32_t>(device_extensions.size()), "device")) {
    std::fprintf(stderr,
                 "[renderer] rejecting Vulkan device %s: required device "
                 "extensions are missing\n",
                 properties.deviceName);
    return false;
  }

  families = find_queue_families(physical_device, surface_);
  if (!queue_families_complete(families)) {
    std::fprintf(stderr,
                 "[renderer] rejecting Vulkan device %s: graphics and present "
                 "queues are required\n",
                 properties.deviceName);
    return false;
  }

  SwapchainSupport swapchain_support{};
  if (!query_swapchain_support(physical_device, surface_, swapchain_support)) {
    return false;
  }
  if (swapchain_support.formats.empty() ||
      swapchain_support.present_modes.empty()) {
    std::fprintf(stderr,
                 "[renderer] rejecting Vulkan device %s: swapchain formats "
                 "and present modes are required\n",
                 properties.deviceName);
    return false;
  }

  return true;
}

bool VulkanContext::create_device() {
  std::vector<VkDeviceQueueCreateInfo> queue_infos;
  std::vector<uint32_t> unique_queue_families{queue_families_.graphics};
  if (queue_families_.present != queue_families_.graphics) {
    unique_queue_families.push_back(queue_families_.present);
  }

  float queue_priority = 1.0f;
  for (uint32_t queue_family : unique_queue_families) {
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;
    queue_infos.push_back(queue_info);
  }

  VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features{};
  descriptor_indexing_features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
  descriptor_indexing_features.runtimeDescriptorArray = VK_TRUE;
  descriptor_indexing_features.shaderStorageBufferArrayNonUniformIndexing =
      VK_TRUE;
  descriptor_indexing_features.descriptorBindingPartiallyBound = VK_TRUE;
  descriptor_indexing_features.descriptorBindingStorageBufferUpdateAfterBind =
      VK_TRUE;
  descriptor_indexing_features.shaderSampledImageArrayNonUniformIndexing =
      VK_TRUE;
  descriptor_indexing_features.descriptorBindingSampledImageUpdateAfterBind =
      VK_TRUE;

  VkPhysicalDeviceVulkan13Features vulkan13_features{};
  vulkan13_features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  vulkan13_features.pNext = &descriptor_indexing_features;
  vulkan13_features.dynamicRendering = VK_TRUE;
  vulkan13_features.synchronization2 = VK_TRUE;

  VkPhysicalDeviceFeatures2 features{};
  features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  features.pNext = &vulkan13_features;
  features.features.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;

  VkDeviceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  create_info.pNext = &features;
  create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size());
  create_info.pQueueCreateInfos = queue_infos.data();
  create_info.enabledExtensionCount =
      static_cast<uint32_t>(device_extensions_.size());
  create_info.ppEnabledExtensionNames = device_extensions_.data();

  if (!check_vk(
          vkCreateDevice(physical_device_, &create_info, nullptr, &device_),
          "failed to create Vulkan device")) {
    return false;
  }

  vkGetDeviceQueue(device_, queue_families_.graphics, 0, &graphics_queue_);
  vkGetDeviceQueue(device_, queue_families_.present, 0, &present_queue_);
  return true;
}

} // namespace visual_runtime::vulkan
