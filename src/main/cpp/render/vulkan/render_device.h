/*!
 * \author ddubois 
 * \date 15-Oct-17.
 */

#ifndef RENDERER_RENDER_DEVICE_H
#define RENDERER_RENDER_DEVICE_H

#include <vulkan/vulkan.h>
#include "../windowing/glfw_vk_window.h"

namespace nova {
    struct gpu_info {
        VkPhysicalDevice device;
        std::vector<VkQueueFamilyProperties> queue_family_props;
        std::vector<VkExtensionProperties> extention_props;
        VkSurfaceCapabilitiesKHR surface_capabilities;
        std::vector<VkSurfaceFormatKHR> surface_formats;
        VkPhysicalDeviceMemoryProperties mem_props;
        VkPhysicalDeviceProperties props;
        std::vector<VkPresentModeKHR> present_modes;
    };

    /*!
     * \brief An abstraction over Vulkan physical and logical devices
     */
    class render_device {
    public:
        VkInstance instance = nullptr;
        VkSurfaceKHR surface;

        VkDebugReportCallbackEXT callback;

        uint32_t graphics_family_idx;
        uint32_t present_family_idx;
        gpu_info *gpu;

        void create_instance(glfw_vk_window &window);

        void setup_debug_callback();

        void find_device_and_queues();

    private:
        std::vector<gpu_info> gpus;
        VkPhysicalDevice physical_device;
        VkDevice device;

        VkQueue graphics_queue;
        VkQueue present_queue;

        std::vector<const char *> validation_layers;
        std::vector<const char *> extensions;

        void create_logical_device_and_queues();

        void enumerate_gpus();

        void select_physical_device();
    };

    VkResult
    CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT *pCreateInfo,
                                 const VkAllocationCallbacks *pAllocator, VkDebugReportCallbackEXT *pCallback);

    void DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback,
                                       const VkAllocationCallbacks *pAllocator);
}

#endif //RENDERER_RENDER_DEVICE_H
