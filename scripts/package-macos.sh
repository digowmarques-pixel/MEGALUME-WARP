#!/bin/bash
# Builds Lumen.app (self-contained: FFmpeg/GLFW dylibs copied inside).
# Needs Homebrew: brew install cmake pkg-config glfw ffmpeg dylibbundler
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build-macos}"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --parallel
(cd "$BUILD" && ctest --output-on-failure)

APP="$BUILD/Lumen.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/libs"
cp "$BUILD/lumen" "$APP/Contents/MacOS/lumen"
cp -R "$ROOT/examples" "$APP/Contents/Resources/examples"
cp -R "$ROOT/effects" "$APP/Contents/MacOS/effects" # next to the executable, like on Windows
VERSION="$(grep -oE 'project\(Lumen VERSION [0-9.]+' "$ROOT/CMakeLists.txt" | grep -oE '[0-9.]+$')"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>Lumen</string>
  <key>CFBundleIdentifier</key><string>com.megalume.lumen</string>
  <key>CFBundleExecutable</key><string>lumen</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSCameraUsageDescription</key><string>Usar câmeras e placas de captura como mídia ao vivo.</string>
  <key>NSMicrophoneUsageDescription</key><string>Capturar áudio de entradas ao vivo.</string>
  <key>NSLocalNetworkUsageDescription</key><string>Receber OSC e enviar Art-Net/sACN para controladores de LED.</string>
</dict></plist>
PLIST
# Copy every non-system dylib into the bundle and point the binary at it.
dylibbundler -od -b -x "$APP/Contents/MacOS/lumen" -d "$APP/Contents/libs" -p @executable_path/../libs/
# Ad-hoc signature (required on Apple Silicon; not a Developer ID).
codesign --force --deep --sign - "$APP"
echo "built $APP"
