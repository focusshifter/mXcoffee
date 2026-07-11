pub const SHOT_PRESSURE_THRESHOLD_MBAR: i16 = 1_000;
pub const FLOW_UPDATE_INTERVAL_MS: u64 = 200;
pub const FLOW_WINDOW_MS: u64 = 1_000;
pub const SCALE_STALE_TIMEOUT_MS: u64 = 5_000;

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct SessionState {
    pub timer_running: bool,
    pub timer_start_ms: u64,
    pub shot_start_ms: Option<u64>,
    pub shot_total_ms: u64,
    pub shot_weight: f32,
    pub last_shot_weight: f32,
    pub flow_rate: f32,
    pub last_weight_update_ms: Option<u64>,
    pub last_scale_sample_ms: Option<u64>,
    pub flow_window_start_ms: Option<u64>,
    pub flow_window_start_weight: f32,
}

impl Default for SessionState {
    fn default() -> Self {
        Self {
            timer_running: false,
            timer_start_ms: 0,
            shot_start_ms: None,
            shot_total_ms: 0,
            shot_weight: 0.0,
            last_shot_weight: 0.0,
            flow_rate: 0.0,
            last_weight_update_ms: None,
            last_scale_sample_ms: None,
            flow_window_start_ms: None,
            flow_window_start_weight: 0.0,
        }
    }
}

impl SessionState {
    pub fn current_shot_duration_ms(&self, now_ms: u64) -> u64 {
        if self.timer_running {
            self.shot_total_ms
                .saturating_add(now_ms.saturating_sub(self.timer_start_ms))
        } else {
            self.shot_total_ms
        }
    }

    pub fn update_pressure(&mut self, pressure_mbar: i16, now_ms: u64) {
        if pressure_mbar > SHOT_PRESSURE_THRESHOLD_MBAR {
            if !self.timer_running {
                self.timer_running = true;
                self.timer_start_ms = now_ms;
                self.shot_start_ms.get_or_insert(now_ms);
            } else {
                self.accumulate_timer(now_ms);
            }
        } else if self.timer_running {
            self.accumulate_timer(now_ms);
            self.timer_running = false;
        }
    }

    pub fn update_scale(&mut self, connected: bool, weight: f32, now_ms: u64) {
        if connected {
            self.last_scale_sample_ms = Some(now_ms);
            match self.last_weight_update_ms {
                None => {
                    self.shot_weight = weight;
                    self.last_shot_weight = weight;
                    self.last_weight_update_ms = Some(now_ms);
                    self.flow_window_start_ms = Some(now_ms);
                    self.flow_window_start_weight = weight;
                }
                Some(last_update) => {
                    if now_ms.saturating_sub(last_update) >= FLOW_UPDATE_INTERVAL_MS {
                        self.last_shot_weight = self.shot_weight;
                        self.shot_weight = weight;
                        self.last_weight_update_ms = Some(now_ms);
                    }

                    if let Some(window_start) = self.flow_window_start_ms {
                        let elapsed_ms = now_ms.saturating_sub(window_start);
                        if elapsed_ms >= FLOW_WINDOW_MS {
                            self.flow_rate = (self.shot_weight - self.flow_window_start_weight)
                                / (elapsed_ms as f32 / 1_000.0);
                            self.flow_window_start_ms = Some(now_ms);
                            self.flow_window_start_weight = self.shot_weight;
                        }
                    }
                }
            }
        } else {
            self.flow_rate = 0.0;
            if !self.scale_visible(now_ms) {
                self.clear_scale_values();
            }
        }
    }

    pub fn scale_visible(&self, now_ms: u64) -> bool {
        self.last_scale_sample_ms
            .is_some_and(|last| now_ms.saturating_sub(last) <= SCALE_STALE_TIMEOUT_MS)
    }

    fn accumulate_timer(&mut self, now_ms: u64) {
        self.shot_total_ms = self
            .shot_total_ms
            .saturating_add(now_ms.saturating_sub(self.timer_start_ms));
        self.timer_start_ms = now_ms;
    }

    fn clear_scale_values(&mut self) {
        self.shot_weight = 0.0;
        self.last_shot_weight = 0.0;
        self.last_weight_update_ms = None;
        self.last_scale_sample_ms = None;
        self.flow_window_start_ms = None;
        self.flow_window_start_weight = 0.0;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pressure_starts_accumulates_and_stops_timer() {
        let mut state = SessionState::default();
        state.update_pressure(1_001, 100);
        state.update_pressure(8_000, 350);
        state.update_pressure(0, 600);
        assert!(!state.timer_running);
        assert_eq!(state.shot_start_ms, Some(100));
        assert_eq!(state.shot_total_ms, 500);
    }

    #[test]
    fn threshold_itself_does_not_start_timer() {
        let mut state = SessionState::default();
        state.update_pressure(SHOT_PRESSURE_THRESHOLD_MBAR, 100);
        assert!(!state.timer_running);
    }

    #[test]
    fn reports_in_progress_duration_without_mutating_accumulator() {
        let mut state = SessionState::default();
        state.update_pressure(8_000, 100);
        assert_eq!(state.current_shot_duration_ms(350), 250);
        assert_eq!(state.shot_total_ms, 0);
    }

    #[test]
    fn computes_flow_over_one_second_window() {
        let mut state = SessionState::default();
        state.update_scale(true, 10.0, 0);
        state.update_scale(true, 10.8, 200);
        state.update_scale(true, 12.0, 1_000);
        assert_eq!(state.shot_weight, 12.0);
        assert!((state.flow_rate - 2.0).abs() < 0.001);
    }

    #[test]
    fn preserves_weight_during_stale_grace_then_clears_it() {
        let mut state = SessionState::default();
        state.update_scale(true, 21.8, 100);
        state.update_scale(false, 0.0, 5_100);
        assert_eq!(state.shot_weight, 21.8);
        assert!(state.scale_visible(5_100));

        state.update_scale(false, 0.0, 5_101);
        assert_eq!(state.shot_weight, 0.0);
        assert!(!state.scale_visible(5_101));
    }
}
