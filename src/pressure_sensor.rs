use core::fmt::Write as _;

use esp_idf_hal::{delay::TickType, i2c::I2cDriver};
use esp_idf_sys::EspError;
use heapless::String;

const SENSOR_ADDRESS: u8 = 0x6D;
const SENSOR_DATA_REGISTER: u8 = 0x06;
const SAMPLE_COUNT: usize = 3;

pub struct PressureSensor {
    last_hex: String<16>,
    samples: [f32; SAMPLE_COUNT],
    sample_count: usize,
    next_sample: usize,
}

impl PressureSensor {
    pub fn new() -> Self {
        Self {
            last_hex: String::new(),
            samples: [0.0; SAMPLE_COUNT],
            sample_count: 0,
            next_sample: 0,
        }
    }

    pub fn read_pressure_mbar(&mut self, i2c: &mut I2cDriver<'_>) -> Result<i16, EspError> {
        let sample = self.read_sample(i2c)?;
        self.samples[self.next_sample] = sample;
        self.next_sample = (self.next_sample + 1) % SAMPLE_COUNT;
        self.sample_count = (self.sample_count + 1).min(SAMPLE_COUNT);

        let avg = self.samples[..self.sample_count]
            .iter()
            .copied()
            .sum::<f32>()
            / self.sample_count as f32;
        let mbar = (avg * 1000f32).round() as i32;

        Ok(mbar.clamp(0, 20_000) as i16)
    }

    pub fn max_pressure_mbar(&self) -> i16 {
        20_000
    }

    pub fn last_hex_payload(&self) -> &str {
        &self.last_hex
    }

    fn read_sample(&mut self, i2c: &mut I2cDriver<'_>) -> Result<f32, EspError> {
        let mut buffer = [0u8; 3];

        i2c.write_read(
            SENSOR_ADDRESS,
            &[SENSOR_DATA_REGISTER],
            &mut buffer,
            TickType::new_millis(10).ticks(),
        )?;

        self.last_hex.clear();
        write!(
            self.last_hex,
            "{:02X} {:02X} {:02X}",
            buffer[0], buffer[1], buffer[2]
        )
        .ok();

        let raw = ((buffer[0] as u32) << 16) | ((buffer[1] as u32) << 8) | buffer[2] as u32;
        let signed = if raw & 0x800000 != 0 {
            raw as i32 - 16_777_216
        } else {
            raw as i32
        };

        // Linear regression coefficients derived from original firmware
        let a = 3.9628e-6f32;
        let b = -4.9509f32;

        Ok(a * signed as f32 + b)
    }
}
