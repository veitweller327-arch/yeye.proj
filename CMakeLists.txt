# Timing Shift FX - OpenFX plugin for DaVinci Resolve
#
# Windows (Visual Studio 2022):
#   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
#   cmake --build build --config Release
#   -> build/TimingShiftFX.ofx.bundle/Contents/Win64/TimingShiftFX.ofx
cmake_minimum_required(VERSION 3.16)
project(TimingShiftFX VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Static MSVC runtime: the plugin then needs no Visual C++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(TSFX_BUILD_TESTS "Build the unit tests and the mock-host test" ON)

set(OFX_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/openfx/include)

# Image processing core (no OpenFX dependency, unit-testable).
add_library(tsfx_core STATIC src/TimingShiftCore.cpp)
target_include_directories(tsfx_core PUBLIC src)
set_target_properties(tsfx_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)
target_link_libraries(tsfx_core PUBLIC Threads::Threads)

# The OFX plugin itself: a shared library with the extension ".ofx".
add_library(TimingShiftFX MODULE src/TimingShiftPlugin.cpp)
target_include_directories(TimingShiftFX PRIVATE ${OFX_INCLUDE_DIR})
target_link_libraries(TimingShiftFX PRIVATE tsfx_core)
set_target_properties(TimingShiftFX PROPERTIES PREFIX "" SUFFIX ".ofx"
                      CXX_VISIBILITY_PRESET hidden)

# OpenFX bundle layout: <Name>.ofx.bundle/Contents/<Arch>/<Name>.ofx
if(WIN32)
  set(OFX_ARCH Win64)
elseif(APPLE)
  set(OFX_ARCH MacOS)
else()
  set(OFX_ARCH Linux-x86-64)
endif()
set(BUNDLE_DIR ${CMAKE_BINARY_DIR}/TimingShiftFX.ofx.bundle/Contents)
foreach(cfg IN ITEMS "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
  set_target_properties(TimingShiftFX PROPERTIES LIBRARY_OUTPUT_DIRECTORY${cfg}
                        ${BUNDLE_DIR}/${OFX_ARCH})
endforeach()
# Bundle metadata (optional for Resolve, but part of the OFX bundle standard).
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/installer/Info.plist
               ${BUNDLE_DIR}/Info.plist COPYONLY)

if(MSVC)
  target_compile_options(tsfx_core PRIVATE /O2 /W3)
  target_compile_options(TimingShiftFX PRIVATE /O2 /W3)
else()
  target_compile_options(tsfx_core PRIVATE -O3 -Wall -Wextra)
  target_compile_options(TimingShiftFX PRIVATE -O3 -Wall -Wextra)
endif()

if(TSFX_BUILD_TESTS)
  enable_testing()
  add_executable(core_tests tests/core_tests.cpp)
  target_link_libraries(core_tests PRIVATE tsfx_core)
  add_test(NAME core_tests COMMAND core_tests)

  # The mock host loads the real plugin binary like Resolve would.
  add_executable(mock_host tests/mock_host.cpp)
  target_include_directories(mock_host PRIVATE ${OFX_INCLUDE_DIR})
  target_link_libraries(mock_host PRIVATE ${CMAKE_DL_LIBS})
  add_dependencies(mock_host TimingShiftFX)
  add_test(NAME mock_host
           COMMAND mock_host $<TARGET_FILE:TimingShiftFX>)
endif()

# Still-image preview tool (see tools/make_preview.py).
if(TSFX_BUILD_TESTS)
  add_executable(preview tools/preview.cpp)
  target_link_libraries(preview PRIVATE tsfx_core)
endif()
