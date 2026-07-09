/**************************************************************************/
/*  libgodot_ios.mm                                                       */
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

// Unlike platform/macos/libgodot_macos.mm, this does not need to choose between OS subclasses:
// OS_IOS's constructor has no UIApplication/UIViewController dependency (unlike
// DisplayServerAppleEmbedded's constructor, which does), so the same OS_IOS works whether the
// "iOS" (on-screen, requires a live UIApplication driven via main_ios.mm) or "offscreen"
// display driver ends up being selected by --display-driver/--offscreen. A host embedding
// Godot via this API is expected to use --offscreen; it owns its own UIApplication/UIWindow
// already, same as this file's macOS counterpart avoids touching NSApplication.

#import "os_ios.h"

#include "core/extension/godot_instance.h"
#include "core/extension/libgodot.h"
#include "core/extension/libgodot_helpers.h"
#include "main/main.h"

static OS_IOS *os = nullptr;

static GodotInstance *instance = nullptr;

GDExtensionObjectPtr libgodot_create_godot_instance(int p_argc, char *p_argv[], GDExtensionInitializationFunction p_init_func) {
	ERR_FAIL_COND_V_MSG(instance != nullptr, nullptr, "Only one Godot Instance may be created.");

	uint32_t remaining_args = p_argc - 1;
	char **args = remaining_args > 0 ? &p_argv[1] : nullptr;

	os = new OS_IOS();

	@autoreleasepool {
		Error err = Main::setup(p_argv[0], remaining_args, args, false);
		if (err != OK) {
			return nullptr;
		}

		os->initialize_modules();

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
