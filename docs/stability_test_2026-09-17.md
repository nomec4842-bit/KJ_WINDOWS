# Stability test — 2026-09-17

Scope: Windows Release builds in `build/vst381` and `build/no-vst-rack`. This is a bounded stress pass, not release certification.

## Regression results

- Full VST build suite: 21/21 passed.
- Full native build suite: 15/15 passed.
- VST reliability, rack recall, editing panel, live rack, and parameter modulation tests repeated 20 times each: 100/100 passed.
- Native reliability, project data, sampler modes, and continuous piano tests repeated 30 times each: 120/120 passed.
- Total regression test invocations: 256 passed.

## Installed plugin probes

Each plugin was tested in a separate process with a 45-second watchdog. Checks covered two load/unload cycles, 44.1/48 kHz, 64/512-frame maximum blocks, and nonzero output.

- Surge XT: passed, including clean process exit.
- Surge XT Effects: passed, including clean process exit.
- FreeEQ8: processing and explicit unload/reload checks completed, but the standalone probe did not exit within 45 seconds and was terminated by the watchdog. The user separately confirmed successful plugin removal and subsequent KJ shutdown. This is an unresolved probe-exit issue, not an established application shutdown failure. The log does not identify whether final host destruction, COM teardown, or plugin/process cleanup is responsible.

Logs are `build/vst381/stability_surge_instrument.log`, `stability_surge_effects.log`, and `stability_freeeq8.log`.

## Playback soak

Passed 300 seconds at the actual output rate of 192,000 Hz, followed by clean teardown. Completed 2,553 control-edit iterations, 149 effect-rack cycles, 19 stop/start cycles, and four project save/reload cycles. No monitored errors, non-finite samples, or three-second silence/transport stalls occurred. Master recording completed successfully. Private memory increased from 92.97 MiB after warmup to 95.75 MiB at completion (+2.79 MiB); handles changed from 271 to 269. Full output is in `build/vst381/stability_soak.log`.

The manual `kj_stability_soak` target exercises the actual playback engine with a synth, generated sampler data, Surge XT, Surge XT Effects, native EQ, and repeatedly inserted/removed test gain effects. It changes controls during playback, cycles transport, saves/reopens a scratch project, and writes a master recording. Test audio is muted at its own Windows audio session.

The runner is `python tests/run_stability_soak.py`; installed plugin probes use `python tests/run_plugin_stability.py`. The supplied runners use the local installed Surge/FreeEQ8 paths. Scratch output stays under `build/vst381`.

## Limits

The soak detects non-finite output, engine/plugin errors, and unexpected silence or transport stalls lasting three seconds. It does not measure individual buffer underruns or guarantee click-free playback. Recording validation checks successful finalization and file size, not a sample-by-sample reference. Memory and handle checks are coarse growth checks, not proof of leak freedom. This pass does not cover hours-long use, exhaustive GUI interactions, every plugin, or physical device disconnects. Plugin failures are not isolated from the main application.
