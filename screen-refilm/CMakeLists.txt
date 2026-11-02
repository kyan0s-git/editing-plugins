cmake_minimum_required(VERSION 3.16)
project(ScreenRefilm VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

find_package(Threads REQUIRED)

add_library(refilm_core STATIC src/refilm_params.cpp src/refilm_render.cpp)
target_include_directories(refilm_core PUBLIC src)
target_link_libraries(refilm_core PUBLIC Threads::Threads)
if(MSVC)
  target_compile_options(refilm_core PRIVATE /O2 /fp:fast)
else()
  target_compile_options(refilm_core PRIVATE -O3 -fno-math-errno)
endif()

# --- Command line renderer (needs ffmpeg/ffprobe on PATH at runtime) ---
add_executable(screen-refilm src/refilm_cli.cpp)
target_link_libraries(screen-refilm PRIVATE refilm_core)

# --- OpenFX plugin (DaVinci Resolve, Fusion, Nuke, Natron, Vegas, ...) ---
add_library(ScreenRefilm MODULE src/refilm_ofx.cpp)
target_include_directories(ScreenRefilm PRIVATE third_party/openfx/include)
target_link_libraries(ScreenRefilm PRIVATE refilm_core)
set_target_properties(ScreenRefilm PROPERTIES PREFIX "" SUFFIX ".ofx"
                      CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

if(APPLE)
  set(OFX_ARCH MacOS)
elseif(WIN32)
  set(OFX_ARCH Win64)
else()
  set(OFX_ARCH Linux-x86-64)
endif()
set(BUNDLE_DIR ${CMAKE_BINARY_DIR}/ScreenRefilm.ofx.bundle)
add_custom_command(TARGET ScreenRefilm POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${BUNDLE_DIR}/Contents/${OFX_ARCH}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ScreenRefilm> ${BUNDLE_DIR}/Contents/${OFX_ARCH}/
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/resources/Info.plist ${BUNDLE_DIR}/Contents/
  COMMENT "Assembling ScreenRefilm.ofx.bundle")

# `cmake --install build` copies the bundle into the system OFX plugin folder.
if(APPLE)
  set(OFX_INSTALL_DIR "/Library/OFX/Plugins" CACHE PATH "OFX plugin directory")
elseif(WIN32)
  set(OFX_INSTALL_DIR "C:/Program Files/Common Files/OFX/Plugins" CACHE PATH "OFX plugin directory")
else()
  set(OFX_INSTALL_DIR "/usr/OFX/Plugins" CACHE PATH "OFX plugin directory")
endif()
install(DIRECTORY ${BUNDLE_DIR} DESTINATION ${OFX_INSTALL_DIR})

# --- Tests ---
enable_testing()
if(NOT WIN32)
  add_executable(ofx_host_test tests/ofx_host_test.cpp)
  target_include_directories(ofx_host_test PRIVATE third_party/openfx/include)
  target_link_libraries(ofx_host_test PRIVATE ${CMAKE_DL_LIBS})
  add_test(NAME ofx_host COMMAND ofx_host_test $<TARGET_FILE:ScreenRefilm>)
  add_test(NAME cli_smoke COMMAND ${CMAKE_SOURCE_DIR}/tests/cli_smoke_test.sh $<TARGET_FILE:screen-refilm>)
endif()
