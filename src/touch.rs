use esp_idf_hal::{delay::TickType, i2c::I2cDriver};
use esp_idf_sys::EspError;

const FT6336_ADDR: u8 = 0x38;
const TOUCH_STATUS_REG: u8 = 0x02;
const SCREEN_WIDTH: u16 = 320;
const SCREEN_HEIGHT: u16 = 240;
const BUTTON_ZONE_Y: u16 = 200;

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum Button {
    A,
    B,
    C,
}

#[derive(Default, Debug, Clone)]
pub struct ButtonSnapshot {
    #[allow(dead_code)] // Used by the parity implementation for long-press gestures.
    pub held: [bool; 3],
    pub pressed: [bool; 3],
}

impl ButtonSnapshot {
    pub fn any_pressed(&self) -> bool {
        self.pressed.iter().copied().any(|p| p)
    }

    pub fn is_pressed(&self, button: Button) -> bool {
        let idx = match button {
            Button::A => 0,
            Button::B => 1,
            Button::C => 2,
        };
        self.pressed[idx]
    }
}

pub struct TouchButtons {
    previous: [bool; 3],
}

impl TouchButtons {
    pub const fn new() -> Self {
        Self {
            previous: [false; 3],
        }
    }

    pub fn poll(&mut self, i2c: &mut I2cDriver<'_>) -> Result<ButtonSnapshot, EspError> {
        let mut buffer = [0u8; 11];

        if i2c
            .write_read(
                FT6336_ADDR,
                &[TOUCH_STATUS_REG],
                &mut buffer,
                TickType::new_millis(10).ticks(),
            )
            .is_err()
        {
            return Ok(ButtonSnapshot::default());
        }

        let touch_points = buffer[0] & 0x0F;

        let mut current = [false; 3];

        if touch_points > 0 {
            for point in 0..touch_points.min(2) {
                let base = if point == 0 { 1 } else { 7 };
                if base + 3 >= buffer.len() {
                    break;
                }

                let x = (((buffer[base] & 0x0F) as u16) << 8) | buffer[base + 1] as u16;
                let y = (((buffer[base + 2] & 0x0F) as u16) << 8) | buffer[base + 3] as u16;

                let (x, y) = transform_coordinates(x, y);

                if (BUTTON_ZONE_Y..=SCREEN_HEIGHT).contains(&y) {
                    let segment = SCREEN_WIDTH / 3;
                    if x < segment {
                        current[0] = true;
                    } else if x < segment * 2 {
                        current[1] = true;
                    } else {
                        current[2] = true;
                    }
                }
            }
        }

        let mut snapshot = ButtonSnapshot {
            held: current,
            ..ButtonSnapshot::default()
        };

        for (idx, (&now, &was)) in current.iter().zip(self.previous.iter()).enumerate() {
            snapshot.pressed[idx] = now && !was;
        }

        self.previous = current;

        Ok(snapshot)
    }
}

fn transform_coordinates(x: u16, y: u16) -> (u16, u16) {
    // The FT6336 reports coordinates in portrait mode with 0,0 at top-left.
    // The display is used in landscape mode (rotated 90 degrees clockwise).
    // Transform to match the display orientation used by the UI code.
    let clamped_x = x.min(SCREEN_HEIGHT - 1);
    let clamped_y = y.min(SCREEN_WIDTH - 1);

    // Rotate 90 degrees clockwise and mirror to match Core2 default orientation.
    let transformed_x = SCREEN_WIDTH - 1 - clamped_y;
    let transformed_y = clamped_x;

    (transformed_x, transformed_y)
}
