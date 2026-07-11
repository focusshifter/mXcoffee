#[cfg(feature = "benchmark")]
mod benchmark;
mod pressure_sensor;
mod settings;
mod touch;
mod ui;

use crate::pressure_sensor::PressureSensor;
use crate::settings::Settings;
use crate::touch::{Button, TouchButtons};
use crate::ui::{draw_center_message, draw_main_screen, UiData};

use embedded_graphics::pixelcolor::Rgb565;
use embedded_graphics::prelude::RgbColor;
use embedded_graphics::prelude::*;
use embedded_graphics_framebuf::FrameBuf;
use esp_idf_hal::delay::{FreeRtos, TickType};
use esp_idf_hal::gpio::PinDriver;
use esp_idf_hal::i2c::{I2cConfig, I2cDriver};
use esp_idf_hal::peripherals::Peripherals;
use esp_idf_hal::prelude::*;
use esp_idf_hal::spi::{
    config::{self, Duplex},
    Dma, SpiDeviceDriver, SpiDriver, SpiDriverConfig, SPI2,
};
use esp_idf_sys::EspError;
use mipidsi::interface::SpiInterface;
use mipidsi::options::{ColorInversion, ColorOrder};
use mipidsi::{models::ILI9342CRgb565, Builder};
use mxcoffee::session::SessionState;

const AXP2101_ADDR: u8 = 0x34;
const DISPLAY_WIDTH: usize = 320;
const DISPLAY_HEIGHT: usize = 240;
const PIXEL_COUNT: usize = DISPLAY_WIDTH * DISPLAY_HEIGHT;
const PRESSURE_HISTORY_LEN: usize = 160;
const AUTO_OFF_TIMEOUT_MS: u64 = 10 * 60 * 1_000;
const REFRESH_INTERVAL_MS: u64 = 20;
const DISPLAY_SPI_HZ: u32 = 40_000_000;
const DISPLAY_TRANSFER_BUFFER_SIZE: usize = 4096;

struct DeviceState {
    is_asleep: bool,
    bluetooth_on: bool,
    bt_connected: bool,
    last_bt_send_successful: bool,
    debug_mode: bool,
    last_refresh_ms: u64,
    last_activity_ms: u64,
    last_pressure: Option<i16>,
    session: SessionState,
    battery_percent: u8,
    frame_indicator: bool,
    frame_accum_ms: u64,
    frame_counter: u32,
}

impl DeviceState {
    fn new(now_ms: u64, bluetooth_on: bool) -> Self {
        Self {
            is_asleep: false,
            bluetooth_on,
            bt_connected: false,
            last_bt_send_successful: false,
            debug_mode: false,
            last_refresh_ms: now_ms,
            last_activity_ms: now_ms,
            last_pressure: None,
            session: SessionState::default(),
            battery_percent: 100,
            frame_indicator: false,
            frame_accum_ms: 0,
            frame_counter: 0,
        }
    }

    fn record_activity(&mut self, timestamp_ms: u64) {
        self.last_activity_ms = timestamp_ms;
    }
}

fn now_ms() -> u64 {
    unsafe { (esp_idf_sys::esp_timer_get_time() as u64) / 1_000 }
}

fn read_battery_percentage(i2c: &mut I2cDriver<'_>) -> Result<u8, EspError> {
    let mut buffer = [0u8; 1];
    i2c.write_read(
        AXP2101_ADDR,
        &[0xA4],
        &mut buffer,
        TickType::new_millis(10).ticks(),
    )?;
    Ok(buffer[0])
}

fn send_to_ble(state: &mut DeviceState, _pressure: i16) {
    state.last_bt_send_successful = state.bluetooth_on;
}

fn configure_core2_power(i2c: &mut I2cDriver<'_>) -> Result<(), EspError> {
    const REG_DCDC_CTRL: u8 = 0x80;
    const REG_DCDC1_VOLTAGE: u8 = 0x82;
    const REG_DCDC3_VOLTAGE: u8 = 0x84;
    const REG_LDO_CTRL: u8 = 0x90;
    const REG_ALDO2_VOLTAGE: u8 = 0x93;
    const REG_ALDO4_VOLTAGE: u8 = 0x95;
    const REG_BLDO1_VOLTAGE: u8 = 0x96;

    const DCDC1_MASK: u8 = 1 << 0;
    const DCDC3_MASK: u8 = 1 << 2;
    const ALDO2_MASK: u8 = 1 << 1;
    const ALDO4_MASK: u8 = 1 << 3;
    const BLDO1_MASK: u8 = 1 << 4;

    fn write_reg(i2c: &mut I2cDriver<'_>, reg: u8, value: u8) -> Result<(), EspError> {
        i2c.write(
            AXP2101_ADDR,
            &[reg, value],
            TickType::new_millis(10).ticks(),
        )
    }

    fn modify_reg(i2c: &mut I2cDriver<'_>, reg: u8, set_mask: u8) -> Result<(), EspError> {
        let mut current = [0u8; 1];
        i2c.write_read(
            AXP2101_ADDR,
            &[reg],
            &mut current,
            TickType::new_millis(10).ticks(),
        )?;
        let value = current[0] | set_mask;
        i2c.write(
            AXP2101_ADDR,
            &[reg, value],
            TickType::new_millis(10).ticks(),
        )
    }

    fn ldo_voltage_to_reg(voltage_mv: u16) -> u8 {
        let clamped = voltage_mv.clamp(500, 3_500);
        ((clamped - 500) / 100) as u8
    }

    fn dcdc1_voltage_to_reg(voltage_mv: u16) -> u8 {
        let clamped = voltage_mv.clamp(1_500, 3_400);
        ((clamped - 1_500) / 100) as u8
    }

    fn dcdc3_voltage_to_reg(voltage_mv: u16) -> u8 {
        let clamped = voltage_mv.clamp(1_500, 3_400);
        if clamped <= 1_540 {
            (((clamped - 1_220) / 20) as u8) + 0b0100_0111
        } else {
            (((clamped - 1_600) / 100) as u8) + 0b0101_1000
        }
    }

    write_reg(i2c, REG_DCDC1_VOLTAGE, dcdc1_voltage_to_reg(3_300))?;
    write_reg(i2c, REG_DCDC3_VOLTAGE, dcdc3_voltage_to_reg(3_300))?;
    modify_reg(i2c, REG_DCDC_CTRL, DCDC1_MASK | DCDC3_MASK)?;

    write_reg(i2c, REG_ALDO2_VOLTAGE, ldo_voltage_to_reg(3_300))?;
    write_reg(i2c, REG_ALDO4_VOLTAGE, ldo_voltage_to_reg(3_300))?;
    write_reg(i2c, REG_BLDO1_VOLTAGE, ldo_voltage_to_reg(3_300))?;
    modify_reg(i2c, REG_LDO_CTRL, ALDO2_MASK | ALDO4_MASK | BLDO1_MASK)?;

    Ok(())
}

fn main() {
    let peripherals = Peripherals::take().unwrap();
    let gpios = peripherals.pins;

    let settings = match Settings::new() {
        Ok(settings) => Some(settings),
        Err(err) => {
            println!("Settings initialization failed: {err:?}");
            None
        }
    };
    let bluetooth_enabled = settings
        .as_ref()
        .and_then(|settings| settings.bluetooth_enabled().ok())
        .unwrap_or(false);

    let pin_dc = PinDriver::output(gpios.gpio15).unwrap();
    let mut lcd_reset_pin = PinDriver::output(gpios.gpio33).unwrap();

    let mut i2c = I2cDriver::new(
        peripherals.i2c0,
        gpios.gpio21,
        gpios.gpio22,
        &I2cConfig::new().baudrate(400_000.Hz()),
    )
    .unwrap();

    configure_core2_power(&mut i2c).expect("Failed to configure AXP2101");

    let mut pin_lcd_blk = PinDriver::output(gpios.gpio32).unwrap();
    pin_lcd_blk.set_high().ok();

    lcd_reset_pin.set_low().unwrap();
    FreeRtos::delay_ms(100);
    lcd_reset_pin.set_high().unwrap();
    FreeRtos::delay_ms(2000);

    let spi = peripherals.spi2;
    let driver = SpiDriver::new::<SPI2>(
        spi,
        gpios.gpio18,
        gpios.gpio23,
        Some(gpios.gpio19),
        &SpiDriverConfig::new().dma(Dma::Auto(DISPLAY_TRANSFER_BUFFER_SIZE)),
    )
    .unwrap();

    let spi_device_config = config::Config::new()
        .baudrate(DISPLAY_SPI_HZ.Hz())
        .duplex(Duplex::Half)
        .write_only(true);
    let spi_device = SpiDeviceDriver::new(driver, Some(gpios.gpio5), &spi_device_config).unwrap();

    let mut spi_buffer = Box::new([0u8; DISPLAY_TRANSFER_BUFFER_SIZE]);
    let display_interface = SpiInterface::new(spi_device, pin_dc, spi_buffer.as_mut());

    let mut display = Builder::new(ILI9342CRgb565, display_interface)
        .display_size(320, 240)
        .color_order(ColorOrder::Bgr)
        .invert_colors(ColorInversion::Inverted)
        .reset_pin(lcd_reset_pin)
        .init(&mut FreeRtos)
        .unwrap();

    let mut pressure_sensor = PressureSensor::new();
    let mut touch_buttons = TouchButtons::new();
    let mut pressure_history = [0i16; PRESSURE_HISTORY_LEN];

    let mut state = DeviceState::new(now_ms(), bluetooth_enabled);
    let mut framebuffer = Box::new([Rgb565::BLACK; PIXEL_COUNT]);

    #[cfg(feature = "benchmark")]
    run_display_benchmarks(&mut display, framebuffer.as_mut());

    loop {
        let now = now_ms();
        let frame_start = now;

        let buttons = touch_buttons.poll(&mut i2c).unwrap_or_default();
        if buttons.any_pressed() {
            state.record_activity(now);
        }

        if state.is_asleep {
            if buttons.any_pressed() {
                pin_lcd_blk.set_high().ok();
                state.is_asleep = false;
                state.last_refresh_ms = 0;
                continue;
            } else {
                FreeRtos::delay_ms(50);
                continue;
            }
        }

        if buttons.is_pressed(Button::A) {
            state.debug_mode = !state.debug_mode;
        }

        if buttons.is_pressed(Button::B) {
            state.bluetooth_on = !state.bluetooth_on;
            if let Some(settings) = settings.as_ref() {
                if let Err(err) = settings.set_bluetooth_enabled(state.bluetooth_on) {
                    println!("Failed to persist Bluetooth state: {err:?}");
                }
            }
            state.bt_connected = false;
            state.last_bt_send_successful = false;
            let message = if state.bluetooth_on {
                "Bluetooth ON"
            } else {
                "Bluetooth OFF"
            };
            {
                let fb_buf = framebuffer.as_mut();
                let mut fb = FrameBuf::new(fb_buf, DISPLAY_WIDTH, DISPLAY_HEIGHT);
                fb.clear(Rgb565::BLACK).ok();
                let _ = draw_center_message(&mut fb, message);
            }
            if let Err(err) = display.set_pixels(
                0,
                0,
                (DISPLAY_WIDTH - 1) as u16,
                (DISPLAY_HEIGHT - 1) as u16,
                framebuffer.iter().copied(),
            ) {
                println!("Display update failed: {err:?}");
            }
            FreeRtos::delay_ms(300);
            state.last_refresh_ms = 0;
            continue;
        }

        if buttons.is_pressed(Button::C) {
            {
                let fb_buf = framebuffer.as_mut();
                let mut fb = FrameBuf::new(fb_buf, DISPLAY_WIDTH, DISPLAY_HEIGHT);
                fb.clear(Rgb565::BLACK).ok();
                let _ = draw_center_message(&mut fb, "Rebooting...");
            }
            if let Err(err) = display.set_pixels(
                0,
                0,
                (DISPLAY_WIDTH - 1) as u16,
                (DISPLAY_HEIGHT - 1) as u16,
                framebuffer.iter().copied(),
            ) {
                println!("Display update failed: {err:?}");
            }
            FreeRtos::delay_ms(200);
            unsafe { esp_idf_sys::esp_restart() };
        }

        if !state.is_asleep && now.saturating_sub(state.last_activity_ms) >= AUTO_OFF_TIMEOUT_MS {
            pin_lcd_blk.set_low().ok();
            state.is_asleep = true;
            continue;
        }

        if now.saturating_sub(state.last_refresh_ms) < REFRESH_INTERVAL_MS {
            FreeRtos::delay_ms(2);
            continue;
        }

        state.last_refresh_ms = now;
        state.frame_indicator = !state.frame_indicator;

        let pressure = match pressure_sensor.read_pressure_mbar(&mut i2c) {
            Ok(value) => value,
            Err(err) => {
                println!("Pressure read failed: {err:?}");
                state.last_pressure.unwrap_or(0)
            }
        };

        if state.last_pressure.map(|p| p != pressure).unwrap_or(true) {
            state.record_activity(now);
        }
        state.last_pressure = Some(pressure);

        pressure_history.rotate_left(1);
        pressure_history[PRESSURE_HISTORY_LEN - 1] = pressure;

        state.session.update_pressure(pressure, now);
        send_to_ble(&mut state, pressure);

        if let Ok(level) = read_battery_percentage(&mut i2c) {
            state.battery_percent = level.min(100);
        }

        let shot_time_secs = state.session.current_shot_duration_ms(now) as f32 / 1_000.0;

        let ui_data = UiData {
            pressure_history: &pressure_history,
            last_pressure: pressure,
            max_sensor_pressure: pressure_sensor.max_pressure_mbar(),
            battery_percent: state.battery_percent,
            bluetooth_on: state.bluetooth_on,
            bt_send_success: state.last_bt_send_successful,
            shot_time_secs,
            pressure_hex: pressure_sensor.last_hex_payload(),
            debug_mode: state.debug_mode,
            last_refresh_ms: state.last_refresh_ms,
            last_activity_ms: state.last_activity_ms,
            now_ms: now,
            auto_off_timeout_ms: AUTO_OFF_TIMEOUT_MS,
            timer_running: state.session.timer_running,
            frame_indicator: state.frame_indicator,
        };

        {
            let fb_buf = framebuffer.as_mut();
            let mut fb = FrameBuf::new(fb_buf, DISPLAY_WIDTH, DISPLAY_HEIGHT);
            fb.clear(Rgb565::BLACK).ok();
            if let Err(err) = draw_main_screen(&mut fb, ui_data) {
                println!("UI draw failed: {err:?}");
            }
        }

        if let Err(err) = display.set_pixels(
            0,
            0,
            (DISPLAY_WIDTH - 1) as u16,
            (DISPLAY_HEIGHT - 1) as u16,
            framebuffer.iter().copied(),
        ) {
            println!("Display update failed: {err:?}");
        }

        let frame_end = now_ms();
        let frame_duration = frame_end.saturating_sub(frame_start);
        state.frame_accum_ms = state.frame_accum_ms.saturating_add(frame_duration);
        state.frame_counter = state.frame_counter.saturating_add(1);
        if state.frame_accum_ms >= 1_000 {
            let avg = state.frame_accum_ms as f32 / state.frame_counter.max(1) as f32;
            println!("Frame duration: {} ms (avg {:.1} ms)", frame_duration, avg);
            state.frame_accum_ms = 0;
            state.frame_counter = 0;
        }

        FreeRtos::delay_ms(2);
    }
}

#[cfg(feature = "benchmark")]
fn run_display_benchmarks<DI, RST>(
    display: &mut mipidsi::Display<DI, ILI9342CRgb565, RST>,
    framebuffer: &mut [Rgb565; PIXEL_COUNT],
) where
    DI: mipidsi::interface::Interface<Word = u8>,
    DI::Error: core::fmt::Debug,
    RST: embedded_hal::digital::OutputPin,
{
    use benchmark::BenchmarkStats;

    const ITERATIONS: usize = 105;
    const WARMUP: usize = 5;
    const FRAME_BYTES: usize = PIXEL_COUNT * 2;

    println!(
        "{{\"type\":\"benchmark_config\",\"spi_hz\":{},\"transfer_buffer\":{},\"dma\":true,\"width\":{},\"height\":{}}}",
        DISPLAY_SPI_HZ, DISPLAY_TRANSFER_BUFFER_SIZE, DISPLAY_WIDTH, DISPLAY_HEIGHT
    );

    let mut spi_stats = BenchmarkStats::new("spi_alternating_frame", FRAME_BYTES);
    for iteration in 0..ITERATIONS {
        let color = if iteration % 2 == 0 {
            Rgb565::new(31, 0, 0)
        } else {
            Rgb565::new(0, 0, 31)
        };
        framebuffer.fill(color);
        let started = unsafe { esp_idf_sys::esp_timer_get_time() as u64 };
        display
            .set_pixels(
                0,
                0,
                (DISPLAY_WIDTH - 1) as u16,
                (DISPLAY_HEIGHT - 1) as u16,
                framebuffer.iter().copied(),
            )
            .expect("benchmark SPI transfer failed");
        let ended = unsafe { esp_idf_sys::esp_timer_get_time() as u64 };
        spi_stats.record(ended.saturating_sub(started));
    }
    spi_stats.report(WARMUP);

    let mut history = [0i16; PRESSURE_HISTORY_LEN];
    for (index, value) in history.iter_mut().enumerate() {
        *value = ((index as i32 * 9_000) / PRESSURE_HISTORY_LEN as i32) as i16;
    }

    let mut render_stats = BenchmarkStats::new("render_dynamic_ui", 0);
    let mut frame_stats = BenchmarkStats::new("frame_dynamic", FRAME_BYTES);
    for iteration in 0..ITERATIONS {
        history.rotate_left(1);
        history[PRESSURE_HISTORY_LEN - 1] = ((iteration * 97) % 10_000) as i16;
        let data = UiData {
            pressure_history: &history,
            last_pressure: history[PRESSURE_HISTORY_LEN - 1],
            max_sensor_pressure: 20_000,
            battery_percent: 87,
            bluetooth_on: true,
            bt_send_success: iteration % 3 != 0,
            shot_time_secs: iteration as f32 / 10.0,
            pressure_hex: "01 02 03",
            debug_mode: false,
            last_refresh_ms: iteration as u64 * 20,
            last_activity_ms: 0,
            now_ms: iteration as u64 * 20,
            auto_off_timeout_ms: AUTO_OFF_TIMEOUT_MS,
            timer_running: true,
            frame_indicator: iteration % 2 == 0,
        };

        let frame_started = unsafe { esp_idf_sys::esp_timer_get_time() as u64 };
        let render_started = frame_started;
        {
            let mut fb = FrameBuf::new(&mut *framebuffer, DISPLAY_WIDTH, DISPLAY_HEIGHT);
            fb.clear(Rgb565::BLACK).ok();
            draw_main_screen(&mut fb, data).expect("benchmark render failed");
        }
        let render_ended = unsafe { esp_idf_sys::esp_timer_get_time() as u64 };
        render_stats.record(render_ended.saturating_sub(render_started));
        display
            .set_pixels(
                0,
                0,
                (DISPLAY_WIDTH - 1) as u16,
                (DISPLAY_HEIGHT - 1) as u16,
                framebuffer.iter().copied(),
            )
            .expect("benchmark frame transfer failed");
        let frame_ended = unsafe { esp_idf_sys::esp_timer_get_time() as u64 };
        frame_stats.record(frame_ended.saturating_sub(frame_started));
    }
    render_stats.report(WARMUP);
    frame_stats.report(WARMUP);
}
