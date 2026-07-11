# Vendored M5 display components

These ESP-IDF components provide the M5-optimized graphics backend used by the
Rust `m5unified` wrapper.

- M5Unified 0.2.18, commit `b1ffcc677014ed8bd01e5a1f240736ae654bfe12`
- M5GFX 0.2.25, commit `ad9b814264d4e2000e9f30070002310bbccaffc9`
- `m5unified-rs` shim copied from `m5unified-sys` 0.3.8

The sources are vendored because the ESP Component Registry package for
M5Unified 0.2.18 resolves its M5GFX dependency with the wrong component-name
case when consumed through `esp-idf-sys` extra components.

Optional CJK font tables, examples, and documentation are omitted. The local
M5GFX patch keeps those font symbols behind `LGFX_ENABLE_MULTILINGUAL_FONTS`.
