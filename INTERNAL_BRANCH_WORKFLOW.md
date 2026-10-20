# Internal branch workflow

Use the Customer Sample directory for both branches. Commit changes before switching.
Run `git switch master` for customer development and
`git switch internal/audio-diagnostics` for internal diagnostics.

## Common customer functionality

1. Implement and commit the change on `master`.
2. Switch to `internal/audio-diagnostics` and run `git merge master`.
3. Resolve conflicts by preserving the internal diagnostics around the updated
   customer behavior.

## Internal-only diagnostics

Commit internal logs, driver counters, timing measurements, and debug API usage
only on `internal/audio-diagnostics`.

Never merge this branch back into `master` or a customer
remote. This branch is intended for local or company-internal storage only.

## Build and run the internal application

- Select the Qt Creator **Debug** configuration. The sample version is
  `1.0.3-internal`; `GVFG_INTERNAL_DIAGNOSTICS` is enabled only for Debug.
- By default this checkout builds the sibling `GVFG_Standalone/GVFG_SDK` sources
  together with the sample, including `gvfg.dll` and `gvfg_playback.dll`.
  Use the MSVC x64 Qt kit for the supplied vendor static library.
- `GVFG_SAMPLE_SDK_BUILD_DIR` selects an explicit prebuilt SDK instead. Debug
  diagnostics require a matching SDK with working internal diagnostic APIs;
  `GVFG_SAMPLE_SDK_SOURCE_DIR` supplies the private `gvfg_debug.h` header.
- Build manually before using `run_diagnostic.bat`. Switching Git branches does
  not update an existing executable. Reconfigure CMake after switching branches.
- `package_debug.bat` copies capture/playback DLLs beside the selected Debug
  application by default. `SAMPLE_SOURCE_BIN` selects that directory;
  `SDK_SOURCE_BIN` can explicitly override its SDK runtime directory.
- Startup timing measures playback submission acceptance, not actual display
  presentation. HDMI's temporary SDK Lock bypass makes `connected` an effective
  SDK state and does not prove physical HDMI lock.
