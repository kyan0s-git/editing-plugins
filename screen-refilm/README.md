# Screen Refilm

An OpenFX video effect that makes footage look like it was filmed off a real
screen. It puts the clip on a physical display (LCD monitor, CRT TV or OLED
phone) and films it with a virtual camera: tilted and close up, with the RGB
sub-pixels visible, a band of focus with soft falloff, lens bulge, colour
fringing, glow, flicker and moiré. The camera moves on its own (push-in, pan,
handheld drift, focus pull), so it works without keyframes.

The effect works in any OpenFX host: **DaVinci Resolve** (Free and Studio),
Fusion, Nuke, Natron, Vegas Pro and others. A command-line renderer with the
same engine is included for previews and batch work.

## Controls

| Group | Control | What it does |
|---|---|---|
| — | **Preset** | Starts a look and resets the other controls. Options: Desk Monitor, Living-Room TV, Phone Close-Up, Dreamy Macro, Glitchy Broadcast, Moire Mess |
| — | **Screen** | `LCD (laptop, monitor)` RGB stripes · `CRT (old TV)` aperture grille + scanlines · `Phone (OLED)` PenTile-style dots |
| Camera | **Zoom** | How close the camera is. 1 = whole screen in frame |
| | **Look At** | Point on the screen the camera aims at (0–1, y up) |
| | **Tilt Up/Down**, **Tilt Left/Right** | Camera angle relative to the screen, in degrees |
| Movement | **Push In** | Slow dolly in over time (negative pulls out) |
| | **Pan Left/Right**, **Pan Up/Down** | Slow glide across the screen over time |
| | **Drift** | Handheld wobble |
| Focus | **Focus Blur** | Softness away from the sharp band |
| | **Focus Shift** | Moves the sharp band |
| | **Focus Pull** | Racks the sharp band back and forth automatically |
| | **Focus Angle** | Rotates the band (tilt-shift). The band follows the camera tilt by default |
| Light | **Brightness** | Screen brightness / exposure |
| | **Glow** | Light blooming off the screen |
| | **Flicker**, **Flicker Speed** | Brightness wavering, from slow breathing to random glitching |
| | **Flicker Glow Only** | Keeps the picture steady and pulses only the glow |
| Lens | **Vignette**, **Lens Bulge**, **Chromatic Aberration**, **RGB Split** | Optical imperfections |
| Screen | **Recording Shape** | Physical screen shape: same as timeline (no bars), 16:9, 4:3, 9:16 or 1:1 |
| | **Moire**, **Moire Bands**, **Moire Colour**, **Moire Speed** | Moving interference bands from the camera sensor and pixel grid |
| | **Pixel Size** | Source pixels per screen pixel (bigger = chunkier display) |
| | **Grid Strength** | How visible the sub-pixel grid is |

**For the most detail**, render at 4K and keep the clip at 100% scale with
Pixel Size 1. Every source pixel then becomes one real screen pixel with its
own sub-pixels.

## Build

You need CMake ≥ 3.16 and a C++17 compiler. The OpenFX headers are vendored
in `third_party/openfx`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build --config Release -j
```

This produces:

- `build/ScreenRefilm.ofx.bundle/`: the plugin bundle
- `build/screen-refilm`: the CLI

### Install the plugin

Copy `ScreenRefilm.ofx.bundle` into the system OFX folder (or run
`cmake --install build` with admin rights), then restart your editor:

| OS | Folder |
|---|---|
| macOS | `/Library/OFX/Plugins/` |
| Windows | `C:\Program Files\Common Files\OFX\Plugins\` |
| Linux | `/usr/OFX/Plugins/` |

In DaVinci Resolve, find it under **Effects → OpenFX → Editing Plugins →
Screen Refilm**. Drag it onto a clip, or onto an adjustment clip to cover
several shots.

On macOS the default build is universal (arm64 + x86_64). Sign it before
distributing: `codesign --force --deep -s "Developer ID Application: …" ScreenRefilm.ofx.bundle`.

## Command line

The CLI needs `ffmpeg` and `ffprobe` on your `PATH`. It renders videos (audio
is copied) or single stills:

```bash
build/screen-refilm input.mp4 output.mp4 --preset "Living-Room TV"
```

```bash
build/screen-refilm input.mp4 frame.png --start 3.5 --set screen=2 --set zoom=5 --set lookAt=0.3,0.6
```

```bash
build/screen-refilm --list
```

`--list` prints every parameter id, its default and the preset names.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

- `ofx_host`: a minimal OpenFX host loads the built `.ofx`, runs
  describe/describe-in-context, applies a preset through `instanceChanged`
  and renders a frame.
- `cli_smoke`: renders a synthetic clip with every preset. Skipped if ffmpeg
  is missing.

## How it works

`src/refilm_render.cpp` is independent of the host:

1. **Screen pass.** Each output pixel is ray-cast from a pinhole camera that
   orbits the Look At point onto the screen plane. The hit gives a screen
   pixel, whose colour is fetched from the source. That colour is multiplied by
   the display's sub-pixel mask. The mask is pre-filtered analytically: each
   emitter is a box or a Gaussian, convolved in closed form with the output
   pixel's footprint. This keeps the grid from aliasing at any zoom while
   preserving average brightness. Moiré is added here, in screen space, so
   perspective bends it naturally.
2. **Lens pass.** A mip pyramid is built from that plate. A variable-radius
   disc blur is gathered from it for the depth-of-field band, with
   per-channel offsets for RGB split and chromatic aberration. Glow comes
   from the coarse mip levels. Then flicker, vignette, and a highlight
   rolloff that desaturates toward white, like an overexposed sensor.

`src/refilm_params.cpp` holds the single table of controls and presets. Both
the OFX plugin (`src/refilm_ofx.cpp`, plain OpenFX C API) and the CLI are
generated from it.

## Limitations

- Rendering is on the CPU, multithreaded. A 1080p frame takes roughly
  0.4–0.6 s on 4 cores, so expect render or cache playback rather than
  real-time. A GPU path (CUDA/Metal/OpenCL) is the obvious next step.
- Adobe Premiere Pro and After Effects don't load OpenFX plugins. Supporting
  them would need a separate wrapper built on the Adobe SDK, around the same
  renderer.
- The Windows and macOS builds have not been tested on those platforms yet
  (the code is portable C++17). It has been built and tested on Linux only.

## License

Plugin code: see repository. The OpenFX headers in `third_party/openfx` are
BSD-3-Clause (see `third_party/openfx/LICENSE.md`).
