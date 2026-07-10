/**************************************************************************/
/*  display_server_offscreen.cpp                                         */
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

#include "display_server_offscreen.h"

#include "core/input/input.h"
#include "core/input/input_event.h"
#include "core/variant/dictionary.h"
#include "servers/rendering/rendering_context_driver.h"

#if defined(RD_ENABLED)
#include "servers/rendering/renderer_rd/renderer_compositor_rd.h"
#include "servers/rendering/rendering_device.h"
#endif

Ref<RenderingOffscreenTarget> DisplayServerOffscreen::_pending_offscreen_target;

void DisplayServerOffscreen::set_offscreen_target(const Ref<RenderingOffscreenTarget> &p_offscreen_target) {
	_pending_offscreen_target = p_offscreen_target;
}

DisplayServerOffscreen::DisplayServerOffscreen(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, const Ref<RenderingOffscreenTarget> &p_offscreen_target, Error &r_error) {
	r_error = OK; // default to OK

	Input::get_singleton()->set_event_dispatch_function(_dispatch_input_events);

	rendering_driver = p_rendering_driver;
	window_size = p_resolution;
	transparent = ((p_flags & DisplayServerEnums::WINDOW_FLAG_TRANSPARENT_BIT) == DisplayServerEnums::WINDOW_FLAG_TRANSPARENT_BIT);

	offscreen_target = p_offscreen_target;
	ERR_FAIL_COND_MSG(offscreen_target.is_null(), "The offscreen display driver requires a RenderingOffscreenTarget to be set via DisplayServerOffscreen::set_offscreen_target() before the display server is created.");

#if defined(RD_ENABLED)
	rendering_context = offscreen_target->create_and_setup_rendering_context(rendering_driver, r_error);
	if (rendering_context == nullptr) {
		return;
	}

	rendering_context->window_set_size(DisplayServerEnums::MAIN_WINDOW_ID, window_size.width, window_size.height);
	rendering_context->window_set_vsync_mode(DisplayServerEnums::MAIN_WINDOW_ID, p_vsync_mode);

	rendering_device = memnew(RenderingDevice);
	rendering_device->initialize(rendering_context, DisplayServerEnums::MAIN_WINDOW_ID);
	rendering_device->screen_create(DisplayServerEnums::MAIN_WINDOW_ID);

	RendererCompositorRD::make_current();
#else
	r_error = ERR_CANT_CREATE;
	ERR_FAIL_MSG("The offscreen display driver requires a RenderingDevice-based renderer.");
#endif
}

DisplayServerOffscreen::~DisplayServerOffscreen() {
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

DisplayServer *DisplayServerOffscreen::create_func(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t /* p_parent_window */, Error &r_error) {
	Ref<RenderingOffscreenTarget> offscreen_target = _pending_offscreen_target;
	_pending_offscreen_target.unref();
	return memnew(DisplayServerOffscreen(p_rendering_driver, p_mode, p_vsync_mode, p_flags, p_position, p_resolution, p_screen, p_context, offscreen_target, r_error));
}

Vector<String> DisplayServerOffscreen::get_rendering_drivers_func() {
	if (_pending_offscreen_target.is_valid()) {
		return _pending_offscreen_target->get_supported_rendering_drivers();
	}
	return Vector<String>();
}

void DisplayServerOffscreen::register_offscreen_driver() {
	register_create_function("offscreen", create_func, get_rendering_drivers_func);
}

// MARK: - Offscreen frame delivery

void DisplayServerOffscreen::offscreen_set_frame_available_callback(const Callable &p_callback) {
	if (offscreen_target.is_valid()) {
		offscreen_target->set_frame_available_callback(p_callback);
	}
}

// MARK: - Mouse

void DisplayServerOffscreen::warp_mouse(const Point2i &p_position) {
	Input::get_singleton()->set_mouse_position(p_position);
}

Point2i DisplayServerOffscreen::mouse_get_position() const {
	// There is no real window to query the OS for a pointer position; the host application is
	// expected to feed input (including mouse motion) via `Input.parse_input_event()`.
	return Point2i(Input::get_singleton()->get_mouse_position());
}

BitField<MouseButtonMask> DisplayServerOffscreen::mouse_get_button_state() const {
	return Input::get_singleton()->get_mouse_button_mask();
}

// MARK: Events

void DisplayServerOffscreen::window_set_rect_changed_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	window_resize_callbacks[p_window] = p_callable;
}

void DisplayServerOffscreen::window_set_window_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	window_event_callbacks[p_window] = p_callable;
}

void DisplayServerOffscreen::window_set_input_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	input_event_callbacks[p_window] = p_callable;
}

void DisplayServerOffscreen::window_set_input_text_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	input_text_callbacks[p_window] = p_callable;
}

void DisplayServerOffscreen::window_set_drop_files_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

void DisplayServerOffscreen::process_events() {
	Input *input = Input::get_singleton();
	input->flush_buffered_events();

	if (offscreen_target.is_valid()) {
		offscreen_target->deliver_pending_frame();
	}
}

void DisplayServerOffscreen::_dispatch_input_events(const Ref<InputEvent> &p_event) {
	Ref<InputEventFromWindow> event_from_window = p_event;
	DisplayServerEnums::WindowID window_id = DisplayServerEnums::INVALID_WINDOW_ID;
	if (event_from_window.is_valid()) {
		window_id = event_from_window->get_window_id();
	}
	DisplayServerOffscreen *ds = (DisplayServerOffscreen *)DisplayServer::get_singleton();
	ds->send_input_event(p_event, window_id);
}

void DisplayServerOffscreen::send_input_event(const Ref<InputEvent> &p_event, DisplayServerEnums::WindowID p_id) const {
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

void DisplayServerOffscreen::send_input_text(const String &p_text, DisplayServerEnums::WindowID p_id) const {
	const Callable *cb = input_text_callbacks.getptr(p_id);
	if (cb) {
		_window_callback(*cb, p_text);
	}
}

void DisplayServerOffscreen::send_window_event_by_id(DisplayServerEnums::WindowEvent p_event, DisplayServerEnums::WindowID p_id) const {
	const Callable *cb = window_event_callbacks.getptr(p_id);
	if (cb) {
		_window_callback(*cb, int(p_event));
	}
}

void DisplayServerOffscreen::_window_callback(const Callable &p_callable, const Variant &p_arg) const {
	if (p_callable.is_valid()) {
		p_callable.call(p_arg);
	}
}

// MARK: -

bool DisplayServerOffscreen::has_feature(DisplayServerEnums::Feature p_feature) const {
	// None of the optional features (clipboard, IME, native menus, TTS, etc.) are implemented
	// by this driver; a host embedding Godot offscreen is expected to own those integrations
	// itself rather than relying on DisplayServer for them.
	return false;
}

String DisplayServerOffscreen::get_name() const {
	return "offscreen";
}

int DisplayServerOffscreen::get_screen_count() const {
	return 1;
}

int DisplayServerOffscreen::get_primary_screen() const {
	return 0;
}

Point2i DisplayServerOffscreen::screen_get_position(int p_screen) const {
	return Point2i();
}

Size2i DisplayServerOffscreen::screen_get_size(int p_screen) const {
	return window_get_size(DisplayServerEnums::MAIN_WINDOW_ID);
}

Rect2i DisplayServerOffscreen::screen_get_usable_rect(int p_screen) const {
	return Rect2i(screen_get_position(p_screen), screen_get_size(p_screen));
}

int DisplayServerOffscreen::screen_get_dpi(int p_screen) const {
	return 96;
}

float DisplayServerOffscreen::screen_get_refresh_rate(int p_screen) const {
	return 60.0f;
}

Vector<DisplayServerEnums::WindowID> DisplayServerOffscreen::get_window_list() const {
	Vector<DisplayServerEnums::WindowID> list;
	list.push_back(DisplayServerEnums::MAIN_WINDOW_ID);
	return list;
}

DisplayServerEnums::WindowID DisplayServerOffscreen::get_window_at_screen_position(const Point2i &p_position) const {
	return DisplayServerEnums::MAIN_WINDOW_ID;
}

void DisplayServerOffscreen::window_attach_instance_id(ObjectID p_instance, DisplayServerEnums::WindowID p_window) {
	window_attached_instance_id[p_window] = p_instance;
}

ObjectID DisplayServerOffscreen::window_get_attached_instance_id(DisplayServerEnums::WindowID p_window) const {
	return window_attached_instance_id[p_window];
}

void DisplayServerOffscreen::window_set_title(const String &p_title, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

int DisplayServerOffscreen::window_get_current_screen(DisplayServerEnums::WindowID p_window) const {
	ERR_FAIL_COND_V(p_window != DisplayServerEnums::MAIN_WINDOW_ID, DisplayServerEnums::INVALID_SCREEN);
	return 0;
}

void DisplayServerOffscreen::window_set_current_screen(int p_screen, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

Point2i DisplayServerOffscreen::window_get_position(DisplayServerEnums::WindowID p_window) const {
	return Point2i();
}

Point2i DisplayServerOffscreen::window_get_position_with_decorations(DisplayServerEnums::WindowID p_window) const {
	return Point2i();
}

void DisplayServerOffscreen::window_set_position(const Point2i &p_position, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

void DisplayServerOffscreen::window_set_transient(DisplayServerEnums::WindowID p_window, DisplayServerEnums::WindowID p_parent) {
	// Not supported.
}

void DisplayServerOffscreen::window_set_max_size(const Size2i p_size, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

Size2i DisplayServerOffscreen::window_get_max_size(DisplayServerEnums::WindowID p_window) const {
	return Size2i();
}

void DisplayServerOffscreen::window_set_min_size(const Size2i p_size, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

Size2i DisplayServerOffscreen::window_get_min_size(DisplayServerEnums::WindowID p_window) const {
	return Size2i();
}

void DisplayServerOffscreen::window_set_size(const Size2i p_size, DisplayServerEnums::WindowID p_window) {
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

Size2i DisplayServerOffscreen::window_get_size(DisplayServerEnums::WindowID p_window) const {
	return window_size;
}

Size2i DisplayServerOffscreen::window_get_size_with_decorations(DisplayServerEnums::WindowID p_window) const {
	return window_get_size(p_window);
}

void DisplayServerOffscreen::window_set_mode(DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

DisplayServerEnums::WindowMode DisplayServerOffscreen::window_get_mode(DisplayServerEnums::WindowID p_window) const {
	return DisplayServerEnums::WindowMode::WINDOW_MODE_WINDOWED;
}

bool DisplayServerOffscreen::window_is_maximize_allowed(DisplayServerEnums::WindowID p_window) const {
	return false;
}

void DisplayServerOffscreen::window_set_flag(DisplayServerEnums::WindowFlags p_flag, bool p_enabled, DisplayServerEnums::WindowID p_window) {
	if (p_flag == DisplayServerEnums::WINDOW_FLAG_TRANSPARENT && p_window == DisplayServerEnums::MAIN_WINDOW_ID) {
		transparent = p_enabled;
	}
}

bool DisplayServerOffscreen::window_get_flag(DisplayServerEnums::WindowFlags p_flag, DisplayServerEnums::WindowID p_window) const {
	if (p_flag == DisplayServerEnums::WINDOW_FLAG_TRANSPARENT && p_window == DisplayServerEnums::MAIN_WINDOW_ID) {
		return transparent;
	}
	return false;
}

void DisplayServerOffscreen::window_request_attention(DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

void DisplayServerOffscreen::window_move_to_foreground(DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

bool DisplayServerOffscreen::window_is_focused(DisplayServerEnums::WindowID p_window) const {
	return true;
}

float DisplayServerOffscreen::screen_get_max_scale() const {
	return 1.0f;
}

bool DisplayServerOffscreen::window_can_draw(DisplayServerEnums::WindowID p_window) const {
	return true;
}

bool DisplayServerOffscreen::can_any_window_draw() const {
	return true;
}

void DisplayServerOffscreen::window_set_ime_active(const bool p_active, DisplayServerEnums::WindowID p_window) {
	// Not supported: the host application is expected to own IME/text input UI itself.
}

void DisplayServerOffscreen::window_set_ime_position(const Point2i &p_pos, DisplayServerEnums::WindowID p_window) {
	// Not supported.
}

void DisplayServerOffscreen::cursor_set_shape(DisplayServerEnums::CursorShape p_shape) {
	cursor_shape = p_shape;
}

void DisplayServerOffscreen::cursor_set_custom_image(const Ref<Resource> &p_cursor, DisplayServerEnums::CursorShape p_shape, const Vector2 &p_hotspot) {
	// Not supported: the host application is expected to render its own cursor.
}

void DisplayServerOffscreen::window_set_vsync_mode(DisplayServerEnums::VSyncMode p_vsync_mode, DisplayServerEnums::WindowID p_window) {
#if defined(RD_ENABLED)
	if (rendering_context) {
		rendering_context->window_set_vsync_mode(p_window, p_vsync_mode);
	}
#endif
}

DisplayServerEnums::VSyncMode DisplayServerOffscreen::window_get_vsync_mode(DisplayServerEnums::WindowID p_window) const {
#if defined(RD_ENABLED)
	if (rendering_context) {
		return rendering_context->window_get_vsync_mode(p_window);
	}
#endif
	return DisplayServerEnums::VSYNC_ENABLED;
}

void DisplayServerOffscreen::swap_buffers() {
	// Only the RenderingDevice path is supported; nothing to do here.
}
