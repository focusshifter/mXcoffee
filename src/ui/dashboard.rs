use core::fmt::Write as _;

use embedded_graphics::mono_font::ascii::FONT_10X20;
use embedded_graphics::mono_font::MonoTextStyle;
use embedded_graphics::pixelcolor::Rgb565;
use embedded_graphics::prelude::*;
use embedded_graphics::primitives::PrimitiveStyle;
use embedded_graphics::text::{Alignment, Text};
use heapless::String;

use crate::vlw::VlwFont;

use super::skin::ChromeStyle;
use super::{Skin, UiData, HEIGHT, WIDTH};

pub(super) fn draw_panel_frames<T>(target: &mut T, skin: &Skin) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    let font = VlwFont::new(skin.assets.fonts.small).unwrap();
    for (panel, header) in skin.layout.panels.iter().zip(skin.panel_headers) {
        if matches!(skin.chrome, ChromeStyle::Flat) {
            panel
                .frame
                .rectangle()
                .into_styled(PrimitiveStyle::with_fill(skin.theme.panel.render))
                .draw(target)?;
            panel
                .inner
                .rectangle()
                .into_styled(PrimitiveStyle::with_fill(skin.theme.panel_inner.render))
                .draw(target)?;
        }
        if !matches!(skin.chrome, ChromeStyle::AuthoredUnderlay(_)) {
            font.draw(
                target,
                header,
                panel.header,
                skin.theme.panel_label.source,
                match skin.chrome {
                    ChromeStyle::Flat => skin.theme.panel.source,
                    ChromeStyle::Alchemy(_) | ChromeStyle::AuthoredUnderlay(_) => {
                        skin.theme.screen.source
                    }
                },
            )?;
        }
    }
    Ok(())
}

pub(super) fn draw_skin_backdrop<T>(target: &mut T, skin: &Skin) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    let Some(backdrop) = skin.assets.backdrop else {
        return Ok(());
    };
    debug_assert!(backdrop.is_valid());
    let width = usize::from(backdrop.width);
    target.draw_iter(
        backdrop
            .rgb565_be
            .chunks_exact(2)
            .enumerate()
            .map(|(index, bytes)| {
                let raw = u16::from_be_bytes([bytes[0], bytes[1]]);
                Pixel(
                    Point::new((index % width) as i32, (index / width) as i32),
                    Rgb565::new(
                        ((raw >> 11) & 0x1f) as u8,
                        ((raw >> 5) & 0x3f) as u8,
                        (raw & 0x1f) as u8,
                    ),
                )
            }),
    )
}

pub(super) fn draw_graph_background<T>(target: &mut T, skin: &Skin) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    let grid = match skin.chrome {
        ChromeStyle::Alchemy(chrome) => chrome.grid,
        ChromeStyle::AuthoredUnderlay(chrome) => chrome.grid,
        ChromeStyle::Flat => return Ok(()),
    };
    let plot = skin.layout.graph.plot;
    for division in 1..4 {
        let y = plot.y + division * (plot.height as i32 - 1) / 4;
        for x in (plot.x..plot.x + plot.width as i32).step_by(4) {
            Pixel(Point::new(x, y), grid.render).draw(target)?;
        }
    }
    for division in 1..6 {
        let x = plot.x + division * (plot.width as i32 - 1) / 6;
        for y in (plot.y..plot.y + plot.height as i32).step_by(4) {
            Pixel(Point::new(x, y), grid.render).draw(target)?;
        }
    }
    Ok(())
}

pub(super) fn draw_skin_assets<T>(target: &mut T, skin: &Skin) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    for asset in skin.assets.decorations {
        debug_assert!(asset.bitmap.is_valid());
        let width = usize::from(asset.bitmap.width);
        target.draw_iter(
            asset
                .bitmap
                .pixels
                .iter()
                .copied()
                .enumerate()
                .filter(|(_, raw)| Some(*raw) != asset.bitmap.transparent)
                .map(|(index, raw)| {
                    let red = ((raw >> 11) & 0x1f) as u8;
                    let green = ((raw >> 5) & 0x3f) as u8;
                    let blue = (raw & 0x1f) as u8;
                    Pixel(
                        Point::new(
                            asset.x + (index % width) as i32,
                            asset.y + (index / width) as i32,
                        ),
                        Rgb565::new(red, green, blue),
                    )
                }),
        )?;
    }
    Ok(())
}

pub(super) fn draw_panel_values<T>(
    target: &mut T,
    data: &UiData<'_>,
    skin: &Skin,
) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    for panel in skin.layout.panels {
        panel
            .inner
            .rectangle()
            .into_styled(PrimitiveStyle::with_fill(skin.theme.panel_inner.render))
            .draw(target)?;
    }

    let small = VlwFont::new(skin.assets.fonts.small).unwrap();
    let primary = VlwFont::new(match skin.chrome {
        ChromeStyle::Flat => skin.assets.fonts.numeric,
        ChromeStyle::Alchemy(_) | ChromeStyle::AuthoredUnderlay(_) => skin.assets.fonts.medium,
    })
    .unwrap();
    let mut value: String<16> = String::new();
    write!(
        &mut value,
        "{}.{:01}",
        data.shot_time_tenths / 10,
        data.shot_time_tenths % 10
    )
    .ok();
    primary.draw_right(
        target,
        &value,
        skin.layout.panels[0].value_right,
        skin.layout.panels[0].value_y,
        skin.theme.panel_text.source,
        skin.theme.panel_inner.source,
    )?;

    value.clear();
    write!(&mut value, "{:.1}g", data.shot_weight).ok();
    small.draw_right(
        target,
        &value,
        skin.layout.panels[1].value_right,
        skin.layout.panels[1].value_y,
        skin.theme.panel_text.source,
        skin.theme.panel_inner.source,
    )?;
    value.clear();
    write!(&mut value, "{:.1} g/s", data.flow_rate).ok();
    small.draw_right(
        target,
        &value,
        skin.layout.panels[1].value_right,
        skin.layout.panels[1].value_y + 20,
        skin.theme.panel_text.source,
        skin.theme.panel_inner.source,
    )?;

    value.clear();
    write!(
        &mut value,
        "{}.{:01}",
        data.last_pressure.max(0) / 1_000,
        (data.last_pressure.max(0) % 1_000) / 100
    )
    .ok();
    primary.draw_right(
        target,
        &value,
        skin.layout.panels[2].value_right,
        skin.layout.panels[2].value_y,
        skin.theme.panel_text.source,
        skin.theme.panel_inner.source,
    )?;
    Ok(())
}

pub(super) fn draw_status_band<T>(
    target: &mut T,
    data: &UiData<'_>,
    skin: &Skin,
) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    let status_fill = match skin.chrome {
        ChromeStyle::Flat => skin.theme.panel.render,
        ChromeStyle::Alchemy(_) | ChromeStyle::AuthoredUnderlay(_) => skin.theme.screen.render,
    };
    skin.layout
        .status
        .rectangle()
        .into_styled(PrimitiveStyle::with_fill(status_fill))
        .draw(target)?;
    if let Some(status_secondary) = skin.layout.status_secondary {
        status_secondary
            .rectangle()
            .into_styled(PrimitiveStyle::with_fill(status_fill))
            .draw(target)?;
    }
    let font = VlwFont::new(skin.assets.fonts.small).unwrap();
    font.draw(
        target,
        match (skin.chrome, data.bluetooth_on) {
            (ChromeStyle::AuthoredUnderlay(_), true) => "ON",
            (ChromeStyle::AuthoredUnderlay(_), false) => "OFF",
            (_, true) => "BT ON",
            (_, false) => "BT OFF",
        },
        skin.layout.status_left,
        skin.theme.status_text.source,
        match skin.chrome {
            ChromeStyle::Flat => skin.theme.panel.source,
            ChromeStyle::Alchemy(_) | ChromeStyle::AuthoredUnderlay(_) => skin.theme.screen.source,
        },
    )?;

    let mut scale: String<40> = String::new();
    if data.scale_connected {
        if matches!(skin.chrome, ChromeStyle::AuthoredUnderlay(_)) {
            scale.push_str("LINKED").ok();
        } else if matches!(skin.chrome, ChromeStyle::Alchemy(_)) {
            scale.push_str("SCALE LINKED").ok();
        } else {
            write!(&mut scale, "Scale: {}", data.scale_name).ok();
        }
    } else if matches!(skin.chrome, ChromeStyle::AuthoredUnderlay(_)) {
        scale.push_str("--").ok();
    } else if matches!(skin.chrome, ChromeStyle::Alchemy(_)) {
        scale.push_str("SCALE --").ok();
    } else {
        scale.push_str("Scale: --").ok();
    }
    font.draw_right(
        target,
        &scale,
        skin.layout.status_right.x,
        skin.layout.status_right.y,
        skin.theme.status_text.source,
        match skin.chrome {
            ChromeStyle::Flat => skin.theme.panel.source,
            ChromeStyle::Alchemy(_) | ChromeStyle::AuthoredUnderlay(_) => skin.theme.screen.source,
        },
    )?;
    Ok(())
}

pub(super) fn draw_center_message<T>(
    target: &mut T,
    text: &str,
    skin: &Skin,
) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    target.clear(skin.theme.screen.render)?;
    Text::with_alignment(
        text,
        Point::new(WIDTH / 2, HEIGHT / 2),
        MonoTextStyle::new(&FONT_10X20, skin.theme.panel_text.render),
        Alignment::Center,
    )
    .draw(target)
    .map(|_| ())
}

pub(super) fn draw_debug_overlay<T>(
    target: &mut T,
    data: &UiData<'_>,
    skin: &Skin,
) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    let font = VlwFont::new(skin.assets.fonts.small).unwrap();
    let mut line: String<64> = String::new();
    write!(
        &mut line,
        "Scale: {}",
        if data.scale_connected {
            data.scale_name
        } else {
            "none"
        }
    )
    .ok();
    font.draw(
        target,
        &line,
        skin.layout.debug_origin,
        skin.theme.splash_text.source,
        skin.theme.screen.source,
    )?;
    line.clear();
    write!(&mut line, "Nearby: {}", data.scale_name).ok();
    font.draw(
        target,
        &line,
        skin.layout.debug_origin + Point::new(0, 20),
        skin.theme.splash_text.source,
        skin.theme.screen.source,
    )?;
    Ok(())
}
