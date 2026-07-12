#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
cpp_reference=${1:-/home/vs/Documents/mxcoffee/screenshots/reference.bmp}
rust_reference="$repo_root/benchmarks/screenshots/rust-reference.bmp"

mkdir -p "$(dirname "$rust_reference")"
cargo run --quiet --manifest-path "$repo_root/Cargo.toml" \
  --target x86_64-unknown-linux-gnu --example render_ui_reference -- "$rust_reference"

if [[ ! -f "$cpp_reference" ]]; then
  printf 'C++ reference not found: %s\n' "$cpp_reference" >&2
  exit 2
fi

metric=$(compare -metric RMSE "$cpp_reference" "$rust_reference" null: 2>&1 || true)
printf 'C++ reference: %s\nRust reference: %s\nRMSE: %s\n' \
  "$cpp_reference" "$rust_reference" "$metric"

if [[ "$metric" != "0 (0)" ]]; then
  printf 'UI reference differs from C++ oracle\n' >&2
  exit 1
fi
