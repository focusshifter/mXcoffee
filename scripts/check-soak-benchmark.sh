#!/usr/bin/env bash
set -euo pipefail

result_file=${1:?usage: check-soak-benchmark.sh RESULTS.jsonl}
command -v jq >/dev/null || {
  echo "jq is required" >&2
  exit 2
}

last_soak=$(jq -sc 'map(select(.type == "soak")) | last' "$result_file")
completion=$(jq -sc 'map(select(.type == "soak_complete")) | last' "$result_file")

if [[ $last_soak == null || $completion == null ]]; then
  echo "missing 30-minute soak telemetry or completion record" >&2
  exit 1
fi

jq -e '
  .elapsed_ms >= 1800000 and
  .frames * 1000 / .elapsed_ms >= 25 and
  .pressure_hz >= 20 and
  .max_pressure_gap_ms <= 100 and
  .pressure_notify_loss == 0 and
  .display_errors == 0 and
  .internal_loss_after_warmup <= 2048 and
  .psram_loss_after_warmup <= 2048
' <<<"$last_soak" >/dev/null || {
  echo "soak telemetry failed" >&2
  jq '{elapsed_ms, frames, pressure_hz, max_pressure_gap_ms, pressure_notify_loss, display_errors, internal_loss_after_warmup, psram_loss_after_warmup}' <<<"$last_soak" >&2
  exit 1
}

jq -e '
  .duration_ms >= 1800000 and
  .display_errors == 0 and
  .pressure_notify_loss == 0 and
  .max_pressure_gap_ms <= 100 and
  .internal_loss_after_warmup <= 2048 and
  .psram_loss_after_warmup <= 2048
' <<<"$completion" >/dev/null || {
  echo "soak completion record failed" >&2
  jq . <<<"$completion" >&2
  exit 1
}

echo "soak benchmark passed"
