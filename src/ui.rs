use core::fmt::Write as _;

use embedded_graphics::mono_font::ascii::{FONT_10X20, FONT_6X10, FONT_9X18_BOLD};
use embedded_graphics::mono_font::MonoTextStyle;
use embedded_graphics::pixelcolor::{Rgb565, Rgb888};
use embedded_graphics::prelude::*;
use embedded_graphics::primitives::{Line, PrimitiveStyle, Rectangle};
use embedded_graphics::text::{Alignment, Text};
use heapless::String;

const WIDTH: i32 = 320;
const HEIGHT: i32 = 240;

const GRAPH_START_X: i32 = 10;
const GRAPH_START_Y: i32 = 60;
const GRAPH_PADDING_RIGHT: i32 = 45;
const GRAPH_PADDING_BOTTOM: i32 = 10;

const AUDIO_BAR_WIDTH: i32 = 30;
const AUDIO_BAR_PADDING: i32 = 10;

const PRESSURE_GRID_VALUES: [i32; 4] = [9, 6, 3, 0];

pub struct UiData<'a> {
    pub pressure_history: &'a [i16],
    pub last_pressure: i16,
    pub max_sensor_pressure: i16,
    pub battery_percent: u8,
    pub bluetooth_on: bool,
    pub bt_send_success: bool,
    pub shot_time_secs: f32,
    pub pressure_hex: &'a str,
    pub debug_mode: bool,
    pub last_refresh_ms: u64,
    pub last_activity_ms: u64,
    pub now_ms: u64,
    pub auto_off_timeout_ms: u64,
    pub timer_running: bool,
    pub frame_indicator: bool,
}

pub fn draw_main_screen<T>(target: &mut T, data: UiData<'_>) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    let graph_width = WIDTH - GRAPH_START_X - GRAPH_PADDING_RIGHT;
    let graph_height = HEIGHT - GRAPH_START_Y - GRAPH_PADDING_BOTTOM;

    let audio_bar_x = WIDTH - AUDIO_BAR_WIDTH - AUDIO_BAR_PADDING;
    let audio_bar_y = GRAPH_START_Y;
    let audio_bar_height = graph_height;

    // Battery indicator
    let battery_color = match data.battery_percent {
        0..=5 => Rgb565::RED,
        6..=15 => Rgb565::from(Rgb888::new(255, 165, 0)),
        16..=25 => Rgb565::YELLOW,
        _ => Rgb565::GREEN,
    };

    let status_style = MonoTextStyle::new(&FONT_6X10, battery_color);
    let mut battery_text: String<8> = String::new();
    write!(&mut battery_text, "{}%", data.battery_percent).ok();
    let _ = Text::with_alignment(
        &battery_text,
        Point::new(WIDTH - 10, 10),
        status_style,
        Alignment::Right,
    )
    .draw(target)?;

    // Bluetooth indicator
    let bt_color = if data.bluetooth_on {
        if data.bt_send_success {
            Rgb565::GREEN
        } else {
            Rgb565::RED
        }
    } else {
        Rgb565::from(Rgb888::new(80, 80, 80))
    };
    let bt_style = MonoTextStyle::new(&FONT_6X10, bt_color);
    let _ = Text::with_alignment("BT", Point::new(WIDTH - 10, 30), bt_style, Alignment::Right)
        .draw(target)?;

    let history_len = data.pressure_history.len();
    let mut last_pressure = data.last_pressure as i32;
    if last_pressure < 0 {
        last_pressure = 0;
    }

    let graph_color = match last_pressure {
        p if p > 12_000 => Rgb565::RED,
        p if p > 9_000 => Rgb565::YELLOW,
        p if p > 6_000 => Rgb565::GREEN,
        _ => Rgb565::GREEN,
    };

    let show_pressure_warning = last_pressure > 12_000;

    let min_pressure = 0i32;
    let max_sensor = i32::from(data.max_sensor_pressure);
    let max_pressure = (max_sensor - 10_000).max(1_000);

    // Grid lines and labels
    let grid_style = PrimitiveStyle::with_stroke(Rgb565::from(Rgb888::new(40, 40, 40)), 1);
    let grid_text_style = MonoTextStyle::new(&FONT_6X10, Rgb565::from(Rgb888::new(120, 120, 120)));

    for &grid in &PRESSURE_GRID_VALUES {
        let pressure_y = GRAPH_START_Y + ((10 - grid) * graph_height).saturating_div(10);
        Line::new(
            Point::new(GRAPH_START_X, pressure_y),
            Point::new(GRAPH_START_X + graph_width, pressure_y),
        )
        .into_styled(grid_style)
        .draw(target)?;

        let mut label: String<4> = String::new();
        write!(&mut label, "{}", grid).ok();
        let _ = Text::new(&label, Point::new(0, pressure_y - 5), grid_text_style).draw(target)?;
    }

    // Pressure graph
    let line_style = PrimitiveStyle::with_stroke(graph_color, 2);
    for idx in 1..history_len {
        let p1 = data.pressure_history[idx - 1].clamp(0, data.max_sensor_pressure) as i32;
        let p2 = data.pressure_history[idx].clamp(0, data.max_sensor_pressure) as i32;

        let y1 = GRAPH_START_Y + graph_height - (p1 * graph_height).saturating_div(max_pressure);
        let y2 = GRAPH_START_Y + graph_height - (p2 * graph_height).saturating_div(max_pressure);

        let x1 = GRAPH_START_X + (graph_width * (idx as i32 - 1)) / history_len as i32;
        let x2 = GRAPH_START_X + (graph_width * idx as i32) / history_len as i32;

        Line::new(Point::new(x1, y1), Point::new(x2, y2))
            .into_styled(line_style)
            .draw(target)?;
    }

    // Pressure numeric display
    let pressure_style = MonoTextStyle::new(&FONT_9X18_BOLD, graph_color);
    let mut pressure_text: String<8> = String::new();
    write!(&mut pressure_text, "{:.1}", last_pressure as f32 / 1000.0).ok();
    let _ = Text::with_alignment(
        &pressure_text,
        Point::new(260, 10),
        pressure_style,
        Alignment::Right,
    )
    .draw(target)?;

    // Shot timer
    let timer_style = MonoTextStyle::new(&FONT_10X20, Rgb565::WHITE);
    let mut timer_text: String<16> = String::new();
    write!(&mut timer_text, "{:.1}s", data.shot_time_secs).ok();
    let _ = Text::with_alignment(
        &timer_text,
        Point::new(130, 10),
        timer_style,
        Alignment::Right,
    )
    .draw(target)?;

    // Audio-style gradient bar
    Rectangle::new(
        Point::new(audio_bar_x, audio_bar_y),
        Size::new(AUDIO_BAR_WIDTH as u32, (audio_bar_height + 1) as u32),
    )
    .into_styled(PrimitiveStyle::with_fill(Rgb565::from(Rgb888::new(
        40, 40, 40,
    ))))
    .draw(target)?;

    let clamped_last = last_pressure.clamp(min_pressure, max_pressure);
    let range = (max_pressure - min_pressure).max(1);
    let bar_height = (clamped_last - min_pressure) * audio_bar_height / range;

    for offset in 0..bar_height {
        let pressure_at_y =
            min_pressure + offset * (max_pressure - min_pressure) / audio_bar_height.max(1);
        let color = gradient_color_for_pressure(pressure_at_y);
        Rectangle::new(
            Point::new(audio_bar_x, audio_bar_y + audio_bar_height - offset),
            Size::new(AUDIO_BAR_WIDTH as u32, 1),
        )
        .into_styled(PrimitiveStyle::with_fill(color))
        .draw(target)?;
    }

    if show_pressure_warning {
        let stop_style = MonoTextStyle::new(&FONT_10X20, Rgb565::RED);
        let _ = Text::with_alignment(
            "STOP!",
            Point::new(WIDTH / 2, HEIGHT / 2),
            stop_style,
            Alignment::Center,
        )
        .draw(target)?;
    }

    let indicator_color = if data.frame_indicator {
        Rgb565::WHITE
    } else {
        Rgb565::RED
    };
    Rectangle::new(Point::new(WIDTH - 12, HEIGHT - 12), Size::new(8, 8))
        .into_styled(PrimitiveStyle::with_fill(indicator_color))
        .draw(target)?;

    if data.debug_mode {
        draw_debug_overlay(target, &data)?;
    }

    Ok(())
}

pub fn draw_center_message<T>(target: &mut T, text: &str) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    target.clear(Rgb565::BLACK)?;
    let style = MonoTextStyle::new(&FONT_10X20, Rgb565::WHITE);
    Text::with_alignment(
        text,
        Point::new(WIDTH / 2, HEIGHT / 2),
        style,
        Alignment::Center,
    )
    .draw(target)
    .map(|_| ())
}

fn draw_debug_overlay<T>(target: &mut T, data: &UiData<'_>) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    let style = MonoTextStyle::new(&FONT_6X10, Rgb565::WHITE);
    let mut y = GRAPH_START_Y + 10;

    let mut buffer: String<64> = String::new();

    buffer.clear();
    write!(&mut buffer, "Last refresh: {} ms", data.last_refresh_ms).ok();
    let _ = Text::new(&buffer, Point::new(40, y), style).draw(target)?;
    y += 12;

    buffer.clear();
    write!(&mut buffer, "Sensor raw: {}", data.pressure_hex).ok();
    let _ = Text::new(&buffer, Point::new(40, y), style).draw(target)?;
    y += 12;

    buffer.clear();
    write!(
        &mut buffer,
        "Pressure: {:.3} bar",
        data.last_pressure as f32 / 1000.0
    )
    .ok();
    let _ = Text::new(&buffer, Point::new(40, y), style).draw(target)?;
    y += 12;

    buffer.clear();
    write!(
        &mut buffer,
        "Shot timer: {} ({:.1}s)",
        if data.timer_running {
            "running"
        } else {
            "paused"
        },
        data.shot_time_secs
    )
    .ok();
    let _ = Text::new(&buffer, Point::new(40, y), style).draw(target)?;
    y += 12;

    buffer.clear();
    write!(&mut buffer, "Last activity: {} ms", data.last_activity_ms).ok();
    let _ = Text::new(&buffer, Point::new(40, y), style).draw(target)?;
    y += 12;

    let auto_off_remaining = data
        .last_activity_ms
        .saturating_add(data.auto_off_timeout_ms)
        .saturating_sub(data.now_ms);

    buffer.clear();
    write!(
        &mut buffer,
        "Auto-off in: {} s",
        (auto_off_remaining / 1000)
    )
    .ok();
    Text::new(&buffer, Point::new(40, y), style).draw(target)?;

    Ok(())
}

fn gradient_color_for_pressure(pressure: i32) -> Rgb565 {
    if pressure <= 6_000 {
        let gray = map(pressure, 0, 6_000, 64, 96).clamp(0, 255) as u8;
        Rgb565::from(Rgb888::new(gray, gray, gray))
    } else if pressure <= 7_000 {
        let gray = map(pressure, 6_000, 7_000, 96, 0).clamp(0, 255) as u8;
        let green = map(pressure, 6_000, 7_000, 96, 255).clamp(0, 255) as u8;
        Rgb565::from(Rgb888::new(gray, green, gray))
    } else if pressure <= 8_000 {
        Rgb565::from(Rgb888::new(0, 255, 0))
    } else if pressure <= 8_500 {
        let red = map(pressure, 8_000, 8_500, 0, 255).clamp(0, 255) as u8;
        Rgb565::from(Rgb888::new(red, 255, 0))
    } else if pressure <= 10_000 {
        let green = map(pressure, 8_500, 10_000, 255, 0).clamp(0, 255) as u8;
        Rgb565::from(Rgb888::new(255, green, 0))
    } else {
        Rgb565::from(Rgb888::new(255, 0, 0))
    }
}

fn map(value: i32, in_min: i32, in_max: i32, out_min: i32, out_max: i32) -> i32 {
    if in_max == in_min {
        return out_min;
    }
    (value - in_min) * (out_max - out_min) / (in_max - in_min) + out_min
}
