cmake_minimum_required(VERSION 3.16)

project(GVFG_Customer_Sample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(NOT WIN32)
  message(FATAL_ERROR "This sample supports Windows only.")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)

add_library(GVFG::Capture SHARED IMPORTED GLOBAL)
set_target_properties(GVFG::Capture PROPERTIES
  IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/bin/gvfg.dll"
  IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/lib/gvfg.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(GVFG::Preview SHARED IMPORTED GLOBAL)
set_target_properties(GVFG::Preview PROPERTIES
  IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/bin/gvfg_preview.dll"
  IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/lib/gvfg_preview.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

qt_add_executable(gvfg_qt_preview WIN32
  src/main.cpp
  src/capture_controller.cpp
  src/capture_controller.h
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
  Qt6::Multimedia
  GVFG::Capture
  GVFG::Preview
)

if(MSVC)
  target_compile_options(gvfg_qt_preview PRIVATE /utf-8)
endif()

add_custom_command(TARGET gvfg_qt_preview POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/bin/gvfg.dll"
    "${CMAKE_CURRENT_SOURCE_DIR}/bin/gvfg_preview.dll"
    $<TARGET_FILE_DIR:gvfg_qt_preview>
)
