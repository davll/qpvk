#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "volk.h"
#include "vk_mem_alloc.h"
#include "linmath.h"
#include "argparse.h"
#include "log.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#define ENGINE_NAME "qpvk"
#define ENGINE_VERSION VK_MAKE_VERSION(0, 1, 0)

#define SHORT_DESCRIPTION "Hello World"
#define LONG_DESCRIPTION "A minimal prototype of Vulkan application"

static VkInstance instance_ = VK_NULL_HANDLE;
static VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
static bool supports_EXT_debug_utils_ = false;
static bool supports_KHR_surface_ = false;
static bool supports_KHR_display_ = false;

typedef struct Context {
    VkPhysicalDevice physical_device;
    VkDevice device;
    VmaAllocator allocator;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    bool supports_KHR_swapchain;
    uint32_t graphics_queue_family;
    uint32_t present_queue_family;
    uint32_t num_swapchain_images;
    VkImage* swapchain_images;
} Context;

static Context context_ = {
    .physical_device = VK_NULL_HANDLE,
    .device = VK_NULL_HANDLE,
    .allocator = VK_NULL_HANDLE,
    .surface = VK_NULL_HANDLE,
    .swapchain = VK_NULL_HANDLE,
    .supports_KHR_swapchain = false,
    .graphics_queue_family = UINT32_MAX,
    .present_queue_family = UINT32_MAX,
    .num_swapchain_images = 0,
    .swapchain_images = NULL,
};

static void initialize_instance(const char* appname, bool debug);
static void destroy_instance();
static void initialize_context(VkPhysicalDevice physical_device, VkSurfaceKHR surface);
static void destroy_context();

static const char *const usage[] = {
    "qpvk [options] [[--] args]",
    "qpvk [options]",
    NULL,
};

int main(int argc, const char** argv)
{
    int verbose = 0;
    int list_available_devices = 0;
    const char* device_name = NULL;

    // Parse commandline arguments
    struct argparse_option options[] = {
        OPT_HELP(),
        OPT_GROUP("Basic options"),
        OPT_BOOLEAN('v', "verbose", &verbose, "enable debugging"),
        OPT_BOOLEAN('l', "list-devices", &list_available_devices, "list available devices"),
        OPT_STRING('d', "device", &device_name, "select device"),
        OPT_END(),
    };
    struct argparse argparse;
    argparse_init(&argparse, options, usage, 0);
    argparse_describe(&argparse, SHORT_DESCRIPTION, LONG_DESCRIPTION);
    argc = argparse_parse(&argparse, argc, argv);

    log_info("VERBOSE: %s", (verbose ? "YES" : "NO"));

    // Init SDL environment
    if (SDL_Init(SDL_INIT_VIDEO)) {
        log_fatal("SDL_Init failed: %s", SDL_GetError());
        return -1;
    }
    atexit(SDL_Quit);

    // Load Vulkan Entrypoints
    if (SDL_Vulkan_LoadLibrary(NULL)) {
        log_fatal("SDL_Vulkan_LoadLibrary failed: %s", SDL_GetError());
        return -1;
    }
    atexit(SDL_Vulkan_UnloadLibrary);

    // Init Vulkan instance
    PFN_vkGetInstanceProcAddr getprocaddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
    if (!getprocaddr) {
        log_fatal("SDL_Vulkan_GetVkGetInstanceProcAddr failed: %s", SDL_GetError());
        return -1;
    }
    volkInitializeCustom(getprocaddr);
    initialize_instance(argv[0], verbose);
    atexit(destroy_instance);

    // List available devices
    {
        VkResult result;
        VkInstance instance = instance_;
        uint32_t num_physical_devices = 0;

        result = vkEnumeratePhysicalDevices(instance, &num_physical_devices, NULL);
        if (result != VK_SUCCESS) {
            log_fatal("vkEnumeratePhysicalDevices failed");
            exit(-1);
        } else if (num_physical_devices == 0) {
            log_error("No available physical device found");
            exit(1);
        }
        
        VkPhysicalDevice* physical_devices = (VkPhysicalDevice*)malloc(sizeof(VkPhysicalDevice) * num_physical_devices);
        result = vkEnumeratePhysicalDevices(instance, &num_physical_devices, physical_devices);
        if (result != VK_SUCCESS) {
            log_fatal("vkEnumeratePhysicalDevices failed");
            exit(-1);
        }

        VkPhysicalDeviceProperties* phyiscal_device_properties = (VkPhysicalDeviceProperties*)malloc(sizeof(VkPhysicalDeviceProperties) * num_physical_devices);
        for (unsigned i = 0, n = num_physical_devices; i < n; ++i) {
            vkGetPhysicalDeviceProperties(physical_devices[i], &phyiscal_device_properties[i]);
        }

        // Show all informations to the output
        if (list_available_devices) {
            printf("Available physical devices: ");
            printf("Count = %u\n", num_physical_devices);
            for (unsigned i = 0, n = num_physical_devices; i < n; ++i) {
                VkPhysicalDeviceProperties* props = &phyiscal_device_properties[i];
                printf("Device %u:\n", i);
                printf("    Name: %s\n", props->deviceName);
                printf("    ID: 0x%08X\n", props->deviceID);
            }
            fflush(stdout);
            return 0;
        }

        // Select physical device
        int selected_physical_device_index = -1;
        if (device_name) {
            if (strncmp(device_name, "0x", 2) == 0) {
                uint32_t id = strtol(device_name, NULL, 16);
                for (unsigned i = 0, n = num_physical_devices; i < n; ++i) {
                    if (phyiscal_device_properties[i].deviceID == id) {
                        selected_physical_device_index = i;
                        break;
                    }
                }
            } else {
                for (unsigned i = 0, n = num_physical_devices; i < n; ++i) {
                    if (strcmp(phyiscal_device_properties[i].deviceName, device_name) == 0) {
                        selected_physical_device_index = i;
                        break;
                    }
                }
                if (selected_physical_device_index == -1) {
                    for (unsigned i = 0, n = num_physical_devices; i < n; ++i) {
                        if (strstr(phyiscal_device_properties[i].deviceName, device_name)) {
                            selected_physical_device_index = i;
                            break;
                        }
                    }
                }
            }
        }
        if (selected_physical_device_index == -1) {
            selected_physical_device_index = 0;
        }

        log_info("Selected device: %s", phyiscal_device_properties[selected_physical_device_index].deviceName);

        free(physical_devices);
    }

    return 0;
}

static void initialize_context(VkPhysicalDevice physical_device, VkSurfaceKHR surface)
{
    context_.physical_device = physical_device;
    context_.surface = surface;

    // Device

    // Swapchain
}

static void destroy_context()
{
    if (context_.allocator)
        vmaDestroyAllocator(context_.allocator);

    if (context_.swapchain_images)
        free(context_.swapchain_images);

    if (context_.swapchain)
        vkDestroySwapchain(context_.device, context_.swapchain, NULL);

    if (context_.device)
        vkDestroyDevice(context_.device, NULL);

    if (context_.surface)
        vkDestroySurfaceKHR(instance_, context_.surface, NULL);
}

static VkBool32 cgvk_HandleDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT                  messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT*      pCallbackData,
    void*                                            pUserData)
{
    const char* message = pCallbackData->pMessage;
    switch (messageSeverity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            log_info("[vk] %s", message);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            log_warn("[vk] %s", message);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            log_error("[vk] %s", message);
            break;
        default:
            break;
    }
    return VK_FALSE;
}

static void init_debug_utils()
{
    const VkDebugUtilsMessengerCreateInfoEXT createinfo = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = NULL,
        .flags = 0,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = cgvk_HandleDebugCallback,
        .pUserData = NULL,
    };

    VkResult result = vkCreateDebugUtilsMessengerEXT(instance_, &createinfo, NULL, &debug_messenger_);
    if (result != VK_SUCCESS) {
        log_fatal("vkCreateDebugUtilsMessengerEXT failed: %d", (int)result);
        abort();
    }
}

static void initialize_instance(const char* appname, bool debug)
{
    assert(!instance_);

    VkResult result;

    uint32_t layer_count = 0;
    const char* layer_names[2];
    uint32_t extension_count = 0;
    const char* extension_names[16];

    // Find validation layers
    if (debug) {
        uint32_t available_layer_count;
        VkResult result = vkEnumerateInstanceLayerProperties(&available_layer_count, NULL);
        if (result != VK_SUCCESS) {
            log_fatal("vkEnumerateInstanceLayerProperties failed: %d", (int)result);
            abort();
        }
        if (available_layer_count > 0) {
            VkLayerProperties properties[available_layer_count];
            result = vkEnumerateInstanceLayerProperties(&available_layer_count, properties);
            if (result != VK_SUCCESS) {
                log_fatal("vkEnumerateInstanceLayerProperties failed: %d", (int)result);
                abort();
            }
            for (unsigned i = 0, n = available_layer_count; i < n; ++i) {
                const VkLayerProperties* props = &properties[i];
                if (strcmp(props->layerName, "VK_LAYER_KHRONOS_validation") == 0) {
                    layer_names[layer_count++] = "VK_LAYER_KHRONOS_validation";
                } else if (strcmp(props->layerName, "VK_LAYER_LUNARG_standard_validation") == 0) {
                    layer_names[layer_count++] = "VK_LAYER_LUNARG_standard_validation";
                }
            }
        }
    }

    // Find debug extensions
    supports_EXT_debug_utils_ = false;
    if (debug) {
        for (uint32_t k = 0; k < layer_count; ++k) {
            uint32_t available_extension_count;
            VkResult result = vkEnumerateInstanceExtensionProperties(layer_names[k], &available_extension_count, NULL);
            if (result != VK_SUCCESS) {
                log_fatal("vkEnumerateInstanceExtensionProperties failed: %d", (int)result);
                abort();
            }
            if (available_extension_count > 0) {
                VkExtensionProperties properties[available_extension_count];
                result = vkEnumerateInstanceExtensionProperties(layer_names[k], &available_extension_count, properties);
                if (result != VK_SUCCESS) {
                    log_fatal("vkEnumerateInstanceExtensionProperties failed: %d", (int)result);
                    abort();
                }
                for (unsigned i = 0, n = available_extension_count; i < n; ++i) {
                    const VkExtensionProperties* props = &properties[i];
                    if (strcmp(props->extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) {
                        supports_EXT_debug_utils_ = true;
                    }
                }
            }
        }
    }

    if (supports_EXT_debug_utils_)
        extension_names[extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    
    // Find surface extensions
    supports_KHR_surface_ = false;
    supports_KHR_display_ = false;
    {
        uint32_t available_extension_count;
        VkResult result = vkEnumerateInstanceExtensionProperties(NULL, &available_extension_count, NULL);
        if (result != VK_SUCCESS) {
            log_fatal("vkEnumerateInstanceExtensionProperties failed: %d", (int)result);
            abort();
        }
        if (available_extension_count > 0) {
            VkExtensionProperties properties[available_extension_count];
            result = vkEnumerateInstanceExtensionProperties(NULL, &available_extension_count, properties);
            if (result != VK_SUCCESS) {
                log_fatal("vkEnumerateInstanceExtensionProperties failed: %d", (int)result);
                abort();
            }
            for (unsigned i = 0, n = available_extension_count; i < n; ++i) {
                const VkExtensionProperties* props = &properties[i];
                const char* extname = props->extensionName;
                if (strcmp(extname, VK_KHR_SURFACE_EXTENSION_NAME) == 0) {
                    supports_KHR_surface_ = true;
                    extension_names[extension_count++] = VK_KHR_SURFACE_EXTENSION_NAME;
                } else if (strcmp(extname, VK_KHR_DISPLAY_EXTENSION_NAME) == 0) {
                    supports_KHR_display_ = true;
                    extension_names[extension_count++] = VK_KHR_DISPLAY_EXTENSION_NAME;
                } else {
#if defined(__linux__) || defined(__linux)
                    if (strcmp(extname, "VK_KHR_wayland_surface") == 0) {
                        extension_names[extension_count++] = "VK_KHR_wayland_surface";
                    }
                    if (strcmp(extname, "VK_KHR_xcb_surface") == 0) {
                        extension_names[extension_count++] = "VK_KHR_xcb_surface";
                    }
                    if (strcmp(extname, "VK_KHR_xlib_surface") == 0) {
                        extension_names[extension_count++] = "VK_KHR_xlib_surface";
                    }
#endif
#if defined(_WIN32) || defined(_WIN64)
                    if (strcmp(extname, "VK_KHR_win32_surface") == 0) {
                        extension_names[extension_count++] = "VK_KHR_win32_surface";
                    }
#endif
#if defined(__APPLE__)
#endif
                }
            }
        }
    }

    // Create instance
    const VkApplicationInfo appinfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = appname,
        .applicationVersion = ENGINE_VERSION,
        .pEngineName = ENGINE_NAME,
        .engineVersion = ENGINE_VERSION,
        .apiVersion = VK_API_VERSION_1_1,
    };
    const VkInstanceCreateInfo createinfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .pApplicationInfo = &appinfo,
        .enabledLayerCount = layer_count,
        .ppEnabledLayerNames = layer_names,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extension_names,
    };

    result = vkCreateInstance(&createinfo, NULL, &instance_);
    if (result != VK_SUCCESS) {
        log_fatal("vkCreateInstance failed: %d", (int)result);
        abort();
    }

    // Load instance functions
    volkLoadInstance(instance_);

    // Attach debug callback
    if (debug && supports_EXT_debug_utils_) {
        init_debug_utils();
    }
}

static void destroy_instance()
{
    if (debug_messenger_) {
        vkDestroyDebugUtilsMessengerEXT(instance_, debug_messenger_, NULL);
    }

    vkDestroyInstance(instance_, NULL);
}
