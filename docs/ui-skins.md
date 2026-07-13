# UI Skins

## Rendering Contract

The UI renders a fixed 320x240 RGB565 framebuffer. Skin colors are authored as
RGB888 values, but every `SkinColor` also contains the deterministic truncated
RGB565 value used by firmware. The display backend, framebuffer size, and SPI
transfer format do not change when a skin changes.

`src/ui.rs` owns screen composition and the compatibility entry points used by
firmware. The implementation is split by responsibility:

- `src/ui/theme.rs`: semantic RGB888 colors and RGB565 conversion;
- `src/ui/layout.rs`: typed panel, graph, bar, status, debug, and damage regions;
- `src/ui/skin.rs`: skin identity, fonts, and typed bitmap assets;
- `src/ui/dashboard.rs`: dashboard panels, values, status, messages, and assets;
- `src/ui/graph.rs`: curves, antialiasing, line rasterization, and pressure bar;
- `src/ui/splash.rs`: splash composition.

`CLASSIC` remains the firmware default. The older functions without a skin
argument delegate to it, so firmware behavior cannot change merely because a
new skin is added.

Build with `--features alchemy-skin` to select Alchemy as the firmware default
or `--features nge-skin` to select the NGE-inspired telemetry skin. The skin
features are mutually exclusive.
without changing the normal production configuration.

## Theme Colors

Define semantic colors once with `SkinColor::new(red, green, blue)`. Do not add
parallel RGB565 and RGB888 constants. VLW font blending uses the RGB888 source;
solid framebuffer drawing uses the paired RGB565 value.

The simulator's Core2 preview is an optical post-process and is not part of a
skin. Press `P` to inspect authoritative raw RGB565 output.

## Layout

Each skin owns a `DashboardLayout`. Coordinates are fixed pixels because the
Core2 is a fixed 320x240 target. Panel frames, inner clear regions, text
anchors, graph geometry, pressure bar, status band, debug origin, and frame
indicator must be defined through the layout rather than in component code.

The graph's retained clear rectangle belongs to `GraphLayout`. Moving the plot
therefore requires updating the same typed layout definition that controls its
damage region. `DashboardLayout::is_valid` rejects regions outside the screen.

## Bitmap Assets

`BitmapAsset` is a typed row-major RGB565 pixel slice with an optional RGB565
transparent key. `PositionedBitmapAsset` places it in the static dashboard.
Assets are drawn only when a framebuffer's static screen is initialized, not
on every dynamic frame. The steady-state render loop performs no image parsing
or allocation.

Current proof assets are Rust constants. A production illustrated skin should
use a build-time converter that emits the same typed representation from
source PNG files. Runtime PNG/JPEG decoding is intentionally outside the
firmware contract.

Memory reference:

| Asset | RGB565 bytes |
| --- | ---: |
| Full 320x240 background | 153,600 |
| 32x32 icon | 2,048 |
| 16x16 icon | 512 |

Assets live in flash. A full static background does not require another
permanent PSRAM framebuffer, but it does increase firmware or filesystem size.

Alchemy uses a real authored PNG underlay at
`assets/skins/alchemy-underlay.png`. `tools/png_to_rgb565.py` validates its
320x240 dimensions and generates the committed 153,600-byte
`alchemy-underlay.rgb565` payload. Firmware copies that payload into each
retained framebuffer only during static initialization. Live values, status,
pressure fill, graph grid, and curves are rendered afterward and remain fully
dynamic.

Regenerate the payload after changing the PNG:

```sh
tools/png_to_rgb565.py \
  assets/skins/alchemy-underlay.png \
  assets/skins/alchemy-underlay.rgb565 \
  --width 320 --height 240 \
  --core2-compensate
```

The compensation is the inverse of the simulator's approximate Core2 preview:
it grades the stored RGB565 asset, not the ILI9342 gamma registers. The authored
PNG therefore remains normal sRGB while preview mode predicts its appearance on
the physical LCD. Raw RGB565 mode intentionally shows the darker compensated
payload.

## Native Artwork Rules

- Author the final composition at 320x240; do not downscale a desktop mockup.
- Test every label and numeric extreme on the physical Core2.
- Reserve uncluttered, opaque regions behind changing values and graphs.
- Keep decoration outside dynamic clear rectangles unless it is intentionally
  redrawn as part of that component.
- Prefer pixel-authored borders and icons over subpixel detail that disappears
  in RGB565.
- Check raw RGB565 and approximate Core2-preview modes in the simulator.
- Treat the coffee/alchemy reference as visual direction, not a source bitmap.

## Development Workflow

Run the simulator:

```sh
cargo +stable run --example ui_simulator --target x86_64-unknown-linux-gnu
```

Press `S` to cycle through Classic, Workshop, and Alchemy. A switch clears and
reinitializes the complete retained framebuffer. Press `P` to toggle the Core2
optical preview.

Validate skin definitions and deterministic references:

```sh
scripts/validate-ui-skins.sh
```

Measure retained host render time and FPS for every skin:

```sh
cargo +stable run --release --example ui_render_benchmark \
  --target x86_64-unknown-linux-gnu
```

The check validates layout and asset bounds, runs the RGB565 checksum
regressions, renders both skins, and compares Classic with the tracked C++-exact
reference. Before accepting a production skin, also flash it to the Core2 and
run the display benchmark because host rendering cannot validate PSRAM or LCD
cadence.

The initial host results are stored in
`benchmarks/results/2026-07-12-skin-ready-ui-host.jsonl`. They are useful for
tracking renderer regressions, but physical Core2 results remain the acceptance
gate.

The physical Core2 gate used a controlled 60-second A/B soak. Pre-skin commit
`1e82c96` completed 1,366 frames (22.76 FPS); skin-ready commit `74bef0a`
completed 1,378 frames (22.96 FPS). Both maintained 50.0 Hz pressure sampling
with zero display errors or PSRAM loss. The raw comparison is stored in
`benchmarks/results/2026-07-12-skin-ready-ui-device-ab.jsonl`.

Both A/B runs were slower than the older 28.44 FPS long-soak record under the
current runtime environment. Because the old and new commits reproduce the
same lower cadence on the same device while isolated render/upload results are
unchanged, that difference is not attributed to the skin architecture.

## Adding a Skin

1. Add a semantic `Theme` in `theme.rs`.
2. Add a bounded `DashboardLayout` in `layout.rs`.
3. Convert fonts and bitmaps into typed static assets.
4. Define the `Skin` in `skin.rs` and include it in the validity test.
5. Add a deterministic RGB565 checksum and inspectable reference image.
6. Add simulator selection without changing the firmware default.
7. Verify retained redraw after switching from every existing skin.
8. Record host render time and physical-device frame time/FPS.

The Alchemy skin implements the coffee/alchemy direction with an authored
native underlay and live overlays. Its host/device references are
`benchmarks/screenshots/2026-07-13-alchemy-skin.png` and
`benchmarks/screenshots/2026-07-13-alchemy-png-device.png`.

On the Core2, its dynamic render measured 15.213 ms (65.73 FPS), pipelined
submit measured 20.975 ms (47.67 FPS), and completed LCD cadence measured
32.532 ms (30.73 FPS). Raw measurements are in
`benchmarks/results/2026-07-13-alchemy-png-40mhz.jsonl`.

The NGE skin uses the same authored-underlay pipeline for a bold asymmetric
coffee telemetry display inspired by 1990s anime command interfaces. Static
color fields, labels, calibration ticks, and framing live in
`assets/skins/nge-underlay.png`;
pressure, weight, flow, time, both graph curves, pressure fill, and connection
states remain live firmware overlays. Its compensated device payload is
`assets/skins/nge-underlay.rgb565`, generated with:

```sh
tools/png_to_rgb565.py --width 320 --height 240 --core2-compensate \
  assets/skins/nge-underlay.png assets/skins/nge-underlay.rgb565
```

The deterministic host reference is
`benchmarks/screenshots/2026-07-13-nge-skin-raw.png`. In the simulator, press
`S` to cycle Classic, Workshop, Alchemy, and NGE.

The initial card-grid NGE round measured 13.039 ms dynamic rendering (76.69
FPS), 17.972 ms pipelined submit (55.64 FPS), and 31.817 ms LCD cadence (31.42
FPS). It was rejected visually and its measurements remain in
`benchmarks/results/2026-07-13-nge-40mhz.jsonl` for comparison.

The replacement asymmetric round measured 12.380 ms dynamic rendering (80.77
FPS), 18.009 ms pipelined submit (55.52 FPS), and 31.743 ms LCD cadence (31.50
FPS), with zero display or pattern-correctness errors. Its measurements are in
`benchmarks/results/2026-07-13-nge-v2-40mhz.jsonl`.
