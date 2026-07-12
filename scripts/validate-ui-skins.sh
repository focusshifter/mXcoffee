#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
target=x86_64-unknown-linux-gnu
classic=$(mktemp --suffix=.bmp)
workshop=$(mktemp --suffix=.bmp)
alchemy=$(mktemp --suffix=.bmp)
nge=$(mktemp --suffix=.bmp)
trap 'rm -f "$classic" "$workshop" "$alchemy" "$nge"' EXIT

cd "$repo_root"
cargo +stable test --lib --target "$target" 'ui::tests::skin_'
cargo +stable test --lib --target "$target" \
  'ui::tests::switching_skins_reinitializes_all_static_pixels'
cargo +stable run --quiet --release --target "$target" \
  --example render_ui_reference -- "$classic"
cargo +stable run --quiet --release --target "$target" \
  --example render_ui_reference -- "$workshop" --workshop --aa
cargo +stable run --quiet --release --target "$target" \
  --example render_ui_reference -- "$alchemy" --alchemy --aa
cargo +stable run --quiet --release --target "$target" \
  --example render_ui_reference -- "$nge" --nge --aa

comparison=$(compare -metric RMSE \
  benchmarks/screenshots/rust-reference.bmp "$classic" null: 2>&1 || true)
if [[ $comparison != "0 (0)" ]]; then
  echo "classic skin changed: RMSE $comparison" >&2
  exit 1
fi

echo "classic_sha256=$(sha256sum "$classic" | cut -d' ' -f1)"
echo "workshop_sha256=$(sha256sum "$workshop" | cut -d' ' -f1)"
echo "alchemy_sha256=$(sha256sum "$alchemy" | cut -d' ' -f1)"
echo "nge_sha256=$(sha256sum "$nge" | cut -d' ' -f1)"
echo "skin validation passed"
