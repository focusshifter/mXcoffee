use esp_idf_hal::delay::FreeRtos;
use m5unified::{colors, Canvas, M5Unified};
use mxcoffee::benchmark::BenchmarkStats;

const WIDTH: usize = 320;
const HEIGHT: usize = 240;
const FRAME_BYTES: usize = WIDTH * HEIGHT * 2;
const ITERATIONS: usize = 105;
const WARMUP: usize = 5;

fn main() {
    esp_idf_sys::link_patches();

    let mut m5 = M5Unified::begin().expect("M5Unified initialization failed");
    let mut canvas =
        Canvas::create(WIDTH as i32, HEIGHT as i32).expect("M5Unified canvas allocation failed");

    println!(
        "{{\"type\":\"benchmark_config\",\"backend\":\"m5unified_canvas\",\"width\":{},\"height\":{}}}",
        WIDTH, HEIGHT
    );

    let mut stats = BenchmarkStats::new("m5_canvas_push", FRAME_BYTES);
    for iteration in 0..ITERATIONS {
        canvas.fill_screen(if iteration % 2 == 0 {
            colors::RED
        } else {
            colors::BLUE
        });

        let started = unsafe { esp_idf_sys::esp_timer_get_time() as u64 };
        canvas.push(0, 0);
        let ended = unsafe { esp_idf_sys::esp_timer_get_time() as u64 };
        stats.record(ended.saturating_sub(started));
        m5.update();
        FreeRtos::delay_ms(1);
    }
    stats.report(WARMUP);

    loop {
        m5.update();
        FreeRtos::delay_ms(100);
    }
}
