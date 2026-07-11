use core::convert::Infallible;

use embedded_graphics::{
    geometry::{OriginDimensions, Size},
    pixelcolor::Rgb565,
    prelude::{DrawTarget, Pixel, Point},
    primitives::Rectangle,
};

pub fn clear_black(pixels: &mut [Rgb565]) {
    // Every RGB565 bit pattern is valid, and black is represented by zero.
    unsafe { core::ptr::write_bytes(pixels.as_mut_ptr(), 0, pixels.len()) };
}

pub fn clear_black_rect(
    pixels: &mut [Rgb565],
    width: usize,
    x: usize,
    y: usize,
    rect_width: usize,
    rect_height: usize,
) {
    assert!(width > 0 && pixels.len() % width == 0);
    let height = pixels.len() / width;
    assert!(x + rect_width <= width && y + rect_height <= height);
    for row in y..y + rect_height {
        let start = row * width + x;
        clear_black(&mut pixels[start..start + rect_width]);
    }
}

pub struct FastFrameBuffer<'a> {
    pixels: &'a mut [Rgb565],
    width: usize,
    height: usize,
}

impl<'a> FastFrameBuffer<'a> {
    pub fn new(pixels: &'a mut [Rgb565], width: usize, height: usize) -> Self {
        assert_eq!(pixels.len(), width * height);
        Self {
            pixels,
            width,
            height,
        }
    }
}

impl OriginDimensions for FastFrameBuffer<'_> {
    fn size(&self) -> Size {
        Size::new(self.width as u32, self.height as u32)
    }
}

impl DrawTarget for FastFrameBuffer<'_> {
    type Color = Rgb565;
    type Error = Infallible;

    fn draw_iter<I>(&mut self, pixels: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Pixel<Self::Color>>,
    {
        for Pixel(Point { x, y }, color) in pixels {
            if x >= 0 && x < self.width as i32 && y >= 0 && y < self.height as i32 {
                let index = y as usize * self.width + x as usize;
                unsafe { *self.pixels.get_unchecked_mut(index) = color };
            }
        }
        Ok(())
    }

    fn fill_solid(&mut self, area: &Rectangle, color: Self::Color) -> Result<(), Self::Error> {
        let x0 = area.top_left.x.max(0).min(self.width as i32) as usize;
        let y0 = area.top_left.y.max(0).min(self.height as i32) as usize;
        let x1 = area
            .top_left
            .x
            .saturating_add(area.size.width as i32)
            .max(0)
            .min(self.width as i32) as usize;
        let y1 = area
            .top_left
            .y
            .saturating_add(area.size.height as i32)
            .max(0)
            .min(self.height as i32) as usize;

        if x0 >= x1 || y0 >= y1 {
            return Ok(());
        }

        for y in y0..y1 {
            let row_start = y * self.width;
            self.pixels[row_start + x0..row_start + x1].fill(color);
        }
        Ok(())
    }

    fn clear(&mut self, color: Self::Color) -> Result<(), Self::Error> {
        self.pixels.fill(color);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use embedded_graphics::{
        pixelcolor::RgbColor,
        prelude::Primitive,
        primitives::{PrimitiveStyle, Rectangle},
        Drawable,
    };

    use super::*;

    #[test]
    fn draws_pixels_and_clips_solid_rectangles() {
        let mut pixels = [Rgb565::BLACK; 12];
        let mut target = FastFrameBuffer::new(&mut pixels, 4, 3);

        target
            .draw_iter([Pixel(Point::new(3, 2), Rgb565::RED)])
            .unwrap();
        Rectangle::new(Point::new(-1, 0), Size::new(3, 2))
            .into_styled(PrimitiveStyle::with_fill(Rgb565::GREEN))
            .draw(&mut target)
            .unwrap();

        assert_eq!(pixels[0], Rgb565::GREEN);
        assert_eq!(pixels[1], Rgb565::GREEN);
        assert_eq!(pixels[2], Rgb565::BLACK);
        assert_eq!(pixels[11], Rgb565::RED);
    }

    #[test]
    fn clears_framebuffer_with_zeroed_rgb565_pixels() {
        let mut pixels = [Rgb565::WHITE; 16];
        clear_black(&mut pixels);
        assert!(pixels.iter().all(|pixel| *pixel == Rgb565::BLACK));
    }

    #[test]
    fn clears_only_the_requested_rectangle() {
        let mut pixels = [Rgb565::WHITE; 20];
        clear_black_rect(&mut pixels, 5, 1, 1, 3, 2);
        assert_eq!(pixels[0], Rgb565::WHITE);
        assert_eq!(pixels[6], Rgb565::BLACK);
        assert_eq!(pixels[8], Rgb565::BLACK);
        assert_eq!(pixels[10], Rgb565::WHITE);
        assert_eq!(pixels[13], Rgb565::BLACK);
        assert_eq!(pixels[19], Rgb565::WHITE);
    }
}
