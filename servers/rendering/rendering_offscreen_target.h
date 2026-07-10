/**************************************************************************/
/*  rendering_offscreen_target.h                                         */
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

#pragma once

#include "core/object/class_db.h"
#include "core/object/ref_counted.h"
#include "core/os/mutex.h"
#include "core/variant/callable.h"

// Exactly one of these is ever defined for a given build: which native-export mechanism
// backs RenderingOffscreenTarget. Neither is defined on platforms/configurations that support
// neither (e.g. a Vulkan-less Linux build, or a platform this driver hasn't been ported to) —
// in that case RenderingOffscreenTarget compiles but create_and_setup_rendering_context()
// always fails at runtime rather than the whole translation unit failing to compile, since
// this header is compiled on every platform (see servers/rendering/SCsub's glob).
#if (defined(MACOS_ENABLED) || defined(IOS_ENABLED)) && defined(METAL_ENABLED)
#define RENDERING_OFFSCREEN_TARGET_METAL 1
#include <IOSurface/IOSurfaceRef.h>
#elif (defined(LINUXBSD_ENABLED) || defined(ANDROID_ENABLED) || defined(WINDOWS_ENABLED)) && defined(VULKAN_ENABLED)
#define RENDERING_OFFSCREEN_TARGET_VULKAN 1
#include "drivers/vulkan/rendering_context_driver_vulkan.h"
#endif

class RenderingContextDriver;

// Owns the platform-specific machinery that renders into a small ring of GPU images and
// exports each finished frame as a native, OS-level shareable handle (IOSurface on macOS/iOS,
// dmabuf on Linux, AHardwareBuffer on Android, a D3D11 shared handle on Windows) instead of
// presenting to a real window. Used by DisplayServerOffscreen (servers/display/display_server_offscreen.h)
// to embed Godot's rendering output into a host application without Godot ever creating an
// on-screen window, a CALayer, an ANativeWindow, or any other native windowing-system surface.
//
// This does NOT wrap an existing native surface the way a "give Godot a real window to render
// into" embedding API would — there is no windowing-system object involved at all. It exists
// purely to hold the offscreen rendering context and forward finished frames to whoever
// registers a callback via set_frame_available_callback().
class RenderingOffscreenTarget : public RefCounted {
	GDCLASS(RenderingOffscreenTarget, RefCounted)

protected:
	static void _bind_methods();

public:
	// Rendering drivers this build can use for offscreen export (exactly one entry in
	// practice: "metal" on macOS/iOS, "vulkan" everywhere else Vulkan-based export is
	// implemented; empty if neither RENDERING_OFFSCREEN_TARGET_METAL nor
	// RENDERING_OFFSCREEN_TARGET_VULKAN applies to this build).
	Vector<String> get_supported_rendering_drivers() const;

	// Creates the platform's RenderingContextDriver (Metal or RenderingContextDriverVulkanOffscreen),
	// initializes it, and creates its offscreen window (DisplayServerEnums::MAIN_WINDOW_ID) with
	// a small buffered ring, wiring this object's present-callback trampoline so frames flow to
	// set_frame_available_callback()'s registrant. The caller (DisplayServerOffscreen) owns the
	// returned driver — sizing/vsync calls and eventual memdelete() are its responsibility.
	RenderingContextDriver *create_and_setup_rendering_context(const String &p_rendering_driver, Error &r_error);

	void set_frame_available_callback(const Callable &p_callback);

	// Drains the most recently produced frame (if any) and invokes the registered callback.
	// Must be called from the main thread; frames are produced by a present-callback trampoline
	// that may run on an arbitrary platform/driver-owned thread (a Metal command buffer
	// completion handler, a Vulkan present thread, etc.), so that trampoline never touches
	// Callable/Variant directly — see pending_frame_mutex below.
	void deliver_pending_frame();

	RenderingOffscreenTarget();
	~RenderingOffscreenTarget();

private:
	Callable frame_available_callback;

	Mutex pending_frame_mutex;
	bool has_pending_frame = false;
	uint32_t pending_width = 0;
	uint32_t pending_height = 0;

#if defined(RENDERING_OFFSCREEN_TARGET_METAL)
	uint32_t pending_iosurface_id = 0;

	static void _offscreen_present_callback(void *p_userdata, IOSurfaceRef p_surface, uint32_t p_width, uint32_t p_height);
#elif defined(RENDERING_OFFSCREEN_TARGET_VULKAN)
#if defined(LINUXBSD_ENABLED)
	int pending_dmabuf_fd = -1;
	uint32_t pending_drm_format = 0;
	uint32_t pending_stride = 0;
	uint32_t pending_offset = 0;
	uint64_t pending_modifier = 0;
#elif defined(ANDROID_ENABLED)
	void *pending_hardware_buffer = nullptr;
#elif defined(WINDOWS_ENABLED)
	void *pending_shared_handle = nullptr;
	uint64_t pending_fence_value = 0;
#endif

	static void _offscreen_present_callback(void *p_userdata, const RenderingContextDriverVulkan::OffscreenExportedSurface *p_exported_surface, uint32_t p_width, uint32_t p_height);
#endif
};
