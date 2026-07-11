pub const DEVICE_NAME: &str = "PRS-mXcoffee";
pub const BATTERY_SERVICE_UUID: u16 = 0x180f;
pub const BATTERY_LEVEL_CHARACTERISTIC_UUID: u16 = 0x2a19;
pub const LOG_SERVICE_UUID: &str = "873ae828-4c5a-4342-b539-9d900bf7ebd0";
pub const LOG_CHARACTERISTIC_UUID: &str = "873ae829-4c5a-4342-b539-9d900bf7ebd0";
pub const PRESSURE_SERVICE_UUID: &str = "873ae82a-4c5a-4342-b539-9d900bf7ebd0";
pub const PRESSURE_CHARACTERISTIC_UUID: &str = "873ae82b-4c5a-4342-b539-9d900bf7ebd0";
pub const PRESSURE_ZERO_CHARACTERISTIC_UUID: &str = "873ae82c-4c5a-4342-b539-9d900bf7ebd0";

pub fn encode_pressure(raw_pressure: i16, zero_offset: i16) -> [u8; 2] {
    raw_pressure.wrapping_sub(zero_offset).to_be_bytes()
}

pub fn clamp_battery_level(level: i32) -> u8 {
    level.clamp(0, 100) as u8
}

#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub struct PressureState {
    last_pressure: i16,
    zero_offset: i16,
}

impl PressureState {
    pub fn update(&mut self, pressure: i16) {
        self.last_pressure = pressure;
    }

    pub fn zero(&mut self) {
        self.zero_offset = self.last_pressure;
    }

    pub fn encoded(&self) -> [u8; 2] {
        encode_pressure(self.last_pressure, self.zero_offset)
    }

    pub fn zero_offset(&self) -> i16 {
        self.zero_offset
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn encodes_signed_pressure_as_big_endian() {
        assert_eq!(encode_pressure(9_000, 0), [0x23, 0x28]);
        assert_eq!(encode_pressure(-1_000, 0), [0xfc, 0x18]);
    }

    #[test]
    fn zero_offsets_following_readings() {
        let mut state = PressureState::default();
        state.update(10_123);
        state.zero();
        state.update(10_200);
        assert_eq!(state.zero_offset(), 10_123);
        assert_eq!(state.encoded(), [0x00, 0x4d]);
    }

    #[test]
    fn battery_is_clamped_to_gatt_range() {
        assert_eq!(clamp_battery_level(-1), 0);
        assert_eq!(clamp_battery_level(42), 42);
        assert_eq!(clamp_battery_level(101), 100);
    }

    #[test]
    fn public_contract_is_stable() {
        assert_eq!(DEVICE_NAME, "PRS-mXcoffee");
        assert_eq!(BATTERY_SERVICE_UUID, 0x180f);
        assert_eq!(BATTERY_LEVEL_CHARACTERISTIC_UUID, 0x2a19);
        assert_eq!(LOG_SERVICE_UUID, "873ae828-4c5a-4342-b539-9d900bf7ebd0");
        assert_eq!(
            LOG_CHARACTERISTIC_UUID,
            "873ae829-4c5a-4342-b539-9d900bf7ebd0"
        );
        assert_eq!(
            PRESSURE_SERVICE_UUID,
            "873ae82a-4c5a-4342-b539-9d900bf7ebd0"
        );
        assert_eq!(
            PRESSURE_CHARACTERISTIC_UUID,
            "873ae82b-4c5a-4342-b539-9d900bf7ebd0"
        );
        assert_eq!(
            PRESSURE_ZERO_CHARACTERISTIC_UUID,
            "873ae82c-4c5a-4342-b539-9d900bf7ebd0"
        );
    }
}
