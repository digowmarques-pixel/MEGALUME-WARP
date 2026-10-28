#!/bin/bash
# Cross-compiles lumen.exe for Windows x64 from Linux with MinGW-w64.
# Usage: scripts/build-windows-mingw.sh <ffmpeg-win64-shared-dir> <glfw-win64-bin-dir>
#   ffmpeg: https://github.com/BtbN/FFmpeg-Builds/releases (win64-lgpl-shared)
#   glfw:   https://github.com/glfw/glfw/releases (glfw-3.x.bin.WIN64.zip)
# The official Windows build is the MSVC + vcpkg one (README); this script is
# for testing on Linux / Wine and for quick packages.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FF="$1"; GLFW="$2"
[ -d "$FF/include" ] && [ -d "$GLFW/include" ] || { echo "usage: $0 <ffmpeg-dir> <glfw-dir>"; exit 2; }
OUT="${OUT:-$ROOT/build-mingw}"
CXX=${CXX:-x86_64-w64-mingw32-g++-posix}
mkdir -p "$OUT/obj" "$OUT/Lumen"
CXXFLAGS="-std=c++20 -O2 -Wall -Wno-unused-parameter -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DGLFW_INCLUDE_NONE -DLUMEN_VERSION=\"$(grep -oP 'project\(Lumen VERSION \K[0-9.]+' "$ROOT/CMakeLists.txt")\" \
  -I$ROOT/src -I$ROOT/third_party -I$ROOT/third_party/khr -I$ROOT/third_party/imgui -I$ROOT/third_party/imgui/backends \
  -I$FF/include -I$GLFW/include -I$ROOT/third_party/spout -I$ROOT/third_party/spout/shim \
  -D__WINDOWS_MM__ -DSPOUT_BUILD_STATIC $EXTRA_CXXFLAGS"
SOURCES="$(cd "$ROOT" && ls src/*.cpp src/*/*.cpp) third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp \
  third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp third_party/imgui/imgui_demo.cpp \
  third_party/imgui/backends/imgui_impl_glfw.cpp third_party/imgui/backends/imgui_impl_opengl3.cpp \
  third_party/rtmidi/RtMidi.cpp $(cd "$ROOT" && ls third_party/spout/*.cpp) $EXTRA_SOURCES"
OBJS=""
pids=()
for s in $SOURCES; do
  o="$OUT/obj/$(echo "$s" | tr '/' '_').o"
  OBJS="$OBJS $o"
  if [ ! -f "$o" ] || [ "$ROOT/$s" -nt "$o" ] || [ -n "$FORCE" ]; then
    extra=""; case "$s" in third_party/spout/*) extra="-msse4.1 -w";; third_party/*) extra="-w";; esac
    $CXX $CXXFLAGS $extra -c "$ROOT/$s" -o "$o" &
    pids+=($!)
    if [ ${#pids[@]} -ge $(nproc) ]; then wait "${pids[0]}"; pids=("${pids[@]:1}"); fi
  fi
done
for p in "${pids[@]}"; do wait "$p"; done
$CXX -o "$OUT/Lumen/lumen.exe" $OBJS -mwindows -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic \
  -L"$GLFW/lib-mingw-w64" -lglfw3 -L"$FF/lib" -lavformat -lavdevice -lavcodec -lswscale -lswresample -lavutil $EXTRA_LIBS \
  -lopengl32 -lgdi32 -lws2_32 -lcomdlg32 -lshell32 -lole32 -loleaut32 -lwinmm -luuid \
  -ld3d11 -ldxgi -ld3d9 -lversion -lcomctl32 -lshlwapi -lpsapi
cp "$FF"/bin/*.dll "$OUT/Lumen/"
# MinGW's pthread runtime (std::thread) when it is linked dynamically.
for d in /usr/x86_64-w64-mingw32/lib /usr/lib/gcc/x86_64-w64-mingw32/*-posix; do
  [ -f "$d/libwinpthread-1.dll" ] && cp "$d/libwinpthread-1.dll" "$OUT/Lumen/" && break
done
rm -rf "$OUT/Lumen/examples" && cp -r "$ROOT/examples" "$OUT/Lumen/examples"
echo "built $OUT/Lumen/lumen.exe"
