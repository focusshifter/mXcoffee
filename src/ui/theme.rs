use embedded_graphics::pixelcolor::{Rgb565, Rgb888};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SkinColor {
    pub source: Rgb888,
    pub render: Rgb565,
}

impl SkinColor {
    pub const fn new(red: u8, green: u8, blue: u8) -> Self {
        Self {
            source: Rgb888::new(red, green, blue),
            render: Rgb565::new(red >> 3, green >> 2, blue >> 3),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Theme {
    pub screen: SkinColor,
    pub panel: SkinColor,
    pub panel_inner: SkinColor,
    pub panel_text: SkinColor,
    pub panel_label: SkinColor,
    pub graph_pressure: SkinColor,
    pub graph_warning: SkinColor,
    pub status_text: SkinColor,
    pub splash_text: SkinColor,
    pub splash_bean: SkinColor,
    pub splash_mark: SkinColor,
    pub pressure_scale: PressureScaleTheme,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PressureScaleTheme {
    pub low: SkinColor,
    pub low_peak: SkinColor,
    pub good: SkinColor,
    pub warning: SkinColor,
    pub high: SkinColor,
}

pub const CLASSIC_THEME: Theme = Theme {
    screen: SkinColor::new(0x03, 0x16, 0x1e),
    panel: SkinColor::new(0x96, 0xcb, 0xbb),
    panel_inner: SkinColor::new(0x0a, 0x3b, 0x44),
    panel_text: SkinColor::new(0xdd, 0xfe, 0xee),
    panel_label: SkinColor::new(0x0a, 0x3b, 0x44),
    graph_pressure: SkinColor::new(0xbd, 0xff, 0xff),
    graph_warning: SkinColor::new(0xd6, 0xa6, 0x7b),
    status_text: SkinColor::new(0x0a, 0x3b, 0x44),
    splash_text: SkinColor::new(0xe6, 0xff, 0xff),
    splash_bean: SkinColor::new(0x5a, 0x3a, 0x1a),
    splash_mark: SkinColor::new(0xf4, 0xe5, 0xc3),
    pressure_scale: PressureScaleTheme {
        low: SkinColor::new(64, 64, 64),
        low_peak: SkinColor::new(96, 96, 96),
        good: SkinColor::new(0, 255, 0),
        warning: SkinColor::new(255, 255, 0),
        high: SkinColor::new(255, 0, 0),
    },
};

pub const WORKSHOP_THEME: Theme = Theme {
    screen: SkinColor::new(0x0d, 0x0f, 0x0d),
    panel: SkinColor::new(0xb6, 0x83, 0x2f),
    panel_inner: SkinColor::new(0x1b, 0x18, 0x13),
    panel_text: SkinColor::new(0xf0, 0xdf, 0xb0),
    panel_label: SkinColor::new(0x1b, 0x18, 0x13),
    graph_pressure: SkinColor::new(0xa8, 0x58, 0xa0),
    graph_warning: SkinColor::new(0xd4, 0x93, 0x27),
    status_text: SkinColor::new(0x18, 0x12, 0x0b),
    splash_text: SkinColor::new(0xf0, 0xdf, 0xb0),
    splash_bean: SkinColor::new(0x63, 0x31, 0x23),
    splash_mark: SkinColor::new(0xd4, 0x93, 0x27),
    pressure_scale: PressureScaleTheme {
        low: SkinColor::new(0x28, 0x22, 0x24),
        low_peak: SkinColor::new(0x54, 0x3b, 0x50),
        good: SkinColor::new(0xa8, 0x58, 0xa0),
        warning: SkinColor::new(0xd4, 0x93, 0x27),
        high: SkinColor::new(0xba, 0x45, 0x2c),
    },
};

pub const ALCHEMY_THEME: Theme = Theme {
    screen: SkinColor::new(7, 0, 0),
    panel: SkinColor::new(58, 21, 0),
    panel_inner: SkinColor::new(7, 0, 0),
    panel_text: SkinColor::new(231, 202, 113),
    panel_label: SkinColor::new(161, 62, 118),
    graph_pressure: SkinColor::new(161, 57, 117),
    graph_warning: SkinColor::new(209, 121, 5),
    status_text: SkinColor::new(213, 163, 56),
    splash_text: SkinColor::new(231, 202, 113),
    splash_bean: SkinColor::new(101, 28, 6),
    splash_mark: SkinColor::new(209, 121, 5),
    pressure_scale: PressureScaleTheme {
        low: SkinColor::new(45, 11, 12),
        low_peak: SkinColor::new(83, 23, 40),
        good: SkinColor::new(161, 57, 117),
        warning: SkinColor::new(198, 90, 150),
        high: SkinColor::new(227, 138, 192),
    },
};
