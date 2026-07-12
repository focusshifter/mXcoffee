const MAX_SAMPLES: usize = 120;

pub struct BenchmarkStats {
    name: &'static str,
    samples_us: [u64; MAX_SAMPLES],
    len: usize,
    bytes_per_iteration: usize,
}

impl BenchmarkStats {
    pub const fn new(name: &'static str, bytes_per_iteration: usize) -> Self {
        Self {
            name,
            samples_us: [0; MAX_SAMPLES],
            len: 0,
            bytes_per_iteration,
        }
    }

    pub fn record(&mut self, duration_us: u64) {
        if self.len < MAX_SAMPLES {
            self.samples_us[self.len] = duration_us;
            self.len += 1;
        }
    }

    pub fn report(&self, warmup: usize) {
        let start = warmup.min(self.len);
        let count = self.len.saturating_sub(start);
        if count == 0 {
            return;
        }

        let mut sorted = [0u64; MAX_SAMPLES];
        sorted[..count].copy_from_slice(&self.samples_us[start..self.len]);
        sorted[..count].sort_unstable();

        let min = sorted[0];
        let median = percentile(&sorted[..count], 50);
        let p95 = percentile(&sorted[..count], 95);
        let p99 = percentile(&sorted[..count], 99);
        let max = sorted[count - 1];
        let total: u64 = sorted[..count].iter().sum();
        let mean = total / count as u64;
        let fps_x100 = if median == 0 { 0 } else { 100_000_000 / median };
        let mib_per_sec_x100 = if median == 0 || self.bytes_per_iteration == 0 {
            0
        } else {
            (self.bytes_per_iteration as u64 * 100_000_000) / (median * 1024 * 1024)
        };

        println!(
            concat!(
                "{{\"type\":\"benchmark\",\"name\":\"{}\",",
                "\"samples\":{},\"warmup\":{},\"bytes_per_iteration\":{},",
                "\"min_us\":{},\"median_us\":{},\"mean_us\":{},",
                "\"p95_us\":{},\"p99_us\":{},\"max_us\":{},",
                "\"fps\":{}.{:02},\"mib_per_sec\":{}.{:02},\"errors\":0}}"
            ),
            self.name,
            count,
            start,
            self.bytes_per_iteration,
            min,
            median,
            mean,
            p95,
            p99,
            max,
            fps_x100 / 100,
            fps_x100 % 100,
            mib_per_sec_x100 / 100,
            mib_per_sec_x100 % 100,
        );
    }
}

fn percentile(sorted: &[u64], percentile: usize) -> u64 {
    let index = ((sorted.len() - 1) * percentile).div_ceil(100);
    sorted[index]
}

const FRAME_HISTOGRAM_BINS: usize = 128;

pub struct LongRunStats {
    frame_histogram: [u16; FRAME_HISTOGRAM_BINS],
    frames: u64,
    pressure_samples: u64,
    last_pressure_sample_ms: Option<u64>,
    max_pressure_gap_ms: u64,
}

impl LongRunStats {
    pub const fn new() -> Self {
        Self {
            frame_histogram: [0; FRAME_HISTOGRAM_BINS],
            frames: 0,
            pressure_samples: 0,
            last_pressure_sample_ms: None,
            max_pressure_gap_ms: 0,
        }
    }

    pub fn record_frame(&mut self, duration_ms: u64) {
        let bin = duration_ms.min((FRAME_HISTOGRAM_BINS - 1) as u64) as usize;
        self.frame_histogram[bin] = self.frame_histogram[bin].saturating_add(1);
        self.frames = self.frames.saturating_add(1);
    }

    pub fn record_pressure_sample(&mut self, now_ms: u64) {
        if let Some(last) = self.last_pressure_sample_ms {
            self.max_pressure_gap_ms = self.max_pressure_gap_ms.max(now_ms.saturating_sub(last));
        }
        self.last_pressure_sample_ms = Some(now_ms);
        self.pressure_samples = self.pressure_samples.saturating_add(1);
    }

    pub fn frames(&self) -> u64 {
        self.frames
    }

    pub fn pressure_samples(&self) -> u64 {
        self.pressure_samples
    }

    pub fn max_pressure_gap_ms(&self) -> u64 {
        self.max_pressure_gap_ms
    }

    pub fn percentile_ms(&self, percentile: u32) -> u16 {
        if self.frames == 0 {
            return 0;
        }
        let target = self
            .frames
            .saturating_mul(u64::from(percentile))
            .div_ceil(100);
        let mut cumulative = 0u64;
        for (duration, count) in self.frame_histogram.iter().enumerate() {
            cumulative = cumulative.saturating_add(u64::from(*count));
            if cumulative >= target {
                return duration as u16;
            }
        }
        (FRAME_HISTOGRAM_BINS - 1) as u16
    }
}

impl Default for LongRunStats {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn long_run_histogram_reports_frame_percentiles() {
        let mut stats = LongRunStats::new();
        for duration in 1..=100 {
            stats.record_frame(duration);
        }
        assert_eq!(stats.percentile_ms(50), 50);
        assert_eq!(stats.percentile_ms(95), 95);
        assert_eq!(stats.percentile_ms(99), 99);
    }

    #[test]
    fn long_run_stats_track_pressure_gaps() {
        let mut stats = LongRunStats::new();
        for timestamp in [100, 150, 205, 255] {
            stats.record_pressure_sample(timestamp);
        }
        assert_eq!(stats.pressure_samples(), 4);
        assert_eq!(stats.max_pressure_gap_ms(), 55);
    }
}
