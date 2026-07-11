#!/usr/bin/env bash
set -euo pipefail

result_file=${1:?usage: check-display-benchmark.sh RESULTS.jsonl}
command -v jq >/dev/null || {
  echo "jq is required" >&2
  exit 2
}

upload=$(jq -sc 'map(select(.type == "benchmark" and .name == "spi_alternating_frame")) | last' "$result_file")
config=$(jq -sc 'map(select(.type == "benchmark_config")) | last' "$result_file")
if [[ $upload == null ]]; then
  echo "missing spi_alternating_frame result" >&2
  exit 1
fi

jq -e '.accepted == true and .visual_status != null' <<<"$config" >/dev/null || {
  echo "benchmark is not accepted and visually verified" >&2
  exit 1
}

for pattern in rgb_quadrants checkerboard_8px; do
  jq -se --arg pattern "$pattern" '
    map(select(.type == "correctness" and .name == $pattern)) |
    last | .errors == 0 and .visual_status == "user_confirmed_correct"
  ' "$result_file" >/dev/null || {
    echo "missing or unverified correctness result: $pattern" >&2
    exit 1
  }
done

jq -e '
  .samples >= 100 and
  .warmup >= 5 and
  .median_us <= 35000 and
  .p95_us <= 38000 and
  .p99_us <= 42000 and
  .fps >= 28 and
  .errors == 0
' <<<"$upload" >/dev/null || {
  jq '{name, samples, warmup, median_us, p95_us, p99_us, fps, errors}' <<<"$upload" >&2
  exit 1
}

echo "display benchmark passed"
