# Firmware Rollback

## Known-Good Revisions

- Pure Rust DMA/dashboard branch: `rust-revival`
- Pure Rust display checkpoint: `53df8a8`
- C++ parity oracle: `951862f` on `screenshot-regression-tests`
- C++ repository/worktree: `/home/vs/Documents/mxcoffee`
- Rust repository/worktree: `/home/vs/Documents/mxcoffee-rust`
- Default Core2 serial port: `/dev/ttyACM0`

The C++ oracle remains the recovery image until the Rust branch completes the
physical pressure, scale, sleep/wake, and 30-minute soak gates.

## Flash Current Rust Firmware

```bash
cd /home/vs/Documents/mxcoffee-rust
cargo +esp build --release
espflash flash --port /dev/ttyACM0 target/xtensa-esp32-espidf/release/mxcoffee
```

For the hardware-independent simulated shot and scale graph:

```bash
cargo +esp build --release --features demo
espflash flash --port /dev/ttyACM0 target/xtensa-esp32-espidf/release/mxcoffee
```

After flashing, reset under a serial monitor and confirm a clean boot, 80 MHz
PSRAM, no watchdog or DMA error, and sustained frame output above 25 FPS:

```bash
espflash monitor --port /dev/ttyACM0
```

## Roll Back to C++

The C++ worktree is pinned to the UI parity oracle. Confirm its state before
building; do not discard unrelated local changes.

```bash
cd /home/vs/Documents/mxcoffee
git branch --show-current
git status --short
git rev-parse --short HEAD
pio run -e m5stack-core2 -t upload
pio device monitor -b 115200
```

Expected branch/commit are `screenshot-regression-tests` and `951862f`. If the
worktree is not at that revision, use a separate clean worktree rather than
resetting the existing checkout:

```bash
git worktree add --detach /tmp/mxcoffee-cpp-rollback 951862f
cd /tmp/mxcoffee-cpp-rollback
pio run -e m5stack-core2 -t upload
```

## Recovery Decision

Roll back to the C++ oracle if the Rust image shows display corruption, a DMA
timeout, watchdog reset, BLE/scale instability, pressure sampling gaps above
100 ms under ordinary load, or sustained UI cadence below 20 FPS during BLE
dual-role activity.

Do not raise the LCD clock to recover performance. Both implementations use a
40 MHz panel write clock; the Rust build uses 80 MHz only for PSRAM.
