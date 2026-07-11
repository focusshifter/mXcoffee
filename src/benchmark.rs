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
