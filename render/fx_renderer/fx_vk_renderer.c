/*
 * scenefx Vulkan renderer — Phase 1 scaffold.
 *
 * This brings up a Vulkan instance + logical device on the same GPU the
 * compositor is driving (matched by the DRM fd via VK_EXT_physical_device_drm)
 * and verifies every device extension the renderer will need. The actual
 * wlr_renderer implementation (render pass, textures, DMABUF import, effect
 * pipelines) is built out on top of this — see docs/vulkan-port.md. Until the
 * render path exists, fx_vk_renderer_create() returns NULL so the caller falls
 * back to GLES2; the init still runs and logs, so this is testable today.
 *
 * Modelled on wlroots' render/vulkan (the intended long-term base to fork).
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <vulkan/vulkan.h>
#include <wlr/util/log.h>

#include "scenefx/render/fx_renderer/fx_vk_renderer.h"

// device extensions the effect renderer depends on
static const char *fx_vk_device_exts[] = {
	VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
	VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
	VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME,
	VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
	VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
	VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
	VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME,
	VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
};

static bool has_ext(const VkExtensionProperties *avail, uint32_t n,
		const char *want) {
	for (uint32_t i = 0; i < n; i++) {
		if (strcmp(avail[i].extensionName, want) == 0) {
			return true;
		}
	}
	return false;
}

static VkInstance create_instance(void) {
	const char *inst_exts[] = {
		VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
		VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
		VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
	};
	VkApplicationInfo app = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "scenefx",
		.apiVersion = VK_API_VERSION_1_2,
	};
	VkInstanceCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app,
		.enabledExtensionCount = sizeof(inst_exts) / sizeof(inst_exts[0]),
		.ppEnabledExtensionNames = inst_exts,
	};
	VkInstance instance = VK_NULL_HANDLE;
	if (vkCreateInstance(&info, NULL, &instance) != VK_SUCCESS) {
		wlr_log(WLR_ERROR, "vulkan: vkCreateInstance failed");
		return VK_NULL_HANDLE;
	}
	return instance;
}

// pick the physical device whose DRM node matches drm_fd
static VkPhysicalDevice pick_device(VkInstance instance, int drm_fd) {
	struct stat drm_stat;
	if (fstat(drm_fd, &drm_stat) != 0) {
		wlr_log(WLR_ERROR, "vulkan: fstat(drm_fd) failed");
		return VK_NULL_HANDLE;
	}

	uint32_t n = 0;
	vkEnumeratePhysicalDevices(instance, &n, NULL);
	if (n == 0) {
		wlr_log(WLR_ERROR, "vulkan: no physical devices");
		return VK_NULL_HANDLE;
	}
	VkPhysicalDevice *devs = calloc(n, sizeof(*devs));
	vkEnumeratePhysicalDevices(instance, &n, devs);

	VkPhysicalDevice found = VK_NULL_HANDLE;
	for (uint32_t i = 0; i < n; i++) {
		VkPhysicalDeviceDrmPropertiesEXT drm = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT,
		};
		VkPhysicalDeviceProperties2 props = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
			.pNext = &drm,
		};
		vkGetPhysicalDeviceProperties2(devs[i], &props);

		dev_t want = drm_stat.st_rdev;
		int wmaj = (int)((want >> 8) & 0xff), wmin = (int)(want & 0xff);
		bool match =
			(drm.hasPrimary && drm.primaryMajor == wmaj &&
			 drm.primaryMinor == wmin) ||
			(drm.hasRender && drm.renderMajor == wmaj &&
			 drm.renderMinor == wmin);
		if (match) {
			wlr_log(WLR_INFO, "vulkan: selected %s (matches DRM node)",
				props.properties.deviceName);
			found = devs[i];
			break;
		}
	}
	free(devs);
	if (found == VK_NULL_HANDLE) {
		wlr_log(WLR_ERROR, "vulkan: no physical device matches the DRM fd");
	}
	return found;
}

static bool check_device_exts(VkPhysicalDevice phy) {
	uint32_t n = 0;
	vkEnumerateDeviceExtensionProperties(phy, NULL, &n, NULL);
	VkExtensionProperties *avail = calloc(n, sizeof(*avail));
	vkEnumerateDeviceExtensionProperties(phy, NULL, &n, avail);
	bool ok = true;
	for (size_t i = 0; i < sizeof(fx_vk_device_exts) / sizeof(char *); i++) {
		if (!has_ext(avail, n, fx_vk_device_exts[i])) {
			wlr_log(WLR_ERROR, "vulkan: missing required extension %s",
				fx_vk_device_exts[i]);
			ok = false;
		}
	}
	free(avail);
	return ok;
}

static int find_graphics_queue(VkPhysicalDevice phy) {
	uint32_t n = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(phy, &n, NULL);
	VkQueueFamilyProperties *fams = calloc(n, sizeof(*fams));
	vkGetPhysicalDeviceQueueFamilyProperties(phy, &n, fams);
	int idx = -1;
	for (uint32_t i = 0; i < n; i++) {
		if (fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			idx = (int)i;
			break;
		}
	}
	free(fams);
	return idx;
}

struct wlr_renderer *fx_vk_renderer_create_with_drm_fd(int drm_fd) {
	wlr_log(WLR_INFO, "vulkan: bringing up scenefx Vulkan renderer (scaffold)");

	VkInstance instance = create_instance();
	if (instance == VK_NULL_HANDLE) {
		return NULL;
	}

	VkPhysicalDevice phy = pick_device(instance, drm_fd);
	if (phy == VK_NULL_HANDLE || !check_device_exts(phy)) {
		vkDestroyInstance(instance, NULL);
		return NULL;
	}

	int qfam = find_graphics_queue(phy);
	if (qfam < 0) {
		wlr_log(WLR_ERROR, "vulkan: no graphics queue family");
		vkDestroyInstance(instance, NULL);
		return NULL;
	}

	VkPhysicalDeviceTimelineSemaphoreFeatures timeline = {
		.sType =
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
		.timelineSemaphore = VK_TRUE,
	};
	float prio = 1.0f;
	VkDeviceQueueCreateInfo qinfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = (uint32_t)qfam,
		.queueCount = 1,
		.pQueuePriorities = &prio,
	};
	VkDeviceCreateInfo dinfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &timeline,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &qinfo,
		.enabledExtensionCount =
			sizeof(fx_vk_device_exts) / sizeof(char *),
		.ppEnabledExtensionNames = fx_vk_device_exts,
	};
	VkDevice device = VK_NULL_HANDLE;
	if (vkCreateDevice(phy, &dinfo, NULL, &device) != VK_SUCCESS) {
		wlr_log(WLR_ERROR, "vulkan: vkCreateDevice failed");
		vkDestroyInstance(instance, NULL);
		return NULL;
	}

	wlr_log(WLR_INFO,
		"vulkan: instance + device up, all required extensions present "
		"(graphics queue family %d). Render path is not implemented yet — "
		"falling back to GLES2.",
		qfam);

	/* Phase 1 milestone reached: Vulkan initialises on the correct GPU with
	 * every needed extension. The wlr_renderer implementation (pass/texture/
	 * dmabuf/effects) is the next increment; until then, tear down and return
	 * NULL so renderer_autocreate() falls back to the GLES2 renderer. */
	vkDestroyDevice(device, NULL);
	vkDestroyInstance(instance, NULL);
	return NULL;
}
