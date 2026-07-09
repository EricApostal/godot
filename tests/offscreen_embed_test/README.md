# Offscreen display driver validation test

The smallest possible end-to-end check for the `offscreen` display driver: a plain AppKit
app that embeds Godot via `libgodot`, receives rendered frames as `IOSurface`s, and displays
them by handing each surface straight to a `CALayer` — no Metal/OpenGL code of its own.

`project/` is a minimal scene with a rotating cube, so success is visible: a window titled
"libgodot offscreen test" showing a spinning box.

## Build

From the repository root, build libgodot as a shared library. `disable_path_overrides=no` is
required so `--path` (used to point the engine at `project/`) isn't rejected — template
builds disable that by default:

```sh
scons platform=macos library_type=shared_library target=template_debug arch=arm64 disable_path_overrides=no
```

Then build the host app:

```sh
cd tests/offscreen_embed_test
./build.sh
```

## Run

```sh
./host_app
```

(Or `./host_app /path/to/some/other/project` to point it at a different project.)

Console output logs every 120th received frame with its `IOSurfaceID`/size, so you can
confirm frames are actually arriving even without looking at the window.
