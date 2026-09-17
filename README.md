# KJ_WINDOWS

## Building on Windows

1. Open a **x64** Developer Command Prompt for Visual Studio 2019 or newer.
2. Use a fresh build directory per architecture (for example, `build/x64`) to avoid mixing 32-bit and 64-bit artifacts.
3. Configure and build with CMake:

   ```bat
   cmake -G "Visual Studio 16 2019" -A x64 -S %cd% -B build\x64
   cmake --build build\x64 --config Release
   ```

If you see `base.lib : fatal error LNK1136: invalid or corrupt file`, the build directory likely contains stale artifacts from a different architecture. Delete the build folder (for example, `rmdir /S /Q build`) and reconfigure using a dedicated directory for the current architecture.

## Experimental VST3 hosting

The optional VST3 build uses Steinberg SDK **3.8.1**, tag `v3.8.1_build_84`.
Download the SDK with its submodules into `external/vst3sdk` (or set
`KJ_VST3_SDK_ROOT` to an existing SDK checkout), then configure a fresh build:

```powershell
git clone --branch v3.8.1_build_84 --depth 1 --recurse-submodules --shallow-submodules https://github.com/steinbergmedia/vst3sdk.git external/vst3sdk
cmake -S . -B build/vst381 -G "Visual Studio 16 2019" -A x64 -DKJ_ENABLE_VST3=ON -DKJ_BUILD_TESTS=ON
cmake --build build/vst381 --config Release --parallel 4
ctest --test-dir build/vst381 -C Release --output-on-failure
```

Run `build/vst381/bin/x64/Release/KJ.exe`. Select a track and use the **VST3**
menu to choose an installed instrument or effect. Loading an instrument changes
the track type to VST3; draw notes in the piano roll or enable sequencer steps
and press Play. Effects process the selected track's existing sound. Each track
has one instrument slot and an ordered effects rack. Loading an instrument
replaces it; adding an effect appends a new slot. The editing panel's **FX** page
mixes KJ EQ, Delay, Compressor, Sidechain and VST3 effects in one signal chain.
Use **+ Effect**, **Up/Down**, **Bypass/Enable**, and **Remove** for either kind.
New tracks start with an empty rack; effects are added explicitly with **+ Effect**.
Use **Edit** for native controls or **Editor** for a plugin's window. Native
effects retain one slot per type and their existing per-track settings and
modulation; removing one retains its settings for re-adding. Rack changes keep
playback and recording running. Plugins are prepared on the main thread and
inserted/removed between audio processing passes. Project save/load still stop
transport; press Play afterward.
Editor parameter changes reach the processor on the next processing block.

The editing panel below the sequencer includes an **Instrument** tab when its
track is VST3. Use it to load/replace, open, bypass, locate, or remove the
instrument. Effects process from top to bottom in the displayed order, including
when a native effect moves before or after a plugin. Volume and pan remain at
the track output. The VST3 menu opens this same panel. Pin and split use its track
bindings; switching its track to a non-VST3 type returns Instrument to FX.

This stage supports mono/stereo audio, note events, and transport context.
Racks with plugins share one 64-sample buffer, without plugin latency compensation.
Native-only tracks process without that buffer. Projects store the mixed order;
older projects preserve their original plugins-then-native signal flow.
Project files restore plugin class/path, component and controller state, current
parameter values, slot order and bypass. Missing plugins remain visible with
their saved state: effects pass through and instruments are silent. Select a
slot and use **Locate** to restore the same plugin from another installed path
or a `.vst3` bundle folder. Builds without VST3 retain this data when saving.
Automation recording and MIDI 2.0 extensions are not implemented.
Device sample-rate changes request a stopped
main-thread reconfiguration automatically. Only 64-bit Windows VST3 is supported,
not legacy VST2 DLLs.

See [VST3 implementation and testing](docs/vst3_current_implementation.md) for
the lifecycle contract, test commands, and remaining work.

## Third-party attributions

- [Cockos WDL LICE](https://www.cockos.com/wdl/): Lightweight Image Compositing Engine used for GUI rendering. Source files from the official Cockos repository are included in `external/wdl`, redistributed under the terms of the Cockos WDL license.
- Steinberg VST3 SDK is **not** vendored in this repository anymore. If VST3 development assets are needed, install/download them separately and reference them from your local environment.
- Copyright (C) 2005 and later Cockos Incorporated
    
    Portions copyright other contributors, see each source file for more information

    
