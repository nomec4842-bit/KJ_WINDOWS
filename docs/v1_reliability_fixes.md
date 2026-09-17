# V1 reliability fixes — 2026-09-17

This pass addresses confirmed data ownership and output-selection defects; it is not a claim that every V1 reliability issue is resolved.

- **Waveform display:** alternating two ordinary buffers did not protect a slow display reader from the writer cycling back to its buffer. A fixed pool of three buffers now pins active readers. Audio publishing never waits or allocates; if all buffers are in use, it skips a display update.
- **Output device metadata:** callers previously retained references into two mutable buffers containing device-name/identity strings. Metadata is now copied under protection. The playback loop only reads the requested identity at startup or when a device change is requested.
- **Rapid output selection:** A → B → A could discard the final selection because it was compared against the last completed device rather than the pending request. Requests now compare against the latest selection. Finishing an older device open also no longer overwrites a newer pending selection.
- **Instrument controls and MIDI routing:** synth, sampler and MIDI setters wrote unsynchronized duplicate fields while getTracks copied the base track. Their existing atomic values now remain authoritative. MIDI port identity and name are published/read together under their existing mutex.

The new `reliability` regression covers a reader held across repeated waveform publishes, fully pinned-buffer behavior, concurrent waveform readers, rapid output requests, long device identity strings, concurrent instrument edits, and consistent MIDI port/name snapshots. Existing audio, project, sampler and VST3 suites provide integration coverage.
