/**************************************************************************/
/*  host_main.mm                                                          */
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

// The simplest possible host application for validating the "offscreen" display driver:
// it embeds Godot via libgodot with no window of its own, receives rendered frames as
// IOSurfaces, and displays them directly by handing each IOSurface straight to a CALayer.
//
// It deliberately never touches the GDExtension/Variant ABI: everything it needs is exposed
// as plain C functions in libgodot.h (see libgodot_godot_instance_start(),
// libgodot_godot_instance_iteration(), and libgodot_godot_instance_set_offscreen_frame_callback()).
// This matters beyond convenience: GodotInstance::start()/iteration() aren't exported with
// default visibility from the shared library, so linking against them directly as C++ symbols
// doesn't work even for an in-tree host — and a non-C++ FFI host (e.g. Dart) couldn't call a
// C++ method at all regardless. libgodot.h's plain C wrappers are the only real entry point.

#import <Cocoa/Cocoa.h>
#import <IOSurface/IOSurface.h>
#import <QuartzCore/QuartzCore.h>

#include <string>
#include <vector>

#include "core/extension/libgodot.h"

static GDExtensionObjectPtr godot_instance = nullptr;

// MARK: - Trivial GDExtension init function.
//
// libgodot hands us straight into the engine's GDExtension loading machinery, but this test
// app doesn't need to register any custom classes, so this just satisfies the required shape.

static void trivial_initialize(void *p_userdata, GDExtensionInitializationLevel p_level) {
}

static void trivial_deinitialize(void *p_userdata, GDExtensionInitializationLevel p_level) {
}

static GDExtensionBool trivial_init_func(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	r_initialization->initialize = trivial_initialize;
	r_initialization->deinitialize = trivial_deinitialize;
	r_initialization->userdata = nullptr;
	r_initialization->minimum_initialization_level = GDEXTENSION_INITIALIZATION_CORE;
	return true;
}

// MARK: - App delegate

@class OffscreenTestAppDelegate;

// Called from the engine's rendering thread/completion handler; must hop to the main thread
// before touching AppKit/CALayer.
static void frameCallback(void *p_userdata, const GodotOffscreenFrame *p_frame);

@interface OffscreenTestAppDelegate : NSObject <NSApplicationDelegate>
@property(strong) NSWindow *window;
@property(strong) CALayer *godotLayer;
@property(strong) NSTimer *iterationTimer;
@property(assign) NSUInteger frameCount;
@end

@implementation OffscreenTestAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
	const int width = 480;
	const int height = 270;

	NSRect frame = NSMakeRect(0, 0, width, height);
	self.window = [[NSWindow alloc] initWithContentRect:frame
											   styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable)
												 backing:NSBackingStoreBuffered
												   defer:NO];
	self.window.title = @"libgodot offscreen test";
	[self.window center];

	self.godotLayer = [CALayer layer];
	self.godotLayer.frame = frame;
	// Godot's frames come out already flipped for on-screen display (see swap_buffers()
	// callers elsewhere in the engine); CALayer's default coordinate space matches, so no
	// extra transform is needed here.
	self.godotLayer.backgroundColor = NSColor.blackColor.CGColor;

	self.window.contentView.wantsLayer = YES;
	[self.window.contentView.layer addSublayer:self.godotLayer];

	[self.window makeKeyAndOrderFront:nil];
	[NSApp activateIgnoringOtherApps:YES];

	NSString *projectPath = [self resolveProjectPath];
	NSLog(@"Using project at: %@", projectPath);

	std::vector<std::string> args_storage = {
		"offscreen_embed_test",
		"--path", projectPath.UTF8String,
		"--offscreen",
		"--resolution", [NSString stringWithFormat:@"%dx%d", width, height].UTF8String,
		"--verbose",
	};
	std::vector<char *> argv;
	for (std::string &arg : args_storage) {
		argv.push_back(arg.data());
	}

	godot_instance = libgodot_create_godot_instance((int)argv.size(), argv.data(), trivial_init_func);
	if (!godot_instance) {
		NSLog(@"Failed to create Godot instance.");
		[NSApp terminate:nil];
		return;
	}

	// DisplayServer (and everything else set up by Main::setup2()) only exists once the
	// instance has started, so the frame callback can only be registered after this.
	if (!libgodot_godot_instance_start(godot_instance)) {
		NSLog(@"Failed to start Godot instance.");
		[NSApp terminate:nil];
		return;
	}

	libgodot_godot_instance_set_offscreen_frame_callback(godot_instance, &frameCallback, (__bridge void *)self);

	NSLog(@"Godot instance started; driving iteration at 60 Hz.");

	__weak OffscreenTestAppDelegate *weakSelf = self;
	self.iterationTimer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 60.0
															repeats:YES
															  block:^(NSTimer *timer) {
																  [weakSelf iterate];
															  }];
}

- (NSString *)resolveProjectPath {
	NSArray<NSString *> *args = NSProcessInfo.processInfo.arguments;
	if (args.count > 1) {
		return args[1].stringByStandardizingPath;
	}

	// Default: host_app and project/ both live directly in tests/offscreen_embed_test/.
	NSString *execDir = NSBundle.mainBundle.executablePath.stringByDeletingLastPathComponent;
	return [execDir stringByAppendingPathComponent:@"project"].stringByStandardizingPath;
}

- (void)iterate {
	if (!godot_instance) {
		return;
	}

	if (libgodot_godot_instance_iteration(godot_instance)) {
		NSLog(@"Engine requested exit.");
		[NSApp terminate:nil];
	}
}

// Called from the engine's rendering thread/completion handler; must hop to the main thread
// before touching AppKit/CALayer.
static void frameCallback(void *p_userdata, const GodotOffscreenFrame *p_frame) {
	if (p_frame->type != GODOT_OFFSCREEN_SURFACE_TYPE_IOSURFACE) {
		return;
	}

	OffscreenTestAppDelegate *self_ = (__bridge OffscreenTestAppDelegate *)p_userdata;

	uint32_t native_surface_id = p_frame->surface.iosurface.iosurface_id;
	IOSurfaceRef surface = IOSurfaceLookup(native_surface_id);
	if (!surface) {
		NSLog(@"IOSurfaceLookup(%u) failed", native_surface_id);
		return;
	}

	uint32_t width = p_frame->width;
	uint32_t height = p_frame->height;

	// IOSurfaceLookup() hands us a +1 reference; keep it alive until the block below (which
	// may run on a later runloop turn) is done handing it to CALayer, then release it.
	dispatch_async(dispatch_get_main_queue(), ^{
		self_.frameCount++;
		if (self_.frameCount % 120 == 0) {
			NSLog(@"Received frame #%lu (%ux%u, iosurface_id=%llu)", (unsigned long)self_.frameCount, width, height, (unsigned long long)native_surface_id);
		}
		[CATransaction begin];
		[CATransaction setDisableActions:YES];
		self_.godotLayer.contents = (__bridge id)surface;
		[CATransaction commit];
		CFRelease(surface);
	});
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
	return YES;
}

- (void)applicationWillTerminate:(NSNotification *)notification {
	[self.iterationTimer invalidate];
	if (godot_instance) {
		libgodot_destroy_godot_instance(godot_instance);
		godot_instance = nullptr;
	}
}

@end

int main(int argc, char *argv[]) {
	@autoreleasepool {
		NSApplication *app = [NSApplication sharedApplication];
		OffscreenTestAppDelegate *delegate = [OffscreenTestAppDelegate new];
		app.delegate = delegate;
		[app run];
	}
	return 0;
}
