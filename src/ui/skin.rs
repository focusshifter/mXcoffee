use super::layout::{
    DashboardLayout, ALCHEMY_LAYOUT, CLASSIC_LAYOUT, LCARS_LAYOUT, NGE_LAYOUT, WORKSHOP_LAYOUT,
};
use super::theme::{
    SkinColor, Theme, ALCHEMY_THEME, CLASSIC_THEME, LCARS_THEME, NGE_THEME, WORKSHOP_THEME,
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SkinId {
    Classic,
    Workshop,
    Alchemy,
    Nge,
    Lcars,
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
    pub backdrop: Option<FullScreenAsset>,
    pub decorations: &'static [PositionedBitmapAsset],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FullScreenAsset {
    pub width: u16,
    pub height: u16,
    pub rgb565_be: &'static [u8],
}

impl FullScreenAsset {
    pub const fn is_valid(self) -> bool {
        self.width as usize * self.height as usize * 2 == self.rgb565_be.len()
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FontAssets {
    pub small: &'static [u8],
    pub medium: &'static [u8],
    pub logo: &'static [u8],
    pub numeric: &'static [u8],
}

const MOON_GLOSS_FONTS: FontAssets = FontAssets {
    small: include_bytes!("../../assets/fonts/MoonGloss_16.vlw"),
    medium: include_bytes!("../../assets/fonts/MoonGloss_24.vlw"),
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
    pub chrome: ChromeStyle,
    pub panel_headers: [&'static str; 3],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ChromeStyle {
    Flat,
    Alchemy(AlchemyChrome),
    AuthoredUnderlay(UnderlayChrome),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct UnderlayChrome {
    pub grid: SkinColor,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct AlchemyChrome {
    pub brass: SkinColor,
    pub brass_light: SkinColor,
    pub grid: SkinColor,
}

pub const CLASSIC: Skin = Skin {
    id: SkinId::Classic,
    name: "Classic",
    theme: CLASSIC_THEME,
    layout: CLASSIC_LAYOUT,
    assets: SkinAssets {
        fonts: MOON_GLOSS_FONTS,
        backdrop: None,
        decorations: &[],
    },
    chrome: ChromeStyle::Flat,
    panel_headers: ["SHOT TIME", "WEIGHT G", "PRESSURE"],
};

pub const WORKSHOP: Skin = Skin {
    id: SkinId::Workshop,
    name: "Workshop",
    theme: WORKSHOP_THEME,
    layout: WORKSHOP_LAYOUT,
    assets: SkinAssets {
        fonts: MOON_GLOSS_FONTS,
        backdrop: None,
        decorations: &WORKSHOP_DECORATIONS,
    },
    chrome: ChromeStyle::Flat,
    panel_headers: ["SHOT TIME", "WEIGHT G", "PRESSURE"],
};

pub const ALCHEMY: Skin = Skin {
    id: SkinId::Alchemy,
    name: "Alchemy",
    theme: ALCHEMY_THEME,
    layout: ALCHEMY_LAYOUT,
    assets: SkinAssets {
        fonts: MOON_GLOSS_FONTS,
        backdrop: Some(FullScreenAsset {
            width: 320,
            height: 240,
            rgb565_be: include_bytes!("../../assets/skins/alchemy-underlay.rgb565"),
        }),
        decorations: &[],
    },
    chrome: ChromeStyle::Alchemy(AlchemyChrome {
        brass: SkinColor::new(164, 85, 4),
        brass_light: SkinColor::new(222, 168, 51),
        grid: SkinColor::new(49, 24, 5),
    }),
    panel_headers: ["TIME", "WEIGHT", "PRESSURE"],
};

pub const NGE: Skin = Skin {
    id: SkinId::Nge,
    name: "NGE",
    theme: NGE_THEME,
    layout: NGE_LAYOUT,
    assets: SkinAssets {
        fonts: MOON_GLOSS_FONTS,
        backdrop: Some(FullScreenAsset {
            width: 320,
            height: 240,
            rgb565_be: include_bytes!("../../assets/skins/nge-underlay.rgb565"),
        }),
        decorations: &[],
    },
    chrome: ChromeStyle::AuthoredUnderlay(UnderlayChrome {
        grid: SkinColor::new(24, 70, 45),
    }),
    panel_headers: ["", "", ""],
};

pub const LCARS: Skin = Skin {
    id: SkinId::Lcars,
    name: "LCARS",
    theme: LCARS_THEME,
    layout: LCARS_LAYOUT,
    assets: SkinAssets {
        fonts: MOON_GLOSS_FONTS,
        backdrop: Some(FullScreenAsset {
            width: 320,
            height: 240,
            rgb565_be: include_bytes!("../../assets/skins/lcars-underlay.rgb565"),
        }),
        decorations: &[],
    },
    chrome: ChromeStyle::AuthoredUnderlay(UnderlayChrome {
        grid: SkinColor::new(55, 39, 79),
    }),
    panel_headers: ["", "", ""],
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
