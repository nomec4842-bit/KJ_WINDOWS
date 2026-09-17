# Live Audio In and Mixer

Use **Type: Audio In** on a track, then choose the input device and mono channel or stereo pair in its FX editor or Mixer strip. **Refresh Devices** is in that input menu. Turn **Monitor** on to hear the input. Device identity and routing are saved; Monitor always starts off after project loading or changing track type.

**View > Mixer** opens the resizable, horizontally scrollable mixer. Its filled-bar volume/pan controls share track state with the editor. Mute overrides Solo; multiple solos are supported. Audio muting happens after effects. MIDI Out strips have no audio fader/meter; muting or excluding them with Solo releases held notes, and unmuting can resume notes still held in the pattern. The master clip indication latches until clicked.

**Preferences > Monitor audio inputs while stopped** defaults on. Monitoring still requires the per-track Monitor switch. With the preference enabled, live input effects continue processing without advancing the sequencer. Existing File > Record captures the final stereo master, including monitored input.

## Implementation

- WASAPI shared-mode capture uses the selected endpoint identity, with one worker/stream per monitored device. Windows driver inputs, including USB microphones/interfaces, are supported; ASIO is not implemented.
- Capture workers publish to bounded atomic sample buffers. Playback performs 64-tap windowed-sinc resampling with a smoothed occupancy-based correction for independent input/output clocks.
- Tracks sharing a device use one playback clock and sample position, with independent channel selection. Capture lifecycle and buffer retirement stay outside the sample-processing callback.
- Missing/disconnected inputs yield silence and retain their selected endpoint. Capture retries that endpoint rather than falling back to another microphone.
- Audio In bypasses note generation and does not open a piano roll. Native/VST3 effects and the existing volume, pan, and master recorder are shared with other audio tracks.

## Validation (2026-09-17)

- VST3 Release build: 20/20 CTest tests passed.
- Native-only Release build: 14/14 CTest tests passed.
- New deterministic tests cover mono/stereo routing, invalid channels, 44.1/48/96 kHz conversion, +/-800 ppm clock differences, underrun/overrun, disconnect/reconnect state, independently selected channels on a shared stream, aligned late-joining readers, project recall/legacy defaults, Mixer selection, faders, mute and horizontal scrolling. A 30 kHz tone downsampled from 96 to 48 kHz is suppressed below 0.001 RMS (input RMS 0.07071).
- Live render tests feed a timed synthetic capture source through the actual Windows playback engine. They verify stopped monitoring and Play/Stop, native EQ/bypass, mixed VST3/native effects and VST3 bypass, volume/pan, stereo meters, shared input, multiple solos/mute precedence, master recording/clipping and simulated disconnect/reconnect. A test MIDI sink verifies actual note-on/off dispatch and cleanup/resume across mute/solo changes.
- The Mixer was rendered and visually checked, with a preview under build/vst381/mixer_preview.png.
- The available hardware microphone, **Microphone (High Definition Audio Device)**, opened successfully in shared mode at **192000 Hz, 2 channels** and delivered capture packets. Hardware access required running the smoke test outside the Codex sandbox.
- Windows exposed USB speakers, but no USB capture endpoint. USB capture and physical unplug/replug remain hardware checks; disconnect/reconnect logic was exercised with a simulated source.

Reproduce the suites:

```powershell
cmake --build build/vst381 --config Release --parallel 4
ctest --test-dir build/vst381 -C Release --output-on-failure
cmake --build build/no-vst-rack --config Release --parallel 4
ctest --test-dir build/no-vst-rack -C Release --output-on-failure
```

The manual `kj_input_live_tests` executable needs Windows output/input access. Run from a build directory (it writes a synthetic input_integration_test.wav). The VST3 version takes the absolute path to kj_vst3_fixture.vst3; the native-only version takes no argument. It mutes only its own Windows output session, so test tones and monitored microphone data are not played through the speakers. Hardware capture is not written to a recording.
