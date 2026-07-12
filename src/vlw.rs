use embedded_graphics::pixelcolor::{Rgb565, Rgb888};
use embedded_graphics::prelude::{DrawTarget, Pixel, Point, RgbColor};

const HEADER_SIZE: usize = 24;
const GLYPH_RECORD_SIZE: usize = 28;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum VlwError {
    InvalidFont,
    UnsupportedCharacter(char),
}

#[derive(Clone, Copy)]
struct Glyph<'a> {
    width: usize,
    advance: i32,
    baseline_delta: i32,
    x_offset: i32,
    bitmap: &'a [u8],
}

pub struct VlwFont<'a> {
    data: &'a [u8],
    glyph_count: usize,
    max_ascent: i32,
    max_descent: i32,
}

impl<'a> VlwFont<'a> {
    pub fn new(data: &'a [u8]) -> Result<Self, VlwError> {
        if data.len() < HEADER_SIZE {
            return Err(VlwError::InvalidFont);
        }
        let glyph_count = read_u32(data, 0)? as usize;
        let records_end = HEADER_SIZE.checked_add(glyph_count.saturating_mul(GLYPH_RECORD_SIZE));
        if glyph_count == 0
            || match records_end {
                Some(end) => end > data.len(),
                None => true,
            }
        {
            return Err(VlwError::InvalidFont);
        }

        let mut max_ascent = read_i32(data, 16)?.unsigned_abs() as i32;
        let mut max_descent = read_i32(data, 20)?.unsigned_abs() as i32;
        for index in 0..glyph_count {
            let record = HEADER_SIZE + index * GLYPH_RECORD_SIZE;
            let codepoint = read_u32(data, record)?;
            let height = read_u32(data, record + 4)? as i32;
            let baseline_delta = read_i32(data, record + 16)?;
            if (codepoint > 0xff || ((0x20..0xa0).contains(&codepoint) && codepoint != 0x7f))
                && codepoint != 0x3000
            {
                max_ascent = max_ascent.max(baseline_delta);
                max_descent = max_descent.max(height - baseline_delta);
            }
        }

        let font = Self {
            data,
            glyph_count,
            max_ascent,
            max_descent,
        };
        font.validate_bitmaps()?;
        Ok(font)
    }

    pub fn height(&self) -> i32 {
        self.max_ascent + self.max_descent
    }

    pub fn measure(&self, text: &str) -> Result<i32, VlwError> {
        text.chars().try_fold(0, |width, character| {
            Ok(width + self.glyph(character)?.advance)
        })
    }

    pub fn draw<T>(
        &self,
        target: &mut T,
        text: &str,
        position: Point,
        foreground: Rgb888,
        background: Rgb888,
    ) -> Result<(), T::Error>
    where
        T: DrawTarget<Color = Rgb565>,
    {
        let mut cursor_x = position.x;
        for character in text.chars() {
            if character == ' ' {
                cursor_x += self.height() * 2 / 7;
                continue;
            }
            let glyph = self
                .glyph(character)
                .expect("dashboard strings only contain glyphs embedded in MoonGloss");
            let top = position.y + self.max_ascent - glyph.baseline_delta;
            let left = cursor_x + glyph.x_offset;
            let pixels = glyph
                .bitmap
                .iter()
                .enumerate()
                .filter_map(|(index, alpha)| {
                    if *alpha == 0 {
                        return None;
                    }
                    let point = Point::new(
                        left + (index % glyph.width) as i32,
                        top + (index / glyph.width) as i32,
                    );
                    Some(Pixel(point, blend_rgb565(foreground, background, *alpha)))
                });
            target.draw_iter(pixels)?;
            cursor_x += glyph.advance;
        }
        Ok(())
    }

    pub fn draw_right<T>(
        &self,
        target: &mut T,
        text: &str,
        right: i32,
        top: i32,
        foreground: Rgb888,
        background: Rgb888,
    ) -> Result<(), T::Error>
    where
        T: DrawTarget<Color = Rgb565>,
    {
        let width = self
            .measure(text)
            .expect("dashboard strings only contain glyphs embedded in MoonGloss");
        self.draw(
            target,
            text,
            Point::new(right - width, top),
            foreground,
            background,
        )
    }

    fn validate_bitmaps(&self) -> Result<(), VlwError> {
        let mut bitmap_offset = HEADER_SIZE + self.glyph_count * GLYPH_RECORD_SIZE;
        for index in 0..self.glyph_count {
            let record = HEADER_SIZE + index * GLYPH_RECORD_SIZE;
            let height = read_u32(self.data, record + 4)? as usize;
            let width = read_u32(self.data, record + 8)? as usize;
            bitmap_offset = bitmap_offset
                .checked_add(width.saturating_mul(height))
                .ok_or(VlwError::InvalidFont)?;
            if bitmap_offset > self.data.len() {
                return Err(VlwError::InvalidFont);
            }
        }
        Ok(())
    }

    fn glyph(&self, character: char) -> Result<Glyph<'a>, VlwError> {
        let codepoint = character as u32;
        let mut bitmap_offset = HEADER_SIZE + self.glyph_count * GLYPH_RECORD_SIZE;
        for index in 0..self.glyph_count {
            let record = HEADER_SIZE + index * GLYPH_RECORD_SIZE;
            let height = read_u32(self.data, record + 4)? as usize;
            let width = read_u32(self.data, record + 8)? as usize;
            let bitmap_len = width.saturating_mul(height);
            if read_u32(self.data, record)? == codepoint {
                return Ok(Glyph {
                    width,
                    advance: read_u32(self.data, record + 12)? as i32,
                    baseline_delta: read_i32(self.data, record + 16)?,
                    x_offset: read_i32(self.data, record + 20)? as i8 as i32,
                    bitmap: &self.data[bitmap_offset..bitmap_offset + bitmap_len],
                });
            }
            bitmap_offset += bitmap_len;
        }
        Err(VlwError::UnsupportedCharacter(character))
    }
}

fn read_u32(data: &[u8], offset: usize) -> Result<u32, VlwError> {
    let bytes = data.get(offset..offset + 4).ok_or(VlwError::InvalidFont)?;
    Ok(u32::from_be_bytes(bytes.try_into().unwrap()))
}

fn read_i32(data: &[u8], offset: usize) -> Result<i32, VlwError> {
    Ok(read_u32(data, offset)? as i32)
}

fn blend_rgb565(foreground: Rgb888, background: Rgb888, alpha: u8) -> Rgb565 {
    let weight = u32::from(alpha) + 1;
    let blend = |foreground: u8, background: u8| {
        ((u32::from(foreground) * weight + u32::from(background) * (257 - weight)) >> 8) as u8
    };
    let red = blend(foreground.r(), background.r());
    let green = blend(foreground.g(), background.g());
    let blue = blend(foreground.b(), background.b());
    Rgb565::new(red >> 3, green >> 2, blue >> 3)
}

#[cfg(test)]
mod tests {
    use super::*;

    const MOON_GLOSS_16: &[u8] = include_bytes!("../assets/fonts/MoonGloss_16.vlw");
    const MOON_GLOSS_48: &[u8] = include_bytes!("../assets/fonts/MoonGloss_48_Numeric.vlw");

    #[test]
    fn parses_m5gfx_vlw_metrics() {
        let small = VlwFont::new(MOON_GLOSS_16).unwrap();
        let large = VlwFont::new(MOON_GLOSS_48).unwrap();

        assert_eq!(small.height(), 17);
        assert_eq!(small.measure("SHOT TIME"), Ok(89));
        assert_eq!(large.height(), 44);
        assert_eq!(large.measure("12.3"), Ok(95));
        assert_eq!(large.measure("8.4"), Ok(68));
    }

    #[test]
    fn rejects_truncated_fonts_and_unknown_glyphs() {
        assert_eq!(
            VlwFont::new(&MOON_GLOSS_16[..40]).err(),
            Some(VlwError::InvalidFont)
        );
        let font = VlwFont::new(MOON_GLOSS_16).unwrap();
        assert_eq!(
            font.measure("coffee ☕"),
            Err(VlwError::UnsupportedCharacter('☕'))
        );
    }
}
