#ifndef SCENEFX_RENDER_FX_RENDERER_FX_VK_RENDERER_H
#define SCENEFX_RENDER_FX_RENDERER_FX_VK_RENDERER_H

#include <wlr/render/wlr_renderer.h>

/*
 * scenefx Vulkan renderer (WIP — Phase 1 scaffold).
 *
 * Brings up a Vulkan instance + device on the GPU behind drm_fd. Returns NULL
 * until the render path is implemented, so callers fall back to GLES2. Only
 * built when the `vulkan` renderer is selected via meson.
 */
struct wlr_renderer *fx_vk_renderer_create_with_drm_fd(int drm_fd);

#endif
