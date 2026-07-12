use std::time::Instant;

use embedded_graphics::pixelcolor::{IntoStorage, Rgb565};
use embedded_graphics::prelude::RgbColor;
use minifb::{Key, KeyRepeat, Scale, Window, WindowOptions};
use mxcoffee::demo::{simulated_pressure_mbar, SimulatedScale};
use mxcoffee::fast_framebuffer::FastFrameBuffer;
use mxcoffee::session::SessionState;
use mxcoffee::ui::{
    draw_main_screen_retained, draw_splash, initialize_main_screen, UiData, HEIGHT, HISTORY_LEN,
    WIDTH,
};

const HISTORY_INTERVAL_MS: u64 = 30_000 / HISTORY_LEN as u64;
const SHOT_CYCLE_MS: u64 = 40_000;

fn main() {
    let mut window = Window::new(
        "mXcoffee Rust UI Simulator - approximate Core2 preview",
        WIDTH as usize,
        HEIGHT as usize,
        WindowOptions {
            resize: true,
            scale: Scale::X2,
            ..WindowOptions::default()
        },
    )
    .expect("failed to create simulator window");
    window.set_target_fps(50);

    println!(
        "D debug | B Bluetooth | A antialias | P panel preview | Space pause | R restart | Esc quit"
    );

    let mut pixels = vec![Rgb565::BLACK; WIDTH as usize * HEIGHT as usize];
    let mut window_pixels = vec![0u32; pixels.len()];
    let mut pressure_history = [0i16; HISTORY_LEN];
    let mut weight_history = [0i16; HISTORY_LEN];
    let history_times = core::array::from_fn::<_, HISTORY_LEN, _>(|index| {
        (30_000usize * index).div_ceil(HISTORY_LEN) as u32
    });
    let mut session = SessionState::default();
    let mut scale = SimulatedScale::default();
    let mut cycle_started = Instant::now();
    let mut last_history_ms = 0u64;
    let mut paused_at: Option<Instant> = None;
    let mut debug = false;
    let mut bluetooth = true;
    let mut antialias = true;
    let mut panel_preview = true;
    let mut frame_indicator = false;
    let mut static_screen_initialized = false;

    while window.is_open() && !window.is_key_down(Key::Escape) {
        if window.is_key_pressed(Key::D, KeyRepeat::No) {
            debug = !debug;
        }
        if window.is_key_pressed(Key::B, KeyRepeat::No) {
            bluetooth = !bluetooth;
        }
        if window.is_key_pressed(Key::A, KeyRepeat::No) {
            antialias = !antialias;
        }
        if window.is_key_pressed(Key::P, KeyRepeat::No) {
            panel_preview = !panel_preview;
            window.set_title(if panel_preview {
                "mXcoffee Rust UI Simulator - approximate Core2 preview"
            } else {
                "mXcoffee Rust UI Simulator - raw RGB565"
            });
        }
        if window.is_key_pressed(Key::Space, KeyRepeat::No) {
            if let Some(instant) = paused_at.take() {
                cycle_started += instant.elapsed();
            } else {
                paused_at = Some(Instant::now());
            }
        }
        if window.is_key_pressed(Key::R, KeyRepeat::No) {
            reset_cycle(
                &mut cycle_started,
                &mut session,
                &mut scale,
                &mut pressure_history,
                &mut weight_history,
                &mut last_history_ms,
            );
            paused_at = None;
        }

        let elapsed = paused_at
            .map_or_else(
                || cycle_started.elapsed(),
                |instant| instant - cycle_started,
            )
            .as_millis() as u64;
        if elapsed >= SHOT_CYCLE_MS {
            reset_cycle(
                &mut cycle_started,
                &mut session,
                &mut scale,
                &mut pressure_history,
                &mut weight_history,
                &mut last_history_ms,
            );
        }
        let elapsed = paused_at
            .map_or_else(
                || cycle_started.elapsed(),
                |instant| instant - cycle_started,
            )
            .as_millis() as u64;

        if elapsed < 1_000 {
            draw_splash(&mut FastFrameBuffer::new(
                &mut pixels,
                WIDTH as usize,
                HEIGHT as usize,
            ))
            .unwrap();
            static_screen_initialized = false;
        } else {
            let shot_ms = elapsed - 1_000;
            let pressure = simulated_pressure_mbar(shot_ms);
            let weight = scale.update(shot_ms);
            session.update_pressure(pressure, shot_ms);
            session.update_scale(true, weight, shot_ms);

            if shot_ms.saturating_sub(last_history_ms) >= HISTORY_INTERVAL_MS {
                last_history_ms = shot_ms;
                pressure_history.rotate_left(1);
                pressure_history[HISTORY_LEN - 1] = pressure;
                weight_history.rotate_left(1);
                weight_history[HISTORY_LEN - 1] = (weight * 10.0) as i16;
            }

            let mut target = FastFrameBuffer::new(&mut pixels, WIDTH as usize, HEIGHT as usize);
            if !static_screen_initialized {
                initialize_main_screen(&mut target).unwrap();
                static_screen_initialized = true;
            }
            draw_main_screen_retained(
                &mut target,
                UiData {
                    pressure_history: &pressure_history,
                    weight_history_tenths: &weight_history,
                    history_times_ms: &history_times,
                    last_pressure: pressure,
                    max_sensor_pressure: 20_000,
                    shot_weight: session.shot_weight,
                    flow_rate: session.flow_rate,
                    bluetooth_on: bluetooth,
                    scale_connected: true,
                    scale_name: "SimScale",
                    shot_time_tenths: session.current_shot_duration_ms(shot_ms) / 100,
                    pressure_hex: "SIM",
                    debug_mode: debug,
                    last_refresh_ms: shot_ms,
                    last_activity_ms: 0,
                    now_ms: shot_ms,
                    auto_off_timeout_ms: u64::MAX,
                    timer_running: session.timer_running,
                    frame_indicator: Some(frame_indicator),
                    antialias_graph: antialias,
                },
            )
            .unwrap();
            frame_indicator = !frame_indicator;
        }

        convert_frame(&pixels, &mut window_pixels, panel_preview);
        window
            .update_with_buffer(&window_pixels, WIDTH as usize, HEIGHT as usize)
            .expect("failed to update simulator window");
    }
}

fn reset_cycle(
    cycle_started: &mut Instant,
    session: &mut SessionState,
    scale: &mut SimulatedScale,
    pressure: &mut [i16; HISTORY_LEN],
    weight: &mut [i16; HISTORY_LEN],
    last_history_ms: &mut u64,
) {
    *cycle_started = Instant::now();
    *session = SessionState::default();
    *scale = SimulatedScale::default();
    pressure.fill(0);
    weight.fill(0);
    *last_history_ms = 0;
}

fn convert_frame(source: &[Rgb565], output: &mut [u32], panel_preview: bool) {
    for (source, output) in source.iter().zip(output) {
        let raw = source.into_storage();
        let red = expand_5_bit((raw >> 11) & 0x1f);
        let green = expand_6_bit((raw >> 5) & 0x3f);
        let blue = expand_5_bit(raw & 0x1f);
        let (red, green, blue) = if panel_preview {
            (
                panel_channel(red, 0.0, 0.95),
                panel_channel(green, 14.0, 0.82),
                panel_channel(blue, 22.0, 0.70),
            )
        } else {
            (red, green, blue)
        };
        *output = (u32::from(red) << 16) | (u32::from(green) << 8) | u32::from(blue);
    }
}

fn expand_5_bit(value: u16) -> u8 {
    ((value << 3) | (value >> 2)) as u8
}

fn expand_6_bit(value: u16) -> u8 {
    ((value << 2) | (value >> 4)) as u8
}

fn panel_channel(value: u8, black_floor: f32, gamma: f32) -> u8 {
    let normalized = f32::from(value) / 255.0;
    (black_floor + (255.0 - black_floor) * normalized.powf(gamma))
        .round()
        .clamp(0.0, 255.0) as u8
}
