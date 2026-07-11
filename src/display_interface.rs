use embedded_graphics::pixelcolor::Rgb565;
use embedded_hal::{digital::OutputPin, spi::SpiDevice};
use esp_idf_hal::spi::SpiError as HalSpiError;
use esp_idf_sys::{
    heap_caps_free, heap_caps_malloc, spi_device_acquire_bus, spi_device_get_trans_result,
    spi_device_handle_t, spi_device_queue_trans, spi_device_release_bus, spi_transaction_t,
    EspError, MALLOC_CAP_8BIT, MALLOC_CAP_DMA, SPI_TRANS_CS_KEEP_ACTIVE,
};
use mipidsi::interface::{Interface, SpiError};

const DMA_BUFFER_COUNT: usize = 2;
const DMA_TIMEOUT_TICKS: u32 = 1_000;

struct DmaBuffer {
    ptr: *mut u8,
    len: usize,
}

unsafe impl Send for DmaBuffer {}

impl DmaBuffer {
    fn allocate(len: usize) -> Self {
        let ptr = unsafe { heap_caps_malloc(len, MALLOC_CAP_DMA | MALLOC_CAP_8BIT).cast::<u8>() };
        assert!(!ptr.is_null(), "failed to allocate display DMA buffer");
        Self { ptr, len }
    }

    fn copy_from_swapped(&mut self, source: &[u8]) {
        assert!(source.len() <= self.len);
        assert_eq!(source.len() % 2, 0);

        let words = source.len() / 4;
        for index in 0..words {
            let value = unsafe { source.as_ptr().cast::<u32>().add(index).read_unaligned() };
            let swapped = ((value & 0x00FF_00FF) << 8) | ((value & 0xFF00_FF00) >> 8);
            unsafe { self.ptr.cast::<u32>().add(index).write_unaligned(swapped) };
        }

        if source.len() % 4 != 0 {
            let index = words * 4;
            unsafe {
                self.ptr.add(index).write(source[index + 1]);
                self.ptr.add(index + 1).write(source[index]);
            }
        }
    }
}

impl Drop for DmaBuffer {
    fn drop(&mut self) {
        unsafe { heap_caps_free(self.ptr.cast()) };
    }
}

pub struct FastSpiInterface<'a, SPI, DC> {
    spi: SPI,
    dc: DC,
    buffer: &'a mut [u8],
    raw_device: spi_device_handle_t,
    dma_buffers: [DmaBuffer; DMA_BUFFER_COUNT],
    dma_transactions: [spi_transaction_t; DMA_BUFFER_COUNT],
}

unsafe impl<SPI: Send, DC: Send> Send for FastSpiInterface<'_, SPI, DC> {}

impl<'a, SPI: SpiDevice, DC: OutputPin> FastSpiInterface<'a, SPI, DC> {
    pub fn new(spi: SPI, dc: DC, buffer: &'a mut [u8], raw_device: spi_device_handle_t) -> Self {
        Self {
            spi,
            dc,
            raw_device,
            dma_buffers: core::array::from_fn(|_| DmaBuffer::allocate(buffer.len())),
            dma_transactions: core::array::from_fn(|_| spi_transaction_t::default()),
            buffer,
        }
    }
}

impl<SPI, DC> FastSpiInterface<'_, SPI, DC>
where
    SPI: SpiDevice<Error = HalSpiError>,
    DC: OutputPin,
{
    pub fn send_frame_queued(
        &mut self,
        pixels: &[Rgb565],
    ) -> Result<(), SpiError<HalSpiError, DC::Error>> {
        self.send_command(0x2A, &[0, 0, 0x01, 0x3F])?;
        self.send_command(0x2B, &[0, 0, 0, 0xEF])?;
        self.send_command(0x2C, &[])?;

        let bytes =
            unsafe { core::slice::from_raw_parts(pixels.as_ptr().cast::<u8>(), pixels.len() * 2) };
        self.queue_bytes(bytes)
            .map_err(HalSpiError::from)
            .map_err(SpiError::Spi)
    }

    fn queue_bytes(&mut self, bytes: &[u8]) -> Result<(), EspError> {
        esp_result(unsafe { spi_device_acquire_bus(self.raw_device, u32::MAX) })?;

        let result = self.queue_bytes_locked(bytes);
        unsafe { spi_device_release_bus(self.raw_device) };
        result
    }

    fn queue_bytes_locked(&mut self, bytes: &[u8]) -> Result<(), EspError> {
        let mut available = [0usize, 1usize];
        let mut available_count = DMA_BUFFER_COUNT;
        let mut in_flight = 0usize;

        for (chunk_index, source) in bytes.chunks(self.buffer.len()).enumerate() {
            if available_count == 0 {
                let completed = self.wait_for_transaction()?;
                available[available_count] = completed;
                available_count += 1;
                in_flight -= 1;
            }

            available_count -= 1;
            let slot = available[available_count];
            self.dma_buffers[slot].copy_from_swapped(source);

            let is_last = (chunk_index + 1) * self.buffer.len() >= bytes.len();
            let transaction = &mut self.dma_transactions[slot];
            *transaction = spi_transaction_t::default();
            transaction.flags = if is_last { 0 } else { SPI_TRANS_CS_KEEP_ACTIVE };
            transaction.length = source.len() * 8;
            transaction.__bindgen_anon_1.tx_buffer = self.dma_buffers[slot].ptr.cast();

            esp_result(unsafe {
                spi_device_queue_trans(self.raw_device, transaction, DMA_TIMEOUT_TICKS)
            })?;
            in_flight += 1;
        }

        while in_flight > 0 {
            self.wait_for_transaction()?;
            in_flight -= 1;
        }

        Ok(())
    }

    fn wait_for_transaction(&mut self) -> Result<usize, EspError> {
        let mut completed = core::ptr::null_mut();
        esp_result(unsafe {
            spi_device_get_trans_result(self.raw_device, &mut completed, DMA_TIMEOUT_TICKS)
        })?;

        self.dma_transactions
            .iter_mut()
            .position(|transaction| core::ptr::eq(transaction, completed))
            .ok_or_else(EspError::from_infallible::<{ esp_idf_sys::ESP_ERR_INVALID_STATE }>)
    }
}

fn esp_result(code: i32) -> Result<(), EspError> {
    if code == esp_idf_sys::ESP_OK {
        Ok(())
    } else {
        Err(EspError::from(code).expect("ESP-IDF returned an invalid error code"))
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
