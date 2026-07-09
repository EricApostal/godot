/**************************************************************************/
/*  display_server_ios_offscreen.mm                                      */
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

#import "display_server_ios_offscreen.h"

#import "core/input/input.h"
#import "core/input/input_event.h"
#import "core/os/os.h"
#import "core/variant/dictionary.h"
#import "servers/display/native_menu.h"

#import <IOSurface/IOSurfaceRef.h>

#if defined(RD_ENABLED)
#import "servers/rendering/renderer_rd/renderer_compositor_rd.h"
#import "servers/rendering/rendering_device.h"

#if defined(METAL_ENABLED)
#import "drivers/metal/rendering_context_driver_metal.h"
#endif
#endif // RD_ENABLED

DisplayServerIOSOffscreen::DisplayServerIOSOffscreen(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, Error &r_error) {
	r_error = OK; // default to OK

	native_menu = memnew(NativeMenu);

	Input::get_singleton()->set_event_dispatch_function(_dispatch_input_events);

	rendering_driver = p_rendering_driver;
	window_size = p_resolution;
	transparent = ((p_flags & DisplayServerEnums::WINDOW_FLAG_TRANSPARENT_BIT) == DisplayServerEnums::WINDOW_FLAG_TRANSPARENT_BIT);

#if defined(RD_ENABLED)
#if defined(METAL_ENABLED)
	if (rendering_driver == "metal") {
		if (@available(iOS 14.0, *)) {
			rendering_context = memnew(RenderingContextDriverMetal);
		} else {
			r_error = ERR_UNAVAILABLE;
			ERR_FAIL_MSG("Metal is only supported on iOS 14.0 and later.");
		}
	}
#endif

	if (rendering_context == nullptr) {
		r_error = ERR_CANT_CREATE;
		ERR_FAIL_MSG(vformat("The offscreen display driver only supports the Metal rendering driver, but \"%s\" was requested.", rendering_driver));
	}

	if (rendering_context->initialize() != OK) {
		memdelete(rendering_context);
		rendering_context = nullptr;
		r_error = ERR_CANT_CREATE;
		ERR_FAIL_MSG(vformat("Could not initialize %s.", rendering_driver));
	}

#if defined(METAL_ENABLED)
	RenderingContextDriverMetal::WindowPlatformData wpd;
	wpd.layer = nullptr;
	wpd.offscreen = true;
	wpd.offscreen_buffer_count = 3;
	wpd.offscreen_present_callback = &DisplayServerIOSOffscreen::_offscreen_present_callback;
	wpd.offscreen_present_userdata = this;

	Error err = rendering_context->window_create(DisplayServerEnums::MAIN_WINDOW_ID, &wpd);
	ERR_FAIL_COND_MSG(err != OK, vformat("Can't create a %s offscreen context.", rendering_driver));

	rendering_context->window_set_size(DisplayServerEnums::MAIN_WINDOW_ID, window_size.width, window_size.height);
	rendering_context->window_set_vsync_mode(DisplayServerEnums::MAIN_WINDOW_ID, p_vsync_mode);
#endif

	rendering_device = memnew(RenderingDevice);
	rendering_device->initialize(rendering_context, DisplayServerEnums::MAIN_WINDOW_ID);
	rendering_device->screen_create(DisplayServerEnums::MAIN_WINDOW_ID);

	RendererCompositorRD::make_current();
#endif // RD_ENABLED
}

DisplayServerIOSOffscreen::~DisplayServerIOSOffscreen() {
	if (native_menu) {
		memdelete(native_menu);
		native_menu = nullptr;
	}

#if defined(RD_ENABLED)
	if (rendering_device) {
		memdelete(rendering_device);
		rendering_device = nullptr;
	}

	if (rendering_context) {
		memdelete(rendering_context);
		rendering_context = nullptr;
	}
#endif
}

DisplayServer *DisplayServerIOSOffscreen::create_func(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t /* p_parent_window */, Error &r_error) {
	return memnew(DisplayServerIOSOffscreen(p_rendering_driver, p_mode, p_vsync_mode, p_flags, p_position, p_resolution, p_screen, p_context, r_error));
}

Vector<String> DisplayServerIOSOffscreen::get_rendering_drivers_func() {
	Vector<String> drivers;

#if defined(METAL_ENABLED)
	drivers.push_back("metal");
#endif

	return drivers;
}

void DisplayServerIOSOffscreen::register_offscreen_driver() {
	register_create_function("offscreen", create_func, get_rendering_drivers_func);
}

// MARK: - Offscreen frame delivery

void DisplayServerIOSOffscreen::offscreen_set_frame_available_callback(const Callable &p_callback) {
	frame_available_callback = p_callback;
}

void DisplayServerIOSOffscreen::_offscreen_present_callback(void *p_userdata, IOSurfaceRef p_surface, uint32_t p_width, uint32_t p_height) {
	// Called from a Metal command buffer completion handler; may run on an arbitrary
	// Metal-owned thread, so only stash the frame here and hand off to the main thread in
	// `process_events()`.
	DisplayServerIOSOffscreen *ds = (DisplayServerIOSOffscreen *)p_userdata;

	MutexLock lock(ds->pending_frame_mutex);
	ds->has_pending_frame = true;
	ds->pending_iosurface_id = IOSurfaceGetID(p_surface);
	ds->pending_width = p_width;
	ds->pending_height = p_height;
}

void DisplayServerIOSOffscreen::_deliver_pending_frame() {
	bool deliver = false;
	uint32_t iosurface_id = 0;
	uint32_t frame_width = 0;
	uint32_t frame_height = 0;

	{
		MutexLock lock(pending_frame_mutex);
		if (has_pending_frame) {
			deliver = true;
			iosurface_id = pending_iosurface_id;
			frame_width = pending_width;
			frame_height = pending_height;
			has_pending_frame = false;
		}
	}

	if (deliver && frame_available_callback.is_valid()) {
		Dictionary frame_data;
		// Matches GodotOffscreenSurfaceType::GODOT_OFFSCREEN_SURFACE_TYPE_IOSURFACE in libgodot.h.
		frame_data["type"] = 0;
		frame_data["iosurface_id"] = iosurface_id;
		frame_data["width"] = frame_width;
		frame_data["height"] = frame_height;
		_window_callback(frame_available_callback, frame_data);
	}
}

// MARK: Events

void DisplayServerIOSOffscreen::window_set_rect_changed_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	window_resize_callbacks[p_window] = p_callable;
}

void DisplayServerIOSOffscreen::window_set_window_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	window_event_callbacks[p_window] = p_callable;
}

void DisplayServerIOSOffscreen::window_set_input_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	input_event_callbacks[p_window] = p_callable;
}

void DisplayServerIOSOffscreen::window_set_input_text_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	input_text_callbacks[p_window] = p_callable;
}

void DisplayServerIOSOffscreen::window_set_drop_files_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

void DisplayServerIOSOffscreen::process_events() {
	Input *input = Input::get_singleton();
	input->flush_buffered_events();

	_deliver_pending_frame();
}

void DisplayServerIOSOffscreen::_dispatch_input_events(const Ref<InputEvent> &p_event) {
	Ref<InputEventFromWindow> event_from_window = p_event;
	DisplayServerEnums::WindowID window_id = DisplayServerEnums::INVALID_WINDOW_ID;
	if (event_from_window.is_valid()) {
		window_id = event_from_window->get_window_id();
	}
	DisplayServerIOSOffscreen *ds = (DisplayServerIOSOffscreen *)DisplayServer::get_singleton();
	ds->send_input_event(p_event, window_id);
}

void DisplayServerIOSOffscreen::send_input_event(const Ref<InputEvent> &p_event, DisplayServerEnums::WindowID p_id) const {
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

void DisplayServerIOSOffscreen::send_input_text(const String &p_text, DisplayServerEnums::WindowID p_id) const {
	const Callable *cb = input_text_callbacks.getptr(p_id);
	if (cb) {
		_window_callback(*cb, p_text);
	}
}

void DisplayServerIOSOffscreen::send_window_event_by_id(DisplayServerEnums::WindowEvent p_event, DisplayServerEnums::WindowID p_id) const {
	const Callable *cb = window_event_callbacks.getptr(p_id);
	if (cb) {
		_window_callback(*cb, int(p_event));
	}
}

void DisplayServerIOSOffscreen::_window_callback(const Callable &p_callable, const Variant &p_arg) const {
	if (p_callable.is_valid()) {
		p_callable.call(p_arg);
	}
}

// MARK: -

bool DisplayServerIOSOffscreen::has_feature(DisplayServerEnums::Feature p_feature) const {
	// None of the optional features (clipboard, IME, cursor shapes, etc.) are implemented by
	// this driver; a host embedding Godot offscreen is expected to own those integrations
	// itself (e.g. UIPasteboard for clipboard) rather than relying on DisplayServer for them.
	return false;
}

String DisplayServerIOSOffscreen::get_name() const {
	return "offscreen";
}

int DisplayServerIOSOffscreen::get_screen_count() const {
	return 1;
}

int DisplayServerIOSOffscreen::get_primary_screen() const {
	return 0;
}

Point2i DisplayServerIOSOffscreen::screen_get_position(int p_screen) const {
	return Point2i();
}

Size2i DisplayServerIOSOffscreen::screen_get_size(int p_screen) const {
	return window_get_size(DisplayServerEnums::MAIN_WINDOW_ID);
}

Rect2i DisplayServerIOSOffscreen::screen_get_usable_rect(int p_screen) const {
	return Rect2i(screen_get_position(p_screen), screen_get_size(p_screen));
}

int DisplayServerIOSOffscreen::screen_get_dpi(int p_screen) const {
	return 96;
}

float DisplayServerIOSOffscreen::screen_get_refresh_rate(int p_screen) const {
	return 60.0f;
}

Vector<DisplayServerEnums::WindowID> DisplayServerIOSOffscreen::get_window_list() const {
	Vector<DisplayServerEnums::WindowID> list;
	list.push_back(DisplayServerEnums::MAIN_WINDOW_ID);
	return list;
}

DisplayServerEnums::WindowID DisplayServerIOSOffscreen::get_window_at_screen_position(const Point2i &p_position) const {
	return DisplayServerEnums::MAIN_WINDOW_ID;
}

void DisplayServerIOSOffscreen::window_attach_instance_id(ObjectID p_instance, DisplayServerEnums::WindowID p_window) {
	window_attached_instance_id[p_window] = p_instance;
}

ObjectID DisplayServerIOSOffscreen::window_get_attached_instance_id(DisplayServerEnums::WindowID p_window) const {
	return window_attached_instance_id[p_window];
}

void DisplayServerIOSOffscreen::window_set_title(const String &p_title, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

int DisplayServerIOSOffscreen::window_get_current_screen(DisplayServerEnums::WindowID p_window) const {
	ERR_FAIL_COND_V(p_window != DisplayServerEnums::MAIN_WINDOW_ID, DisplayServerEnums::INVALID_SCREEN);
	return 0;
}

void DisplayServerIOSOffscreen::window_set_current_screen(int p_screen, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

Point2i DisplayServerIOSOffscreen::window_get_position(DisplayServerEnums::WindowID p_window) const {
	return Point2i();
}

Point2i DisplayServerIOSOffscreen::window_get_position_with_decorations(DisplayServerEnums::WindowID p_window) const {
	return Point2i();
}

void DisplayServerIOSOffscreen::window_set_position(const Point2i &p_position, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

void DisplayServerIOSOffscreen::window_set_transient(DisplayServerEnums::WindowID p_window, DisplayServerEnums::WindowID p_parent) {
	// Not supported.
}

void DisplayServerIOSOffscreen::window_set_max_size(const Size2i p_size, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

Size2i DisplayServerIOSOffscreen::window_get_max_size(DisplayServerEnums::WindowID p_window) const {
	return Size2i();
}

void DisplayServerIOSOffscreen::window_set_min_size(const Size2i p_size, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

Size2i DisplayServerIOSOffscreen::window_get_min_size(DisplayServerEnums::WindowID p_window) const {
	return Size2i();
}

void DisplayServerIOSOffscreen::window_set_size(const Size2i p_size, DisplayServerEnums::WindowID p_window) {
	if (p_size.width <= 0 || p_size.height <= 0 || window_size == p_size) {
		return;
	}

	window_size = p_size;

#if defined(RD_ENABLED)
	if (rendering_context) {
		rendering_context->window_set_size(p_window, window_size.width, window_size.height);
	}
#endif

	Callable *cb = window_resize_callbacks.getptr(p_window);
	if (cb) {
		Variant resize_rect = Rect2i(Point2i(), window_size);
		_window_callback(*cb, resize_rect);
	}
}

Size2i DisplayServerIOSOffscreen::window_get_size(DisplayServerEnums::WindowID p_window) const {
	return window_size;
}

Size2i DisplayServerIOSOffscreen::window_get_size_with_decorations(DisplayServerEnums::WindowID p_window) const {
	return window_get_size(p_window);
}

void DisplayServerIOSOffscreen::window_set_mode(DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

DisplayServerEnums::WindowMode DisplayServerIOSOffscreen::window_get_mode(DisplayServerEnums::WindowID p_window) const {
	return DisplayServerEnums::WindowMode::WINDOW_MODE_WINDOWED;
}

bool DisplayServerIOSOffscreen::window_is_maximize_allowed(DisplayServerEnums::WindowID p_window) const {
	return false;
}

void DisplayServerIOSOffscreen::window_set_flag(DisplayServerEnums::WindowFlags p_flag, bool p_enabled, DisplayServerEnums::WindowID p_window) {
	if (p_flag == DisplayServerEnums::WINDOW_FLAG_TRANSPARENT && p_window == DisplayServerEnums::MAIN_WINDOW_ID) {
		transparent = p_enabled;
	}
}

bool DisplayServerIOSOffscreen::window_get_flag(DisplayServerEnums::WindowFlags p_flag, DisplayServerEnums::WindowID p_window) const {
	if (p_flag == DisplayServerEnums::WINDOW_FLAG_TRANSPARENT && p_window == DisplayServerEnums::MAIN_WINDOW_ID) {
		return transparent;
	}
	return false;
}

void DisplayServerIOSOffscreen::window_request_attention(DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

void DisplayServerIOSOffscreen::window_move_to_foreground(DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

bool DisplayServerIOSOffscreen::window_is_focused(DisplayServerEnums::WindowID p_window) const {
	return true;
}

float DisplayServerIOSOffscreen::screen_get_max_scale() const {
	return 1.0f;
}

bool DisplayServerIOSOffscreen::window_can_draw(DisplayServerEnums::WindowID p_window) const {
	return true;
}

bool DisplayServerIOSOffscreen::can_any_window_draw() const {
	return true;
}

void DisplayServerIOSOffscreen::window_set_vsync_mode(DisplayServerEnums::VSyncMode p_vsync_mode, DisplayServerEnums::WindowID p_window) {
#if defined(RD_ENABLED)
	if (rendering_context) {
		rendering_context->window_set_vsync_mode(p_window, p_vsync_mode);
	}
#endif
}

DisplayServerEnums::VSyncMode DisplayServerIOSOffscreen::window_get_vsync_mode(DisplayServerEnums::WindowID p_window) const {
#if defined(RD_ENABLED)
	if (rendering_context) {
		return rendering_context->window_get_vsync_mode(p_window);
	}
#endif
	return DisplayServerEnums::VSYNC_ENABLED;
}

void DisplayServerIOSOffscreen::swap_buffers() {
	// Only the Metal (RenderingDevice) path is supported; nothing to do here.
}
