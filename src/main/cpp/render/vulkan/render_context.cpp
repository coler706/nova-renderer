/*!
 * \author ddubois 
 * \date 15-Oct-17.
 */

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <easylogging++.h>
#include <unordered_set>
#include "render_context.h"
#include "../windowing/glfw_vk_window.h"
#include "command_pool.h"

namespace nova {
    bool layers_are_supported(std::vector<const char*>& validation_layers);

    std::vector<const char *> get_required_extensions(glfw_vk_window &window);

    VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
            VkDebugReportFlagsEXT flags,
            VkDebugReportObjectTypeEXT objType,
            uint64_t obj,
            size_t location,
            int32_t code,
            const char *layerPrefix,
            const char *msg,
            void *userData);

    void render_context::create_instance(glfw_vk_window &window) {
        validation_layers = {
                "VK_LAYER_GOOGLE_threading",
                "VK_LAYER_LUNARG_parameter_validation",
                "VK_LAYER_LUNARG_object_tracker",
                "VK_LAYER_LUNARG_core_validation",
                "VK_LAYER_GOOGLE_unique_objects"
        };

        vk::ApplicationInfo app_info = {};
        app_info.pApplicationName = "Minecraft Nova Renderer";
        app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.pEngineName = "Nova Renderer 0.5";
        app_info.engineVersion = VK_MAKE_VERSION(0, 5, 0);
        app_info.apiVersion = VK_API_VERSION_1_0;
        LOG(TRACE) << "Created vk::ApplicationInfo struct";

        vk::InstanceCreateInfo create_info = {};
        create_info.pApplicationInfo = &app_info;

        extensions = get_required_extensions(window);
        create_info.ppEnabledExtensionNames = extensions.data();
        create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());

#ifdef NDEBUG
        create_info.enabledLayerCount = 0;
#else
        if(!layers_are_supported(validation_layers)) {
            LOG(FATAL) << "The layers we need aren't available";
        }

        create_info.enabledLayerCount = static_cast<uint32_t>(validation_layers.size());
        create_info.ppEnabledLayerNames = validation_layers.data();
#endif

        vk_instance = vk::createInstance(create_info, nullptr);
    }

    void render_context::setup_debug_callback() {
#ifndef NDEBUG
        CreateDebugReportCallback = (PFN_vkCreateDebugReportCallbackEXT)vk_instance.getProcAddr("vkCreateDebugReportCallbackEXT");
        DestroyDebugReportCallback = (PFN_vkDestroyDebugReportCallbackEXT)vk_instance.getProcAddr("vkDestroyDebugReportCallbackEXT");

        VkDebugReportCallbackCreateInfoEXT callbackCreateInfo = {};
        callbackCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
        callbackCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
        callbackCreateInfo.pfnCallback = (PFN_vkDebugReportCallbackEXT)debug_callback;

        if(CreateDebugReportCallback(vk_instance, &callbackCreateInfo, nullptr, &debug_report_callback) != VK_SUCCESS) {
            LOG(FATAL) << "Could not set up debug callback";
        }
#endif
    }

    void render_context::find_device_and_queues() {
        enumerate_gpus();
        LOG(TRACE) << "Enumerated GPUs";
        select_physical_device();
        LOG(TRACE) << "Found a physical device that will work I guess";
        create_logical_device_and_queues();
        LOG(TRACE) << "Basic queue and logical device was found";
    }

    void render_context::enumerate_gpus() {
        auto devices = vk_instance.enumeratePhysicalDevices();
        LOG(TRACE) << "There are " << devices.size() << " physical devices";
        if(devices.empty()) {
            LOG(FATAL) << "Apparently you have zero devices. You know you need a GPU to run Nova, right?";
        }

        this->gpus.resize(devices.size());
        LOG(TRACE) << "Reserved " << devices.size() << " slots for devices";
        for(uint32_t i = 0; i < devices.size(); i++) {
            gpu_info& gpu = this->gpus[i];
            gpu.device = devices[i];

            // get the queues the device supports
            gpu.queue_family_props = gpu.device.getQueueFamilyProperties();
            LOG(TRACE) << "Got the physical device queue properties";

            // Get the extensions the device supports
            gpu.extention_props = gpu.device.enumerateDeviceExtensionProperties();
            LOG(TRACE) << "Got the device extension properties";

            gpu.surface_capabilities = gpu.device.getSurfaceCapabilitiesKHR(surface);
            LOG(TRACE) << "Got the physical device surface capabilities";

            gpu.surface_formats = gpu.device.getSurfaceFormatsKHR(surface);
            LOG(TRACE) << "Got the physical device's surface formats";

            gpu.present_modes = gpu.device.getSurfacePresentModesKHR(surface);
            LOG(TRACE) << "Got the surface present modes";

            gpu.mem_props = gpu.device.getMemoryProperties();
            gpu.props = gpu.device.getProperties();
            gpu.supported_features = gpu.device.getFeatures();
            LOG(TRACE) << "Got the memory properties and device properties";
        }
    }

    void render_context::select_physical_device() {
        // TODO: More complex logic to try and use a non-Intel GPU if possible (Vulkan book page 9)
        for(auto& gpu : gpus) {
            if(gpu.props.vendorID == 0x8086 && gpus.size() > 1) {
                // We found an Intel GPU, but there's other GPUs available on this system so we should skip the Intel
                // one - other GPUs are all but garaunteed to be more powerful
                continue;
            }

            uint32_t graphics_idx = 0xFFFFFFFF;
            uint32_t present_idx = 0xFFFFFFFF;

            if(gpu.surface_formats.empty()) {
                continue;
            }

            if(gpu.present_modes.empty()) {
                continue;
            }

            // Find graphics queue family
            for(size_t i = 0; i < gpu.queue_family_props.size(); i++) {
                auto &props = gpu.queue_family_props[i];
                if(props.queueCount == 0) {
                    continue;
                }

                if(props.queueFlags & vk::QueueFlagBits::eGraphics) {
                    graphics_idx = i;
                    break;
                }
            }

            // Find present queue family
            for(size_t i = 0; i < gpu.queue_family_props.size(); i++) {
                auto &props = gpu.queue_family_props[i];

                if(props.queueCount == 0) {
                    continue;
                }

                vk::Bool32 supports_present = VK_FALSE;
                gpu.device.getSurfaceSupportKHR(i, surface, &supports_present);
                if(supports_present == VK_TRUE) {
                    present_idx = i;
                    break;
                }
            }

            if(graphics_idx >= 0 && present_idx >= 0) {
                graphics_family_idx = graphics_idx;
                present_family_idx = present_idx;
                physical_device = gpu.device;
                this->gpu = gpu;
                LOG(INFO) << "Selected graphics device " << gpu.props.deviceName;
                LOG(INFO) << "It has a limit of " << gpu.props.limits.maxImageDimension2D << " texels in a 2D texture";
                LOG(INFO) << "It has a limit of " << gpu.props.limits.maxImageArrayLayers << " array layers";
                return;
            }
        }

        LOG(FATAL) << "Could not find a device with both present and graphics queues";
    }

    void render_context::create_logical_device_and_queues() {
        std::unordered_set<uint32_t> unique_idx;
        unique_idx.insert(graphics_family_idx);
        unique_idx.insert(present_family_idx);

        std::vector<vk::DeviceQueueCreateInfo> devq_info;

        // TODO: Possibly create a queue for texture streaming and another for geometry streaming?
        const float priority = 1.0;
        for(auto idx : unique_idx) {
            vk::DeviceQueueCreateInfo qinfo = {};
            qinfo.queueFamilyIndex = idx;
            qinfo.queueCount = 1;

            qinfo.pQueuePriorities = &priority;
            devq_info.push_back(qinfo);
        }

        // Do I have to look at the loaded shaderpack and see what features it needs? For now I'll just add whatever looks
        // good
        vk::PhysicalDeviceFeatures device_features = {};
        device_features.geometryShader = VK_TRUE;
        device_features.tessellationShader = VK_TRUE;
        device_features.samplerAnisotropy = VK_TRUE;

        vk::DeviceCreateInfo info = {};
        info.queueCreateInfoCount = static_cast<uint32_t>(devq_info.size());
        info.pQueueCreateInfos = devq_info.data();
        info.pEnabledFeatures = &device_features;
        info.enabledExtensionCount = 1;
        const char* swapchain_extension = "VK_KHR_swapchain";
        info.ppEnabledExtensionNames = &swapchain_extension;

        info.enabledLayerCount = static_cast<uint32_t>(validation_layers.size());
        if(!validation_layers.empty()) {
            info.ppEnabledLayerNames = validation_layers.data();
        }

        device = physical_device.createDevice(info, nullptr);

        graphics_queue = device.getQueue(graphics_family_idx, 0);
        present_queue = device.getQueue(graphics_family_idx, 0);

        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice = (VkPhysicalDevice)physical_device;
        allocatorInfo.device = (VkDevice)device;
        vmaCreateAllocator(&allocatorInfo, &allocator);
    }

    void render_context::create_semaphores() {
        acquire_semaphores.resize(NUM_FRAME_DATA);
        render_complete_semaphores.resize(NUM_FRAME_DATA);

        vk::SemaphoreCreateInfo semaphore_create_info = {};
        for(int i = 0; i < NUM_FRAME_DATA; i++) {
            acquire_semaphores[i] = device.createSemaphore(semaphore_create_info, nullptr);
            render_complete_semaphores[i] = device.createSemaphore(semaphore_create_info, nullptr);
        }
    }

    void render_context::create_command_pool_and_command_buffers() {
        // TODO: Get the number of threads dynamically based on the user's CPU core count
        command_buffer_pool = std::make_unique<command_pool>(device, graphics_family_idx, 8);
    }



    render_context::~render_context() {
#ifndef NDEBUG
        DestroyDebugReportCallback(vk_instance, debug_report_callback, nullptr);
#endif

        vmaDestroyAllocator(allocator);

        for(auto& semi : acquire_semaphores) {
            device.destroySemaphore(semi);
        }

        for(auto& semi : render_complete_semaphores) {
            device.destroySemaphore(semi);
        }

        device.destroyPipelineCache(pipeline_cache);

        vk_instance.destroy();

        LOG(TRACE) << "Destroyed the render context";
    }

    void render_context::create_pipeline_cache() {
        vk::PipelineCacheCreateInfo cache_create_info = {};
        pipeline_cache = device.createPipelineCache(cache_create_info);
    }

    // This function should really be outside of this file, but I want to keep vulkan creation things in here
    // to avoid making nova_renderer.cpp any larger than it needs to be
    std::vector<const char *> get_required_extensions(glfw_vk_window &window) {
        uint32_t glfw_extensions_count = 0;
        auto glfw_extensions = window.get_required_extensions(&glfw_extensions_count);

        std::vector<const char *> extensions;

        for(uint32_t i = 0; i < glfw_extensions_count; i++) {
            LOG(DEBUG) << "GLFW requires " << glfw_extensions[i];
            extensions.push_back(glfw_extensions[i]);
        }

#ifndef NDEBUG
        extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
#endif

        return extensions;
    }

    bool layers_are_supported(std::vector<const char*>& validation_layers) {
        auto available_layers = vk::enumerateInstanceLayerProperties();

        for(auto layer_name : validation_layers) {
            LOG(TRACE) << "Checking for layer " << layer_name;

            bool layer_found = false;

            for(const auto &layer_propeties : available_layers) {
                if(strcmp(layer_name, layer_propeties.layerName) == 0) {
                    LOG(TRACE) << "Found it!";
                    layer_found = true;
                    break;
                }
            }

            if(!layer_found) {
                LOG(ERROR) << "Could not find layer " << layer_name;
                return false;
            }
        }

        return true;
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
            VkDebugReportFlagsEXT flags,
            VkDebugReportObjectTypeEXT objType,
            uint64_t obj,
            size_t location,
            int32_t code,
            const char *layer_prefix,
            const char *msg,
            void *userData) {

        if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
        {
            LOG(ERROR) << "ERROR: API: " << layer_prefix << " " << msg;
        }
        // Warnings may hint at unexpected / non-spec API usage
        if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
        {
            LOG(ERROR) << "WARNING: API: " << layer_prefix << " " << msg;
        }
        // May indicate sub-optimal usage of the API
        if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)
        {
            LOG(ERROR) << "PERFORMANCE WARNING: API: " << layer_prefix << " " << msg;
        }
        // Informal messages that may become handy during debugging
        if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT)
        {
            LOG(ERROR) << "INFORMATION: API: " << layer_prefix << " " << msg;
        }
        // Diagnostic info from the Vulkan loader and layers
        // Usually not helpful in terms of API usage, but may help to debug layer and loader problems
        if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT)
        {
            LOG(ERROR) << "DEBUG: API: " << layer_prefix << " " << msg;
        }

        return VK_FALSE;
    }
}