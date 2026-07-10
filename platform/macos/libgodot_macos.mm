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
#include "core/extension/libgodot_helpers.h"
#include "main/main.h"
#include "servers/display/display_server_offscreen.h"
#include "servers/rendering/rendering_offscreen_target.h"

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

		// RenderingOffscreenTarget is a GDCLASS/RefCounted Object, so it can't be constructed
		// until core systems (StringName, ClassDB) are up — i.e. not before Main::setup(), only
		// after. DisplayServer::create() (which reads this) doesn't happen until Main::setup2(),
		// called later via GodotInstance::start()/libgodot_godot_instance_start(), so setting it
		// here is still in time.
		if (is_offscreen) {
			Ref<RenderingOffscreenTarget> offscreen_target;
			offscreen_target.instantiate();
			DisplayServerOffscreen::set_offscreen_target(offscreen_target);
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

// libgodot_godot_instance_start/iteration/set_offscreen_frame_callback are implemented in
// libgodot_helpers.h (included above); see the comment there for why they need to live in
// this translation unit rather than a shared core/extension/*.cpp file.
