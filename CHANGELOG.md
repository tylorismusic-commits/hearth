# Changelog

## 0.3.1

- Preserve per-plugin skin overrides in `~/.config/hearth/skins.conf` when
  changing the suite default from the header.

## 0.3.0

- Added the suite-wide skin system. The free Classic theme remains the default,
  and the header swatch cycles through any installed skins.
- Skin selection is user-level, shared across the suite, and persists in
  `~/.config/hearth/skins.conf`. It never changes audio, presets, or project
  state.
- Added the loader for Catppuccin Mocha, Studio 4000, Tokyo Night, and
  user-created skins.

## 0.2.3

- The IN/BYPASS control is now a normal plugin parameter, not the CLAP host
  bypass. Toggling it no longer changes REAPER's own FX bypass state.

## 0.2.2

- CLAP builds now forward the meter ports to the native UI, so the VU and
  gain-reduction meters update in CLAP exactly as they do in LV2.

## 0.2.1

- Added the Harrison/Ardour inline-display extension with a live
  gain-reduction bar in the mixer.
- Added a CLAP build that uses the same DSP and the same X11 UI, and reports
  gain reduction through `clap.gain-adjustment-metering` so REAPER can display
  it on the track meter.
- Added a CLAP host test covering gain-reduction reporting and UI embedding.

## 0.2.0

- New vertical console-strip UI with a metal chassis, recessed panels,
  brushed-metal texture, glass VU meter, glossy buttons, and deeper knob
  shadows.
- Added an in-UI preset bar with five musical presets and one *Broken Console*
  preset. Presets write every control through the LV2 UI, so they work even in
  hosts that do not apply `.ttl` presets.
- Added matching LV2 preset resources for hosts that support them.
- Changed the wordmark to Jost ExtraBold (SIL OFL 1.1), a geometric sans close
  to the modern Neve wordmark.
- Added original Neve-style knob artwork.
- Halved Drive and Character, reduced EQ gain ranges to ±8/±9 dB, and widened
  the default mid Q to 0.4.
- Added a hidden +0.2 dBFS ADAA soft clipper at the output.
- Fixed the compressor Mix default (the RDF range was missing, causing hosts to
  clamp it to 0).
- Fixed REAPER LV2 scanning (inline port lists) and X11 parent-window handling.
- Made the UI resizable and HiDPI aware.

## 0.1.0

- Initial DSP and UI.
