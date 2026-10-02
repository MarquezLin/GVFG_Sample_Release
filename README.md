# GVFG Qt Customer Sample

This package contains a Qt application sample and the prebuilt GVFG SDK files required to compile it.

## Requirements

- Windows 10/11 x64
- CMake 3.16 or newer
- Qt 6 with Widgets and Multimedia
- A compatible Windows x64 C++ toolchain
- Compatible GVFG driver and capture hardware

## Build

Open this directory as a CMake project in Qt Creator and select a Windows x64 Qt kit, or configure and build it with CMake using your preferred compatible toolchain.

Example:

```text
cmake -S . -B build -DCMAKE_PREFIX_PATH=<path-to-Qt>
cmake --build build --config Release
```

## Package layout

```text
bin/      GVFG runtime DLLs
include/  Public GVFG headers
lib/      GVFG x64 import libraries
src/      Qt sample application source
```

The sample demonstrates device enumeration, channel start/stop, signal events, video preview, zero-copy selection, output-format selection, and optional audio monitoring through the public GVFG APIs.

## Release packaging

Run `package_release.bat` after manually building the Release application in Qt Creator.
The default executable is `build/Desktop_Qt_6_10_2_MSVC2022_64bit-Release/bin/gvfg_qt_preview.exe`.
The script never builds the application or searches other build directories.
It packages the public SDK DLLs from `bin/` and rejects internal diagnostic exports/imports.
For a different kit, pass the executable and matching Qt bin directory explicitly:

```bat
package_release.bat "<application.exe>" "<Qt-bin>" "<output-directory>"
```
