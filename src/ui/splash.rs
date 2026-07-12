use embedded_graphics::pixelcolor::{Rgb565, Rgb888};
use embedded_graphics::prelude::*;

use crate::vlw::VlwFont;

use super::graph::draw_m5_line;
use super::{Skin, HEIGHT, WIDTH};

pub(super) fn draw<T>(target: &mut T, skin: &Skin) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    const SEGMENTS: i32 = 20;
    const INV_SQRT_2: f32 = 0.707_106_77;

    target.clear(Rgb565::BLACK)?;
    let font = VlwFont::new(skin.assets.fonts.logo).unwrap();
    let font_height = font.height();
    let x_size = (font_height as f32 * 1.1) as i32;
    let width_m = font.measure("m").unwrap();
    let width_coffee = font.measure("coffee").unwrap();
    let start_x = (WIDTH - width_m - x_size - width_coffee) / 2;
    let text_y = (HEIGHT - font_height) / 2;
    let center_x = start_x + width_m + x_size / 2 - 1;
    let center_y = text_y + font_height / 2 + 2;
    let radius = (x_size as f32 * 0.55) as i32;

    font.draw(
        target,
        "m",
        Point::new(start_x, text_y),
        skin.theme.splash_text.source,
        Rgb888::BLACK,
    )?;
    font.draw(
        target,
        "coffee",
        Point::new(start_x + width_m + x_size, text_y),
        skin.theme.splash_text.source,
        Rgb888::BLACK,
    )?;

    target.draw_iter((-radius..=radius).flat_map(|y| {
        (-radius..=radius).filter_map(move |x| {
            (x * x + y * y <= radius * radius).then_some(Pixel(
                Point::new(center_x + x, center_y + y),
                skin.theme.splash_bean.render,
            ))
        })
    }))?;

    let length = radius as f32 * 0.6;
    let curve_amplitude = radius as f32 * 0.45;
    for (base_x, base_y, perpendicular_x, perpendicular_y) in [
        (INV_SQRT_2, INV_SQRT_2, -INV_SQRT_2, INV_SQRT_2),
        (INV_SQRT_2, -INV_SQRT_2, INV_SQRT_2, INV_SQRT_2),
    ] {
        for segment in 0..SEGMENTS {
            let t1 = -1.0 + 2.0 * segment as f32 / SEGMENTS as f32;
            let t2 = -1.0 + 2.0 * (segment + 1) as f32 / SEGMENTS as f32;
            let offset1 = (t1 * core::f32::consts::PI).sin() * curve_amplitude;
            let offset2 = (t2 * core::f32::consts::PI).sin() * curve_amplitude;
            let ax = t1 * length + offset1 * perpendicular_x;
            let ay = t1 * length + offset1 * perpendicular_y;
            let bx = t2 * length + offset2 * perpendicular_x;
            let by = t2 * length + offset2 * perpendicular_y;
            let start = Point::new(
                center_x + (base_x * ax - base_y * ay) as i32,
                center_y + (base_y * ax + base_x * ay) as i32,
            );
            let end = Point::new(
                center_x + (base_x * bx - base_y * by) as i32,
                center_y + (base_y * bx + base_x * by) as i32,
            );
            draw_m5_line(target, start, end, skin.theme.splash_mark.render)?;
            draw_m5_line(
                target,
                start + Point::new(1, 0),
                end + Point::new(1, 0),
                skin.theme.splash_mark.render,
            )?;
        }
    }
    Ok(())
}
