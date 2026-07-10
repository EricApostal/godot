/**************************************************************************/
/*  rendering_offscreen_target.cpp                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "rendering_offscreen_target.h"

#include "core/variant/dictionary.h"
#include "servers/display/display_server_enums.h"
#include "servers/rendering/rendering_context_driver.h"

#if defined(RENDERING_OFFSCREEN_TARGET_METAL)
#include "drivers/metal/rendering_context_driver_metal.h"
#elif defined(RENDERING_OFFSCREEN_TARGET_VULKAN)
#include "drivers/vulkan/rendering_context_driver_vulkan_offscreen.h"
#endif

void RenderingOffscreenTarget::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_frame_available_callback", "callback"), &RenderingOffscreenTarget::set_frame_available_callback);
	ClassDB::bind_method(D_METHOD("get_supported_rendering_drivers"), &RenderingOffscreenTarget::get_supported_rendering_drivers);
}

Vector<String> RenderingOffscreenTarget::get_supported_rendering_drivers() const {
	Vector<String> drivers;
#if defined(RENDERING_OFFSCREEN_TARGET_METAL)
	drivers.push_back("metal");
#elif defined(RENDERING_OFFSCREEN_TARGET_VULKAN)
	drivers.push_back("vulkan");
#endif
	return drivers;
}

RenderingContextDriver *RenderingOffscreenTarget::create_and_setup_rendering_context(const String &p_rendering_driver, Error &r_error) {
	r_error = OK;
	RenderingContextDriver *context = nullptr;

#if defined(RENDERING_OFFSCREEN_TARGET_METAL)
	if (p_rendering_driver == "metal") {
		context = memnew(RenderingContextDriverMetal);
	}
#elif defined(RENDERING_OFFSCREEN_TARGET_VULKAN)
	if (p_rendering_driver == "vulkan") {
		context = memnew(RenderingContextDriverVulkanOffscreen);
	}
#endif

	if (context == nullptr) {
		r_error = ERR_CANT_CREATE;
		ERR_FAIL_V_MSG(nullptr, vformat("The offscreen display driver does not support the \"%s\" rendering driver on this platform.", p_rendering_driver));
	}

	if (context->initialize() != OK) {
		memdelete(context);
		r_error = ERR_CANT_CREATE;
		ERR_FAIL_V_MSG(nullptr, vformat("Could not initialize %s.", p_rendering_driver));
	}

	Error create_err = FAILED;
#if defined(RENDERING_OFFSCREEN_TARGET_METAL)
	RenderingContextDriverMetal::WindowPlatformData wpd;
	wpd.layer = nullptr;
	wpd.offscreen = true;
	wpd.offscreen_buffer_count = 3;
	wpd.offscreen_present_callback = &RenderingOffscreenTarget::_offscreen_present_callback;
	wpd.offscreen_present_userdata = this;
	create_err = context->window_create(DisplayServerEnums::MAIN_WINDOW_ID, &wpd);
#elif defined(RENDERING_OFFSCREEN_TARGET_VULKAN)
	RenderingContextDriverVulkanOffscreen::WindowPlatformData wpd;
	wpd.offscreen_buffer_count = 3;
	wpd.offscreen_present_callback = &RenderingOffscreenTarget::_offscreen_present_callback;
	wpd.offscreen_present_userdata = this;
	create_err = context->window_create(DisplayServerEnums::MAIN_WINDOW_ID, &wpd);
#endif

	if (create_err != OK) {
		memdelete(context);
		r_error = ERR_CANT_CREATE;
		ERR_FAIL_V_MSG(nullptr, vformat("Can't create a %s offscreen rendering context.", p_rendering_driver));
	}

	return context;
}

void RenderingOffscreenTarget::set_frame_available_callback(const Callable &p_callback) {
	frame_available_callback = p_callback;
}

void RenderingOffscreenTarget::deliver_pending_frame() {
	bool deliver = false;
	uint32_t frame_width = 0;
	uint32_t frame_height = 0;

#if defined(RENDERING_OFFSCREEN_TARGET_METAL)
	uint32_t iosurface_id = 0;
#elif defined(RENDERING_OFFSCREEN_TARGET_VULKAN)
#if defined(LINUXBSD_ENABLED)
	int dmabuf_fd = -1;
	uint32_t drm_format = 0;
	uint32_t stride = 0;
	uint32_t offset = 0;
	uint64_t modifier = 0;
#elif defined(ANDROID_ENABLED)
	void *hardware_buffer = nullptr;
#elif defined(WINDOWS_ENABLED)
	void *shared_handle = nullptr;
	uint64_t fence_value = 0;
#endif
#endif

	{
		MutexLock lock(pending_frame_mutex);
		if (has_pending_frame) {
			deliver = true;
			frame_width = pending_width;
			frame_height = pending_height;
#if defined(RENDERING_OFFSCREEN_TARGET_METAL)
			iosurface_id = pending_iosurface_id;
#elif defined(RENDERING_OFFSCREEN_TARGET_VULKAN)
#if defined(LINUXBSD_ENABLED)
			dmabuf_fd = pending_dmabuf_fd;
			drm_format = pending_drm_format;
			stride = pending_stride;
			offset = pending_offset;
			modifier = pending_modifier;
#elif defined(ANDROID_ENABLED)
			hardware_buffer = pending_hardware_buffer;
#elif defined(WINDOWS_ENABLED)
			shared_handle = pending_shared_handle;
			fence_value = pending_fence_value;
#endif
#endif
			has_pending_frame = false;
		}
	}

	if (!deliver || !frame_available_callback.is_valid()) {
		return;
	}

	Dictionary frame_data;
	frame_data["width"] = frame_width;
	frame_data["height"] = frame_height;

#if defined(RENDERING_OFFSCREEN_TARGET_METAL)
	// Matches GodotOffscreenSurfaceType::GODOT_OFFSCREEN_SURFACE_TYPE_IOSURFACE in libgodot.h.
	frame_data["type"] = 0;
	frame_data["iosurface_id"] = iosurface_id;
#elif defined(RENDERING_OFFSCREEN_TARGET_VULKAN)
#if defined(LINUXBSD_ENABLED)
	// Matches GodotOffscreenSurfaceType::GODOT_OFFSCREEN_SURFACE_TYPE_DMABUF in libgodot.h.
	frame_data["type"] = 1;
	frame_data["fd"] = dmabuf_fd;
	frame_data["drm_format"] = (int64_t)drm_format;
	frame_data["stride"] = (int64_t)stride;
	frame_data["offset"] = (int64_t)offset;
	frame_data["modifier"] = (int64_t)modifier;
#elif defined(ANDROID_ENABLED)
	// Matches GodotOffscreenSurfaceType::GODOT_OFFSCREEN_SURFACE_TYPE_AHARDWAREBUFFER in libgodot.h.
	frame_data["type"] = 2;
	frame_data["hardware_buffer"] = (int64_t)(uintptr_t)hardware_buffer;
#elif defined(WINDOWS_ENABLED)
	// Matches GodotOffscreenSurfaceType::GODOT_OFFSCREEN_SURFACE_TYPE_D3D11_SHARED_HANDLE in libgodot.h.
	frame_data["type"] = 3;
	frame_data["shared_handle"] = (int64_t)(uintptr_t)shared_handle;
	frame_data["fence_value"] = (int64_t)fence_value;
#endif
#endif

	frame_available_callback.call(frame_data);
}

#if defined(RENDERING_OFFSCREEN_TARGET_METAL)
void RenderingOffscreenTarget::_offscreen_present_callback(void *p_userdata, IOSurfaceRef p_surface, uint32_t p_width, uint32_t p_height) {
	// Called from a Metal command buffer completion handler; may run on an arbitrary
	// Metal-owned thread, so only stash the frame here and hand off to the main thread in
	// deliver_pending_frame().
	RenderingOffscreenTarget *target = (RenderingOffscreenTarget *)p_userdata;

	MutexLock lock(target->pending_frame_mutex);
	target->has_pending_frame = true;
	target->pending_iosurface_id = IOSurfaceGetID(p_surface);
	target->pending_width = p_width;
	target->pending_height = p_height;
}
#elif defined(RENDERING_OFFSCREEN_TARGET_VULKAN)
void RenderingOffscreenTarget::_offscreen_present_callback(void *p_userdata, const RenderingContextDriverVulkan::OffscreenExportedSurface *p_exported_surface, uint32_t p_width, uint32_t p_height) {
	// May run on a non-main thread (see the comment on deliver_pending_frame() in the header),
	// so only stash the frame here and hand off to the main thread separately.
	RenderingOffscreenTarget *target = (RenderingOffscreenTarget *)p_userdata;

	MutexLock lock(target->pending_frame_mutex);
	target->has_pending_frame = true;
	target->pending_width = p_width;
	target->pending_height = p_height;
#if defined(LINUXBSD_ENABLED)
	target->pending_dmabuf_fd = p_exported_surface->dmabuf_fd;
	target->pending_drm_format = p_exported_surface->drm_format;
	target->pending_stride = p_exported_surface->stride;
	target->pending_offset = p_exported_surface->offset;
	target->pending_modifier = p_exported_surface->modifier;
#elif defined(ANDROID_ENABLED)
	target->pending_hardware_buffer = p_exported_surface->hardware_buffer;
#elif defined(WINDOWS_ENABLED)
	target->pending_shared_handle = p_exported_surface->shared_handle;
	target->pending_fence_value = p_exported_surface->fence_value;
#endif
}
#endif

RenderingOffscreenTarget::RenderingOffscreenTarget() {
}

RenderingOffscreenTarget::~RenderingOffscreenTarget() {
}
