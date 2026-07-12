use super::layout::{DashboardLayout, CLASSIC_LAYOUT, WORKSHOP_LAYOUT};
use super::theme::{Theme, CLASSIC_THEME, WORKSHOP_THEME};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SkinId {
    Classic,
    Workshop,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BitmapAsset {
    pub width: u16,
    pub height: u16,
    pub pixels: &'static [u16],
    pub transparent: Option<u16>,
}

impl BitmapAsset {
    pub const fn is_valid(self) -> bool {
        self.width as usize * self.height as usize == self.pixels.len()
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PositionedBitmapAsset {
    pub x: i32,
    pub y: i32,
    pub bitmap: BitmapAsset,
}

impl PositionedBitmapAsset {
    pub const fn is_valid(self, screen_width: i32, screen_height: i32) -> bool {
        self.bitmap.is_valid()
            && self.x >= 0
            && self.y >= 0
            && self.x + self.bitmap.width as i32 <= screen_width
            && self.y + self.bitmap.height as i32 <= screen_height
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SkinAssets {
    pub fonts: FontAssets,
    pub decorations: &'static [PositionedBitmapAsset],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FontAssets {
    pub small: &'static [u8],
    pub logo: &'static [u8],
    pub numeric: &'static [u8],
}

const MOON_GLOSS_FONTS: FontAssets = FontAssets {
    small: include_bytes!("../../assets/fonts/MoonGloss_16.vlw"),
    logo: include_bytes!("../../assets/fonts/MoonGloss_24_Logo.vlw"),
    numeric: include_bytes!("../../assets/fonts/MoonGloss_48_Numeric.vlw"),
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Skin {
    pub id: SkinId,
    pub name: &'static str,
    pub theme: Theme,
    pub layout: DashboardLayout,
    pub assets: SkinAssets,
}

pub const CLASSIC: Skin = Skin {
    id: SkinId::Classic,
    name: "Classic",
    theme: CLASSIC_THEME,
    layout: CLASSIC_LAYOUT,
    assets: SkinAssets {
        fonts: MOON_GLOSS_FONTS,
        decorations: &[],
    },
};

pub const WORKSHOP: Skin = Skin {
    id: SkinId::Workshop,
    name: "Workshop",
    theme: WORKSHOP_THEME,
    layout: WORKSHOP_LAYOUT,
    assets: SkinAssets {
        fonts: MOON_GLOSS_FONTS,
        decorations: &WORKSHOP_DECORATIONS,
    },
};

const TRANSPARENT: u16 = 0x0000;
const GOLD: u16 = 0xac05;
const LIGHT_GOLD: u16 = 0xe6ca;
const PURPLE: u16 = 0x9a99;

const WORKSHOP_SIGIL: [u16; 48] = [
    TRANSPARENT,
    TRANSPARENT,
    GOLD,
    GOLD,
    GOLD,
    GOLD,
    TRANSPARENT,
    TRANSPARENT,
    TRANSPARENT,
    GOLD,
    LIGHT_GOLD,
    TRANSPARENT,
    TRANSPARENT,
    LIGHT_GOLD,
    GOLD,
    TRANSPARENT,
    GOLD,
    LIGHT_GOLD,
    PURPLE,
    PURPLE,
    PURPLE,
    PURPLE,
    LIGHT_GOLD,
    GOLD,
    GOLD,
    LIGHT_GOLD,
    PURPLE,
    LIGHT_GOLD,
    LIGHT_GOLD,
    PURPLE,
    LIGHT_GOLD,
    GOLD,
    TRANSPARENT,
    GOLD,
    LIGHT_GOLD,
    TRANSPARENT,
    TRANSPARENT,
    LIGHT_GOLD,
    GOLD,
    TRANSPARENT,
    TRANSPARENT,
    TRANSPARENT,
    GOLD,
    GOLD,
    GOLD,
    GOLD,
    TRANSPARENT,
    TRANSPARENT,
];

const WORKSHOP_DECORATIONS: [PositionedBitmapAsset; 1] = [PositionedBitmapAsset {
    x: 156,
    y: 81,
    bitmap: BitmapAsset {
        width: 8,
        height: 6,
        pixels: &WORKSHOP_SIGIL,
        transparent: Some(TRANSPARENT),
    },
}];
