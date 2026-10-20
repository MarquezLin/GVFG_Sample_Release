cmake_minimum_required(VERSION 3.16)

project(GVFG_Customer_Sample LANGUAGES CXX)

set(GVFG_SAMPLE_VERSION "1.0.3")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(NOT WIN32)
  message(FATAL_ERROR "This sample supports Windows only.")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Development override: validate against a freshly built SDK without replacing
# the packaged runtime. Without an override, prefer sibling SDK sources; a
# standalone customer package uses its own include/bin/lib layout.
set(GVFG_SAMPLE_SDK_BUILD_DIR "" CACHE PATH "GVFG standalone build directory")
set(GVFG_SAMPLE_SDK_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../GVFG_Standalone/GVFG_SDK" CACHE PATH "GVFG standalone sources")
set(_sdk_bin "${CMAKE_CURRENT_SOURCE_DIR}/bin")
set(_sdk_lib "${CMAKE_CURRENT_SOURCE_DIR}/lib")
set(_sdk_include "${CMAKE_CURRENT_SOURCE_DIR}/include")
if(GVFG_SAMPLE_SDK_BUILD_DIR)
  set(_sdk_bin "${GVFG_SAMPLE_SDK_BUILD_DIR}/bin")
  set(_sdk_lib "${GVFG_SAMPLE_SDK_BUILD_DIR}/lib")
  set(_sdk_include "${GVFG_SAMPLE_SDK_SOURCE_DIR}/sdk/gvfg/include;${GVFG_SAMPLE_SDK_SOURCE_DIR}/helpers/gvfg_preview/include;${GVFG_SAMPLE_SDK_SOURCE_DIR}/helpers/gvfg_audio_playback/include;${GVFG_SAMPLE_SDK_SOURCE_DIR}/helpers/gvfg_playback/include")
endif()

# In a development checkout, build the sibling SDK from source so newly added
# APIs cannot accidentally link against stale package import libraries.
if(NOT GVFG_SAMPLE_SDK_BUILD_DIR AND EXISTS "${GVFG_SAMPLE_SDK_SOURCE_DIR}/CMakeLists.txt")
  add_subdirectory("${GVFG_SAMPLE_SDK_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/gvfg_sdk")
  add_library(GVFG::Capture ALIAS gvfg)
  add_library(GVFG::Playback ALIAS gvfg_playback)
else()
  if(MSVC)
    set(GVFG_CAPTURE_IMPLIB "${_sdk_lib}/gvfg.lib")
    set(GVFG_PLAYBACK_IMPLIB "${_sdk_lib}/gvfg_playback.lib")
  elseif(MINGW)
    set(GVFG_CAPTURE_IMPLIB "${_sdk_lib}/libgvfg.dll.a")
    set(GVFG_PLAYBACK_IMPLIB "${_sdk_lib}/libgvfg_playback.dll.a")
  else()
    message(FATAL_ERROR "Unsupported compiler. Use MSVC or MinGW on Windows x64.")
  endif()

  foreach(GVFG_REQUIRED_FILE
      "${_sdk_bin}/gvfg.dll"
      "${_sdk_bin}/gvfg_playback.dll"
      "${GVFG_CAPTURE_IMPLIB}"
      "${GVFG_PLAYBACK_IMPLIB}")
    if(NOT EXISTS "${GVFG_REQUIRED_FILE}")
      message(FATAL_ERROR "Required GVFG SDK file was not found: ${GVFG_REQUIRED_FILE}")
    endif()
  endforeach()

  add_library(GVFG::Capture SHARED IMPORTED GLOBAL)
  set_target_properties(GVFG::Capture PROPERTIES
    IMPORTED_LOCATION "${_sdk_bin}/gvfg.dll"
    IMPORTED_IMPLIB "${GVFG_CAPTURE_IMPLIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_sdk_include}"
  )

  add_library(GVFG::Playback SHARED IMPORTED GLOBAL)
  set_target_properties(GVFG::Playback PROPERTIES
    IMPORTED_LOCATION "${_sdk_bin}/gvfg_playback.dll"
    IMPORTED_IMPLIB "${GVFG_PLAYBACK_IMPLIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_sdk_include}"
  )

endif()

qt_add_executable(gvfg_qt_preview WIN32
  src/main.cpp
  src/playback.cpp
  src/playback.h
  src/capture_controller.cpp
  src/capture_controller.h
  src/capture_controller_status.cpp
  src/sample_log.cpp
  src/sample_log.h
  src/mainwindow.cpp
  src/mainwindow.h
  src/mainwindow.ui
  src/previewwindow.cpp
  src/previewwindow.h
  src/previewwindow.ui
)

target_link_libraries(gvfg_qt_preview PRIVATE
  Qt6::Widgets
  GVFG::Capture
  GVFG::Playback
)

target_compile_definitions(gvfg_qt_preview PRIVATE
  GVFG_SAMPLE_VERSION="${GVFG_SAMPLE_VERSION}"
)

if(MSVC)
  target_compile_options(gvfg_qt_preview PRIVATE /utf-8)
endif()

add_custom_command(TARGET gvfg_qt_preview POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:GVFG::Capture>
    $<TARGET_FILE:GVFG::Playback>
    $<TARGET_FILE_DIR:gvfg_qt_preview>
)
