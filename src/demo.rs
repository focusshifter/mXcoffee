const PREINFUSION_RAMP_MS: u64 = 500;
const PREINFUSION_END_MS: u64 = 5_000;
const RAMP_UP_END_MS: u64 = 6_000;
const TAPER_END_MS: u64 = 31_000;
const RAMP_DOWN_END_MS: u64 = 32_000;

const PREINFUSION_MBAR: u64 = 2_000;
const PEAK_MBAR: u64 = 9_000;
const END_MBAR: u64 = 6_000;

pub fn simulated_pressure_mbar(elapsed_ms: u64) -> i16 {
    let pressure = match elapsed_ms {
        0..PREINFUSION_RAMP_MS => {
            PREINFUSION_MBAR * elapsed_ms * elapsed_ms / (PREINFUSION_RAMP_MS * PREINFUSION_RAMP_MS)
        }
        PREINFUSION_RAMP_MS..PREINFUSION_END_MS => PREINFUSION_MBAR,
        PREINFUSION_END_MS..RAMP_UP_END_MS => {
            let elapsed = elapsed_ms - PREINFUSION_END_MS;
            PREINFUSION_MBAR + (PEAK_MBAR - PREINFUSION_MBAR) * elapsed * elapsed / 1_000_000
        }
        RAMP_UP_END_MS..TAPER_END_MS => {
            let elapsed = elapsed_ms - RAMP_UP_END_MS;
            PEAK_MBAR - (PEAK_MBAR - END_MBAR) * elapsed / 25_000
        }
        TAPER_END_MS..RAMP_DOWN_END_MS => {
            let remaining = RAMP_DOWN_END_MS - elapsed_ms;
            END_MBAR * remaining * remaining / 1_000_000
        }
        _ => 0,
    };

    pressure as i16
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn follows_cpp_simulated_shot_phases() {
        assert_eq!(simulated_pressure_mbar(0), 0);
        assert_eq!(simulated_pressure_mbar(250), 500);
        assert_eq!(simulated_pressure_mbar(500), 2_000);
        assert_eq!(simulated_pressure_mbar(4_999), 2_000);
        assert_eq!(simulated_pressure_mbar(5_500), 3_750);
        assert_eq!(simulated_pressure_mbar(6_000), 9_000);
        assert_eq!(simulated_pressure_mbar(18_500), 7_500);
        assert_eq!(simulated_pressure_mbar(31_000), 6_000);
        assert_eq!(simulated_pressure_mbar(31_500), 1_500);
        assert_eq!(simulated_pressure_mbar(32_000), 0);
    }

    #[test]
    fn stays_idle_after_the_shot() {
        assert_eq!(simulated_pressure_mbar(60_000), 0);
    }
}
