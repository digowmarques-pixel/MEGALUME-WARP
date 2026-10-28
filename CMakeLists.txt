cmake_minimum_required(VERSION 3.20)
project(Lumen VERSION 0.1.0 LANGUAGES C CXX)

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
  pkg_check_modules(FFMPEG_PC REQUIRED IMPORTED_TARGET libavformat libavcodec libswscale libavutil)
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
target_compile_definitions(imgui PUBLIC GLFW_INCLUDE_NONE)

# ---------------------------------------------------------------- Lumen
add_executable(lumen
  src/main.cpp
  src/app.cpp
  src/gl/gl.cpp
  src/render/gpu.cpp
  src/media/decoder.cpp
  src/media/source.cpp
  src/core/warp.cpp
  src/core/project.cpp
  src/engine/engine.cpp
  src/engine/compositor.cpp
  src/engine/outputs.cpp
  src/control/osc.cpp
  src/control/mapping.cpp
  src/ui/ui.cpp
  src/ui/workspace.cpp
  src/platform/platform.cpp)

target_include_directories(lumen PRIVATE
  src
  third_party/khr
  third_party)
target_link_libraries(lumen PRIVATE imgui glfw OpenGL::GL lumen_ffmpeg)
target_compile_definitions(lumen PRIVATE GLFW_INCLUDE_NONE LUMEN_VERSION="${PROJECT_VERSION}")

if(WIN32)
  target_link_libraries(lumen PRIVATE ws2_32 comdlg32 shell32)
  target_compile_definitions(lumen PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
  # GUI program (no console window); logs go to the terminal when launched from one.
  set_target_properties(lumen PROPERTIES WIN32_EXECUTABLE TRUE)
  if(MSVC)
    target_compile_options(lumen PRIVATE /W3 /utf-8)
    target_link_options(lumen PRIVATE /ENTRY:mainCRTStartup)
  endif()
else()
  target_compile_options(lumen PRIVATE -Wall -Wextra -Wno-unused-parameter)
  find_package(Threads REQUIRED)
  target_link_libraries(lumen PRIVATE Threads::Threads)
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
    src/core/warp.cpp
    src/core/project.cpp
    src/control/osc.cpp
    src/platform/platform.cpp)
  target_include_directories(lumen_tests PRIVATE src third_party)
  if(WIN32)
    target_link_libraries(lumen_tests PRIVATE ws2_32 comdlg32 shell32)
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
