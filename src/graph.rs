pub const DEFAULT_WINDOW_MS: u32 = 30_000;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct GraphPoint {
    pub time_ms: u32,
    pub value: i16,
}

pub fn compress_history(
    values: &[i16],
    times: &[u32],
    bucket_count: usize,
    minimum_window_ms: u32,
) -> Vec<GraphPoint> {
    let count = values.len().min(times.len());
    if count < 2 || bucket_count < 2 {
        return Vec::new();
    }

    let last_time = times[count - 1];
    let render_window_ms = minimum_window_ms.max(last_time).max(1);
    let mut bucket_min = vec![i16::MAX; bucket_count];
    let mut bucket_max = vec![i16::MIN; bucket_count];
    let mut bucket_min_time = vec![0u32; bucket_count];
    let mut bucket_max_time = vec![0u32; bucket_count];
    let mut bucket_counts = vec![0u32; bucket_count];

    for index in 0..count {
        let bucket = ((times[index] as u64 * bucket_count as u64) / render_window_ms as u64)
            .min((bucket_count - 1) as u64) as usize;
        let value = values[index];
        if value < bucket_min[bucket] {
            bucket_min[bucket] = value;
            bucket_min_time[bucket] = times[index];
        }
        if value > bucket_max[bucket] {
            bucket_max[bucket] = value;
            bucket_max_time[bucket] = times[index];
        }
        bucket_counts[bucket] += 1;
    }

    let last_bucket = ((last_time as u64 * bucket_count as u64) / render_window_ms as u64)
        .min((bucket_count - 1) as u64) as usize;
    let mut points = Vec::with_capacity((last_bucket + 1) * 2);

    for bucket in 0..=last_bucket {
        let bucket_time =
            (render_window_ms as u64 * bucket as u64 / (bucket_count - 1) as u64) as u32;
        if bucket_counts[bucket] == 0 {
            points.push(GraphPoint {
                time_ms: bucket_time,
                value: 0,
            });
        } else if bucket_min_time[bucket] <= bucket_max_time[bucket] {
            points.push(GraphPoint {
                time_ms: bucket_min_time[bucket],
                value: bucket_min[bucket],
            });
            points.push(GraphPoint {
                time_ms: bucket_max_time[bucket],
                value: bucket_max[bucket],
            });
        } else {
            points.push(GraphPoint {
                time_ms: bucket_max_time[bucket],
                value: bucket_max[bucket],
            });
            points.push(GraphPoint {
                time_ms: bucket_min_time[bucket],
                value: bucket_min[bucket],
            });
        }
    }

    points
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn requires_two_aligned_samples() {
        assert!(compress_history(&[1], &[0], 160, DEFAULT_WINDOW_MS).is_empty());
        assert!(compress_history(&[1, 2], &[0], 160, DEFAULT_WINDOW_MS).is_empty());
    }

    #[test]
    fn preserves_minimum_before_maximum_within_bucket() {
        let points = compress_history(&[5, 1, 9], &[0, 1, 2], 2, DEFAULT_WINDOW_MS);
        assert_eq!(
            points[0],
            GraphPoint {
                time_ms: 1,
                value: 1
            }
        );
        assert_eq!(
            points[1],
            GraphPoint {
                time_ms: 2,
                value: 9
            }
        );
    }

    #[test]
    fn preserves_temporal_order_when_maximum_arrives_first() {
        let points = compress_history(&[5, 9, 1], &[0, 1, 2], 2, DEFAULT_WINDOW_MS);
        assert_eq!(
            points[0],
            GraphPoint {
                time_ms: 1,
                value: 9
            }
        );
        assert_eq!(
            points[1],
            GraphPoint {
                time_ms: 2,
                value: 1
            }
        );
    }

    #[test]
    fn expands_render_window_for_long_shots() {
        let points = compress_history(&[0, 8_000, 0], &[0, 30_000, 60_000], 3, DEFAULT_WINDOW_MS);
        assert_eq!(points.last().unwrap().time_ms, 60_000);
    }

    #[test]
    fn inserts_zeroes_for_sparse_buckets_like_cpp_renderer() {
        let points = compress_history(&[1, 2], &[0, 30_000], 4, DEFAULT_WINDOW_MS);
        assert!(points.contains(&GraphPoint {
            time_ms: 10_000,
            value: 0
        }));
        assert!(points.contains(&GraphPoint {
            time_ms: 20_000,
            value: 0
        }));
    }
}
