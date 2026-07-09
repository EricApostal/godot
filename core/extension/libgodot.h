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
 * @name GodotOffscreenSurfaceType
 * @since 4.6
 *
 * Identifies which member of GodotOffscreenFrame::surface is populated. One display driver
 * (and therefore one platform) only ever produces one of these.
 */
typedef enum {
	GODOT_OFFSCREEN_SURFACE_TYPE_IOSURFACE = 0, // macOS, iOS.
	GODOT_OFFSCREEN_SURFACE_TYPE_DMABUF = 1, // Linux (X11, Wayland).
	GODOT_OFFSCREEN_SURFACE_TYPE_AHARDWAREBUFFER = 2, // Android.
	GODOT_OFFSCREEN_SURFACE_TYPE_D3D11_SHARED_HANDLE = 3, // Windows.
} GodotOffscreenSurfaceType;

/**
 * @name GodotOffscreenFrame
 * @since 4.6
 *
 * Describes a single rendered frame delivered by \ref libgodot_godot_instance_set_offscreen_frame_callback,
 * for the "offscreen" display driver. The underlying native surface/buffer is owned by the
 * engine and reused across frames from a small fixed-size ring (double/triple buffered); the
 * engine does not, in general, wait for the host to finish reading a buffer before reusing its
 * ring slot — see the per-platform notes on `surface` below for what (if anything) each
 * backend does to avoid the engine writing a buffer the host is still reading.
 */
typedef struct {
	GodotOffscreenSurfaceType type;
	uint32_t width;
	uint32_t height;

	union {
		// GODOT_OFFSCREEN_SURFACE_TYPE_IOSURFACE.
		//
		// `iosurface_id` is an IOSurfaceID (see IOSurfaceLookup()). The engine does not wait
		// for the host; bracket reads with IOSurfaceIncrementUseCount()/IOSurfaceDecrementUseCount()
		// for a stronger guarantee against tearing.
		struct {
			uint32_t iosurface_id;
		} iosurface;

		// GODOT_OFFSCREEN_SURFACE_TYPE_DMABUF.
		//
		// `fd` is owned by the engine; the host must dup() it (e.g. implicitly, by importing
		// it into EGL/Vulkan) if it needs to keep using it past this callback. Synchronization
		// against the engine's GPU writes relies on the kernel's implicit dma-buf fencing
		// (honored by any well-behaved DRM/Vulkan/EGL driver importing the buffer) rather than
		// on the host waiting for anything from the engine explicitly.
		struct {
			int fd;
			uint32_t drm_format; // DRM fourcc (see drm_fourcc.h), e.g. DRM_FORMAT_ARGB8888.
			uint32_t stride;
			uint32_t offset;
			uint64_t modifier; // DRM format modifier; DRM_FORMAT_MOD_LINEAR (0) unless the driver requested a specific tiling/compression layout.
		} dmabuf;

		// GODOT_OFFSCREEN_SURFACE_TYPE_AHARDWAREBUFFER.
		//
		// `hardware_buffer` is an `AHardwareBuffer *`, owned by the engine. If the host needs
		// to keep using it past this callback, it must call `AHardwareBuffer_acquire()` (and
		// eventually `AHardwareBuffer_release()`). As with dmabuf, synchronization relies on
		// the platform's implicit fencing for the buffer (the same mechanism `ANativeWindow`/
		// `SurfaceTexture` consumers rely on), not on the host waiting for the engine.
		struct {
			void *hardware_buffer;
		} ahardwarebuffer;

		// GODOT_OFFSCREEN_SURFACE_TYPE_D3D11_SHARED_HANDLE.
		//
		// `shared_handle` is a Windows `HANDLE` (from `IDXGIResource1::CreateSharedHandle`)
		// naming a D3D11 shared texture that aliases the engine's Vulkan-rendered image (via
		// `VK_KHR_external_memory_win32`); open it with `ID3D11Device1::OpenSharedResource1`.
		// Unlike the other backends, the host *must* synchronize explicitly: acquire the
		// paired keyed mutex (`IDXGIKeyedMutex::AcquireSync` with the key given in `fence_value`)
		// before reading, and release it when done, or it will race the engine's next write.
		struct {
			void *shared_handle;
			uint64_t fence_value;
		} d3d11;
	} surface;
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
