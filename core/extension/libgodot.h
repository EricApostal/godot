/**************************************************************************/
/*  libgodot.h                                                            */
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

#include "core/extension/gdextension_interface.gen.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// Export macros for DLL visibility
#if defined(_MSC_VER) || defined(__MINGW32__)
#define LIBGODOT_API __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define LIBGODOT_API __attribute__((visibility("default")))
#else
#define LIBGODOT_API
#endif

/**
 * @name libgodot_create_godot_instance
 * @since 4.6
 *
 * Creates a new Godot instance.
 *
 * @param p_argc The number of command line arguments.
 * @param p_argv The C-style array of command line arguments.
 * @param p_init_func GDExtension initialization function of the host application.
 *
 * @return A pointer to created \ref GodotInstance GDExtension object or nullptr if there was an error.
 */
LIBGODOT_API GDExtensionObjectPtr libgodot_create_godot_instance(int p_argc, char *p_argv[], GDExtensionInitializationFunction p_init_func);

/**
 * @name libgodot_destroy_godot_instance
 * @since 4.6
 *
 * Destroys an existing Godot instance.
 *
 * @param p_godot_instance The reference to the GodotInstance object to destroy.
 *
 */
LIBGODOT_API void libgodot_destroy_godot_instance(GDExtensionObjectPtr p_godot_instance);

/**
 * @name libgodot_godot_instance_start
 * @since 4.6
 *
 * Convenience wrapper around GodotInstance::start(), for callers who don't want to talk to
 * the GDExtension/Variant ABI just to drive the instance's lifecycle.
 *
 * @param p_godot_instance The GodotInstance object returned by \ref libgodot_create_godot_instance.
 *
 * @return True if the instance started successfully.
 */
LIBGODOT_API GDExtensionBool libgodot_godot_instance_start(GDExtensionObjectPtr p_godot_instance);

/**
 * @name libgodot_godot_instance_iteration
 * @since 4.6
 *
 * Convenience wrapper around GodotInstance::iteration(). Runs a single iteration of a
 * previously started instance's main loop; the caller is expected to call this repeatedly
 * (e.g. from a display link or timer) to keep the engine running.
 *
 * @param p_godot_instance The GodotInstance object returned by \ref libgodot_create_godot_instance.
 *
 * @return True if the engine wants to quit (the caller should stop calling this and destroy
 * the instance), false to keep iterating.
 */
LIBGODOT_API GDExtensionBool libgodot_godot_instance_iteration(GDExtensionObjectPtr p_godot_instance);

/**
 * @name GodotOffscreenFrame
 * @since 4.6
 *
 * Describes a single rendered frame delivered by \ref libgodot_godot_instance_set_offscreen_frame_callback,
 * for the "offscreen" display driver. `native_surface_id` is platform-specific; on macOS it
 * is an `IOSurfaceID` (see `IOSurfaceLookup`). The underlying native surface is owned by the
 * engine and reused across frames (double/triple buffered); the host must bracket any access
 * to it with the platform's equivalent of `IOSurfaceIncrementUseCount`/`IOSurfaceDecrementUseCount`
 * so the engine doesn't start rendering the next frame into a buffer the host is still reading.
 */
typedef struct {
	uint64_t native_surface_id;
	uint32_t width;
	uint32_t height;
} GodotOffscreenFrame;

typedef void (*GodotOffscreenFrameCallback)(void *p_userdata, const GodotOffscreenFrame *p_frame);

/**
 * @name libgodot_godot_instance_set_offscreen_frame_callback
 * @since 4.6
 *
 * Convenience wrapper around DisplayServer::offscreen_set_frame_available_callback(), for
 * callers who don't want to talk to the GDExtension/Variant ABI just to receive frames from
 * the "offscreen" display driver (see `--offscreen` in \ref libgodot_create_godot_instance).
 *
 * Only meaningful once the instance has been started and is running the "offscreen" display
 * driver; a no-op (with an engine-side warning) otherwise.
 *
 * @param p_godot_instance The GodotInstance object returned by \ref libgodot_create_godot_instance.
 * @param p_callback The callback to invoke for each new frame, or NULL to unregister.
 * @param p_userdata Opaque pointer passed back to `p_callback`.
 */
LIBGODOT_API void libgodot_godot_instance_set_offscreen_frame_callback(GDExtensionObjectPtr p_godot_instance, GodotOffscreenFrameCallback p_callback, void *p_userdata);

#ifdef __cplusplus
}
#endif // __cplusplus
