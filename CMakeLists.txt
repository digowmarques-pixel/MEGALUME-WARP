cmake_minimum_required(VERSION 3.20)
project(Lumen VERSION 0.10.12 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------- dependencies
# Windows: use vcpkg (see vcpkg.json / README). Linux: system packages via pkg-config.
find_package(OpenGL REQUIRED)

find_package(glfw3 CONFIG QUIET)
if(NOT TARGET glfw)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3)
  add_library(glfw ALIAS PkgConfig::GLFW)
endif()

find_package(FFMPEG QUIET) # provided by vcpkg's ffmpeg port
if(FFMPEG_FOUND)
  add_library(lumen_ffmpeg INTERFACE)
  target_include_directories(lumen_ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
  target_link_directories(lumen_ffmpeg INTERFACE ${FFMPEG_LIBRARY_DIRS})
  target_link_libraries(lumen_ffmpeg INTERFACE ${FFMPEG_LIBRARIES})
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(FFMPEG_PC REQUIRED IMPORTED_TARGET libavformat libavcodec libswscale libswresample libavdevice libavutil)
  add_library(lumen_ffmpeg INTERFACE)
  target_link_libraries(lumen_ffmpeg INTERFACE PkgConfig::FFMPEG_PC)
endif()

# ---------------------------------------------------------------- Dear ImGui
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
target_compile_definitions(imgui PUBLIC GLFW_INCLUDE_NONE IMGUI_USER_CONFIG="lumen_imconfig.h")
target_include_directories(imgui PUBLIC ${CMAKE_SOURCE_DIR}/src)

# ---------------------------------------------------------------- RtMidi (MIDI input)
add_library(rtmidi STATIC third_party/rtmidi/RtMidi.cpp)
target_include_directories(rtmidi PUBLIC third_party)
if(WIN32)
  target_compile_definitions(rtmidi PUBLIC __WINDOWS_MM__)
  target_link_libraries(rtmidi PUBLIC winmm)
elseif(APPLE)
  target_compile_definitions(rtmidi PUBLIC __MACOSX_CORE__)
  target_link_libraries(rtmidi PUBLIC "-framework CoreMIDI" "-framework CoreAudio" "-framework CoreFoundation")
else()
  find_package(ALSA QUIET)
  if(ALSA_FOUND)
    target_compile_definitions(rtmidi PUBLIC __LINUX_ALSA__)
    target_link_libraries(rtmidi PUBLIC ALSA::ALSA)
  else()
    message(STATUS "ALSA not found: MIDI disabled")
    target_compile_definitions(rtmidi PUBLIC __RTMIDI_DUMMY__)
  endif()
  find_package(Threads REQUIRED)
  target_link_libraries(rtmidi PUBLIC Threads::Threads)
endif()

# ---------------------------------------------------------------- Spout (Windows only)
if(WIN32)
  file(GLOB SPOUT_SOURCES third_party/spout/*.cpp)
  add_library(spout STATIC ${SPOUT_SOURCES})
  target_include_directories(spout PUBLIC third_party/spout)
  target_compile_definitions(spout PUBLIC SPOUT_BUILD_STATIC PRIVATE NOMINMAX)
  target_link_libraries(spout PUBLIC opengl32 d3d11 dxgi d3d9 winmm version comctl32 shlwapi)
  if(MSVC)
    target_compile_options(spout PRIVATE /W0 /utf-8)
  else()
    target_compile_options(spout PRIVATE -msse4.1 -w) # SpoutCopy uses SSE intrinsics
  endif()
endif()

# ---------------------------------------------------------------- Lumen
# Everything but main() is compiled once into lumen_core; on Windows the same
# objects also make lumen-console.exe (a console program: the log live, for
# the diagnostics shortcut of the installer and TESTE-WINDOWS.md).
add_library(lumen_core OBJECT
  src/app.cpp
  src/diagnose.cpp
  src/gl/gl.cpp
  src/render/gpu.cpp
  src/render/stream.cpp
  src/core/parallel.cpp
  src/render/mask.cpp
  src/render/isf_renderer.cpp
  src/media/decoder.cpp
  src/media/hap.cpp
  src/media/upload.cpp
  src/media/audio.cpp
  src/core/audio_route.cpp
  src/media/source.cpp
  src/media/text.cpp
  src/io/spout.cpp
  src/io/ndi.cpp
  src/control/midi.cpp
  src/core/warp.cpp
  src/core/dmx.cpp
  src/core/blend.cpp
  src/core/project.cpp
  src/core/svg.cpp
  src/core/isf.cpp
  src/core/blob.cpp
  src/engine/engine.cpp
  src/engine/sync.cpp
  src/engine/leds.cpp
  src/engine/compositor.cpp
  src/engine/effects.cpp
  src/engine/outputs.cpp
  src/control/osc.cpp
  src/control/dmxin.cpp
  src/control/devices.cpp
  src/io/serial.cpp
  src/core/md5.cpp
  src/core/timecode.cpp
  src/control/mapping.cpp
  src/ui/ui.cpp
  src/ui/i18n.cpp
  src/core/machine_settings.cpp
  src/ui/workspace.cpp
  src/ui/outputs_window.cpp
  src/platform/platform.cpp)

target_include_directories(lumen_core PUBLIC
  src
  third_party/khr
  third_party)
target_link_libraries(lumen_core PUBLIC imgui glfw OpenGL::GL lumen_ffmpeg rtmidi)
if(WIN32)
  target_link_libraries(lumen_core PUBLIC spout)
elseif(APPLE)
  target_link_libraries(lumen_core PUBLIC "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
else()
  target_link_libraries(lumen_core PUBLIC ${CMAKE_DL_LIBS} m) # miniaudio loads backends at runtime
endif()
target_compile_definitions(lumen_core PUBLIC GLFW_INCLUDE_NONE LUMEN_VERSION="${PROJECT_VERSION}" LUMEN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

if(WIN32)
  target_link_libraries(lumen_core PUBLIC ws2_32 comdlg32 shell32 ole32)
  target_compile_definitions(lumen_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
  if(MSVC)
    target_compile_options(lumen_core PUBLIC /W3 /utf-8)
  endif()
else()
  target_compile_options(lumen_core PUBLIC -Wall -Wextra -Wno-unused-parameter)
  find_package(Threads REQUIRED)
  target_link_libraries(lumen_core PUBLIC Threads::Threads)
endif()

add_executable(lumen src/main.cpp)
target_link_libraries(lumen PRIVATE lumen_core)
if(WIN32)
  # GUI program (no console window); logs go to the terminal when launched from one.
  set_target_properties(lumen PROPERTIES WIN32_EXECUTABLE TRUE)
  set_source_files_properties(src/lumen.rc PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/assets")
  target_sources(lumen PRIVATE src/lumen.rc)
  add_executable(lumen-console src/main.cpp src/lumen.rc)
  target_link_libraries(lumen-console PRIVATE lumen_core)
  if(MSVC)
    target_link_options(lumen PRIVATE /ENTRY:mainCRTStartup)
    # The Visual C++ runtime next to the program: it must start on a PC without
    # the VC++ redistributable installed (the package copies every DLL here).
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
      add_custom_command(TARGET lumen POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} $<TARGET_FILE_DIR:lumen>)
    endif()
  endif()
endif()

# Copy shipped example assets next to the executable.
add_custom_command(TARGET lumen POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/examples $<TARGET_FILE_DIR:lumen>/examples)

# ---------------------------------------------------------------- tests
option(LUMEN_BUILD_TESTS "Build unit tests" ON)
if(LUMEN_BUILD_TESTS)
  enable_testing()
  add_executable(lumen_tests
    tests/test_core.cpp
    src/media/hap.cpp
    src/control/midi.cpp
    src/core/warp.cpp
    src/core/dmx.cpp
    src/core/blend.cpp
    src/core/project.cpp
    src/core/svg.cpp
    src/core/isf.cpp
    src/core/blob.cpp
    src/core/md5.cpp
    src/core/timecode.cpp
    src/core/parallel.cpp
    src/core/audio_route.cpp
    src/control/devices.cpp
    src/io/serial.cpp
    src/control/osc.cpp
    src/ui/i18n.cpp
    src/core/machine_settings.cpp
    src/platform/platform.cpp)
  target_include_directories(lumen_tests PRIVATE src third_party)
  target_compile_definitions(lumen_tests PRIVATE LUMEN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(lumen_tests PRIVATE rtmidi)
  if(WIN32)
    target_link_libraries(lumen_tests PRIVATE ws2_32 comdlg32 shell32 ole32)
    target_compile_definitions(lumen_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    if(MSVC)
      target_compile_options(lumen_tests PRIVATE /utf-8)
    endif()
  else()
    find_package(Threads REQUIRED)
    target_link_libraries(lumen_tests PRIVATE Threads::Threads)
  endif()
  add_test(NAME core COMMAND lumen_tests)
endif()

# miniaudio is third-party code compiled inside audio.cpp: keep its warnings quiet.
if(NOT MSVC)
  set_source_files_properties(src/media/audio.cpp PROPERTIES COMPILE_OPTIONS "-Wno-unused-result;-Wno-unused-function")
endif()
