# VST3 implementation status

This document supersedes the historical integration notes, which referenced a
hosting implementation that was absent from this source copy.

## Implemented

- Optional `KJ_ENABLE_VST3` build, pinned locally to SDK `v3.8.1_build_84`
  (commit `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`).
- SDK Windows bundle loading, audio-class selection, component/controller
  initialization, connection and initial state synchronization.
- One VST3 instrument and an ordered effects rack per track. Instruments receive
  sequencer and piano-roll note events. KJ EQ, compressor, delay and sidechain
  occupy native entries in the same chain as VST3 effects; pan/volume follow it.
- A 64-frame adapter between KJ's per-sample renderer and plugin processing.
  The whole chain adds 64 samples regardless of slot count; plugin-reported latency is not compensated.
- Rack controls support add, remove, up/down, bypass, native Edit, plugin Editor,
  and plugin Locate. Stable IDs preserve plugin editor identity when effects
  move. Native effects retain one slot per type with existing per-track settings
  and modulation. Rack mutations preserve transport and recording.
- Integrated editing-panel racks: conditional Instrument tab for a VST3-bound
  track and mixed native/plugin effect slots inside FX. Slot actions use stable IDs; native
  plugin editors open from each row. Panel scrolling, pinning and FX/Mod split
  remain available. The VST3 menu opens the same editing panel.
- `Track::fxOrder` and the project's `fxOrder` array define mixed processing order.
  Old projects migrate to plugins followed by EQ, compressor, delay, sidechain.
  Native callbacks execute at their exact place within a shared plugin block;
  per-frame compressor, delay-mix and sidechain detector inputs are buffered
  alongside audio. Native-only tracks retain per-sample processing with no
  additional buffer. Native enable controls reinsert removed effects as needed.
- Project recall stores class/path, component/controller streams and normalized
  parameter values, plus slot identity, order and bypass. Streams are hex encoded
  in `.jik` files; parameter values retain double precision. Missing/unavailable
  plugins retain their state and can be relinked. Missing effects pass through;
  missing instruments are silent. SDK-disabled builds preserve plugin metadata.
  Save/load pause and join audio before accessing plugins; recording must stop first.
- Tempo, project sample position, musical position and 4/4 transport context.
- Main-thread load/prepare/teardown, audio-thread start/process/stop. Live edits
  commit through a synchronous main-to-audio handoff between processing passes.
  Prepared map nodes and reserved slot storage avoid insertion allocations in
  that handoff; removed plugins are stopped on audio and disposed on the main
  thread. Unchanged slots retain their buffered audio and processing state.
  Whole-project capture/restore and device reconfiguration still join audio.
- Audio buffers and note storage are allocated before rendering. Output
  parameter storage uses SDK helpers; strict real-time auditing remains work.
  Invalid audio and queue overflow disable the affected slot and trigger a GUI
  error (effects pass through, instruments become silent). Rate mismatch temporarily mutes until reconfiguration. Native plugin
  crashes are not isolated.
- Note-offs and queued-buffer reset on stop; instrument slots are silent when
  unloaded. Clearing a project releases session plugins on the GUI thread.
- Native Windows editors with IPlugFrame resizing, DPI scaling, close/reopen,
  and removal before module teardown. Editors open on load and through the menu.
- Atomic controller-to-audio parameter transfer. Controller parameters are
  enumerated after component connection and initial state synchronization,
  because Surge's JUCE controller does not expose them earlier.
- Device-rate mismatches request main-thread reconfiguration rather than a
  fatal processing error. Other errors include track, slot and exact operation.

## Tests

`kj_vst3_probe` exercises individual plugin classes at 44.1 and 48 kHz, with
variable frames up to 64/512, finite output checks, note events for instruments,
and repeated load/unload. `--require-audio` additionally requires nonzero output.

`kj_vst3_track_tests` exercises the actual track adapter: instrument into effect,
a separate effect track, non-block-aligned note onset, 64-frame buffering, and
stop/restart at both rates. It uses explicit checks enabled in Release builds.

Register installed test plugins with CMake; paths are local configuration and
are not automatically scanned or hardcoded into the build:

```powershell
cmake -S . -B build/vst381 `
  '-DKJ_VST3_TEST_INSTRUMENT=C:/Program Files/Common Files/VST3/Surge Synth Team/Surge XT.vst3' `
  '-DKJ_VST3_TEST_EFFECT=C:/Program Files/Common Files/VST3/Surge Synth Team/Surge XT Effects.vst3'
cmake --build build/vst381 --config Release --parallel 4
ctest --test-dir build/vst381 -C Release --output-on-failure
```

The installed Surge plugins report plugin SDK 3.7.2. These tests exercise
backward-compatible loading with a host built using SDK 3.8.1; they do not
certify all optional 3.8.1 interfaces. The earlier CardinalFX probe terminated
unsuccessfully; compatibility with Cardinal remains unresolved.

## Remaining work

Component restart requests beyond parameter-value/title changes;
plugin latency compensation; auxiliary/multichannel routing; MIDI CC and MIDI 2.0;
transport-control requests; background isolated scanning and crash recovery.
`kj_vst3_editor_tests` passes with Surge XT: native child view attachment,
close/reopen, unload with a view open, and editor parameter transfer verified by
Surge XT Effects' bypass audio. The track adapter tests cover device-rate
reconfiguration requests, state restoration and subsequent successful processing.
The user confirmed Surge XT editor and sequencer playback work. The manual
`kj_vst3_engine_tests` cannot obtain an active audio device in the tool process;
the current rack changes still need an audible check in the desktop application.

`kj_vst3_rack_tests` uses two local deterministic VST3 fixture effects to verify
noncommutative ordering (gain then clipping versus clipping then gain), one
64-frame delay, stable slot IDs, bypass, project recall, edits saved before first
processing, missing-effect pass-through/state retention, relinking and failed
load preservation. The fixture is a test artifact and is not installed.
`kj_vst3_project_data_tests` checks binary state preservation, malformed-input
rejection before replacing the session, and old projects without plugin fields.
Run it in both SDK-enabled and SDK-disabled builds.
`kj_vst3_panel_tests` draws the real panel in a hidden window and exercises its
hit-testing for effect reorder, bypass and removal. It also checks conditional
instrument visibility and fallback to FX after a track-type change.
Mixed-rack tests additionally verify native/plugin boundary reordering through
the panel, native bypass/removal, callback dispatch before/after a plugin with
noncommutative test operations, the unbuffered native-only path, and mixed-order
project recall. Native order/enable state and legacy migration are checked in
both VST3-enabled and disabled builds.

Transport stop currently cuts output immediately, including plugin tails, in
keeping with KJ's existing stopped-render behavior. Plugins may retain internal
release/reverb tails when processing resumes. Effect changes no longer restart
the device or reset the sequencer. Adding/removing the first/last plugin changes
that track's buffering latency; plugin-specific processing stalls or clicks are
not eliminated by this change.

`kj_vst3_live_rack_tests` runs concurrent audio during repeated slow plugin
construction, insertion, removal, reorder, bypass and failed loads. It checks
continuous output on an unchanged track and that established rack buffers do
not reset. Panel tests assert that native and plugin actions leave Play enabled,
including adding a native effect through the chooser.
