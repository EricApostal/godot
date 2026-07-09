/**************************************************************************/
/*  display_server_linuxbsd_offscreen.cpp                                */
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

#ifdef VULKAN_ENABLED

#include "display_server_linuxbsd_offscreen.h"

#include "core/input/input.h"
#include "core/input/input_event.h"
#include "core/os/os.h"
#include "core/variant/dictionary.h"
#include "drivers/vulkan/rendering_context_driver_vulkan_offscreen.h"
#include "servers/display/native_menu.h"
#include "servers/rendering/renderer_rd/renderer_compositor_rd.h"
#include "servers/rendering/rendering_device.h"

DisplayServerLinuxBSDOffscreen::DisplayServerLinuxBSDOffscreen(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, Error &r_error) {
	r_error = OK; // default to OK

	native_menu = memnew(NativeMenu);

	Input::get_singleton()->set_event_dispatch_function(_dispatch_input_events);

	rendering_driver = p_rendering_driver;
	window_size = p_resolution;
	transparent = ((p_flags & DisplayServerEnums::WINDOW_FLAG_TRANSPARENT_BIT) == DisplayServerEnums::WINDOW_FLAG_TRANSPARENT_BIT);

	if (rendering_driver != "vulkan") {
		r_error = ERR_CANT_CREATE;
		ERR_FAIL_MSG(vformat("The offscreen display driver only supports the Vulkan rendering driver, but \"%s\" was requested.", rendering_driver));
	}

	// Uses a dedicated context driver that never talks to X11/Wayland at all, rather than
	// reusing RenderingContextDriverVulkanX11/Wayland — those require a running X server /
	// Wayland compositor to even initialize, which offscreen mode has no need for.
	rendering_context = memnew(RenderingContextDriverVulkanOffscreen);

	if (rendering_context->initialize() != OK) {
		memdelete(rendering_context);
		rendering_context = nullptr;
		r_error = ERR_CANT_CREATE;
		ERR_FAIL_MSG("Could not initialize Vulkan.");
	}

	RenderingContextDriverVulkanOffscreen::WindowPlatformData wpd;
	wpd.offscreen_buffer_count = 3;
	wpd.offscreen_present_callback = &DisplayServerLinuxBSDOffscreen::_offscreen_present_callback;
	wpd.offscreen_present_userdata = this;

	Error err = rendering_context->window_create(DisplayServerEnums::MAIN_WINDOW_ID, &wpd);
	ERR_FAIL_COND_MSG(err != OK, "Can't create a Vulkan offscreen context.");

	rendering_context->window_set_size(DisplayServerEnums::MAIN_WINDOW_ID, window_size.width, window_size.height);
	rendering_context->window_set_vsync_mode(DisplayServerEnums::MAIN_WINDOW_ID, p_vsync_mode);

	rendering_device = memnew(RenderingDevice);
	rendering_device->initialize(rendering_context, DisplayServerEnums::MAIN_WINDOW_ID);
	rendering_device->screen_create(DisplayServerEnums::MAIN_WINDOW_ID);

	RendererCompositorRD::make_current();
}

DisplayServerLinuxBSDOffscreen::~DisplayServerLinuxBSDOffscreen() {
	if (native_menu) {
		memdelete(native_menu);
		native_menu = nullptr;
	}

	if (rendering_device) {
		memdelete(rendering_device);
		rendering_device = nullptr;
	}

	if (rendering_context) {
		memdelete(rendering_context);
		rendering_context = nullptr;
	}
}

DisplayServer *DisplayServerLinuxBSDOffscreen::create_func(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t /* p_parent_window */, Error &r_error) {
	return memnew(DisplayServerLinuxBSDOffscreen(p_rendering_driver, p_mode, p_vsync_mode, p_flags, p_position, p_resolution, p_screen, p_context, r_error));
}

Vector<String> DisplayServerLinuxBSDOffscreen::get_rendering_drivers_func() {
	Vector<String> drivers;
	drivers.push_back("vulkan");
	return drivers;
}

void DisplayServerLinuxBSDOffscreen::register_offscreen_driver() {
	register_create_function("offscreen", create_func, get_rendering_drivers_func);
}

// MARK: - Offscreen frame delivery

void DisplayServerLinuxBSDOffscreen::offscreen_set_frame_available_callback(const Callable &p_callback) {
	frame_available_callback = p_callback;
}

void DisplayServerLinuxBSDOffscreen::_offscreen_present_callback(void *p_userdata, const RenderingContextDriverVulkan::OffscreenExportedSurface *p_exported_surface, uint32_t p_width, uint32_t p_height) {
	// May run on a non-main thread (see the comment on DisplayServerLinuxBSDOffscreen::pending_frame_mutex
	// in the header), so only stash the frame here and hand off to the main thread in process_events().
	DisplayServerLinuxBSDOffscreen *ds = (DisplayServerLinuxBSDOffscreen *)p_userdata;

	MutexLock lock(ds->pending_frame_mutex);
	ds->has_pending_frame = true;
	ds->pending_dmabuf_fd = p_exported_surface->dmabuf_fd;
	ds->pending_drm_format = p_exported_surface->drm_format;
	ds->pending_stride = p_exported_surface->stride;
	ds->pending_offset = p_exported_surface->offset;
	ds->pending_modifier = p_exported_surface->modifier;
	ds->pending_width = p_width;
	ds->pending_height = p_height;
}

void DisplayServerLinuxBSDOffscreen::_deliver_pending_frame() {
	bool deliver = false;
	int dmabuf_fd = -1;
	uint32_t drm_format = 0;
	uint32_t stride = 0;
	uint32_t offset = 0;
	uint64_t modifier = 0;
	uint32_t frame_width = 0;
	uint32_t frame_height = 0;

	{
		MutexLock lock(pending_frame_mutex);
		if (has_pending_frame) {
			deliver = true;
			dmabuf_fd = pending_dmabuf_fd;
			drm_format = pending_drm_format;
			stride = pending_stride;
			offset = pending_offset;
			modifier = pending_modifier;
			frame_width = pending_width;
			frame_height = pending_height;
			has_pending_frame = false;
		}
	}

	if (deliver && frame_available_callback.is_valid()) {
		Dictionary frame_data;
		// Matches GodotOffscreenSurfaceType::GODOT_OFFSCREEN_SURFACE_TYPE_DMABUF in libgodot.h.
		frame_data["type"] = 1;
		frame_data["fd"] = dmabuf_fd;
		frame_data["drm_format"] = (int64_t)drm_format;
		frame_data["stride"] = (int64_t)stride;
		frame_data["offset"] = (int64_t)offset;
		frame_data["modifier"] = (int64_t)modifier;
		frame_data["width"] = frame_width;
		frame_data["height"] = frame_height;
		_window_callback(frame_available_callback, frame_data);
	}
}

// MARK: Events

void DisplayServerLinuxBSDOffscreen::window_set_rect_changed_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	window_resize_callbacks[p_window] = p_callable;
}

void DisplayServerLinuxBSDOffscreen::window_set_window_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	window_event_callbacks[p_window] = p_callable;
}

void DisplayServerLinuxBSDOffscreen::window_set_input_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	input_event_callbacks[p_window] = p_callable;
}

void DisplayServerLinuxBSDOffscreen::window_set_input_text_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	input_text_callbacks[p_window] = p_callable;
}

void DisplayServerLinuxBSDOffscreen::window_set_drop_files_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

void DisplayServerLinuxBSDOffscreen::process_events() {
	Input *input = Input::get_singleton();
	input->flush_buffered_events();

	_deliver_pending_frame();
}

void DisplayServerLinuxBSDOffscreen::_dispatch_input_events(const Ref<InputEvent> &p_event) {
	Ref<InputEventFromWindow> event_from_window = p_event;
	DisplayServerEnums::WindowID window_id = DisplayServerEnums::INVALID_WINDOW_ID;
	if (event_from_window.is_valid()) {
		window_id = event_from_window->get_window_id();
	}
	DisplayServerLinuxBSDOffscreen *ds = (DisplayServerLinuxBSDOffscreen *)DisplayServer::get_singleton();
	ds->send_input_event(p_event, window_id);
}

void DisplayServerLinuxBSDOffscreen::send_input_event(const Ref<InputEvent> &p_event, DisplayServerEnums::WindowID p_id) const {
	if (p_id != DisplayServerEnums::INVALID_WINDOW_ID) {
		const Callable *cb = input_event_callbacks.getptr(p_id);
		if (cb) {
			_window_callback(*cb, p_event);
		}
	} else {
		for (const KeyValue<DisplayServerEnums::WindowID, Callable> &E : input_event_callbacks) {
			_window_callback(E.value, p_event);
		}
	}
}

void DisplayServerLinuxBSDOffscreen::send_input_text(const String &p_text, DisplayServerEnums::WindowID p_id) const {
	const Callable *cb = input_text_callbacks.getptr(p_id);
	if (cb) {
		_window_callback(*cb, p_text);
	}
}

void DisplayServerLinuxBSDOffscreen::send_window_event_by_id(DisplayServerEnums::WindowEvent p_event, DisplayServerEnums::WindowID p_id) const {
	const Callable *cb = window_event_callbacks.getptr(p_id);
	if (cb) {
		_window_callback(*cb, int(p_event));
	}
}

void DisplayServerLinuxBSDOffscreen::_window_callback(const Callable &p_callable, const Variant &p_arg) const {
	if (p_callable.is_valid()) {
		p_callable.call(p_arg);
	}
}

// MARK: -

bool DisplayServerLinuxBSDOffscreen::has_feature(DisplayServerEnums::Feature p_feature) const {
	// None of the optional features (clipboard, IME, cursor shapes, etc.) are implemented by
	// this driver; a host embedding Godot offscreen is expected to own those integrations
	// itself rather than relying on DisplayServer for them.
	return false;
}

String DisplayServerLinuxBSDOffscreen::get_name() const {
	return "offscreen";
}

int DisplayServerLinuxBSDOffscreen::get_screen_count() const {
	return 1;
}

int DisplayServerLinuxBSDOffscreen::get_primary_screen() const {
	return 0;
}

Point2i DisplayServerLinuxBSDOffscreen::screen_get_position(int p_screen) const {
	return Point2i();
}

Size2i DisplayServerLinuxBSDOffscreen::screen_get_size(int p_screen) const {
	return window_get_size(DisplayServerEnums::MAIN_WINDOW_ID);
}

Rect2i DisplayServerLinuxBSDOffscreen::screen_get_usable_rect(int p_screen) const {
	return Rect2i(screen_get_position(p_screen), screen_get_size(p_screen));
}

int DisplayServerLinuxBSDOffscreen::screen_get_dpi(int p_screen) const {
	return 96;
}

float DisplayServerLinuxBSDOffscreen::screen_get_refresh_rate(int p_screen) const {
	return 60.0f;
}

Vector<DisplayServerEnums::WindowID> DisplayServerLinuxBSDOffscreen::get_window_list() const {
	Vector<DisplayServerEnums::WindowID> list;
	list.push_back(DisplayServerEnums::MAIN_WINDOW_ID);
	return list;
}

DisplayServerEnums::WindowID DisplayServerLinuxBSDOffscreen::get_window_at_screen_position(const Point2i &p_position) const {
	return DisplayServerEnums::MAIN_WINDOW_ID;
}

void DisplayServerLinuxBSDOffscreen::window_attach_instance_id(ObjectID p_instance, DisplayServerEnums::WindowID p_window) {
	window_attached_instance_id[p_window] = p_instance;
}

ObjectID DisplayServerLinuxBSDOffscreen::window_get_attached_instance_id(DisplayServerEnums::WindowID p_window) const {
	return window_attached_instance_id[p_window];
}

void DisplayServerLinuxBSDOffscreen::window_set_title(const String &p_title, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

int DisplayServerLinuxBSDOffscreen::window_get_current_screen(DisplayServerEnums::WindowID p_window) const {
	ERR_FAIL_COND_V(p_window != DisplayServerEnums::MAIN_WINDOW_ID, DisplayServerEnums::INVALID_SCREEN);
	return 0;
}

void DisplayServerLinuxBSDOffscreen::window_set_current_screen(int p_screen, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

Point2i DisplayServerLinuxBSDOffscreen::window_get_position(DisplayServerEnums::WindowID p_window) const {
	return Point2i();
}

Point2i DisplayServerLinuxBSDOffscreen::window_get_position_with_decorations(DisplayServerEnums::WindowID p_window) const {
	return Point2i();
}

void DisplayServerLinuxBSDOffscreen::window_set_position(const Point2i &p_position, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

void DisplayServerLinuxBSDOffscreen::window_set_transient(DisplayServerEnums::WindowID p_window, DisplayServerEnums::WindowID p_parent) {
	// Not supported.
}

void DisplayServerLinuxBSDOffscreen::window_set_max_size(const Size2i p_size, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

Size2i DisplayServerLinuxBSDOffscreen::window_get_max_size(DisplayServerEnums::WindowID p_window) const {
	return Size2i();
}

void DisplayServerLinuxBSDOffscreen::window_set_min_size(const Size2i p_size, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

Size2i DisplayServerLinuxBSDOffscreen::window_get_min_size(DisplayServerEnums::WindowID p_window) const {
	return Size2i();
}

void DisplayServerLinuxBSDOffscreen::window_set_size(const Size2i p_size, DisplayServerEnums::WindowID p_window) {
	if (p_size.width <= 0 || p_size.height <= 0 || window_size == p_size) {
		return;
	}

	window_size = p_size;

	if (rendering_context) {
		rendering_context->window_set_size(p_window, window_size.width, window_size.height);
	}

	Callable *cb = window_resize_callbacks.getptr(p_window);
	if (cb) {
		Variant resize_rect = Rect2i(Point2i(), window_size);
		_window_callback(*cb, resize_rect);
	}
}

Size2i DisplayServerLinuxBSDOffscreen::window_get_size(DisplayServerEnums::WindowID p_window) const {
	return window_size;
}

Size2i DisplayServerLinuxBSDOffscreen::window_get_size_with_decorations(DisplayServerEnums::WindowID p_window) const {
	return window_get_size(p_window);
}

void DisplayServerLinuxBSDOffscreen::window_set_mode(DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

DisplayServerEnums::WindowMode DisplayServerLinuxBSDOffscreen::window_get_mode(DisplayServerEnums::WindowID p_window) const {
	return DisplayServerEnums::WindowMode::WINDOW_MODE_WINDOWED;
}

bool DisplayServerLinuxBSDOffscreen::window_is_maximize_allowed(DisplayServerEnums::WindowID p_window) const {
	return false;
}

void DisplayServerLinuxBSDOffscreen::window_set_flag(DisplayServerEnums::WindowFlags p_flag, bool p_enabled, DisplayServerEnums::WindowID p_window) {
	if (p_flag == DisplayServerEnums::WINDOW_FLAG_TRANSPARENT && p_window == DisplayServerEnums::MAIN_WINDOW_ID) {
		transparent = p_enabled;
	}
}

bool DisplayServerLinuxBSDOffscreen::window_get_flag(DisplayServerEnums::WindowFlags p_flag, DisplayServerEnums::WindowID p_window) const {
	if (p_flag == DisplayServerEnums::WINDOW_FLAG_TRANSPARENT && p_window == DisplayServerEnums::MAIN_WINDOW_ID) {
		return transparent;
	}
	return false;
}

void DisplayServerLinuxBSDOffscreen::window_request_attention(DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

void DisplayServerLinuxBSDOffscreen::window_move_to_foreground(DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

bool DisplayServerLinuxBSDOffscreen::window_is_focused(DisplayServerEnums::WindowID p_window) const {
	return true;
}

float DisplayServerLinuxBSDOffscreen::screen_get_max_scale() const {
	return 1.0f;
}

bool DisplayServerLinuxBSDOffscreen::window_can_draw(DisplayServerEnums::WindowID p_window) const {
	return true;
}

bool DisplayServerLinuxBSDOffscreen::can_any_window_draw() const {
	return true;
}

void DisplayServerLinuxBSDOffscreen::window_set_vsync_mode(DisplayServerEnums::VSyncMode p_vsync_mode, DisplayServerEnums::WindowID p_window) {
	if (rendering_context) {
		rendering_context->window_set_vsync_mode(p_window, p_vsync_mode);
	}
}

DisplayServerEnums::VSyncMode DisplayServerLinuxBSDOffscreen::window_get_vsync_mode(DisplayServerEnums::WindowID p_window) const {
	if (rendering_context) {
		return rendering_context->window_get_vsync_mode(p_window);
	}
	return DisplayServerEnums::VSYNC_ENABLED;
}

void DisplayServerLinuxBSDOffscreen::swap_buffers() {
	// Only the Vulkan (RenderingDevice) path is supported; nothing to do here.
}

#endif // VULKAN_ENABLED
