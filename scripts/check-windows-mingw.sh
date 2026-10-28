#!/bin/bash
# Compile-only check of every source file for Windows using MinGW-w64.
# Catches Windows-specific errors (winsock, Win32 API, headers) without a
# Windows machine. It does not link and does not replace a real MSVC build.
# Needs: mingw-w64 and the Linux dev headers of GLFW / FFmpeg (headers only).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${OUT:-$ROOT/build-mingw-check}"
INC="$OUT/include"
mkdir -p "$OUT" "$INC"
# Platform-neutral third-party headers copied away from /usr/include so the
# MinGW compiler does not see Linux system headers.
for d in GLFW libavcodec libavformat libavutil libswscale libswresample libavdevice; do
  [ -d /usr/include/$d ] && cp -r /usr/include/$d "$INC/" 2>/dev/null || true
  [ -d /usr/include/x86_64-linux-gnu/$d ] && cp -r /usr/include/x86_64-linux-gnu/$d "$INC/" 2>/dev/null || true
done
CXX=${CXX:-x86_64-w64-mingw32-g++-posix}
FLAGS="-std=c++20 -fsyntax-only -Wall -Wno-unused-parameter -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DGLFW_INCLUDE_NONE -DLUMEN_VERSION=\"check\" -I$ROOT/src -I$ROOT/third_party -I$ROOT/third_party/khr -I$ROOT/third_party/imgui -I$ROOT/third_party/imgui/backends -I$INC -I$ROOT/third_party/spout -I$ROOT/third_party/spout/shim -D__WINDOWS_MM__ -DSPOUT_BUILD_STATIC $EXTRA_FLAGS"
fail=0
for f in $(cd "$ROOT" && find src tests -name '*.cpp' | sort) $EXTRA_SOURCES; do
  if ! $CXX $FLAGS "$ROOT/$f" 2> "$OUT/err.txt"; then
    echo "FAIL $f"; head -40 "$OUT/err.txt"; fail=1
  elif [ -s "$OUT/err.txt" ]; then
    echo "WARN $f"; head -20 "$OUT/err.txt"
  fi
done
[ $fail = 0 ] && echo "Windows (MinGW) syntax check: OK"
exit $fail
