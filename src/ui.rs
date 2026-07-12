use core::fmt::Write as _;

use embedded_graphics::mono_font::ascii::FONT_10X20;
use embedded_graphics::mono_font::MonoTextStyle;
use embedded_graphics::pixelcolor::{Rgb565, Rgb888};
use embedded_graphics::prelude::*;
use embedded_graphics::primitives::{Line, PrimitiveStyle, Rectangle};
use embedded_graphics::text::{Alignment, Text};
use heapless::String;

use crate::vlw::VlwFont;

pub const WIDTH: i32 = 320;
pub const HEIGHT: i32 = 240;
pub const HISTORY_LEN: usize = 160;
pub const GRAPH_WINDOW_MS: u32 = 30_000;

const GRAPH_X: i32 = 10;
const GRAPH_Y: i32 = 90;
const GRAPH_WIDTH: i32 = 265;
const GRAPH_HEIGHT: i32 = 120;
const BAR_X: i32 = 280;
const BAR_WIDTH: u32 = 30;

const MOON_GLOSS_16: &[u8] = include_bytes!("../assets/fonts/MoonGloss_16.vlw");
const MOON_GLOSS_24: &[u8] = include_bytes!("../assets/fonts/MoonGloss_24_Logo.vlw");
const MOON_GLOSS_48: &[u8] = include_bytes!("../assets/fonts/MoonGloss_48_Numeric.vlw");

const DARK_BG: Rgb565 = rgb(0x03, 0x16, 0x1e);
const ACCENT_BG: Rgb565 = rgb(0x96, 0xcb, 0xbb);
const ACCENT_TEXT: Rgb565 = rgb(0x0a, 0x3b, 0x44);
const GRAPH_GOOD: Rgb565 = rgb(0xbd, 0xff, 0xff);
const GRAPH_WARNING: Rgb565 = rgb(0xd6, 0xa6, 0x7b);
const BAR_BG: Rgb565 = rgb(0x0a, 0x3b, 0x44);

const ACCENT_BG_SOURCE: Rgb888 = Rgb888::new(0x96, 0xcb, 0xbb);
const ACCENT_TEXT_SOURCE: Rgb888 = Rgb888::new(0x0a, 0x3b, 0x44);
const PANEL_TEXT_SOURCE: Rgb888 = Rgb888::new(0xdd, 0xfe, 0xee);
const SPLASH_TEXT_SOURCE: Rgb888 = Rgb888::new(0xe6, 0xff, 0xff);

const fn rgb(red: u8, green: u8, blue: u8) -> Rgb565 {
    Rgb565::new(red >> 3, green >> 2, blue >> 3)
}

pub struct UiData<'a> {
    pub pressure_history: &'a [i16],
    pub weight_history_tenths: &'a [i16],
    pub history_times_ms: &'a [u32],
    pub last_pressure: i16,
    pub max_sensor_pressure: i16,
    pub shot_weight: f32,
    pub flow_rate: f32,
    pub bluetooth_on: bool,
    pub scale_connected: bool,
    pub scale_name: &'a str,
    pub shot_time_tenths: u64,
    pub pressure_hex: &'a str,
    pub debug_mode: bool,
    pub last_refresh_ms: u64,
    pub last_activity_ms: u64,
    pub now_ms: u64,
    pub auto_off_timeout_ms: u64,
    pub timer_running: bool,
    pub frame_indicator: Option<bool>,
}

pub fn draw_splash<T>(target: &mut T) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    const BROWN: Rgb565 = rgb(0x5a, 0x3a, 0x1a);
    const LATTE: Rgb565 = rgb(0xf4, 0xe5, 0xc3);
    const SEGMENTS: i32 = 20;
    const INV_SQRT_2: f32 = 0.707_106_77;

    target.clear(Rgb565::BLACK)?;
    let font = VlwFont::new(MOON_GLOSS_24).unwrap();
    let font_height = font.height();
    let x_size = (font_height as f32 * 1.1) as i32;
    let width_m = font.measure("m").unwrap();
    let width_coffee = font.measure("coffee").unwrap();
    let start_x = (WIDTH - width_m - x_size - width_coffee) / 2;
    let text_y = (HEIGHT - font_height) / 2;
    let center_x = start_x + width_m + x_size / 2 - 1;
    let center_y = text_y + font_height / 2 + 2;
    let radius = (x_size as f32 * 0.55) as i32;

    font.draw(
        target,
        "m",
        Point::new(start_x, text_y),
        SPLASH_TEXT_SOURCE,
        Rgb888::BLACK,
    )?;
    font.draw(
        target,
        "coffee",
        Point::new(start_x + width_m + x_size, text_y),
        SPLASH_TEXT_SOURCE,
        Rgb888::BLACK,
    )?;

    target.draw_iter((-radius..=radius).flat_map(|y| {
        (-radius..=radius).filter_map(move |x| {
            (x * x + y * y <= radius * radius)
                .then_some(Pixel(Point::new(center_x + x, center_y + y), BROWN))
        })
    }))?;

    let length = radius as f32 * 0.6;
    let curve_amplitude = radius as f32 * 0.45;
    for (base_x, base_y, perpendicular_x, perpendicular_y) in [
        (INV_SQRT_2, INV_SQRT_2, -INV_SQRT_2, INV_SQRT_2),
        (INV_SQRT_2, -INV_SQRT_2, INV_SQRT_2, INV_SQRT_2),
    ] {
        for segment in 0..SEGMENTS {
            let t1 = -1.0 + 2.0 * segment as f32 / SEGMENTS as f32;
            let t2 = -1.0 + 2.0 * (segment + 1) as f32 / SEGMENTS as f32;
            let offset1 = (t1 * core::f32::consts::PI).sin() * curve_amplitude;
            let offset2 = (t2 * core::f32::consts::PI).sin() * curve_amplitude;
            let ax = t1 * length + offset1 * perpendicular_x;
            let ay = t1 * length + offset1 * perpendicular_y;
            let bx = t2 * length + offset2 * perpendicular_x;
            let by = t2 * length + offset2 * perpendicular_y;
            let start = Point::new(
                center_x + (base_x * ax - base_y * ay) as i32,
                center_y + (base_y * ax + base_x * ay) as i32,
            );
            let end = Point::new(
                center_x + (base_x * bx - base_y * by) as i32,
                center_y + (base_y * bx + base_x * by) as i32,
            );
            draw_m5_line(target, start, end, LATTE)?;
            draw_m5_line(
                target,
                start + Point::new(1, 0),
                end + Point::new(1, 0),
                LATTE,
            )?;
        }
    }
    Ok(())
}

pub fn draw_main_screen<T>(target: &mut T, data: UiData<'_>) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    target.clear(DARK_BG)?;
    draw_panel_frames(target)?;
    draw_main_screen_retained(target, data)
}

pub fn initialize_main_screen<T>(target: &mut T) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    target.clear(DARK_BG)?;
    draw_panel_frames(target)
}

pub fn draw_main_screen_retained<T>(target: &mut T, data: UiData<'_>) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    // Each framebuffer preserves its static background between full-screen uploads.
    // This region covers both graphs and any previous debug overlay.
    Rectangle::new(Point::new(8, 88), Size::new(268, 123))
        .into_styled(PrimitiveStyle::with_fill(DARK_BG))
        .draw(target)?;
    draw_status_band(target, &data)?;
    draw_graph(
        target,
        data.pressure_history,
        data.history_times_ms,
        max_pressure(&data),
        pressure_color(data.last_pressure),
    )?;
    draw_graph(
        target,
        data.weight_history_tenths,
        data.history_times_ms,
        500,
        GRAPH_WARNING,
    )?;
    draw_pressure_bar(target, data.last_pressure, max_pressure(&data))?;
    draw_panel_values(target, &data)?;

    if data.debug_mode {
        draw_debug_overlay(target, &data)?;
    }

    if let Some(frame_indicator) = data.frame_indicator {
        let indicator = if frame_indicator {
            Rgb565::WHITE
        } else {
            Rgb565::RED
        };
        Rectangle::new(Point::new(WIDTH - 4, HEIGHT - 4), Size::new(4, 4))
            .into_styled(PrimitiveStyle::with_fill(indicator))
            .draw(target)?;
    }
    Ok(())
}

fn max_pressure(data: &UiData<'_>) -> i32 {
    (i32::from(data.max_sensor_pressure) - 10_000).max(1_000)
}

fn pressure_color(pressure: i16) -> Rgb565 {
    if pressure > 9_000 {
        GRAPH_WARNING
    } else {
        GRAPH_GOOD
    }
}

fn draw_panel_frames<T>(target: &mut T) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    let font = VlwFont::new(MOON_GLOSS_16).unwrap();
    for (x, header) in [(0, "SHOT TIME"), (110, "WEIGHT G"), (220, "PRESSURE")] {
        Rectangle::new(Point::new(x, 0), Size::new(100, 80))
            .into_styled(PrimitiveStyle::with_fill(ACCENT_BG))
            .draw(target)?;
        Rectangle::new(Point::new(x + 2, 18), Size::new(96, 60))
            .into_styled(PrimitiveStyle::with_fill(ACCENT_TEXT))
            .draw(target)?;
        font.draw(
            target,
            header,
            Point::new(x + 2, 2),
            ACCENT_TEXT_SOURCE,
            ACCENT_BG_SOURCE,
        )?;
    }
    Ok(())
}

fn draw_panel_values<T>(target: &mut T, data: &UiData<'_>) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    for x in [0, 110, 220] {
        Rectangle::new(Point::new(x + 2, 18), Size::new(96, 60))
            .into_styled(PrimitiveStyle::with_fill(ACCENT_TEXT))
            .draw(target)?;
    }

    let small = VlwFont::new(MOON_GLOSS_16).unwrap();
    let large = VlwFont::new(MOON_GLOSS_48).unwrap();

    let mut value: String<16> = String::new();
    write!(
        &mut value,
        "{}.{:01}",
        data.shot_time_tenths / 10,
        data.shot_time_tenths % 10
    )
    .ok();
    large.draw_right(
        target,
        &value,
        92,
        25,
        PANEL_TEXT_SOURCE,
        ACCENT_TEXT_SOURCE,
    )?;

    value.clear();
    write!(&mut value, "{:.1}g", data.shot_weight).ok();
    small.draw_right(
        target,
        &value,
        200,
        30,
        PANEL_TEXT_SOURCE,
        ACCENT_TEXT_SOURCE,
    )?;
    value.clear();
    write!(&mut value, "{:.1} g/s", data.flow_rate).ok();
    small.draw_right(
        target,
        &value,
        200,
        50,
        PANEL_TEXT_SOURCE,
        ACCENT_TEXT_SOURCE,
    )?;

    value.clear();
    write!(
        &mut value,
        "{}.{:01}",
        data.last_pressure.max(0) / 1_000,
        (data.last_pressure.max(0) % 1_000) / 100
    )
    .ok();
    large.draw_right(
        target,
        &value,
        312,
        25,
        PANEL_TEXT_SOURCE,
        ACCENT_TEXT_SOURCE,
    )?;
    Ok(())
}

fn draw_status_band<T>(target: &mut T, data: &UiData<'_>) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    Rectangle::new(Point::new(0, 220), Size::new(320, 20))
        .into_styled(PrimitiveStyle::with_fill(ACCENT_BG))
        .draw(target)?;
    let font = VlwFont::new(MOON_GLOSS_16).unwrap();
    font.draw(
        target,
        if data.bluetooth_on { "BT ON" } else { "BT OFF" },
        Point::new(4, 222),
        ACCENT_TEXT_SOURCE,
        ACCENT_BG_SOURCE,
    )?;

    let mut scale: String<40> = String::new();
    if data.scale_connected {
        write!(&mut scale, "Scale: {}", data.scale_name).ok();
    } else {
        scale.push_str("Scale: --").ok();
    }
    font.draw_right(
        target,
        &scale,
        316,
        222,
        ACCENT_TEXT_SOURCE,
        ACCENT_BG_SOURCE,
    )?;
    Ok(())
}

fn draw_graph<T>(
    target: &mut T,
    values: &[i16],
    times: &[u32],
    max_value: i32,
    color: Rgb565,
) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    let count = values.len().min(HISTORY_LEN);
    if count < 2 || max_value <= 0 {
        return Ok(());
    }
    let use_times = times.len() >= count;
    let last_time = if use_times {
        times[count - 1]
    } else {
        GRAPH_WINDOW_MS
    };
    let window = GRAPH_WINDOW_MS.max(last_time).max(1);
    let point = |index: usize| {
        let time = if use_times {
            times[index]
        } else {
            (index as u32 * window) / (count - 1) as u32
        };
        let value = i32::from(values[index]).clamp(0, max_value);
        Point::new(
            GRAPH_X + (i64::from(time) * i64::from(GRAPH_WIDTH) / i64::from(window)) as i32,
            GRAPH_Y + GRAPH_HEIGHT - value * GRAPH_HEIGHT / max_value,
        )
    };
    let mut previous = point(0);
    for index in 1..count {
        let current = point(index);
        draw_m5_line(target, previous, current, color)?;
        previous = current;
    }
    Ok(())
}

fn draw_m5_line<T>(
    target: &mut T,
    mut start: Point,
    mut end: Point,
    color: Rgb565,
) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    let steep = (end.y - start.y).abs() > (end.x - start.x).abs();
    if steep {
        core::mem::swap(&mut start.x, &mut start.y);
        core::mem::swap(&mut end.x, &mut end.y);
    }
    if start.x > end.x {
        core::mem::swap(&mut start, &mut end);
    }

    let delta_y = (end.y - start.y).abs();
    let y_step = if end.y > start.y { 1 } else { -1 };
    let delta_x = end.x - start.x;
    let mut error = delta_x >> 1;
    let mut y = start.y;
    target.draw_iter((start.x..=end.x).map(|x| {
        let point = if steep {
            Point::new(y, x)
        } else {
            Point::new(x, y)
        };
        error -= delta_y;
        if error < 0 {
            error += delta_x;
            y += y_step;
        }
        Pixel(point, color)
    }))
}

fn draw_pressure_bar<T>(target: &mut T, pressure: i16, max_value: i32) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    Rectangle::new(
        Point::new(BAR_X, GRAPH_Y),
        Size::new(BAR_WIDTH, (GRAPH_HEIGHT + 1) as u32),
    )
    .into_styled(PrimitiveStyle::with_fill(BAR_BG))
    .draw(target)?;
    let height =
        (i32::from(pressure).clamp(0, max_value) * GRAPH_HEIGHT / max_value).clamp(0, GRAPH_HEIGHT);
    for offset in 0..height {
        let pressure_at_y = offset * max_value / GRAPH_HEIGHT;
        Line::new(
            Point::new(BAR_X, GRAPH_Y + GRAPH_HEIGHT - offset),
            Point::new(
                BAR_X + BAR_WIDTH as i32 - 1,
                GRAPH_Y + GRAPH_HEIGHT - offset,
            ),
        )
        .into_styled(PrimitiveStyle::with_stroke(
            gradient_color_for_pressure(pressure_at_y),
            1,
        ))
        .draw(target)?;
    }
    Ok(())
}

pub fn draw_center_message<T>(target: &mut T, text: &str) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    target.clear(DARK_BG)?;
    Text::with_alignment(
        text,
        Point::new(WIDTH / 2, HEIGHT / 2),
        MonoTextStyle::new(&FONT_10X20, Rgb565::WHITE),
        Alignment::Center,
    )
    .draw(target)
    .map(|_| ())
}

fn draw_debug_overlay<T>(target: &mut T, data: &UiData<'_>) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    let font = VlwFont::new(MOON_GLOSS_16).unwrap();
    let mut line: String<64> = String::new();
    write!(
        &mut line,
        "Scale: {}",
        if data.scale_connected {
            data.scale_name
        } else {
            "none"
        }
    )
    .ok();
    font.draw(
        target,
        &line,
        Point::new(10, 100),
        SPLASH_TEXT_SOURCE,
        Rgb888::new(0x03, 0x16, 0x1e),
    )?;
    line.clear();
    write!(&mut line, "Nearby: {}", data.scale_name).ok();
    font.draw(
        target,
        &line,
        Point::new(10, 120),
        SPLASH_TEXT_SOURCE,
        Rgb888::new(0x03, 0x16, 0x1e),
    )?;
    Ok(())
}

pub fn build_reference_histories(
    pressure: &mut [i16; HISTORY_LEN],
    weight: &mut [i16; HISTORY_LEN],
    times: &mut [u32; HISTORY_LEN],
) {
    for index in 0..HISTORY_LEN {
        times[index] = (GRAPH_WINDOW_MS as usize * index).div_ceil(HISTORY_LEN) as u32;
        pressure[index] = if index < 32 {
            (8_400 * index / 32) as i16
        } else if index < 116 {
            8_400
        } else {
            (8_400 - ((index - 116) * 8_400 / (HISTORY_LEN - 116))) as i16
        };
        weight[index] = if index < 24 {
            0
        } else {
            (40 + (218 - 40) * (index - 24) / (HISTORY_LEN - 24)) as i16
        };
    }
}

fn gradient_color_for_pressure(pressure: i32) -> Rgb565 {
    if pressure <= 6_000 {
        let gray = map(pressure, 0, 6_000, 64, 96).clamp(0, 255) as u8;
        rgb(gray, gray, gray)
    } else if pressure <= 7_000 {
        let gray = map(pressure, 6_000, 7_000, 96, 0).clamp(0, 255) as u8;
        let green = map(pressure, 6_000, 7_000, 96, 255).clamp(0, 255) as u8;
        rgb(gray, green, gray)
    } else if pressure <= 8_000 {
        Rgb565::GREEN
    } else if pressure <= 8_500 {
        rgb(map(pressure, 8_000, 8_500, 0, 255) as u8, 255, 0)
    } else if pressure <= 10_000 {
        rgb(255, map(pressure, 8_500, 10_000, 255, 0) as u8, 0)
    } else {
        Rgb565::RED
    }
}

fn map(value: i32, in_min: i32, in_max: i32, out_min: i32, out_max: i32) -> i32 {
    (value - in_min) * (out_max - out_min) / (in_max - in_min) + out_min
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fast_framebuffer::FastFrameBuffer;

    fn reference_data<'a>(
        pressure: &'a [i16],
        weight: &'a [i16],
        times: &'a [u32],
        frame_indicator: bool,
    ) -> UiData<'a> {
        UiData {
            pressure_history: pressure,
            weight_history_tenths: weight,
            history_times_ms: times,
            last_pressure: 8_400,
            max_sensor_pressure: 20_000,
            shot_weight: 21.8,
            flow_rate: 1.0,
            bluetooth_on: true,
            scale_connected: true,
            scale_name: "LFSMART SCALE",
            shot_time_tenths: 123,
            pressure_hex: "reference",
            debug_mode: false,
            last_refresh_ms: 0,
            last_activity_ms: 0,
            now_ms: 0,
            auto_off_timeout_ms: 600_000,
            timer_running: true,
            frame_indicator: Some(frame_indicator),
        }
    }

    #[test]
    fn reference_histories_match_cpp_oracle_endpoints() {
        let mut pressure = [0; HISTORY_LEN];
        let mut weight = [0; HISTORY_LEN];
        let mut times = [0; HISTORY_LEN];
        build_reference_histories(&mut pressure, &mut weight, &mut times);
        assert_eq!(
            (pressure[0], pressure[32], pressure[115], pressure[159]),
            (0, 8_400, 8_400, 191)
        );
        assert_eq!((weight[23], weight[24], weight[159]), (0, 40, 216));
        assert_eq!(times[159], 29_813);
    }

    #[test]
    fn reference_dashboard_uses_all_cpp_layout_regions() {
        let mut pressure = [0; HISTORY_LEN];
        let mut weight = [0; HISTORY_LEN];
        let mut times = [0; HISTORY_LEN];
        build_reference_histories(&mut pressure, &mut weight, &mut times);
        let mut pixels = [Rgb565::BLACK; WIDTH as usize * HEIGHT as usize];
        let mut target = FastFrameBuffer::new(&mut pixels, WIDTH as usize, HEIGHT as usize);
        draw_main_screen(
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
                debug_mode: false,
                last_refresh_ms: 0,
                last_activity_ms: 0,
                now_ms: 0,
                auto_off_timeout_ms: 600_000,
                timer_running: true,
                frame_indicator: Some(true),
            },
        )
        .unwrap();
        assert_eq!(pixels[0], ACCENT_BG);
        assert_eq!(pixels[110], ACCENT_BG);
        assert_eq!(pixels[220], ACCENT_BG);
        assert_eq!(pixels[220 * WIDTH as usize], ACCENT_BG);
        assert_ne!(
            pixels[(GRAPH_Y as usize + GRAPH_HEIGHT as usize) * WIDTH as usize + GRAPH_X as usize],
            DARK_BG
        );
    }

    #[test]
    fn splash_logo_is_centered_and_nonempty() {
        let mut pixels = [Rgb565::BLACK; WIDTH as usize * HEIGHT as usize];
        let mut target = FastFrameBuffer::new(&mut pixels, WIDTH as usize, HEIGHT as usize);
        draw_splash(&mut target).unwrap();

        let drawn: Vec<_> = pixels
            .iter()
            .enumerate()
            .filter(|(_, pixel)| **pixel != Rgb565::BLACK)
            .map(|(index, _)| (index % WIDTH as usize, index / WIDTH as usize))
            .collect();
        assert!(drawn.len() > 500);
        assert!(drawn
            .iter()
            .all(|(x, y)| (90..230).contains(x) && (100..140).contains(y)));
    }

    #[test]
    fn retained_redraw_matches_clean_full_redraw() {
        let mut pressure = [0; HISTORY_LEN];
        let mut weight = [0; HISTORY_LEN];
        let mut times = [0; HISTORY_LEN];
        build_reference_histories(&mut pressure, &mut weight, &mut times);
        let mut expected = vec![Rgb565::BLACK; WIDTH as usize * HEIGHT as usize];
        let mut retained = expected.clone();

        initialize_main_screen(&mut FastFrameBuffer::new(
            &mut retained,
            WIDTH as usize,
            HEIGHT as usize,
        ))
        .unwrap();
        draw_main_screen_retained(
            &mut FastFrameBuffer::new(&mut retained, WIDTH as usize, HEIGHT as usize),
            reference_data(&pressure, &weight, &times, true),
        )
        .unwrap();
        draw_main_screen(
            &mut FastFrameBuffer::new(&mut expected, WIDTH as usize, HEIGHT as usize),
            reference_data(&pressure, &weight, &times, true),
        )
        .unwrap();
        assert_eq!(retained, expected);

        pressure.rotate_left(1);
        pressure[HISTORY_LEN - 1] = 2_500;
        draw_main_screen_retained(
            &mut FastFrameBuffer::new(&mut retained, WIDTH as usize, HEIGHT as usize),
            reference_data(&pressure, &weight, &times, false),
        )
        .unwrap();
        draw_main_screen(
            &mut FastFrameBuffer::new(&mut expected, WIDTH as usize, HEIGHT as usize),
            reference_data(&pressure, &weight, &times, false),
        )
        .unwrap();
        assert_eq!(retained, expected);
    }
}
