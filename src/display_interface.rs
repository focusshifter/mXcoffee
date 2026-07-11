use embedded_graphics::pixelcolor::Rgb565;
use embedded_hal::{digital::OutputPin, spi::SpiDevice};
use mipidsi::interface::{Interface, SpiError};

pub struct FastSpiInterface<'a, SPI, DC> {
    spi: SPI,
    dc: DC,
    buffer: &'a mut [u8],
}

impl<'a, SPI: SpiDevice, DC: OutputPin> FastSpiInterface<'a, SPI, DC> {
    pub fn new(spi: SPI, dc: DC, buffer: &'a mut [u8]) -> Self {
        Self { spi, dc, buffer }
    }

    pub fn send_frame(&mut self, pixels: &[Rgb565]) -> Result<(), SpiError<SPI::Error, DC::Error>> {
        self.send_command(0x2a, &[0, 0, 0x01, 0x3f])?;
        self.send_command(0x2b, &[0, 0, 0, 0xef])?;
        self.send_command(0x2c, &[])?;

        let bytes =
            unsafe { core::slice::from_raw_parts(pixels.as_ptr().cast::<u8>(), pixels.len() * 2) };
        for source in bytes.chunks(self.buffer.len()) {
            self.buffer[..source.len()].copy_from_slice(source);
            self.spi
                .write(&self.buffer[..source.len()])
                .map_err(SpiError::Spi)?;
        }
        Ok(())
    }
}

impl<SPI: SpiDevice, DC: OutputPin> Interface for FastSpiInterface<'_, SPI, DC> {
    type Word = u8;
    type Error = SpiError<SPI::Error, DC::Error>;

    fn send_command(&mut self, command: u8, args: &[u8]) -> Result<(), Self::Error> {
        self.dc.set_low().map_err(SpiError::Dc)?;
        self.spi.write(&[command]).map_err(SpiError::Spi)?;
        self.dc.set_high().map_err(SpiError::Dc)?;
        self.spi.write(args).map_err(SpiError::Spi)?;
        Ok(())
    }

    fn send_pixels<const N: usize>(
        &mut self,
        pixels: impl IntoIterator<Item = [u8; N]>,
    ) -> Result<(), Self::Error> {
        let mut pixels = pixels.into_iter();
        loop {
            let mut used = 0;
            for chunk in self.buffer.chunks_exact_mut(N) {
                let Some(pixel) = pixels.next() else {
                    break;
                };
                chunk.copy_from_slice(&pixel);
                used += N;
            }
            if used == 0 {
                return Ok(());
            }
            self.spi
                .write(&self.buffer[..used])
                .map_err(SpiError::Spi)?;
        }
    }

    fn send_repeated_pixel<const N: usize>(
        &mut self,
        pixel: [u8; N],
        mut count: u32,
    ) -> Result<(), Self::Error> {
        let capacity = self.buffer.len() / N;
        for chunk in self.buffer.chunks_exact_mut(N) {
            chunk.copy_from_slice(&pixel);
        }
        while count > 0 {
            let batch = capacity.min(count as usize);
            self.spi
                .write(&self.buffer[..batch * N])
                .map_err(SpiError::Spi)?;
            count -= batch as u32;
        }
        Ok(())
    }
}
