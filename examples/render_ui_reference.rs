use std::fs::File;
use std::io::{BufWriter, Write};

use embedded_graphics::pixelcolor::{IntoStorage, Rgb565};
use embedded_graphics::prelude::RgbColor;
use mxcoffee::fast_framebuffer::FastFrameBuffer;
use mxcoffee::ui::{
    build_reference_histories, draw_main_screen_with_skin, draw_splash_with_skin, UiData, ALCHEMY,
    CLASSIC, HEIGHT, HISTORY_LEN, NGE, WIDTH, WORKSHOP,
};

fn main() {
    let output = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "benchmarks/screenshots/rust-reference.bmp".into());
    let arguments: Vec<_> = std::env::args().skip(2).collect();
    let render_splash = arguments.iter().any(|argument| argument == "--splash");
    let render_debug = arguments.iter().any(|argument| argument == "--debug");
    let render_antialias = arguments.iter().any(|argument| argument == "--aa");
    let skin = if arguments.iter().any(|argument| argument == "--nge") {
        &NGE
    } else if arguments.iter().any(|argument| argument == "--alchemy") {
        &ALCHEMY
    } else if arguments.iter().any(|argument| argument == "--workshop") {
        &WORKSHOP
    } else {
        &CLASSIC
    };
    let mut pressure = [0; HISTORY_LEN];
    let mut weight = [0; HISTORY_LEN];
    let mut times = [0; HISTORY_LEN];
    build_reference_histories(&mut pressure, &mut weight, &mut times);

    let mut pixels = vec![Rgb565::BLACK; WIDTH as usize * HEIGHT as usize];
    let mut target = FastFrameBuffer::new(&mut pixels, WIDTH as usize, HEIGHT as usize);
    if render_splash {
        draw_splash_with_skin(&mut target, skin).unwrap();
    } else {
        draw_main_screen_with_skin(
            &mut target,
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
                pressure_hex: "reference",
                debug_mode: render_debug,
                last_refresh_ms: 0,
                last_activity_ms: 0,
                now_ms: 0,
                auto_off_timeout_ms: 600_000,
                timer_running: true,
                frame_indicator: None,
                antialias_graph: render_debug || render_antialias,
            },
            skin,
        )
        .unwrap();
    }

    write_bmp(&output, &pixels).expect("failed to write Rust UI reference BMP");
    println!("wrote {output}");
}

fn write_bmp(path: &str, pixels: &[Rgb565]) -> std::io::Result<()> {
    let row_size = (WIDTH as u32 * 3).div_ceil(4) * 4;
    let image_size = row_size * HEIGHT as u32;
    let file_size = 54 + image_size;
    let mut header = [0u8; 54];
    header[0..2].copy_from_slice(b"BM");
    header[2..6].copy_from_slice(&file_size.to_le_bytes());
    header[10..14].copy_from_slice(&54u32.to_le_bytes());
    header[14..18].copy_from_slice(&40u32.to_le_bytes());
    header[18..22].copy_from_slice(&WIDTH.to_le_bytes());
    header[22..26].copy_from_slice(&HEIGHT.to_le_bytes());
    header[26..28].copy_from_slice(&1u16.to_le_bytes());
    header[28..30].copy_from_slice(&24u16.to_le_bytes());
    header[34..38].copy_from_slice(&image_size.to_le_bytes());

    let mut writer = BufWriter::new(File::create(path)?);
    writer.write_all(&header)?;
    let padding = vec![0; row_size as usize - WIDTH as usize * 3];
    for y in (0..HEIGHT as usize).rev() {
        for pixel in &pixels[y * WIDTH as usize..(y + 1) * WIDTH as usize] {
            let raw = pixel.into_storage();
            let red = ((raw >> 11) & 0x1f) as u8;
            let green = ((raw >> 5) & 0x3f) as u8;
            let blue = (raw & 0x1f) as u8;
            writer.write_all(&[
                (blue << 3) | (blue >> 2),
                (green << 2) | (green >> 4),
                (red << 3) | (red >> 2),
            ])?;
        }
        writer.write_all(&padding)?;
    }
    writer.flush()
}
