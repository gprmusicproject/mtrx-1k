#!/bin/sh
# Configures and builds MTRX-1K (VST3 + AU + Standalone) for macOS on Apple Silicon with
# Apple clang, CMake and Ninja (brew install cmake ninja).
# Usage: ./build_mac.sh [configure|build|install]   (default: configure and build)
#   install: also copies the plugins to ~/Library/Audio/Plug-Ins (VST3 and Components)
# MTRX_ARCHS=arm64;x86_64 builds a universal binary instead of arm64 only.
set -e

SRC="$(cd "$(dirname "$0")" && pwd)"
BLD="$SRC/build/mac-release"
ARCHS="${MTRX_ARCHS:-arm64}"
OUT="$BLD/Retromulator_artefacts/Release"

# Homebrew is not always on the PATH of a new shell
[ -x /opt/homebrew/bin/brew ] && eval "$(/opt/homebrew/bin/brew shellenv)"

configure() {
	cmake -S "$SRC" -B "$BLD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_OSX_ARCHITECTURES="$ARCHS" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
}

build() {
	cmake --build "$BLD" --target Retromulator_VST3 Retromulator_AU Retromulator_Standalone
	# ad-hoc signature: enough for this Mac; a Developer ID and notarisation are needed to
	# hand the plugins to others
	for bundle in "$OUT/VST3/MTRX-1K.vst3" "$OUT/AU/MTRX-1K.component" "$OUT/Standalone/MTRX-1K.app"; do
		codesign --force --deep --sign - "$bundle"
	done
}

install_plugins() {
	mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Library/Audio/Plug-Ins/Components"
	rm -rf "$HOME/Library/Audio/Plug-Ins/VST3/MTRX-1K.vst3" "$HOME/Library/Audio/Plug-Ins/Components/MTRX-1K.component"
	cp -R "$OUT/VST3/MTRX-1K.vst3" "$HOME/Library/Audio/Plug-Ins/VST3/"
	cp -R "$OUT/AU/MTRX-1K.component" "$HOME/Library/Audio/Plug-Ins/Components/"
	# hosts find a new Audio Unit once the component cache knows it
	killall -9 AudioComponentRegistrar 2>/dev/null || true
	echo "Installed in ~/Library/Audio/Plug-Ins (VST3 and Components)"
}

case "$1" in
	configure) configure ;;
	build) build ;;
	install) configure; build; install_plugins ;;
	*) configure; build ;;
esac
