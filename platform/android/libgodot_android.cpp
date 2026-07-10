/**************************************************************************/
/*  libgodot_android.cpp                                                 */
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

// Unlike libgodot_macos.mm/libgodot_ios.mm/libgodot_linuxbsd.cpp/libgodot_windows.cpp,
// libgodot_create_godot_instance()/libgodot_destroy_godot_instance() are NOT implemented here.
// Android's engine bootstrap is fundamentally JNI-driven (see java_godot_lib_jni.cpp's
// Java_..._GodotLib_initialize/setup/step): it needs a live JNIEnv/JavaVM and non-null
// GodotJavaWrapper/GodotIOJavaWrapper Java objects (GodotJavaWrapper's method-bind lookup calls
// CallVoidMethod on them unconditionally, which is unsafe with a null receiver), neither of
// which a freestanding C function can supply. There is no way around this without a much larger
// rewrite of OS_Android's Java dependencies, which is out of scope here.
//
// So the actual bootstrap on Android is: a small Kotlin object (NOT a GodotActivity/GodotFragment
// — just enough to implement GodotNativeBridge/GodotIO's JNI-facing methods, mostly as one-line
// no-ops, with getDataDir()/getCacheDir()/getTempDir() delegating to the host's own
// android.content.Context) calls GodotLib.initialize()/setup()/step() directly, passing the
// address of the host's GDExtensionInitializationFunction as the new `p_init_func` parameter on
// GodotLib.setup() (0 for the standard GodotActivity runtime, which never touches any of this).
// GodotLib.setup() then constructs a GodotInstance purely to reuse its initialize() logic (see
// java_godot_lib_jni.cpp) — its start()/iteration() are deliberately never called, since
// GodotLib.step() already drives Main::setup2()/Main::start()/the main loop directly.
//
// What IS exposed here, matching every other platform's libgodot.h surface exactly, is frame
// delivery: libgodot_godot_instance_set_offscreen_frame_callback() (defined in
// core/extension/libgodot_helpers.h, included below) works identically once the host has
// obtained a GDExtensionObjectPtr via libgodot_android_get_godot_instance() below — this is the
// actual per-frame hot path, and it needs no JNI/Kotlin involvement at all once wired up.
//
// libgodot_godot_instance_start()/libgodot_godot_instance_iteration() (also defined in
// libgodot_helpers.h, and exported here for symbol-shape parity with the other platforms) MUST
// NOT be called on Android: GodotInstance::start() calls Main::setup2(), which GodotLib.step()
// already calls directly — calling both would run engine setup twice.

#include "java_godot_lib_jni.h"

#include "core/extension/godot_instance.h"
#include "core/extension/libgodot.h"
#include "core/extension/libgodot_helpers.h"

/**
 * @name libgodot_android_get_godot_instance
 * @since 4.6
 *
 * Android-specific. Returns the GodotInstance created by the most recent GodotLib.setup() call
 * that received a non-zero init-func pointer (see the file comment above), or nullptr if none —
 * either because GodotLib.setup() hasn't run yet, or because it ran with a zero init-func
 * (the standard GodotActivity/export runtime).
 *
 * @return The GodotInstance object to pass to libgodot_godot_instance_set_offscreen_frame_callback(),
 * or nullptr.
 */
LIBGODOT_API GDExtensionObjectPtr libgodot_android_get_godot_instance() {
	return (GDExtensionObjectPtr)android_get_embedded_godot_instance();
}
