# GVFG Qt Customer Sample 1.0.0

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
