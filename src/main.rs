use mipidsi::interface::SpiInterface;
use embedded_graphics::mono_font::ascii::FONT_6X10;
use embedded_graphics::{
    mono_font::MonoTextStyle,
    pixelcolor::Rgb565,
    prelude::*,
    text::{Alignment, Text},
};
use esp_idf_hal::gpio::PinDriver;
use esp_idf_hal::prelude::*;
use esp_idf_hal::spi::{config, SpiDeviceDriver, SpiDriver, SpiDriverConfig, SPI2};
use mipidsi::{Builder, models::ILI9342CRgb565};
use mipidsi::options::{ColorOrder, ColorInversion};
use std::{thread, time::Duration};

use axp2101::{
    Axp2101,
    Aldo2,
    Regulator as _,
};

fn main() {
    let peripherals = esp_idf_hal::peripherals::Peripherals::take().unwrap();
    let gpios = peripherals.pins;

    println!("GPIOs ready");
    let pin_dc = PinDriver::output(gpios.gpio15).unwrap(); // DC = GPIO15 (M5Core2 default)
    let mut lcd_reset_pin = PinDriver::output(gpios.gpio33).unwrap(); // RESET = GPIO33
    println!("Pin drivers for DC and RESET ready");

    // --- Enable BLDO1 (LCD backlight power) via AXP192 PMIC ---
    use esp_idf_hal::i2c::{I2cDriver, I2cConfig};
    println!("About to init I2C...");
    let mut i2c = I2cDriver::new(
        peripherals.i2c0,
        gpios.gpio21,
        gpios.gpio22,
        &I2cConfig::new().baudrate(100_000.Hz()),
    ).unwrap();
    println!("I2C init done");

    // --- AXP2101 power rails setup using axp2101-rs crate ---
    println!("AXP2101: Creating driver instance...");
    let mut axp = Axp2101::new(i2c);

    let mut aldo2: Aldo2<_> = axp.into();

    // --- Set ALDO2 (LCD logic) to 3.3V (if supported) ---
    let res_aldo2_v = axp.write_register(0x28, 0xF8); // 3.3V for ALDO2 (see datasheet)
    println!("AXP2101: Set ALDO2 voltage to 3.3V (reg 0x28): {:?}", res_aldo2_v);

    // --- Set BLDO1 (backlight) to 3.0V (if supported) ---
    let res_bldo1_v = axp.write_register(0x12, 0x01); // Enable BLDO1 (bit 0)
    println!("AXP2101: Enable BLDO1 (reg 0x12, bit 0): {:?}", res_bldo1_v);

    // Optionally print status (if available)
    if let Ok(v) = axp.battery_voltage() {
        println!("AXP2101: Battery voltage: {} mV", v);
    }
    if let Ok(p) = axp.battery_percent() {
        println!("AXP2101: Battery percent: {}%", p);
    }
    println!("AXP2101 power rails setup complete (manual register writes)");

    // Initialize back light of the display
    let mut pin_lcd_blk = PinDriver::output(gpios.gpio32).unwrap(); // BL = GPIO32
    pin_lcd_blk.set_high().unwrap();
    println!("Backlight ON (GPIO32 set high)");
    // (Backlight FET enabled, but true power comes from DCDC3 above)

    // Issue LCD Reset
    lcd_reset_pin.set_low().unwrap();
    thread::sleep(Duration::from_millis(100));
    lcd_reset_pin.set_high().unwrap();
    thread::sleep(Duration::from_millis(2000));

    println!("About to init SPI driver...");
    let spi = peripherals.spi2;
    let driver = SpiDriver::new::<SPI2>(
        spi,
        gpios.gpio18,
        gpios.gpio23,
        Some(gpios.gpio19),
        &SpiDriverConfig::new(),
    )
    .unwrap();

    let spi_device_config = config::Config::new().baudrate(4.MHz().into());
    let spi_device = SpiDeviceDriver::new(driver, Some(gpios.gpio5), &spi_device_config).unwrap();
    println!("SPI device ready");

    // Initialize Display Interface
    let mut spi_buffer = [0u8; 128];
    let display_interface = SpiInterface::new(spi_device, pin_dc, &mut spi_buffer);
    println!("Display interface ready");

    // Initialize ILI9342C (RGB565) for M5Core2, matching working code
    let mut display = Builder::new(ILI9342CRgb565, display_interface)
        .display_size(320, 240)
        .color_order(ColorOrder::Bgr)
        .invert_colors(ColorInversion::Inverted)
        .reset_pin(lcd_reset_pin)
        .init(&mut esp_idf_hal::delay::FreeRtos)
        .unwrap();
    println!("Display builder/init complete");

    // Draw 5x5 rectangle at position (0, 0).
    display
        .set_pixels(
            0,
            0,
            5,
            5,
            core::iter::repeat(Rgb565::RED).take(25).into_iter(),
        )
        .unwrap();

    // Draw text at position (50, 50)
    let style = MonoTextStyle::new(&FONT_6X10, Rgb565::RED);
    Text::with_alignment(
        "Hello World!!",
        Point::new(50, 50),
        style,
        Alignment::Center,
    )
    .draw(&mut display)
    .unwrap();

    // Loop to print PING every second for debugging
    loop {
        println!("PING");
        thread::sleep(Duration::from_secs(1));
    }
}