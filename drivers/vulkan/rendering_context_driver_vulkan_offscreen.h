/**************************************************************************/
/*  rendering_context_driver_vulkan_offscreen.h                           */
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

#ifdef VULKAN_ENABLED

#include "rendering_context_driver_vulkan.h"

// A RenderingContextDriverVulkan that never talks to any windowing system (X11, Wayland,
// Win32, Android's ANativeWindow, ...) at all — every surface it creates is offscreen (see
// RenderingContextDriverVulkan::Surface::offscreen). Used by the "offscreen" display driver
// (see e.g. platform/linuxbsd/display_server_linuxbsd_offscreen.cpp) instead of that
// platform's normal windowing-specific context driver (RenderingContextDriverVulkanX11, etc.),
// so offscreen embedding doesn't depend on X11/Wayland/etc. being compiled in or available at
// runtime (e.g. no X server / no Wayland compositor reachable) — reasonable to expect of a
// process that's never going to open a real window in the first place.
class RenderingContextDriverVulkanOffscreen : public RenderingContextDriverVulkan {
protected:
	SurfaceID surface_create(const void *p_platform_data) override final;

public:
	struct WindowPlatformData {
		uint32_t offscreen_buffer_count = 3;
		OffscreenPresentCallback offscreen_present_callback = nullptr;
		void *offscreen_present_userdata = nullptr;
	};

	RenderingContextDriverVulkanOffscreen();
	~RenderingContextDriverVulkanOffscreen();
};

#endif // VULKAN_ENABLED
