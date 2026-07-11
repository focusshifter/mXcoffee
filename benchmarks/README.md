# Display hardware benchmarks

Build and flash the instrumented firmware:

```sh
source ~/export-esp.sh
cargo +esp build --release --features benchmark --bin mxcoffee
espflash flash --port /dev/ttyACM0 target/xtensa-esp32-espidf/release/mxcoffee
espflash monitor --port /dev/ttyACM0 --baud 115200 --non-interactive
```

The target is an M5Stack Core2 v1.1 with ESP32 revision 3.1 and a 240 MHz CPU.
The accepted production configuration matches M5's memory setup with 80 MHz
PSRAM while retaining the panel's 40 MHz display SPI clock. Each result uses
five warmup frames and 100 measured frames. The accepted full-frame transfer
limits are 35 ms median, 38 ms p95, 42 ms p99, at least 28 FPS, and zero errors.
Sustained completed-frame cadence must also reach 25 FPS.

Validate a captured JSONL file with:

```sh
scripts/check-display-benchmark.sh benchmarks/results/2026-07-11-raw-8k-psram80-40mhz.jsonl
```

Visual inspection of alternating solid frames and the RGB/checkerboard pattern
is still required; serial timing cannot detect color or pixel corruption.
Use `--features benchmark-soak` for the additional 1,000-upload run.

`2026-07-11-cpp-current-ui-40mhz.jsonl` records the C++ parity oracle's
existing `UI::draw` profiler around its 24-bit full-canvas redraw and
`pushSprite`. It is an end-to-end UI reference, not an isolated upload result,
and is therefore not input to `check-display-benchmark.sh`.
