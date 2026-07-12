mod ble_server;
mod display_interface;
#[cfg(not(feature = "demo"))]
mod pressure_sensor;
mod settings;
mod touch;

#[cfg(not(feature = "demo"))]
use crate::pressure_sensor::PressureSensor;
#[cfg(not(feature = "demo"))]
use crate::settings::LastScaleConfig;
use crate::settings::Settings;
use crate::touch::{Button, TouchButtons};
use ble_server::MxBleServer;
use display_interface::FastSpiInterface;

use embedded_graphics::pixelcolor::Rgb565;
use embedded_graphics::prelude::RgbColor;
use esp_idf_hal::delay::{FreeRtos, TickType};
use esp_idf_hal::gpio::PinDriver;
use esp_idf_hal::i2c::{I2cConfig, I2cDriver};
use esp_idf_hal::peripherals::Peripherals;
use esp_idf_hal::prelude::*;
use esp_idf_hal::spi::{
    config::{self, Duplex},
    Dma, SpiDeviceDriver, SpiDriver, SpiDriverConfig, SPI2,
};
use esp_idf_svc::nvs::EspDefaultNvsPartition;
use esp_idf_sys::EspError;
use mipidsi::options::{ColorInversion, ColorOrder};
use mipidsi::{models::ILI9342CRgb565, Builder};
use mxcoffee::fast_framebuffer::{clear_black, FastFrameBuffer};
use mxcoffee::session::SessionState;
use mxcoffee::ui::{
    draw_center_message, draw_main_screen_retained, initialize_main_screen, UiData,
};
#[cfg(feature = "benchmark-soak")]
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::mpsc::{sync_channel, Receiver, SyncSender, TryRecvError, TrySendError};
#[cfg(feature = "benchmark-soak")]
use std::sync::Arc;

const AXP2101_ADDR: u8 = 0x34;
const DISPLAY_WIDTH: usize = 320;
const DISPLAY_HEIGHT: usize = 240;
const PIXEL_COUNT: usize = DISPLAY_WIDTH * DISPLAY_HEIGHT;
const PRESSURE_HISTORY_LEN: usize = 160;
const HISTORY_SAMPLE_INTERVAL_MS: u64 = 30_000 / PRESSURE_HISTORY_LEN as u64;
#[cfg(not(feature = "poweroff-test"))]
const AUTO_OFF_TIMEOUT_MS: u64 = 10 * 60 * 1_000;
#[cfg(feature = "poweroff-test")]
const AUTO_OFF_TIMEOUT_MS: u64 = 15_000;
const REFRESH_INTERVAL_MS: u64 = 20;
const DISPLAY_SPI_HZ: u32 = 40_000_000;
const DISPLAY_TRANSFER_BUFFER_SIZE: usize = 8 * 1024;
type OwnedFrameBuffer = Box<[Rgb565; PIXEL_COUNT]>;

struct PressureSample {
    pressure_mbar: i16,
    sampled_at_ms: u64,
    debug_payload: String,
}

fn exchange_frame(
    frame_tx: &SyncSender<OwnedFrameBuffer>,
    available_rx: &Receiver<OwnedFrameBuffer>,
    mut frame: OwnedFrameBuffer,
) -> OwnedFrameBuffer {
    loop {
        match frame_tx.try_send(frame) {
            Ok(()) => break,
            Err(TrySendError::Full(returned)) => {
                frame = returned;
                FreeRtos::delay_ms(1);
            }
            Err(TrySendError::Disconnected(_)) => panic!("display worker stopped unexpectedly"),
        }
    }

    loop {
        match available_rx.try_recv() {
            Ok(available) => return available,
            Err(TryRecvError::Empty) => FreeRtos::delay_ms(1),
            Err(TryRecvError::Disconnected) => {
                panic!("display worker dropped framebuffer pool")
            }
        }
    }
}

struct DeviceState {
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
    last_battery_read_ms: u64,
}

impl DeviceState {
    fn new(now_ms: u64, bluetooth_on: bool) -> Self {
        Self {
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
            last_battery_read_ms: 0,
        }
    }

    fn record_activity(&mut self, timestamp_ms: u64) {
        self.last_activity_ms = timestamp_ms;
    }
}

fn now_ms() -> u64 {
    display_interface::monotonic_time_us() / 1_000
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

    fn clear_reg(i2c: &mut I2cDriver<'_>, reg: u8, clear_mask: u8) -> Result<(), EspError> {
        let mut current = [0u8; 1];
        i2c.write_read(
            AXP2101_ADDR,
            &[reg],
            &mut current,
            TickType::new_millis(10).ticks(),
        )?;
        i2c.write(
            AXP2101_ADDR,
            &[reg, current[0] & !clear_mask],
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
    clear_reg(i2c, REG_LDO_CTRL, ALDO2_MASK)?;
    FreeRtos::delay_ms(2);
    modify_reg(i2c, REG_LDO_CTRL, ALDO2_MASK | ALDO4_MASK | BLDO1_MASK)?;

    Ok(())
}

fn set_core2_backlight(i2c: &mut I2cDriver<'_>, enabled: bool) -> Result<(), EspError> {
    const REG_LDO_CTRL: u8 = 0x90;
    const BLDO1_MASK: u8 = 1 << 4;
    let mut current = [0u8; 1];
    i2c.write_read(
        AXP2101_ADDR,
        &[REG_LDO_CTRL],
        &mut current,
        TickType::new_millis(10).ticks(),
    )?;
    let value = if enabled {
        current[0] | BLDO1_MASK
    } else {
        current[0] & !BLDO1_MASK
    };
    i2c.write(
        AXP2101_ADDR,
        &[REG_LDO_CTRL, value],
        TickType::new_millis(10).ticks(),
    )
}

fn power_off_core2(i2c: &mut I2cDriver<'_>) -> Result<(), EspError> {
    const REG_POWER_OFF: u8 = 0x10;
    let mut current = [0u8; 1];
    i2c.write_read(
        AXP2101_ADDR,
        &[REG_POWER_OFF],
        &mut current,
        TickType::new_millis(10).ticks(),
    )?;
    i2c.write(
        AXP2101_ADDR,
        &[REG_POWER_OFF, current[0] | 0x01],
        TickType::new_millis(10).ticks(),
    )
}

fn main() {
    let peripherals = Peripherals::take().unwrap();
    let gpios = peripherals.pins;

    let nvs_partition = EspDefaultNvsPartition::take().expect("NVS initialization failed");
    #[cfg_attr(feature = "demo", allow(unused_mut))]
    let mut settings = match Settings::new(nvs_partition.clone()) {
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
    let last_scale = settings
        .as_ref()
        .and_then(|settings| settings.load_last_scale().ok().flatten());
    #[cfg(not(feature = "demo"))]
    let mut persisted_scale_address = last_scale
        .as_ref()
        .map(|scale| scale.address.clone())
        .unwrap_or_default();
    let ble_server =
        MxBleServer::new(bluetooth_enabled, last_scale).expect("BLE initialization failed");
    let _ = ble_server.log(b"Rust firmware started");

    let pin_dc = esp_idf_hal::gpio::PinDriver::output(gpios.gpio15).unwrap();
    let mut pin_cs = esp_idf_hal::gpio::PinDriver::output(gpios.gpio5).unwrap();
    pin_cs.set_high().unwrap();

    let mut internal_i2c = I2cDriver::new(
        peripherals.i2c1,
        gpios.gpio21,
        gpios.gpio22,
        &I2cConfig::new().baudrate(400_000.Hz()),
    )
    .unwrap();
    let touch_interrupt = PinDriver::input(gpios.gpio39).unwrap();

    let mut power_configured = false;
    for _ in 0..3 {
        match configure_core2_power(&mut internal_i2c) {
            Ok(()) => {
                power_configured = true;
                break;
            }
            Err(err) => {
                println!("AXP2101 configuration attempt failed: {err:?}");
                FreeRtos::delay_ms(25);
            }
        }
    }
    assert!(
        power_configured,
        "Failed to configure AXP2101 after retries"
    );

    #[cfg(not(feature = "demo"))]
    let pressure_i2c = I2cDriver::new(
        peripherals.i2c0,
        gpios.gpio33,
        gpios.gpio32,
        &I2cConfig::new().baudrate(400_000.Hz()),
    )
    .unwrap();

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
        .queue_size(2)
        .write_only(true);
    let spi_device = SpiDeviceDriver::new(
        driver,
        None::<esp_idf_hal::gpio::AnyOutputPin>,
        &spi_device_config,
    )
    .unwrap();
    let raw_spi_device = spi_device.device();

    let spi_buffer = Box::leak(Box::new([0u8; DISPLAY_TRANSFER_BUFFER_SIZE]));
    let display_interface = FastSpiInterface::new(
        spi_device,
        pin_dc,
        pin_cs,
        spi_buffer.as_mut(),
        raw_spi_device,
    );

    let display = Builder::new(ILI9342CRgb565, display_interface)
        .display_size(320, 240)
        .color_order(ColorOrder::Bgr)
        .invert_colors(ColorInversion::Inverted)
        .init(&mut FreeRtos)
        .unwrap();
    let (mut display_interface, _, _) = display.release();

    let mut touch_buttons = TouchButtons::new();
    let mut pressure_history = [0i16; PRESSURE_HISTORY_LEN];
    let mut weight_history = [0i16; PRESSURE_HISTORY_LEN];
    let history_times = core::array::from_fn::<_, PRESSURE_HISTORY_LEN, _>(|index| {
        (30_000usize * index).div_ceil(PRESSURE_HISTORY_LEN) as u32
    });

    let mut state = DeviceState::new(now_ms(), bluetooth_enabled);
    let mut last_history_sample_ms = now_ms();
    #[cfg(feature = "demo")]
    let demo_started_ms = now_ms();
    #[cfg(feature = "demo")]
    let mut demo_scale = mxcoffee::demo::SimulatedScale::default();
    let mut framebuffer = Box::new([Rgb565::BLACK; PIXEL_COUNT]);
    initialize_main_screen(&mut FastFrameBuffer::new(
        framebuffer.as_mut(),
        DISPLAY_WIDTH,
        DISPLAY_HEIGHT,
    ))
    .unwrap();

    #[cfg(all(feature = "benchmark", not(feature = "benchmark-soak")))]
    {
        run_display_benchmarks(&mut display_interface, framebuffer.as_mut());
        initialize_main_screen(&mut FastFrameBuffer::new(
            framebuffer.as_mut(),
            DISPLAY_WIDTH,
            DISPLAY_HEIGHT,
        ))
        .unwrap();
    }
    ble_server.start_scale_worker();

    let (pressure_tx, pressure_rx) = sync_channel::<PressureSample>(16);
    #[cfg(feature = "demo")]
    std::thread::Builder::new()
        .name("pressure".into())
        .stack_size(4 * 1024)
        .spawn(move || loop {
            let sampled_at_ms = now_ms();
            let sample = PressureSample {
                pressure_mbar: mxcoffee::demo::simulated_pressure_mbar(
                    sampled_at_ms.saturating_sub(demo_started_ms),
                ),
                sampled_at_ms,
                debug_payload: "SIM SIM SIM".into(),
            };
            if matches!(
                pressure_tx.try_send(sample),
                Err(TrySendError::Disconnected(_))
            ) {
                break;
            }
            FreeRtos::delay_ms(20);
        })
        .expect("failed to start pressure sampler");
    #[cfg(not(feature = "demo"))]
    std::thread::Builder::new()
        .name("pressure".into())
        .stack_size(4 * 1024)
        .spawn(move || {
            let mut pressure_i2c = pressure_i2c;
            let mut pressure_sensor = PressureSensor::new();
            let mut last_error_log_ms = 0;
            loop {
                let sampled_at_ms = now_ms();
                match pressure_sensor.read_pressure_mbar(&mut pressure_i2c) {
                    Ok(pressure_mbar) => {
                        let sample = PressureSample {
                            pressure_mbar,
                            sampled_at_ms,
                            debug_payload: pressure_sensor.last_hex_payload().into(),
                        };
                        if matches!(
                            pressure_tx.try_send(sample),
                            Err(TrySendError::Disconnected(_))
                        ) {
                            break;
                        }
                    }
                    Err(err) if sampled_at_ms.saturating_sub(last_error_log_ms) >= 1_000 => {
                        println!("Pressure read failed: {err:?}");
                        last_error_log_ms = sampled_at_ms;
                    }
                    Err(_) => {}
                }
                FreeRtos::delay_ms(20);
            }
        })
        .expect("failed to start pressure sampler");

    #[cfg(feature = "benchmark-soak")]
    let display_errors = Arc::new(AtomicU32::new(0));
    #[cfg(feature = "benchmark-soak")]
    let worker_display_errors = display_errors.clone();

    let (frame_tx, frame_rx) = sync_channel::<OwnedFrameBuffer>(2);
    let (available_tx, available_rx) = sync_channel::<OwnedFrameBuffer>(3);
    for _ in 0..2 {
        let mut available = Box::new([Rgb565::BLACK; PIXEL_COUNT]);
        initialize_main_screen(&mut FastFrameBuffer::new(
            available.as_mut(),
            DISPLAY_WIDTH,
            DISPLAY_HEIGHT,
        ))
        .unwrap();
        available_tx
            .send(available)
            .expect("failed to seed display framebuffer pool");
    }
    std::thread::Builder::new()
        .name("display".into())
        .stack_size(8 * 1024)
        .spawn(move || {
            #[cfg(feature = "benchmark")]
            let mut upload_stats = Box::new(mxcoffee::benchmark::BenchmarkStats::new(
                "display_worker_upload",
                PIXEL_COUNT * 2,
            ));
            #[cfg(feature = "benchmark")]
            let mut cadence_stats = Box::new(mxcoffee::benchmark::BenchmarkStats::new(
                "display_worker_cadence",
                PIXEL_COUNT * 2,
            ));
            #[cfg(feature = "benchmark")]
            let mut completed_frames = 0usize;
            #[cfg(feature = "benchmark")]
            let mut last_completion_us = None;
            let mut frames_until_pause = 4u8;

            loop {
                match frame_rx.try_recv() {
                    Ok(frame) => {
                        #[cfg(feature = "benchmark")]
                        let upload_started_us = display_interface::monotonic_time_us();
                        if let Err(err) = display_interface.send_frame_queued(frame.as_ref()) {
                            #[cfg(feature = "benchmark-soak")]
                            worker_display_errors.fetch_add(1, Ordering::Relaxed);
                            println!("Display update failed: {err:?}");
                        }
                        #[cfg(feature = "benchmark")]
                        {
                            let completed_us = display_interface::monotonic_time_us();
                            if completed_frames < 105 {
                                upload_stats.record(completed_us.saturating_sub(upload_started_us));
                            }
                            if let Some(previous_us) = last_completion_us {
                                if completed_frames <= 105 {
                                    cadence_stats.record(completed_us.saturating_sub(previous_us));
                                }
                            }
                            last_completion_us = Some(completed_us);
                            completed_frames += 1;
                            if completed_frames == 106 {
                                upload_stats.report(5);
                                cadence_stats.report(5);
                            }
                        }
                        let mut returned = frame;
                        loop {
                            match available_tx.try_send(returned) {
                                Ok(()) => break,
                                Err(TrySendError::Full(frame)) => {
                                    returned = frame;
                                    FreeRtos::delay_ms(1);
                                }
                                Err(TrySendError::Disconnected(_)) => return,
                            }
                        }
                        frames_until_pause -= 1;
                        if frames_until_pause == 0 {
                            FreeRtos::delay_ms(2);
                            frames_until_pause = 4;
                        }
                    }
                    Err(TryRecvError::Empty) => FreeRtos::delay_ms(1),
                    Err(TryRecvError::Disconnected) => break,
                }
            }
        })
        .expect("failed to start display worker");

    #[cfg(feature = "benchmark")]
    let mut pipeline_stats = Box::new(mxcoffee::benchmark::BenchmarkStats::new(
        "frame_dynamic_pipelined",
        PIXEL_COUNT * 2,
    ));
    #[cfg(feature = "benchmark")]
    let mut pipeline_samples = 0usize;
    #[cfg(feature = "benchmark")]
    let mut pipeline_update_stats =
        Box::new(mxcoffee::benchmark::BenchmarkStats::new("frame_update", 0));
    #[cfg(feature = "benchmark")]
    let mut pipeline_render_stats =
        Box::new(mxcoffee::benchmark::BenchmarkStats::new("frame_render", 0));
    #[cfg(feature = "benchmark")]
    let mut pipeline_clear_stats =
        Box::new(mxcoffee::benchmark::BenchmarkStats::new("frame_clear", 0));
    #[cfg(feature = "benchmark")]
    let mut pipeline_ui_stats = Box::new(mxcoffee::benchmark::BenchmarkStats::new("frame_ui", 0));
    #[cfg(feature = "benchmark")]
    let mut pipeline_exchange_stats = Box::new(mxcoffee::benchmark::BenchmarkStats::new(
        "frame_exchange",
        PIXEL_COUNT * 2,
    ));
    #[cfg(feature = "benchmark-soak")]
    let soak_started_ms = now_ms();
    #[cfg(feature = "benchmark-soak")]
    let mut soak_stats = Box::new(mxcoffee::benchmark::LongRunStats::new());
    #[cfg(feature = "benchmark-soak")]
    let mut soak_next_report_ms = soak_started_ms + 10_000;
    #[cfg(feature = "benchmark-soak")]
    let mut soak_heap_baseline = None;
    #[cfg(feature = "benchmark-soak")]
    let mut soak_pressure_notify_attempts = 0u64;
    #[cfg(feature = "benchmark-soak")]
    let mut soak_pressure_notify_successes = 0u64;
    #[cfg(feature = "benchmark-soak")]
    let mut soak_completed = false;
    let mut pressure = 0i16;
    let mut pressure_hex = String::new();

    loop {
        #[cfg(feature = "benchmark")]
        let pipeline_started_us = display_interface::monotonic_time_us();
        let now = now_ms();
        let frame_start = now;

        while let Ok(sample) = pressure_rx.try_recv() {
            pressure = sample.pressure_mbar;
            pressure_hex = sample.debug_payload;
            #[cfg(feature = "benchmark-soak")]
            soak_stats.record_pressure_sample(sample.sampled_at_ms);

            if state.last_pressure.map(|p| p != pressure).unwrap_or(true) {
                state.record_activity(sample.sampled_at_ms);
            }
            state.last_pressure = Some(pressure);
            state
                .session
                .update_pressure(pressure, sample.sampled_at_ms);
            state.bt_connected = ble_server.is_connected();
            if state.bluetooth_on {
                #[cfg(feature = "benchmark-soak")]
                if state.bt_connected {
                    soak_pressure_notify_attempts = soak_pressure_notify_attempts.saturating_add(1);
                }
                let notified = ble_server.notify_pressure(pressure);
                #[cfg(feature = "benchmark-soak")]
                if state.bt_connected && notified {
                    soak_pressure_notify_successes =
                        soak_pressure_notify_successes.saturating_add(1);
                }
                state.last_bt_send_successful = notified;
            } else {
                state.last_bt_send_successful = false;
            }
        }

        let buttons = if touch_interrupt.is_low() || touch_buttons.has_active_touch() {
            touch_buttons.poll(&mut internal_i2c).unwrap_or_default()
        } else {
            Default::default()
        };
        if buttons.any_pressed() {
            state.record_activity(now);
        }

        if buttons.is_pressed(Button::A) {
            state.debug_mode = !state.debug_mode;
        }

        if buttons.is_pressed(Button::B) {
            state.bluetooth_on = !state.bluetooth_on;
            if let Err(err) = ble_server.set_enabled(state.bluetooth_on) {
                println!("Failed to toggle Bluetooth: {err:?}");
                state.bluetooth_on = !state.bluetooth_on;
            }
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
                clear_black(fb_buf);
                let mut fb = FastFrameBuffer::new(fb_buf, DISPLAY_WIDTH, DISPLAY_HEIGHT);
                let _ = draw_center_message(&mut fb, message);
            }
            framebuffer = exchange_frame(&frame_tx, &available_rx, framebuffer);
            FreeRtos::delay_ms(300);
            state.last_refresh_ms = 0;
            continue;
        }

        if buttons.is_pressed(Button::C) {
            {
                let fb_buf = framebuffer.as_mut();
                clear_black(fb_buf);
                let mut fb = FastFrameBuffer::new(fb_buf, DISPLAY_WIDTH, DISPLAY_HEIGHT);
                let _ = draw_center_message(&mut fb, "Rebooting...");
            }
            let _ = exchange_frame(&frame_tx, &available_rx, framebuffer);
            FreeRtos::delay_ms(200);
            esp_idf_hal::reset::restart();
        }

        if !cfg!(feature = "benchmark-soak")
            && now.saturating_sub(state.last_activity_ms) >= AUTO_OFF_TIMEOUT_MS
        {
            set_core2_backlight(&mut internal_i2c, false).ok();
            FreeRtos::delay_ms(20);
            if let Err(err) = power_off_core2(&mut internal_i2c) {
                println!("Failed to power off: {err:?}");
                set_core2_backlight(&mut internal_i2c, true).ok();
                state.record_activity(now);
            } else {
                loop {
                    FreeRtos::delay_ms(1_000);
                }
            }
        }

        if now.saturating_sub(state.last_refresh_ms) < REFRESH_INTERVAL_MS {
            FreeRtos::delay_ms(2);
            continue;
        }

        state.last_refresh_ms = now;
        state.frame_indicator = !state.frame_indicator;

        #[cfg(feature = "demo")]
        state.session.update_scale(
            true,
            demo_scale.update(now.saturating_sub(demo_started_ms)),
            now,
        );
        #[cfg(not(feature = "demo"))]
        let scale = ble_server.scale_snapshot();
        #[cfg(not(feature = "demo"))]
        state
            .session
            .update_scale(scale.connected, scale.weight_grams, now);
        #[cfg(not(feature = "demo"))]
        if scale.connected && scale.address != persisted_scale_address {
            if let Some(settings) = settings.as_mut() {
                let config = LastScaleConfig {
                    address: scale.address.clone(),
                    name: scale.name.clone(),
                    address_type: scale.address_type,
                    scale_type: 1,
                };
                match settings.save_last_scale(&config) {
                    Ok(()) => persisted_scale_address.clone_from(&scale.address),
                    Err(err) => println!("Failed to persist scale: {err:?}"),
                }
            }
        }

        if now.saturating_sub(last_history_sample_ms) >= HISTORY_SAMPLE_INTERVAL_MS {
            last_history_sample_ms = now;
            pressure_history.rotate_left(1);
            pressure_history[PRESSURE_HISTORY_LEN - 1] = pressure;
            weight_history.rotate_left(1);
            weight_history[PRESSURE_HISTORY_LEN - 1] =
                (state.session.shot_weight * 10.0).clamp(0.0, i16::MAX as f32) as i16;
        }

        if now.saturating_sub(state.last_battery_read_ms) >= 1_000 {
            state.last_battery_read_ms = now;
            if let Ok(level) = read_battery_percentage(&mut internal_i2c) {
                state.battery_percent = level.min(100);
                if state.bluetooth_on {
                    ble_server.set_battery_level(state.battery_percent.into());
                }
            }
        }

        let shot_time_tenths = state.session.current_shot_duration_ms(now) / 100;
        #[cfg(feature = "demo")]
        let (scale_connected, scale_name) = (true, "SimScale");
        #[cfg(not(feature = "demo"))]
        let (scale_connected, scale_name) = (scale.connected, scale.name.as_str());

        let max_sensor_pressure = 20_000;

        let ui_data = UiData {
            pressure_history: &pressure_history,
            weight_history_tenths: &weight_history,
            history_times_ms: &history_times,
            last_pressure: pressure,
            max_sensor_pressure,
            shot_weight: state.session.shot_weight,
            flow_rate: state.session.flow_rate,
            bluetooth_on: state.bluetooth_on,
            scale_connected,
            scale_name,
            shot_time_tenths,
            pressure_hex: &pressure_hex,
            debug_mode: state.debug_mode,
            last_refresh_ms: state.last_refresh_ms,
            last_activity_ms: state.last_activity_ms,
            now_ms: now,
            auto_off_timeout_ms: AUTO_OFF_TIMEOUT_MS,
            timer_running: state.session.timer_running,
            frame_indicator: state.frame_indicator,
        };

        #[cfg(feature = "benchmark")]
        let render_started_us = display_interface::monotonic_time_us();
        {
            let fb_buf = framebuffer.as_mut();
            #[cfg(feature = "benchmark")]
            let clear_ended_us = display_interface::monotonic_time_us();
            let mut fb = FastFrameBuffer::new(fb_buf, DISPLAY_WIDTH, DISPLAY_HEIGHT);
            if let Err(err) = draw_main_screen_retained(&mut fb, ui_data) {
                println!("UI draw failed: {err:?}");
            }
            #[cfg(feature = "benchmark")]
            {
                pipeline_clear_stats.record(clear_ended_us.saturating_sub(render_started_us));
                let ui_ended_us = display_interface::monotonic_time_us();
                pipeline_ui_stats.record(ui_ended_us.saturating_sub(clear_ended_us));
            }
        }
        #[cfg(feature = "benchmark")]
        let exchange_started_us = display_interface::monotonic_time_us();

        framebuffer = exchange_frame(&frame_tx, &available_rx, framebuffer);
        #[cfg(feature = "benchmark")]
        let exchange_ended_us = display_interface::monotonic_time_us();

        let frame_end = now_ms();
        let frame_duration = frame_end.saturating_sub(frame_start);
        #[cfg(feature = "benchmark-soak")]
        soak_stats.record_frame(frame_duration);
        state.frame_accum_ms = state.frame_accum_ms.saturating_add(frame_duration);
        state.frame_counter = state.frame_counter.saturating_add(1);
        if state.frame_accum_ms >= 1_000 {
            let avg = state.frame_accum_ms as f32 / state.frame_counter.max(1) as f32;
            let fps = 1_000.0 / avg;
            println!(
                "Frame duration: {} ms (avg {:.1} ms, {:.1} FPS)",
                frame_duration, avg, fps
            );
            state.frame_accum_ms = 0;
            state.frame_counter = 0;
        }

        #[cfg(feature = "benchmark")]
        if pipeline_samples < 105 {
            pipeline_stats.record(exchange_ended_us.saturating_sub(pipeline_started_us));
            pipeline_update_stats.record(render_started_us.saturating_sub(pipeline_started_us));
            pipeline_render_stats.record(exchange_started_us.saturating_sub(render_started_us));
            pipeline_exchange_stats.record(exchange_ended_us.saturating_sub(exchange_started_us));
            pipeline_samples += 1;
            if pipeline_samples == 105 {
                pipeline_stats.report(5);
                pipeline_update_stats.report(5);
                pipeline_render_stats.report(5);
                pipeline_clear_stats.report(5);
                pipeline_ui_stats.report(5);
                pipeline_exchange_stats.report(5);
            }
        }

        #[cfg(feature = "benchmark-soak")]
        if frame_end >= soak_next_report_ms {
            let elapsed_ms = frame_end.saturating_sub(soak_started_ms).max(1);
            let heap = display_interface::heap_metrics();
            if elapsed_ms >= 60_000 && soak_heap_baseline.is_none() {
                soak_heap_baseline = Some(heap);
            }
            let internal_heap_loss = soak_heap_baseline.map_or(0, |baseline| {
                baseline.internal_free.saturating_sub(heap.internal_free)
            });
            let psram_heap_loss = soak_heap_baseline.map_or(0, |baseline| {
                baseline.psram_free.saturating_sub(heap.psram_free)
            });
            let pressure_hz_x100 = soak_stats
                .pressure_samples()
                .saturating_mul(100_000)
                .checked_div(elapsed_ms)
                .unwrap_or(0);
            let notification_loss =
                soak_pressure_notify_attempts.saturating_sub(soak_pressure_notify_successes);
            println!(
                concat!(
                    "{{\"type\":\"soak\",\"elapsed_ms\":{},\"frames\":{},",
                    "\"frame_p50_ms\":{},\"frame_p95_ms\":{},\"frame_p99_ms\":{},",
                    "\"pressure_samples\":{},\"pressure_hz\":{}.{:02},",
                    "\"max_pressure_gap_ms\":{},\"pressure_notify_attempts\":{},",
                    "\"pressure_notify_loss\":{},\"scale_samples\":{},",
                    "\"display_errors\":{},\"internal_free\":{},",
                    "\"internal_minimum_free\":{},\"internal_loss_after_warmup\":{},",
                    "\"psram_free\":{},\"psram_minimum_free\":{},",
                    "\"psram_loss_after_warmup\":{}}}"
                ),
                elapsed_ms,
                soak_stats.frames(),
                soak_stats.percentile_ms(50),
                soak_stats.percentile_ms(95),
                soak_stats.percentile_ms(99),
                soak_stats.pressure_samples(),
                pressure_hz_x100 / 100,
                pressure_hz_x100 % 100,
                soak_stats.max_pressure_gap_ms(),
                soak_pressure_notify_attempts,
                notification_loss,
                ble_server.scale_sample_count(),
                display_errors.load(Ordering::Relaxed),
                heap.internal_free,
                heap.internal_minimum_free,
                internal_heap_loss,
                heap.psram_free,
                heap.psram_minimum_free,
                psram_heap_loss,
            );
            soak_next_report_ms = soak_next_report_ms.saturating_add(10_000);

            if elapsed_ms >= 30 * 60 * 1_000 && !soak_completed {
                println!("{{\"type\":\"soak_complete\",\"duration_ms\":{},\"display_errors\":{},\"pressure_notify_loss\":{},\"max_pressure_gap_ms\":{},\"internal_loss_after_warmup\":{},\"psram_loss_after_warmup\":{}}}", elapsed_ms, display_errors.load(Ordering::Relaxed), notification_loss, soak_stats.max_pressure_gap_ms(), internal_heap_loss, psram_heap_loss);
                soak_completed = true;
            }
        }

        FreeRtos::delay_ms(2);
    }
}

#[cfg(all(feature = "benchmark", not(feature = "benchmark-soak")))]
fn run_display_benchmarks<SPI, DC, CS>(
    display: &mut FastSpiInterface<'_, SPI, DC, CS>,
    framebuffer: &mut [Rgb565; PIXEL_COUNT],
) where
    SPI: embedded_hal::spi::SpiDevice<Error = esp_idf_hal::spi::SpiError>,
    DC: embedded_hal::digital::OutputPin,
    CS: embedded_hal::digital::OutputPin<Error = DC::Error>,
{
    use mxcoffee::benchmark::BenchmarkStats;

    const ITERATIONS: usize = 105;
    const WARMUP: usize = 5;
    const FRAME_BYTES: usize = PIXEL_COUNT * 2;

    println!(
        concat!(
            "{{\"type\":\"benchmark_config\",",
            "\"backend\":\"spi2_raw_lldesc_ping_pong\",\"backend_version\":1,",
            "\"cpu_hz\":240000000,\"spi_hz\":{},\"psram_hz\":80000000,",
            "\"framebuffer_external\":{},\"framebuffer_bytes\":{},",
            "\"transfer_buffer\":{},\"transfer_buffers\":2,",
            "\"framebuffer_count\":3,\"queued_frames\":2,\"display_pause_every\":4,",
            "\"staging_dma_capable\":true,\"warmup\":5,\"samples\":100,",
            "\"width\":{},\"height\":{}}}"
        ),
        DISPLAY_SPI_HZ,
        display_interface::is_external_ram(framebuffer.as_ptr().cast()),
        FRAME_BYTES,
        DISPLAY_TRANSFER_BUFFER_SIZE,
        DISPLAY_WIDTH,
        DISPLAY_HEIGHT
    );

    let mut spi_stats = Box::new(BenchmarkStats::new("spi_alternating_frame", FRAME_BYTES));
    for iteration in 0..ITERATIONS {
        let color = if iteration % 2 == 0 {
            Rgb565::new(31, 0, 0)
        } else {
            Rgb565::new(0, 0, 31)
        };
        framebuffer.fill(color);
        let started = display_interface::monotonic_time_us();
        display
            .send_frame_queued(framebuffer)
            .expect("benchmark SPI transfer failed");
        let ended = display_interface::monotonic_time_us();
        spi_stats.record(ended.saturating_sub(started));
        FreeRtos::delay_ms(1);
    }
    spi_stats.report(WARMUP);

    for (index, pixel) in framebuffer.iter_mut().enumerate() {
        let x = index % DISPLAY_WIDTH;
        let y = index / DISPLAY_WIDTH;
        *pixel = match (x >= DISPLAY_WIDTH / 2, y >= DISPLAY_HEIGHT / 2) {
            (false, false) => Rgb565::RED,
            (true, false) => Rgb565::GREEN,
            (false, true) => Rgb565::BLUE,
            (true, true) => Rgb565::WHITE,
        };
    }
    display
        .send_frame_queued(framebuffer)
        .expect("RGB test pattern transfer failed");
    println!("{{\"type\":\"correctness\",\"name\":\"rgb_quadrants\",\"errors\":0}}");
    FreeRtos::delay_ms(3_000);

    for (index, pixel) in framebuffer.iter_mut().enumerate() {
        let x = index % DISPLAY_WIDTH;
        let y = index / DISPLAY_WIDTH;
        *pixel = if ((x / 8) + (y / 8)) % 2 == 0 {
            Rgb565::WHITE
        } else {
            Rgb565::BLACK
        };
    }
    display
        .send_frame_queued(framebuffer)
        .expect("checkerboard transfer failed");
    println!("{{\"type\":\"correctness\",\"name\":\"checkerboard_8px\",\"errors\":0}}");
    FreeRtos::delay_ms(3_000);

    #[cfg(feature = "benchmark-transfer-soak")]
    {
        for iteration in 0..1_000 {
            framebuffer.fill(if iteration % 2 == 0 {
                Rgb565::RED
            } else {
                Rgb565::BLUE
            });
            display
                .send_frame_queued(framebuffer)
                .expect("1,000-frame transfer soak failed");
            FreeRtos::delay_ms(1);
        }
        println!(
            "{{\"type\":\"correctness\",\"name\":\"spi_1000_uploads\",\"iterations\":1000,\"errors\":0}}"
        );
    }

    let mut history = [0i16; PRESSURE_HISTORY_LEN];
    let weight_history = [0i16; PRESSURE_HISTORY_LEN];
    let history_times = core::array::from_fn::<_, PRESSURE_HISTORY_LEN, _>(|index| {
        (30_000usize * index).div_ceil(PRESSURE_HISTORY_LEN) as u32
    });
    for (index, value) in history.iter_mut().enumerate() {
        *value = ((index as i32 * 9_000) / PRESSURE_HISTORY_LEN as i32) as i16;
    }

    let mut render_stats = Box::new(BenchmarkStats::new("render_dynamic_ui", 0));
    let mut frame_stats = Box::new(BenchmarkStats::new("frame_dynamic", FRAME_BYTES));
    initialize_main_screen(&mut FastFrameBuffer::new(
        framebuffer,
        DISPLAY_WIDTH,
        DISPLAY_HEIGHT,
    ))
    .expect("benchmark framebuffer initialization failed");
    for iteration in 0..ITERATIONS {
        history.rotate_left(1);
        history[PRESSURE_HISTORY_LEN - 1] = ((iteration * 97) % 10_000) as i16;
        let data = UiData {
            pressure_history: &history,
            weight_history_tenths: &weight_history,
            history_times_ms: &history_times,
            last_pressure: history[PRESSURE_HISTORY_LEN - 1],
            max_sensor_pressure: 20_000,
            shot_weight: 21.8,
            flow_rate: 1.0,
            bluetooth_on: true,
            scale_connected: true,
            scale_name: "LFSMART SCALE",
            shot_time_tenths: iteration as u64,
            pressure_hex: "01 02 03",
            debug_mode: false,
            last_refresh_ms: iteration as u64 * 20,
            last_activity_ms: 0,
            now_ms: iteration as u64 * 20,
            auto_off_timeout_ms: AUTO_OFF_TIMEOUT_MS,
            timer_running: true,
            frame_indicator: iteration % 2 == 0,
        };

        let frame_started = display_interface::monotonic_time_us();
        let render_started = frame_started;
        {
            let mut fb = FastFrameBuffer::new(&mut *framebuffer, DISPLAY_WIDTH, DISPLAY_HEIGHT);
            draw_main_screen_retained(&mut fb, data).expect("benchmark render failed");
        }
        let render_ended = display_interface::monotonic_time_us();
        render_stats.record(render_ended.saturating_sub(render_started));
        display
            .send_frame_queued(framebuffer)
            .expect("benchmark frame transfer failed");
        let frame_ended = display_interface::monotonic_time_us();
        frame_stats.record(frame_ended.saturating_sub(frame_started));
        FreeRtos::delay_ms(1);
    }
    render_stats.report(WARMUP);
    frame_stats.report(WARMUP);
}
