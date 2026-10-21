# GVFG Internal Diagnostic Sample

This internal Qt sample demonstrates the public Capture and Playback APIs: device enumeration,
channel setup, start/stop, YUY2/Y210 selection, copy/zero-copy capture, video preview,
and optional audio playback.

The SDK's `gvfg_playback.dll` manages playback and synchronization. Debug builds enable
A/V synchronization and internal diagnostics. Release builds use independent playback.
Both configurations include the video reception counter and the helper-managed NO SIGNAL card.

## 1. Requirements

- Windows 10/11 x64 with compatible GVFG drivers and capture hardware.
- CMake 3.16 or newer.
- Qt 6 Widgets and a compatible Windows x64 C++ toolchain.
- No Qt Multimedia module is required.
- Internal Debug diagnostics require the sibling SDK sources and MSVC x64.

## 2. Package Contents and Dependencies

```text
include/  gvfg_capture.h, gvfg_playback.h, gvfg_preview.h, gvfg_audio_playback.h
bin/      gvfg.dll, gvfg_playback.dll
lib/      gvfg.lib, gvfg_playback.lib, and GNU import libraries
src/      Qt sample source
```

Link against `gvfg.lib` and `gvfg_playback.lib`, and deploy `gvfg.dll` and
`gvfg_playback.dll` beside the executable. `gvfg_preview.h` and
`gvfg_audio_playback.h` provide data types used by Playback.

The repository includes public SDK headers, import libraries, and DLLs. Internal
Debug builds additionally use private headers from the sibling SDK source checkout.
SDK updates must include matching headers, import libraries, and DLLs.

## 3. Building

Open this directory's `CMakeLists.txt` in Qt Creator and select a Windows x64 Qt kit,
or run the following in a terminal configured for your compiler. Adjust the Qt path
to match your installation:

```bat
cmake -S . -B build_release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.10.2/msvc2022_64
cmake --build build_release --target gvfg_qt_preview
```

For a Visual Studio multi-configuration generator, select the appropriate generator
and pass `--config Release` when building.

This internal branch defaults to the sibling `GVFG_Standalone/GVFG_SDK` source tree
and builds matching SDK targets. Use `GVFG_SAMPLE_SDK_SOURCE_DIR` to select another
SDK source tree. An explicit `GVFG_SAMPLE_SDK_BUILD_DIR` selects its prebuilt bin/lib;
the headers must match that build. CMake copies the selected DLLs beside the executable.

Customer `master` independently defaults to the repository's packaged include/bin/lib.

## 4. API Usage in the Source

| File | Purpose |
|---|---|
| `src/playback.cpp` | Thin wrapper around create, attach, default_config, start, submit, reset, stop, and destroy |
| `src/capture_controller.cpp` | Capture lifecycle, video/audio reads, frame release after use/copy, and event handling |
| `src/capture_controller_status.cpp` | Signal/format/FPS display, received frame counts, and basic API errors |
| `src/internal_diagnostics.cpp` | Debug-only backend counters, startup timing, diagnostic snapshots, and warmup/reset baselines |
| `src/mainwindow.cpp`, `src/previewwindow.cpp` | UI, HWND, and preview window lifecycle |

Typical operation: select a device and output format, choose whether to enable audio,
then click Start and Stop. The UI exposes CH0; it does not provide a complete
two-channel interface.

Hiding the preview does not stop video timestamp submissions. A/V mode still requires
both streams. On stop, the sample joins its read workers and stops playback before
releasing capture resources.

The sample displays basic status, versions, and API errors. Its existing status loop
passes `connected` from `gvfg_get_channel_signal_status()` to
`gvfg_playback_set_signal_connected()`. The Playback helper owns the built-in
NO SIGNAL card; the sample does not need image assets or card rendering code.
Signal state is not inferred from FPS, frame timeouts, or black pixels.

Internal Debug retains no-frame/resume logs and backend diagnostics. Signal text
describes SDK connection state, which does not by itself prove physical HDMI lock
or successful frame delivery.

See [gvfg_capture.h](include/gvfg_capture.h) for Capture APIs and ownership contracts,
and [gvfg_playback.h](include/gvfg_playback.h) for playback modes, synchronization,
and signal card APIs.

## 5. Release Packaging

Build Release first, then run:

```bat
package_release.bat "C:\path\to\gvfg_qt_preview.exe" "C:\Qt\6.10.2\msvc2022_64\bin" "C:\path\to\packages"
```

Without arguments, the script uses its configured Qt Creator Release build path.
It does not build the application or search other build directories. It takes
`gvfg.dll` and `gvfg_playback.dll` from the executable's directory and uses the
matching `windeployqt` to package Qt and runtime dependencies.

The application runtime ZIP and the source development package serve different
purposes. For source delivery, include `CMakeLists.txt`, `README.md`,
`package_release.bat`, and `src/include/bin/lib`. Exclude local build directories,
`CMakeCache.txt`, Git metadata, and Qt Creator user settings so each user can create
their own build directory.
