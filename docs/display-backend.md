# Pure-Rust Core2 Display Backend

## Scope

The production display path is implemented in Rust. M5GFX and M5Unified are
reference implementations only: neither library, any C++ shim, nor a native
wrapper is compiled or linked into the firmware.

The application still uses `embedded-graphics` to render a 320x240 RGB565
framebuffer. The hardware-specific backend in `src/display_interface.rs` owns
the optimized ESP32 SPI2 transfer path. ESP-IDF remains responsible for device
startup, SPI bus allocation, memory allocation, scheduling, BLE, NVS, and the
other platform services.

## Hardware Configuration

- Target: M5Stack Core2 v1.1, original ESP32 revision 3.1
- Panel: ILI9342C, 320x240, RGB565
- SPI peripheral: SPI2
- MOSI: GPIO23
- MISO: GPIO19
- SCLK: GPIO18
- DC: GPIO15
- CS: GPIO5
- CPU: 240 MHz
- Display SPI: 40 MHz (`0x1001` ESP32 clock divider)
- PSRAM: 80 MHz, matching the M5/Arduino board configuration

The 80 MHz setting applies to PSRAM, not the panel. The panel remains at the
40 MHz write clock selected by M5GFX for the Core2.

## Frame Ownership

The renderer uses three PSRAM-backed RGB565 framebuffers:

1. one owned by the application renderer;
2. one being uploaded by the display worker;
3. one available or queued between them.

Bounded Rust channels transfer ownership of whole `Box<[Rgb565; 76800]>`
values. A buffer is returned to the renderer only after its DMA upload has
completed, so the renderer cannot mutate memory that the display worker is
reading. Frames remain ordered, with at most two queued for the worker.

Each buffer retains its static dashboard background after upload. The renderer
erases and redraws the graph, status, value, bar, and debug regions before the
next full-frame submission; panel frames and labels are initialized once per
buffer. This reduces PSRAM contention without changing the display contract:
every update still transfers all 153,600 bytes to the panel. A host regression
compares successive retained redraws pixel-for-pixel with clean full redraws.

The display worker pauses for one scheduler tick after every four completed
frames. This gives the ESP32 idle task regular CPU time without adding a
scheduler delay after every upload.

Pressure acquisition runs in a separate 20 ms Rust task and feeds a bounded
sample queue. Rendering can therefore slow or block without reducing sensor
cadence. The BLE scale worker discovers an advertising scale before connecting;
it does not issue long direct-connect attempts to an unavailable persisted
address.

## Transaction Sequence

`FastSpiInterface::send_frame_queued` performs one display transaction:

1. Acquire the ESP-IDF SPI device bus.
2. Drive panel CS low.
3. Configure SPI2 for MOSI-only mode, software-controlled CS, and 40 MHz.
4. Send `CASET` (`0x2A`) for columns 0 through 319.
5. Send `PASET` (`0x2B`) for rows 0 through 239.
6. Send `RAMWR` (`0x2C`).
7. Wait for `RAMWR` to finish before changing DC from command to data mode.
8. Upload the complete 153,600-byte pixel stream through SPI DMA.
9. Wait for the final DMA transfer, clear the DMA out-link, and mark the ESP32
   DMA workaround idle.
10. Release the ESP-IDF bus and drive panel CS high.

Commands and pixel data share the same acquired bus window and CS assertion.
The explicit wait between `RAMWR` and the DC transition is required; omitting
it caused displaced quadrants and checkerboard remnants during development.

## DMA Staging

The framebuffer resides in PSRAM and its RGB565 words use the CPU's byte order.
SPI DMA therefore uses two reusable 8 KiB internal buffers allocated with
`MALLOC_CAP_DMA | MALLOC_CAP_8BIT`.

For each chunk, Rust:

1. reads aligned 32-bit words from the PSRAM framebuffer;
2. swaps the two bytes in each RGB565 pixel;
3. writes the converted data into the next internal staging buffer;
4. builds an ESP32 linked DMA descriptor chain;
5. waits before reusing a buffer that may still be in flight;
6. starts the next SPI DMA transfer.

ESP32 DMA descriptors carry at most 4,092 bytes. The final descriptor records
the true byte length, a four-byte-aligned allocation size, EOF, owner, and a
null next pointer. Non-final descriptors link to the following descriptor.

Before every chunk, the backend follows M5GFX's classic ESP32 sequence: reset
the DMA state, enable descriptor burst reads, enable data burst reads only for
four-byte-aligned transfers, install the out-link, set MOSI bit length, notify
the ESP-IDF DMA workaround, and set `SPI_USR`.

## Safety Boundary

Unsafe code is confined to `src/display_interface.rs` and covers:

- DMA-capable allocation and release;
- volatile SPI2 and DPORT register access;
- ESP32 DMA descriptor pointers;
- the temporary byte view over the RGB565 framebuffer;
- ESP-IDF SPI/DMA C API calls;
- moving the bus-owning display object to its dedicated worker thread.

The backend asserts DMA channel validity, buffer alignment, descriptor count,
transfer bounds, and even byte lengths. Every SPI busy wait has a 100 ms
timeout. Staging buffers and descriptors outlive all transfers that reference
them, and the display worker has exclusive mutable access to the interface.

Calling ESP-IDF C APIs is platform integration, not a C++ wrapper. Application
state, rendering, UI, BLE, power, touch, and sensors remain Rust-owned.

## Source Attribution

The transaction and DMA mechanics were ported from M5GFX 0.2.0
`Bus_SPI.cpp`, whose ESP32 implementation originates in LovyanGFX:

- M5GFX: MIT, copyright M5Stack
- LovyanGFX: FreeBSD license

The exact references are recorded at the top of `src/display_interface.rs`.

## Verified Performance

Accepted Core2 result at 40 MHz display SPI and 80 MHz PSRAM:

| Measurement | Median | Rate |
| --- | ---: | ---: |
| Isolated full-frame upload | 32.325 ms | 30.93 FPS |
| Dynamic render | 15.438 ms | 64.77 FPS |
| Pipelined render/submit | 34.010 ms | 29.40 FPS |
| Concurrent display-worker upload | 35.991 ms | 27.78 FPS |
| Completed LCD frame cadence | 36.072 ms | 27.72 FPS |

The same build completed 1,000 consecutive full-frame uploads with zero
reported transfer errors. The user confirmed that the graph and screen are
stable; only the intentional corner frame indicator blinks.

Raw measurements live in
`benchmarks/results/2026-07-11-raw-8k-psram80-40mhz.jsonl` and are checked by
`scripts/check-display-benchmark.sh`.

### Retained dashboard and independent sampling

The 2026-07-12 whole-application optimization round produced:

| Measurement | Median | Rate |
| --- | ---: | ---: |
| Retained dynamic render | 20.283 ms | 49.30 FPS |
| Pipelined render/submit | 25.401 ms | 39.36 FPS |
| Concurrent display-worker upload | 31.596 ms | 31.64 FPS |
| Completed LCD frame cadence | 32.013 ms | 31.23 FPS |

A 120-second demo soak completed 3,395 frames (28.29 FPS wall-clock) while
sampling pressure at 50.00 Hz. Its maximum pressure gap was 23 ms; display
errors, watchdogs, and post-warmup heap loss were zero. The frame p50/p95/p99
were 23/33/52 ms. This is a passing short-soak result, not a substitute for the
required 30-minute connected-hardware soak. Raw JSON is stored in
`benchmarks/results/2026-07-12-retained-ui-short-soak-40mhz.jsonl`.

## Diagnostic Builds

- `--features benchmark` prints full-frame, render, pipeline, worker-upload,
  and completed-cadence statistics.
- `--features benchmark-transfer-soak` adds 1,000 alternating full-frame
  transfers to the isolated benchmark matrix.
- `--features demo,benchmark-soak` runs the pipelined application workload and
  emits 10-second heap, PSRAM, frame, pressure, BLE, and display-error telemetry
  for a 30-minute soak.
- `--features demo` replaces only the physical pressure source with the C++
  firmware's simulated shot profile. It still exercises the production Rust
  session, graph, BLE notification, framebuffer queue, and DMA backend.
