use mipidsi::interface::SpiInterface;
use embedded_graphics::mono_font::ascii::FONT_6X10;
use embedded_graphics::{
    mono_font::MonoTextStyle,
    pixelcolor::Rgb565,
    prelude::*,
    text::{Alignment, Text},
};
use embedded_hal::i2c::I2c;
use esp_idf_hal::gpio::PinDriver;
use esp_idf_hal::prelude::*;
use esp_idf_hal::spi::{config, SpiDeviceDriver, SpiDriver, SpiDriverConfig, SPI2};
use esp_idf_sys::EspError;
use mipidsi::{Builder, models::ILI9342CRgb565};
use mipidsi::options::{ColorOrder, ColorInversion};
use std::{thread, time::Duration};

use esp_idf_hal::i2c::{I2cDriver, I2cConfig};

const AXP2101_ADDR: u8 = 0x34;

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
        I2c::write(i2c, AXP2101_ADDR, &[reg, value]).map_err(|err| err.cause())
    }

    fn modify_reg(i2c: &mut I2cDriver<'_>, reg: u8, set_mask: u8) -> Result<(), EspError> {
        let mut current = [0u8; 1];
        I2c::write_read(i2c, AXP2101_ADDR, &[reg], &mut current).map_err(|err| err.cause())?;
        let value = current[0] | set_mask;
        I2c::write(i2c, AXP2101_ADDR, &[reg, value]).map_err(|err| err.cause())
    }

    fn ldo_voltage_to_reg(voltage_mv: u16) -> u8 {
        let clamped = voltage_mv.clamp(500, 3500);
        ((clamped - 500) / 100) as u8
    }

    fn dcdc1_voltage_to_reg(voltage_mv: u16) -> u8 {
        let clamped = voltage_mv.clamp(1500, 3400);
        ((clamped - 1500) / 100) as u8
    }

    fn dcdc3_voltage_to_reg(voltage_mv: u16) -> u8 {
        let clamped = voltage_mv.clamp(1500, 3400);
        if clamped <= 1540 {
            (((clamped - 1220) / 20) as u8) + 0b0100_0111
        } else {
            (((clamped - 1600) / 100) as u8) + 0b0101_1000
        }
    }

    write_reg(i2c, REG_DCDC1_VOLTAGE, dcdc1_voltage_to_reg(3300))?;
    write_reg(i2c, REG_DCDC3_VOLTAGE, dcdc3_voltage_to_reg(3300))?;
    modify_reg(i2c, REG_DCDC_CTRL, DCDC1_MASK | DCDC3_MASK)?;

    write_reg(i2c, REG_ALDO2_VOLTAGE, ldo_voltage_to_reg(3300))?;
    write_reg(i2c, REG_ALDO4_VOLTAGE, ldo_voltage_to_reg(3300))?;
    write_reg(i2c, REG_BLDO1_VOLTAGE, ldo_voltage_to_reg(3300))?;
    modify_reg(i2c, REG_LDO_CTRL, ALDO2_MASK | ALDO4_MASK | BLDO1_MASK)?;

    Ok(())
}

fn main() {
    let peripherals = esp_idf_hal::peripherals::Peripherals::take().unwrap();
    let gpios = peripherals.pins;

    println!("GPIOs ready");
    let pin_dc = PinDriver::output(gpios.gpio15).unwrap(); // DC = GPIO15 (M5Core2 default)
    let mut lcd_reset_pin = PinDriver::output(gpios.gpio33).unwrap(); // RESET = GPIO33
    println!("Pin drivers for DC and RESET ready");

    println!("About to init I2C...");
    let mut i2c = I2cDriver::new(
        peripherals.i2c0,
        gpios.gpio21,
        gpios.gpio22,
        &I2cConfig::new().baudrate(400_000.Hz()),
    ).unwrap();
    println!("I2C init done");

    println!("Configuring AXP2101 power rails...");
    configure_core2_power(&mut i2c).expect("Failed to configure AXP2101");
    println!("AXP2101 rails configured");


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