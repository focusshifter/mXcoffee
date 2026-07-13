use embedded_graphics::pixelcolor::Rgb565;
use embedded_graphics::prelude::*;
use embedded_graphics::primitives::PrimitiveStyle;

mod dashboard;
mod graph;
pub mod layout;
pub mod skin;
mod splash;
pub mod theme;

pub use skin::{Skin, SkinId, ALCHEMY, CLASSIC, LCARS, NGE, WORKSHOP};

#[cfg(any(
    all(feature = "alchemy-skin", feature = "nge-skin"),
    all(feature = "alchemy-skin", feature = "lcars-skin"),
    all(feature = "nge-skin", feature = "lcars-skin")
))]
compile_error!("select only one default skin feature");

#[cfg(all(
    feature = "alchemy-skin",
    not(any(feature = "nge-skin", feature = "lcars-skin"))
))]
pub const DEFAULT_SKIN: &Skin = &ALCHEMY;
#[cfg(all(feature = "nge-skin", not(feature = "lcars-skin")))]
pub const DEFAULT_SKIN: &Skin = &NGE;
#[cfg(feature = "lcars-skin")]
pub const DEFAULT_SKIN: &Skin = &LCARS;
#[cfg(not(any(feature = "alchemy-skin", feature = "nge-skin", feature = "lcars-skin")))]
pub const DEFAULT_SKIN: &Skin = &CLASSIC;

use dashboard::{
    draw_debug_overlay, draw_graph_background, draw_panel_frames, draw_panel_values,
    draw_skin_assets, draw_skin_backdrop, draw_status_band,
};
#[cfg(test)]
use graph::draw_antialiased_polyline;
use graph::{draw_graph, draw_pressure_bar};

pub const WIDTH: i32 = 320;
pub const HEIGHT: i32 = 240;
pub const HISTORY_LEN: usize = 160;
pub const GRAPH_WINDOW_MS: u32 = 30_000;

pub struct UiData<'a> {
    pub pressure_history: &'a [i16],
    pub weight_history_tenths: &'a [i16],
    pub history_times_ms: &'a [u32],
    pub last_pressure: i16,
    pub max_sensor_pressure: i16,
    pub shot_weight: f32,
    pub flow_rate: f32,
    pub bluetooth_on: bool,
    pub scale_connected: bool,
    pub scale_name: &'a str,
    pub shot_time_tenths: u64,
    pub pressure_hex: &'a str,
    pub debug_mode: bool,
    pub last_refresh_ms: u64,
    pub last_activity_ms: u64,
    pub now_ms: u64,
    pub auto_off_timeout_ms: u64,
    pub timer_running: bool,
    pub frame_indicator: Option<bool>,
    pub antialias_graph: bool,
}

pub fn draw_splash<T>(target: &mut T) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    draw_splash_with_skin(target, DEFAULT_SKIN)
}

pub fn draw_splash_with_skin<T>(target: &mut T, skin: &Skin) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    splash::draw(target, skin)
}

pub fn draw_main_screen<T>(target: &mut T, data: UiData<'_>) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    draw_main_screen_with_skin(target, data, DEFAULT_SKIN)
}

pub fn draw_main_screen_with_skin<T>(
    target: &mut T,
    data: UiData<'_>,
    skin: &Skin,
) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    target.clear(skin.theme.screen.render)?;
    draw_skin_backdrop(target, skin)?;
    draw_panel_frames(target, skin)?;
    draw_skin_assets(target, skin)?;
    draw_main_screen_retained_with_skin(target, data, skin)
}

pub fn initialize_main_screen<T>(target: &mut T) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    initialize_main_screen_with_skin(target, DEFAULT_SKIN)
}

pub fn initialize_main_screen_with_skin<T>(target: &mut T, skin: &Skin) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    target.clear(skin.theme.screen.render)?;
    draw_skin_backdrop(target, skin)?;
    draw_panel_frames(target, skin)?;
    draw_skin_assets(target, skin)
}

pub fn draw_main_screen_retained<T>(target: &mut T, data: UiData<'_>) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    draw_main_screen_retained_with_skin(target, data, DEFAULT_SKIN)
}

pub fn draw_main_screen_retained_with_skin<T>(
    target: &mut T,
    data: UiData<'_>,
    skin: &Skin,
) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    // Each framebuffer preserves its static background between full-screen uploads.
    // This region covers both graphs and any previous debug overlay.
    skin.layout
        .graph
        .clear
        .rectangle()
        .into_styled(PrimitiveStyle::with_fill(skin.theme.screen.render))
        .draw(target)?;
    draw_graph_background(target, skin)?;
    draw_status_band(target, &data, skin)?;
    draw_graph(
        target,
        data.pressure_history,
        data.history_times_ms,
        max_pressure(&data),
        pressure_color(data.last_pressure, skin),
        data.antialias_graph,
        skin,
    )?;
    draw_graph(
        target,
        data.weight_history_tenths,
        data.history_times_ms,
        500,
        skin.theme.graph_warning.render,
        data.antialias_graph,
        skin,
    )?;
    draw_pressure_bar(target, data.last_pressure, max_pressure(&data), skin)?;
    draw_panel_values(target, &data, skin)?;

    if data.debug_mode {
        draw_debug_overlay(target, &data, skin)?;
    }

    if let Some(frame_indicator) = data.frame_indicator {
        let indicator = if frame_indicator {
            Rgb565::WHITE
        } else {
            Rgb565::RED
        };
        skin.layout
            .frame_indicator
            .rectangle()
            .into_styled(PrimitiveStyle::with_fill(indicator))
            .draw(target)?;
    }
    Ok(())
}

fn max_pressure(data: &UiData<'_>) -> i32 {
    (i32::from(data.max_sensor_pressure) - 10_000).max(1_000)
}

fn pressure_color(pressure: i16, skin: &Skin) -> Rgb565 {
    if pressure > 9_000 {
        skin.theme.graph_warning.render
    } else {
        skin.theme.graph_pressure.render
    }
}

pub fn draw_center_message<T>(target: &mut T, text: &str) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    draw_center_message_with_skin(target, text, DEFAULT_SKIN)
}

pub fn draw_center_message_with_skin<T>(
    target: &mut T,
    text: &str,
    skin: &Skin,
) -> Result<(), T::Error>
where
    T: DrawTarget<Color = Rgb565>,
{
    dashboard::draw_center_message(target, text, skin)
}

pub fn build_reference_histories(
    pressure: &mut [i16; HISTORY_LEN],
    weight: &mut [i16; HISTORY_LEN],
    times: &mut [u32; HISTORY_LEN],
) {
    for index in 0..HISTORY_LEN {
        times[index] = (GRAPH_WINDOW_MS as usize * index).div_ceil(HISTORY_LEN) as u32;
        pressure[index] = if index < 32 {
            (8_400 * index / 32) as i16
        } else if index < 116 {
            8_400
        } else {
            (8_400 - ((index - 116) * 8_400 / (HISTORY_LEN - 116))) as i16
        };
        weight[index] = if index < 24 {
            0
        } else {
            (40 + (218 - 40) * (index - 24) / (HISTORY_LEN - 24)) as i16
        };
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fast_framebuffer::FastFrameBuffer;

    fn reference_data<'a>(
        pressure: &'a [i16],
        weight: &'a [i16],
        times: &'a [u32],
        frame_indicator: bool,
    ) -> UiData<'a> {
        UiData {
            pressure_history: pressure,
            weight_history_tenths: weight,
            history_times_ms: times,
            last_pressure: 8_400,
            max_sensor_pressure: 20_000,
            shot_weight: 21.8,
            flow_rate: 1.0,
            bluetooth_on: true,
            scale_connected: true,
            scale_name: "LFSMART SCALE",
            shot_time_tenths: 123,
            pressure_hex: "reference",
            debug_mode: false,
            last_refresh_ms: 0,
            last_activity_ms: 0,
            now_ms: 0,
            auto_off_timeout_ms: 600_000,
            timer_running: true,
            frame_indicator: Some(frame_indicator),
            antialias_graph: false,
        }
    }

    fn framebuffer_checksum(pixels: &[Rgb565]) -> u64 {
        pixels.iter().fold(0xcbf2_9ce4_8422_2325, |hash, pixel| {
            (hash ^ u64::from(pixel.into_storage())).wrapping_mul(0x0000_0100_0000_01b3)
        })
    }

    fn render_reference(skin: &Skin) -> Vec<Rgb565> {
        let mut pressure = [0; HISTORY_LEN];
        let mut weight = [0; HISTORY_LEN];
        let mut times = [0; HISTORY_LEN];
        build_reference_histories(&mut pressure, &mut weight, &mut times);
        let mut pixels = vec![Rgb565::BLACK; WIDTH as usize * HEIGHT as usize];
        draw_main_screen_with_skin(
            &mut FastFrameBuffer::new(&mut pixels, WIDTH as usize, HEIGHT as usize),
            reference_data(&pressure, &weight, &times, false),
            skin,
        )
        .unwrap();
        pixels
    }

    #[test]
    fn reference_histories_match_cpp_oracle_endpoints() {
        let mut pressure = [0; HISTORY_LEN];
        let mut weight = [0; HISTORY_LEN];
        let mut times = [0; HISTORY_LEN];
        build_reference_histories(&mut pressure, &mut weight, &mut times);
        assert_eq!(
            (pressure[0], pressure[32], pressure[115], pressure[159]),
            (0, 8_400, 8_400, 191)
        );
        assert_eq!((weight[23], weight[24], weight[159]), (0, 40, 216));
        assert_eq!(times[159], 29_813);
    }

    #[test]
    fn skin_definitions_are_valid_and_distinct() {
        for skin in [&CLASSIC, &WORKSHOP, &ALCHEMY, &NGE, &LCARS] {
            assert!(skin.layout.is_valid(WIDTH, HEIGHT));
            assert!(skin.assets.backdrop.is_none_or(|asset| {
                asset.is_valid()
                    && i32::from(asset.width) == WIDTH
                    && i32::from(asset.height) == HEIGHT
            }));
            assert!(skin
                .assets
                .decorations
                .iter()
                .all(|asset| asset.is_valid(WIDTH, HEIGHT)));
        }
        assert_ne!(CLASSIC.theme, WORKSHOP.theme);
        assert_ne!(CLASSIC.layout, WORKSHOP.layout);
        assert_ne!(WORKSHOP.theme, ALCHEMY.theme);
        assert_ne!(WORKSHOP.layout, ALCHEMY.layout);
        assert_ne!(ALCHEMY.theme, NGE.theme);
        assert_ne!(ALCHEMY.layout, NGE.layout);
        assert_ne!(NGE.theme, LCARS.theme);
        assert_ne!(NGE.layout, LCARS.layout);
    }

    #[test]
    fn skin_reference_checksums_are_stable() {
        assert_eq!(
            framebuffer_checksum(&render_reference(&CLASSIC)),
            12_050_328_228_639_863_048
        );
        assert_eq!(
            framebuffer_checksum(&render_reference(&WORKSHOP)),
            13_180_993_026_138_904_416
        );
        assert_eq!(
            framebuffer_checksum(&render_reference(&ALCHEMY)),
            1_939_580_656_284_526_128
        );
        assert_eq!(
            framebuffer_checksum(&render_reference(&NGE)),
            2_602_705_410_438_554_612
        );
        assert_eq!(
            framebuffer_checksum(&render_reference(&LCARS)),
            4_837_969_127_344_024_939
        );
    }

    #[test]
    fn reference_dashboard_uses_all_cpp_layout_regions() {
        let mut pressure = [0; HISTORY_LEN];
        let mut weight = [0; HISTORY_LEN];
        let mut times = [0; HISTORY_LEN];
        build_reference_histories(&mut pressure, &mut weight, &mut times);
        let mut pixels = [Rgb565::BLACK; WIDTH as usize * HEIGHT as usize];
        let mut target = FastFrameBuffer::new(&mut pixels, WIDTH as usize, HEIGHT as usize);
        draw_main_screen(
            &mut target,
            UiData {
                pressure_history: &pressure,
                weight_history_tenths: &weight,
                history_times_ms: &times,
                last_pressure: 8_400,
                max_sensor_pressure: 20_000,
                shot_weight: 21.8,
                flow_rate: 1.0,
                bluetooth_on: true,
                scale_connected: true,
                scale_name: "LFSMART SCALE",
                shot_time_tenths: 123,
                pressure_hex: "reference",
                debug_mode: false,
                last_refresh_ms: 0,
                last_activity_ms: 0,
                now_ms: 0,
                auto_off_timeout_ms: 600_000,
                timer_running: true,
                frame_indicator: Some(true),
                antialias_graph: false,
            },
        )
        .unwrap();
        let theme = CLASSIC.theme;
        let plot = CLASSIC.layout.graph.plot;
        assert_eq!(pixels[0], theme.panel.render);
        assert_eq!(pixels[110], theme.panel.render);
        assert_eq!(pixels[220], theme.panel.render);
        assert_eq!(pixels[220 * WIDTH as usize], theme.panel.render);
        assert_ne!(
            pixels[(plot.y as usize + plot.height as usize - 1) * WIDTH as usize + plot.x as usize],
            theme.screen.render
        );
    }

    #[test]
    fn splash_logo_is_centered_and_nonempty() {
        let mut pixels = [Rgb565::BLACK; WIDTH as usize * HEIGHT as usize];
        let mut target = FastFrameBuffer::new(&mut pixels, WIDTH as usize, HEIGHT as usize);
        draw_splash(&mut target).unwrap();

        let drawn: Vec<_> = pixels
            .iter()
            .enumerate()
            .filter(|(_, pixel)| **pixel != Rgb565::BLACK)
            .map(|(index, _)| (index % WIDTH as usize, index / WIDTH as usize))
            .collect();
        assert!(drawn.len() > 500);
        assert!(drawn
            .iter()
            .all(|(x, y)| (90..230).contains(x) && (100..140).contains(y)));
    }

    #[test]
    fn antialiased_graph_adds_coverage_pixels() {
        let theme = CLASSIC.theme;
        let mut pixels = [theme.screen.render; WIDTH as usize * HEIGHT as usize];
        let mut target = FastFrameBuffer::new(&mut pixels, WIDTH as usize, HEIGHT as usize);
        draw_graph(
            &mut target,
            &[0, 8_400],
            &[0, GRAPH_WINDOW_MS],
            10_000,
            theme.graph_pressure.render,
            true,
            &CLASSIC,
        )
        .unwrap();

        assert!(pixels
            .iter()
            .any(|pixel| *pixel != theme.screen.render && *pixel != theme.graph_pressure.render));
    }

    #[test]
    fn antialiased_polyline_is_stable_under_one_pixel_shift() {
        let points = [
            Point::new(30, 195),
            Point::new(82, 126),
            Point::new(146, 158),
            Point::new(230, 108),
        ];
        let theme = CLASSIC.theme;
        let mut original = [theme.screen.render; WIDTH as usize * HEIGHT as usize];
        let mut shifted = [theme.screen.render; WIDTH as usize * HEIGHT as usize];
        draw_antialiased_polyline(
            &mut FastFrameBuffer::new(&mut original, WIDTH as usize, HEIGHT as usize),
            points.len(),
            |index| points[index],
            theme.graph_pressure.render,
            theme.screen.render,
        )
        .unwrap();
        draw_antialiased_polyline(
            &mut FastFrameBuffer::new(&mut shifted, WIDTH as usize, HEIGHT as usize),
            points.len(),
            |index| points[index] + Point::new(1, 0),
            theme.graph_pressure.render,
            theme.screen.render,
        )
        .unwrap();

        for y in 0..HEIGHT as usize {
            for x in 0..WIDTH as usize - 1 {
                assert_eq!(
                    original[y * WIDTH as usize + x],
                    shifted[y * WIDTH as usize + x + 1],
                    "translation mismatch at ({x}, {y})"
                );
            }
        }
    }

    #[test]
    fn retained_redraw_matches_clean_full_redraw() {
        let mut pressure = [0; HISTORY_LEN];
        let mut weight = [0; HISTORY_LEN];
        let mut times = [0; HISTORY_LEN];
        build_reference_histories(&mut pressure, &mut weight, &mut times);
        let mut expected = vec![Rgb565::BLACK; WIDTH as usize * HEIGHT as usize];
        let mut retained = expected.clone();

        initialize_main_screen(&mut FastFrameBuffer::new(
            &mut retained,
            WIDTH as usize,
            HEIGHT as usize,
        ))
        .unwrap();
        draw_main_screen_retained(
            &mut FastFrameBuffer::new(&mut retained, WIDTH as usize, HEIGHT as usize),
            reference_data(&pressure, &weight, &times, true),
        )
        .unwrap();
        draw_main_screen(
            &mut FastFrameBuffer::new(&mut expected, WIDTH as usize, HEIGHT as usize),
            reference_data(&pressure, &weight, &times, true),
        )
        .unwrap();
        assert_eq!(retained, expected);

        pressure.rotate_left(1);
        pressure[HISTORY_LEN - 1] = 2_500;
        draw_main_screen_retained(
            &mut FastFrameBuffer::new(&mut retained, WIDTH as usize, HEIGHT as usize),
            reference_data(&pressure, &weight, &times, false),
        )
        .unwrap();
        draw_main_screen(
            &mut FastFrameBuffer::new(&mut expected, WIDTH as usize, HEIGHT as usize),
            reference_data(&pressure, &weight, &times, false),
        )
        .unwrap();
        assert_eq!(retained, expected);
    }

    #[test]
    fn switching_skins_reinitializes_all_static_pixels() {
        let mut pressure = [0; HISTORY_LEN];
        let mut weight = [0; HISTORY_LEN];
        let mut times = [0; HISTORY_LEN];
        build_reference_histories(&mut pressure, &mut weight, &mut times);
        let mut switched = render_reference(&CLASSIC);
        let expected = render_reference(&WORKSHOP);

        initialize_main_screen_with_skin(
            &mut FastFrameBuffer::new(&mut switched, WIDTH as usize, HEIGHT as usize),
            &WORKSHOP,
        )
        .unwrap();
        draw_main_screen_retained_with_skin(
            &mut FastFrameBuffer::new(&mut switched, WIDTH as usize, HEIGHT as usize),
            reference_data(&pressure, &weight, &times, false),
            &WORKSHOP,
        )
        .unwrap();

        assert_eq!(switched, expected);

        let expected = render_reference(&ALCHEMY);
        initialize_main_screen_with_skin(
            &mut FastFrameBuffer::new(&mut switched, WIDTH as usize, HEIGHT as usize),
            &ALCHEMY,
        )
        .unwrap();
        draw_main_screen_retained_with_skin(
            &mut FastFrameBuffer::new(&mut switched, WIDTH as usize, HEIGHT as usize),
            reference_data(&pressure, &weight, &times, false),
            &ALCHEMY,
        )
        .unwrap();
        assert_eq!(switched, expected);

        let expected = render_reference(&NGE);
        initialize_main_screen_with_skin(
            &mut FastFrameBuffer::new(&mut switched, WIDTH as usize, HEIGHT as usize),
            &NGE,
        )
        .unwrap();
        draw_main_screen_retained_with_skin(
            &mut FastFrameBuffer::new(&mut switched, WIDTH as usize, HEIGHT as usize),
            reference_data(&pressure, &weight, &times, false),
            &NGE,
        )
        .unwrap();
        assert_eq!(switched, expected);

        let expected = render_reference(&LCARS);
        initialize_main_screen_with_skin(
            &mut FastFrameBuffer::new(&mut switched, WIDTH as usize, HEIGHT as usize),
            &LCARS,
        )
        .unwrap();
        draw_main_screen_retained_with_skin(
            &mut FastFrameBuffer::new(&mut switched, WIDTH as usize, HEIGHT as usize),
            reference_data(&pressure, &weight, &times, false),
            &LCARS,
        )
        .unwrap();
        assert_eq!(switched, expected);
    }
}
