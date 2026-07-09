/**************************************************************************/
/*  libgodot_macos.mm                                                     */
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

#include "os_macos.h"

#include "core/extension/godot_instance.h"
#include "core/extension/libgodot.h"
#include "core/variant/callable.h"
#include "core/variant/dictionary.h"
#include "main/main.h"
#include "servers/display/display_server.h"

static OS_MacOS *os = nullptr;

static GodotInstance *instance = nullptr;

GDExtensionObjectPtr libgodot_create_godot_instance(int p_argc, char *p_argv[], GDExtensionInitializationFunction p_init_func) {
	ERR_FAIL_COND_V_MSG(instance != nullptr, nullptr, "Only one Godot Instance may be created.");

	uint32_t remaining_args = p_argc - 1;
	char **args = remaining_args > 0 ? &p_argv[1] : nullptr;

	// If the host passed `--offscreen`, render into a native GPU surface (IOSurface) that the
	// host can consume directly instead of Godot creating its own `NSWindow`/`NSApplication`
	// UI. This is the path host apps should use to embed Godot into their own view hierarchy.
	bool is_offscreen = false;
	for (uint32_t i = 0; i < remaining_args; i++) {
		if (strcmp("--offscreen", args[i]) == 0) {
			is_offscreen = true;
			break;
		}
	}

	if (is_offscreen) {
		os = new OS_MacOS_Offscreen(p_argv[0], remaining_args, args);
	} else {
		os = new OS_MacOS_NSApp(p_argv[0], remaining_args, args);
	}

	@autoreleasepool {
		Error err = Main::setup(p_argv[0], remaining_args, args, false);
		if (err != OK) {
			return nullptr;
		}

		instance = memnew(GodotInstance);
		if (!instance->initialize(p_init_func)) {
			memdelete(instance);
			instance = nullptr;
			return nullptr;
		}

		return (GDExtensionObjectPtr)instance;
	}
}

void libgodot_destroy_godot_instance(GDExtensionObjectPtr p_godot_instance) {
	GodotInstance *godot_instance = (GodotInstance *)p_godot_instance;
	if (instance == godot_instance) {
		godot_instance->stop();
		memdelete(godot_instance);
		// Note: When Godot Engine supports reinitialization, clear the instance pointer here.
		//instance = nullptr;
		Main::cleanup();
	}
}

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

	void call(const Variant **p_arguments, int p_argcount, Variant &r_return_value, Callable::CallError &r_call_error) const override {
		r_call_error.error = Callable::CallError::CALL_OK;
		r_return_value = Variant();

		if (callback == nullptr || p_argcount < 1 || p_arguments[0] == nullptr || p_arguments[0]->get_type() != Variant::DICTIONARY) {
			return;
		}

		Dictionary frame_data = *p_arguments[0];
		GodotOffscreenFrame frame;
		frame.native_surface_id = (uint64_t)(int64_t)frame_data.get("iosurface_id", 0);
		frame.width = (uint32_t)(int64_t)frame_data.get("width", 0);
		frame.height = (uint32_t)(int64_t)frame_data.get("height", 0);
		callback(userdata, &frame);
	}
};

} // namespace

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
