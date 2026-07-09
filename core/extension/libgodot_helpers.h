/**************************************************************************/
/*  libgodot_helpers.h                                                    */
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

// Shared implementation for the platform-agnostic parts of the libgodot.h convenience API
// (libgodot_godot_instance_start/iteration/set_offscreen_frame_callback). These only forward
// to already cross-platform engine APIs (GodotInstance, DisplayServer), so the logic doesn't
// need to be duplicated per platform — but the *definitions* do need to live in each
// platform's libgodot_<platform> source file rather than their own translation unit.
//
// Why: libgodot_create_godot_instance()/libgodot_destroy_godot_instance() are implemented per
// platform in files (e.g. platform/macos/libgodot_macos.mm) that are passed directly to the
// final shared/static library link. A generic core/extension/*.cpp file, by contrast, is
// compiled into the static "core" archive, and since nothing inside the engine calls these
// functions (only external hosts do), the linker's archive-member selection never has a
// reason to pull that translation unit's object file into the final link — so the symbols
// would silently go missing from the built library. Including this header directly into each
// platform's libgodot_<platform> file sidesteps that: the symbols are guaranteed part of that
// TU's object file, which is always in the link.
//
// Usage: `#include "core/extension/libgodot_helpers.h"` once from each platform's
// libgodot_<platform> file, after `#include "core/extension/godot_instance.h"`.

#include "libgodot.h"

#include <cstring>

#include "core/extension/godot_instance.h"
#include "core/variant/callable.h"
#include "core/variant/dictionary.h"
#include "servers/display/display_server.h"

namespace {

// Wraps a raw C function pointer as a Callable, so libgodot_godot_instance_set_offscreen_frame_callback()
// callers don't need to speak the GDExtension/Variant ABI just to register a frame callback.
class OffscreenFrameCallableCustom : public CallableCustom {
	GodotOffscreenFrameCallback callback = nullptr;
	void *userdata = nullptr;

public:
	OffscreenFrameCallableCustom(GodotOffscreenFrameCallback p_callback, void *p_userdata) :
			callback(p_callback), userdata(p_userdata) {}

	uint32_t hash() const override {
		return uint32_t((uintptr_t)callback) ^ uint32_t((uintptr_t)userdata);
	}

	String get_as_text() const override {
		return "<native offscreen frame callback>";
	}

	static bool compare_equal(const CallableCustom *p_a, const CallableCustom *p_b) {
		const OffscreenFrameCallableCustom *a = static_cast<const OffscreenFrameCallableCustom *>(p_a);
		const OffscreenFrameCallableCustom *b = static_cast<const OffscreenFrameCallableCustom *>(p_b);
		return a->callback == b->callback && a->userdata == b->userdata;
	}

	static bool compare_less(const CallableCustom *p_a, const CallableCustom *p_b) {
		const OffscreenFrameCallableCustom *a = static_cast<const OffscreenFrameCallableCustom *>(p_a);
		const OffscreenFrameCallableCustom *b = static_cast<const OffscreenFrameCallableCustom *>(p_b);
		if (a->callback != b->callback) {
			return (uintptr_t)a->callback < (uintptr_t)b->callback;
		}
		return a->userdata < b->userdata;
	}

	CompareEqualFunc get_compare_equal_func() const override {
		return compare_equal;
	}

	CompareLessFunc get_compare_less_func() const override {
		return compare_less;
	}

	// This callable isn't bound to any Godot Object, so it has no natural lifetime to check
	// against; the base CallableCustom::is_valid() default (which checks get_object() against
	// the ObjectDB) would always report it as invalid. It's valid for as long as the host
	// keeps it registered.
	bool is_valid() const override {
		return true;
	}

	ObjectID get_object() const override {
		return ObjectID();
	}

	// Keys the DisplayServer's frame_data Dictionary is expected to carry; see the per-type
	// comments on GodotOffscreenFrame::surface in libgodot.h for what each one means.
	void call(const Variant **p_arguments, int p_argcount, Variant &r_return_value, Callable::CallError &r_call_error) const override {
		r_call_error.error = Callable::CallError::CALL_OK;
		r_return_value = Variant();

		if (callback == nullptr || p_argcount < 1 || p_arguments[0] == nullptr || p_arguments[0]->get_type() != Variant::DICTIONARY) {
			return;
		}

		Dictionary frame_data = *p_arguments[0];
		GodotOffscreenFrame frame;
		memset(&frame, 0, sizeof(frame));
		frame.type = (GodotOffscreenSurfaceType)(int64_t)frame_data.get("type", 0);
		frame.width = (uint32_t)(int64_t)frame_data.get("width", 0);
		frame.height = (uint32_t)(int64_t)frame_data.get("height", 0);

		switch (frame.type) {
			case GODOT_OFFSCREEN_SURFACE_TYPE_IOSURFACE: {
				frame.surface.iosurface.iosurface_id = (uint32_t)(int64_t)frame_data.get("iosurface_id", 0);
			} break;
			case GODOT_OFFSCREEN_SURFACE_TYPE_DMABUF: {
				frame.surface.dmabuf.fd = (int)(int64_t)frame_data.get("fd", -1);
				frame.surface.dmabuf.drm_format = (uint32_t)(int64_t)frame_data.get("drm_format", 0);
				frame.surface.dmabuf.stride = (uint32_t)(int64_t)frame_data.get("stride", 0);
				frame.surface.dmabuf.offset = (uint32_t)(int64_t)frame_data.get("offset", 0);
				frame.surface.dmabuf.modifier = (uint64_t)(int64_t)frame_data.get("modifier", 0);
			} break;
			case GODOT_OFFSCREEN_SURFACE_TYPE_AHARDWAREBUFFER: {
				frame.surface.ahardwarebuffer.hardware_buffer = (void *)(uintptr_t)(int64_t)frame_data.get("hardware_buffer", 0);
			} break;
			case GODOT_OFFSCREEN_SURFACE_TYPE_D3D11_SHARED_HANDLE: {
				frame.surface.d3d11.shared_handle = (void *)(uintptr_t)(int64_t)frame_data.get("shared_handle", 0);
				frame.surface.d3d11.fence_value = (uint64_t)(int64_t)frame_data.get("fence_value", 0);
			} break;
		}

		callback(userdata, &frame);
	}
};

} // namespace

// Deliberately not `inline`: an unreferenced `inline` definition gets linkonce/weak linkage,
// which dead-code stripping can (and does) discard from the final shared library even though
// LIBGODOT_API asks for default visibility — the visibility attribute controls whether an
// exported symbol is *visible*, not whether the linker considers it *reachable*. Ordinary
// external definitions don't have this problem. This relies on this header only ever being
// included by exactly one translation unit per build (see the file-level comment above).
GDExtensionBool libgodot_godot_instance_start(GDExtensionObjectPtr p_godot_instance) {
	GodotInstance *godot_instance = (GodotInstance *)p_godot_instance;
	ERR_FAIL_NULL_V(godot_instance, false);
	return godot_instance->start();
}

GDExtensionBool libgodot_godot_instance_iteration(GDExtensionObjectPtr p_godot_instance) {
	GodotInstance *godot_instance = (GodotInstance *)p_godot_instance;
	ERR_FAIL_NULL_V(godot_instance, true);
	return godot_instance->iteration();
}

void libgodot_godot_instance_set_offscreen_frame_callback(GDExtensionObjectPtr p_godot_instance, GodotOffscreenFrameCallback p_callback, void *p_userdata) {
	ERR_FAIL_NULL(p_godot_instance);

	DisplayServer *display_server = DisplayServer::get_singleton();
	ERR_FAIL_NULL(display_server);

	if (p_callback == nullptr) {
		display_server->offscreen_set_frame_available_callback(Callable());
		return;
	}

	Callable callable = Callable(memnew(OffscreenFrameCallableCustom(p_callback, p_userdata)));
	display_server->offscreen_set_frame_available_callback(callable);
}
