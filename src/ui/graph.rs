use embedded_graphics::pixelcolor::{Rgb565, Rgb888};
use embedded_graphics::prelude::*;
use embedded_graphics::primitives::{Line, PrimitiveStyle};

use super::{Skin, GRAPH_WINDOW_MS, HISTORY_LEN};

pub(super) fn draw_graph<T>(
    target: &mut T,
    values: &[i16],
    times: &[u32],
    max_value: i32,
    color: Rgb565,
    antialias: bool,
    skin: &Skin,
) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    let count = values.len().min(HISTORY_LEN);
    if count < 2 || max_value <= 0 {
        return Ok(());
    }
    let use_times = times.len() >= count;
    let last_time = if use_times {
        times[count - 1]
    } else {
        GRAPH_WINDOW_MS
    };
    let window = GRAPH_WINDOW_MS.max(last_time).max(1);
    let plot = skin.layout.graph.plot;
    let graph_height = plot.height as i32 - 1;
    let graph_width = plot.width as i32;
    let point = |index: usize| {
        let time = if use_times {
            times[index]
        } else {
            (index as u32 * window) / (count - 1) as u32
        };
        let value = i32::from(values[index]).clamp(0, max_value);
        Point::new(
            plot.x + (i64::from(time) * i64::from(graph_width) / i64::from(window)) as i32,
            plot.y + graph_height - value * graph_height / max_value,
        )
    };
    if antialias {
        return draw_antialiased_polyline(target, count, point, color, skin.theme.screen.render);
    }

    let mut previous = point(0);
    for index in 1..count {
        let current = point(index);
        draw_m5_line(target, previous, current, color)?;
        previous = current;
    }
    Ok(())
}

pub(super) fn draw_antialiased_polyline<T, F>(
    target: &mut T,
    count: usize,
    point: F,
    color: Rgb565,
    background: Rgb565,
) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
    F: Fn(usize) -> Point,
{
    const NEIGHBORS: [(i32, i32); 8] = [
        (-1, -1),
        (0, -1),
        (1, -1),
        (-1, 0),
        (1, 0),
        (-1, 1),
        (0, 1),
        (1, 1),
    ];

    let fringe = blend_graph_pixel(color, background, 72);
    let mut previous = point(0);
    for index in 1..count {
        let current = point(index);
        for (x, y) in NEIGHBORS {
            let offset = Point::new(x, y);
            draw_m5_line(target, previous + offset, current + offset, fringe)?;
        }
        previous = current;
    }
    previous = point(0);
    for index in 1..count {
        let current = point(index);
        draw_m5_line(target, previous, current, color)?;
        previous = current;
    }
    Ok(())
}

fn blend_graph_pixel(foreground: Rgb565, background: Rgb565, alpha: u8) -> Rgb565 {
    let foreground = Rgb888::from(foreground);
    let background = Rgb888::from(background);
    let alpha = u32::from(alpha);
    let blend = |foreground: u8, background: u8| {
        ((u32::from(foreground) * alpha + u32::from(background) * (255 - alpha)) / 255) as u8
    };
    Rgb565::from(Rgb888::new(
        blend(foreground.r(), background.r()),
        blend(foreground.g(), background.g()),
        blend(foreground.b(), background.b()),
    ))
}

pub(super) fn draw_m5_line<T>(
    target: &mut T,
    mut start: Point,
    mut end: Point,
    color: Rgb565,
) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    let steep = (end.y - start.y).abs() > (end.x - start.x).abs();
    if steep {
        core::mem::swap(&mut start.x, &mut start.y);
        core::mem::swap(&mut end.x, &mut end.y);
    }
    if start.x > end.x {
        core::mem::swap(&mut start, &mut end);
    }

    let delta_y = (end.y - start.y).abs();
    let y_step = if end.y > start.y { 1 } else { -1 };
    let delta_x = end.x - start.x;
    let mut error = delta_x >> 1;
    let mut y = start.y;
    target.draw_iter((start.x..=end.x).map(|x| {
        let point = if steep {
            Point::new(y, x)
        } else {
            Point::new(x, y)
        };
        error -= delta_y;
        if error < 0 {
            error += delta_x;
            y += y_step;
        }
        Pixel(point, color)
    }))
}

pub(super) fn draw_pressure_bar<T>(
    target: &mut T,
    pressure: i16,
    max_value: i32,
    skin: &Skin,
) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    let bar = skin.layout.pressure_bar;
    bar.rectangle()
        .into_styled(PrimitiveStyle::with_fill(skin.theme.panel_inner.render))
        .draw(target)?;
    let graph_height = bar.height as i32 - 1;
    let height =
        (i32::from(pressure).clamp(0, max_value) * graph_height / max_value).clamp(0, graph_height);
    for offset in 0..height {
        let pressure_at_y = offset * max_value / graph_height;
        Line::new(
            Point::new(bar.x, bar.y + graph_height - offset),
            Point::new(bar.x + bar.width as i32 - 1, bar.y + graph_height - offset),
        )
        .into_styled(PrimitiveStyle::with_stroke(
            gradient_color_for_pressure(pressure_at_y, skin),
            1,
        ))
        .draw(target)?;
    }
    Ok(())
}

fn gradient_color_for_pressure(pressure: i32, skin: &Skin) -> Rgb565 {
    let scale = skin.theme.pressure_scale;
    if pressure <= 6_000 {
        interpolate(scale.low, scale.low_peak, pressure, 0, 6_000)
    } else if pressure <= 7_000 {
        interpolate(scale.low_peak, scale.good, pressure, 6_000, 7_000)
    } else if pressure <= 8_000 {
        scale.good.render
    } else if pressure <= 8_500 {
        interpolate(scale.good, scale.warning, pressure, 8_000, 8_500)
    } else if pressure <= 10_000 {
        interpolate(scale.warning, scale.high, pressure, 8_500, 10_000)
    } else {
        scale.high.render
    }
}

fn interpolate(
    from: super::theme::SkinColor,
    to: super::theme::SkinColor,
    value: i32,
    in_min: i32,
    in_max: i32,
) -> Rgb565 {
    let red = map(
        value,
        in_min,
        in_max,
        i32::from(from.source.r()),
        i32::from(to.source.r()),
    ) as u8;
    let green = map(
        value,
        in_min,
        in_max,
        i32::from(from.source.g()),
        i32::from(to.source.g()),
    ) as u8;
    let blue = map(
        value,
        in_min,
        in_max,
        i32::from(from.source.b()),
        i32::from(to.source.b()),
    ) as u8;
    Rgb565::new(red >> 3, green >> 2, blue >> 3)
}

fn map(value: i32, in_min: i32, in_max: i32, out_min: i32, out_max: i32) -> i32 {
    (value - in_min) * (out_max - out_min) / (in_max - in_min) + out_min
}
