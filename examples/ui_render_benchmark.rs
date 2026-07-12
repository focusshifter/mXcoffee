use std::time::Instant;

use embedded_graphics::pixelcolor::Rgb565;
use embedded_graphics::prelude::RgbColor;
use mxcoffee::fast_framebuffer::FastFrameBuffer;
use mxcoffee::ui::{
    build_reference_histories, draw_main_screen_retained_with_skin,
    initialize_main_screen_with_skin, Skin, UiData, ALCHEMY, CLASSIC, HEIGHT, HISTORY_LEN, WIDTH,
    WORKSHOP,
};

const WARMUP: usize = 20;
const SAMPLES: usize = 200;

fn main() {
    benchmark(&CLASSIC);
    benchmark(&WORKSHOP);
    benchmark(&ALCHEMY);
}

fn benchmark(skin: &Skin) {
    let mut pressure = [0; HISTORY_LEN];
    let mut weight = [0; HISTORY_LEN];
    let mut times = [0; HISTORY_LEN];
    build_reference_histories(&mut pressure, &mut weight, &mut times);
    let mut pixels = vec![Rgb565::BLACK; WIDTH as usize * HEIGHT as usize];
    initialize_main_screen_with_skin(
        &mut FastFrameBuffer::new(&mut pixels, WIDTH as usize, HEIGHT as usize),
        skin,
    )
    .unwrap();

    let mut samples = Vec::with_capacity(SAMPLES);
    for frame in 0..WARMUP + SAMPLES {
        let started = Instant::now();
        draw_main_screen_retained_with_skin(
            &mut FastFrameBuffer::new(&mut pixels, WIDTH as usize, HEIGHT as usize),
            UiData {
                pressure_history: &pressure,
                weight_history_tenths: &weight,
                history_times_ms: &times,
                last_pressure: 8_400,
                max_sensor_pressure: 20_000,
                shot_weight: 21.8,
                flow_rate: 1.0,
                bluetooth_on: true,
                scale_connected: true,
                scale_name: "LFSMART SCALE",
                shot_time_tenths: 123,
                pressure_hex: "benchmark",
                debug_mode: false,
                last_refresh_ms: 0,
                last_activity_ms: 0,
                now_ms: 0,
                auto_off_timeout_ms: 600_000,
                timer_running: true,
                frame_indicator: Some(frame % 2 == 0),
                antialias_graph: true,
            },
            skin,
        )
        .unwrap();
        if frame >= WARMUP {
            samples.push(started.elapsed().as_micros() as u64);
        }
    }

    samples.sort_unstable();
    let median_us = samples[SAMPLES / 2];
    let mean_us = samples.iter().sum::<u64>() / SAMPLES as u64;
    println!(
        "{{\"type\":\"ui_host_benchmark\",\"skin\":\"{}\",\"samples\":{},\"warmup\":{},\"median_us\":{},\"mean_us\":{},\"fps\":{:.2}}}",
        skin.name,
        SAMPLES,
        WARMUP,
        median_us,
        mean_us,
        1_000_000.0 / median_us as f64
    );
}
