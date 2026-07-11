const PREINFUSION_RAMP_MS: u64 = 500;
const PREINFUSION_END_MS: u64 = 5_000;
const RAMP_UP_END_MS: u64 = 6_000;
const TAPER_END_MS: u64 = 31_000;
const RAMP_DOWN_END_MS: u64 = 32_000;

const PREINFUSION_MBAR: u64 = 2_000;
const PEAK_MBAR: u64 = 9_000;
const END_MBAR: u64 = 6_000;
const FLOW_SCALE: f32 = 0.1995;

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

#[derive(Debug, Default)]
pub struct SimulatedScale {
    weight_grams: f32,
    last_elapsed_ms: Option<u64>,
}

impl SimulatedScale {
    pub fn update(&mut self, elapsed_ms: u64) -> f32 {
        let delta_ms = self
            .last_elapsed_ms
            .map_or(0, |last| elapsed_ms.saturating_sub(last));
        self.last_elapsed_ms = Some(elapsed_ms);
        self.weight_grams = (self.weight_grams
            + simulated_flow_grams_per_second(elapsed_ms) * delta_ms as f32 / 1_000.0)
            .min(40.0);
        self.weight_grams
    }
}

pub fn simulated_flow_grams_per_second(elapsed_ms: u64) -> f32 {
    let elapsed = elapsed_ms as f32 / 1_000.0;
    if elapsed < 6.0 {
        let preinfusion_flow = 2.0 / 6.0;
        if elapsed < 0.5 {
            preinfusion_flow * (elapsed / 0.5).powi(2)
        } else {
            preinfusion_flow
        }
    } else if elapsed < 7.0 {
        let progress = elapsed - 6.0;
        (2.0 + 7.0 * progress.powi(2)) * FLOW_SCALE
    } else if elapsed < 32.0 {
        let progress = (elapsed - 7.0) / 25.0;
        (9.0 - 3.0 * progress) * FLOW_SCALE
    } else if elapsed < 33.0 {
        let progress = elapsed - 32.0;
        let eased = 1.0 - (1.0 - progress).powi(2);
        (6.0 - 6.0 * eased) * FLOW_SCALE
    } else {
        0.0
    }
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

    #[test]
    fn scale_flow_follows_cpp_shot_profile() {
        assert_eq!(simulated_flow_grams_per_second(0), 0.0);
        assert!((simulated_flow_grams_per_second(500) - 1.0 / 3.0).abs() < 0.001);
        assert!((simulated_flow_grams_per_second(7_000) - 1.7955).abs() < 0.001);
        assert!((simulated_flow_grams_per_second(32_000) - 1.197).abs() < 0.001);
        assert_eq!(simulated_flow_grams_per_second(33_000), 0.0);
    }

    #[test]
    fn scale_integrates_weight_and_stops_after_shot() {
        let mut scale = SimulatedScale::default();
        for elapsed in (0..=40_000).step_by(20) {
            scale.update(elapsed);
        }
        let finished = scale.update(60_000);
        assert!(finished > 35.0 && finished <= 40.0);
        assert_eq!(scale.update(61_000), finished);
    }
}
