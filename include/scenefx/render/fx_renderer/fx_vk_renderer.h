#ifndef SCENEFX_RENDER_FX_RENDERER_FX_VK_RENDERER_H
#define SCENEFX_RENDER_FX_RENDERER_FX_VK_RENDERER_H

#include <vulkan/vulkan_core.h>
#include <wlr/render/wlr_renderer.h>

/*
 * scenefx Vulkan renderer (WIP).
 *
 * A fork of wlroots' Vulkan renderer (render/vulkan/) with the effect
 * pipelines layered on top, mirroring how scenefx's GLES2 renderer forks
 * wlroots' GLES2 renderer. Symbols are prefixed `fx_vk_` so they never clash
 * with the Vulkan renderer that ships inside the linked libwlroots. Only built
 * when the `vulkan` renderer is selected via meson.
 */

struct fx_vk_image_attribs {
	VkImage image;
	VkImageLayout layout;
	VkFormat format;
};

struct wlr_renderer *fx_vk_renderer_create_with_drm_fd(int drm_fd);

VkInstance fx_vk_renderer_get_instance(struct wlr_renderer *renderer);
VkPhysicalDevice fx_vk_renderer_get_physical_device(struct wlr_renderer *renderer);
VkDevice fx_vk_renderer_get_device(struct wlr_renderer *renderer);
uint32_t fx_vk_renderer_get_queue_family(struct wlr_renderer *renderer);

bool fx_vk_renderer_is_vk(struct wlr_renderer *wlr_renderer);
bool fx_vk_texture_is_vk(struct wlr_texture *texture);

void fx_vk_texture_get_image_attribs(struct wlr_texture *texture,
	struct fx_vk_image_attribs *attribs);
bool fx_vk_texture_has_alpha(struct wlr_texture *texture);

#endif
