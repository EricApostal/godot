#!/usr/bin/env bash
# Builds the minimal "offscreen" display driver validation host app.
#
# Prerequisite: libgodot must already be built as a shared library for macOS, e.g.:
#   scons platform=macos library_type=shared_library target=template_debug arch=arm64 disable_path_overrides=no
# from the repository root. `disable_path_overrides=no` is required so `--path` isn't
# rejected (template builds disable it by default).
#
# Usage: ./build.sh
# Then:  ./host_app [path/to/project]   (defaults to ./project)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BIN_DIR="${REPO_ROOT}/bin"

DYLIB="$(ls "${BIN_DIR}"/*godot*.dylib 2>/dev/null | head -n1 || true)"
if [[ -z "${DYLIB}" ]]; then
	echo "error: no libgodot .dylib found in ${BIN_DIR}." >&2
	echo "Build it first with:" >&2
	echo "  scons platform=macos library_type=shared_library target=template_debug arch=arm64" >&2
	exit 1
fi

echo "Linking against: ${DYLIB}"

clang++ -std=c++20 -fobjc-arc \
	-I "${REPO_ROOT}" \
	-o "${SCRIPT_DIR}/host_app" \
	"${SCRIPT_DIR}/src/host_main.mm" \
	"${DYLIB}" \
	-Wl,-rpath,"${BIN_DIR}" \
	-framework Cocoa \
	-framework QuartzCore \
	-framework IOSurface

# The dylib's own install name (LC_ID_DYLIB) is whatever relative path SCons happened to
# link it at (e.g. "bin/libgodot...dylib"), not an @rpath-relative one, so dyld would only
# find it when host_app is run with the repo root as the working directory. Rewrite the
# reference in host_app to an absolute path so `./host_app` works from anywhere.
CURRENT_REF="$(otool -L "${SCRIPT_DIR}/host_app" | awk '/libgodot.*\.dylib/{print $1}')"
if [[ -n "${CURRENT_REF}" && "${CURRENT_REF}" != "${DYLIB}" ]]; then
	install_name_tool -change "${CURRENT_REF}" "${DYLIB}" "${SCRIPT_DIR}/host_app"
fi

echo "Built ${SCRIPT_DIR}/host_app"
