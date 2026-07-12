//! Pure-Rust ESP32 SPI-DMA display transport.
//!
//! The transaction, descriptor, and DMA reset sequence is source-derived from
//! M5GFX 0.2.0 `Bus_SPI.cpp` (M5Stack, MIT) and its upstream LovyanGFX
//! implementation (FreeBSD). Neither C++ library is compiled or linked.
//! <https://github.com/m5stack/M5GFX/blob/0.2.0/src/lgfx/v1/platforms/esp32/Bus_SPI.cpp>
//! <https://github.com/m5stack/M5GFX/blob/0.2.0/LICENSE>
//! <https://github.com/lovyan03/LovyanGFX/blob/master/license.txt>

use embedded_graphics::pixelcolor::Rgb565;
use embedded_hal::{digital::OutputPin, spi::SpiDevice};
use esp_idf_hal::spi::SpiError as HalSpiError;
use esp_idf_sys::{
    esp_timer_get_time, heap_caps_free, heap_caps_malloc, spi_device_acquire_bus,
    spi_device_handle_t, spi_device_release_bus, EspError, MALLOC_CAP_8BIT, MALLOC_CAP_DMA,
};
#[cfg(feature = "benchmark-soak")]
use esp_idf_sys::{
    heap_caps_get_free_size, heap_caps_get_minimum_free_size, MALLOC_CAP_INTERNAL,
    MALLOC_CAP_SPIRAM,
};
use mipidsi::interface::{Interface, SpiError};

const DMA_BUFFER_COUNT: usize = 2;
const DMA_DESCRIPTOR_COUNT: usize = 5;
const DMA_MAX_PAYLOAD: usize = 4_092;
const DMA_TIMEOUT_US: i64 = 100_000;

const SPI2_BASE: usize = 0x3FF6_4000;
const SPI_CMD_OFFSET: usize = 0x00;
const SPI_CLOCK_OFFSET: usize = 0x18;
const SPI_USER_OFFSET: usize = 0x1C;
const SPI_MOSI_DLEN_OFFSET: usize = 0x28;
const SPI_PIN_OFFSET: usize = 0x34;
const SPI_DMA_CONF_OFFSET: usize = 0x100;
const SPI_DMA_OUT_LINK_OFFSET: usize = 0x104;
const SPI_W0_OFFSET: usize = 0x80;
const SPI_USR: u32 = 1 << 18;
const SPI_USR_MOSI: u32 = 1 << 27;
const SPI_40MHZ_CLOCK_DIVIDER: u32 = 0x1001;
const SPI_DISABLE_ALL_CS: u32 = 0x7;
const SPI_OUT_DATA_BURST_EN: u32 = 1 << 12;
const SPI_OUTDSCR_BURST_EN: u32 = 1 << 10;
const SPI_AHBM_RST: u32 = 1 << 5;
const SPI_AHBM_FIFO_RST: u32 = 1 << 4;
const SPI_OUT_RST: u32 = 1 << 3;
const SPI_OUTLINK_START: u32 = 1 << 29;
const SPI_OUTLINK_ADDR_MASK: u32 = 0x000F_FFFF;
const DPORT_SPI_DMA_CHAN_SEL: *const u32 = 0x3FF0_05A8 as *const u32;

#[cfg(feature = "benchmark-soak")]
#[derive(Debug, Clone, Copy)]
pub struct HeapMetrics {
    pub internal_free: usize,
    pub internal_minimum_free: usize,
    pub psram_free: usize,
    pub psram_minimum_free: usize,
}

#[cfg(feature = "benchmark-soak")]
pub fn heap_metrics() -> HeapMetrics {
    HeapMetrics {
        internal_free: unsafe { heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) },
        internal_minimum_free: unsafe {
            heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
        },
        psram_free: unsafe { heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) },
        psram_minimum_free: unsafe {
            heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        },
    }
}

unsafe extern "C" {
    fn spicommon_dmaworkaround_idle(dma_channel: i32);
    fn spicommon_dmaworkaround_transfer_active(dma_channel: i32);
}

#[repr(C)]
struct DmaDescriptor {
    control: u32,
    buffer: *const u8,
    next: *mut DmaDescriptor,
}

struct DmaBuffer {
    ptr: *mut u8,
    len: usize,
    descriptors: *mut DmaDescriptor,
}

unsafe impl Send for DmaBuffer {}

impl DmaBuffer {
    fn allocate(len: usize) -> Self {
        let ptr = unsafe { heap_caps_malloc(len, MALLOC_CAP_DMA | MALLOC_CAP_8BIT).cast::<u8>() };
        assert!(!ptr.is_null(), "failed to allocate display DMA buffer");
        let descriptors = unsafe {
            heap_caps_malloc(
                DMA_DESCRIPTOR_COUNT * core::mem::size_of::<DmaDescriptor>(),
                MALLOC_CAP_DMA | MALLOC_CAP_8BIT,
            )
            .cast::<DmaDescriptor>()
        };
        assert!(
            !descriptors.is_null(),
            "failed to allocate display DMA descriptors"
        );
        Self {
            ptr,
            len,
            descriptors,
        }
    }

    fn copy_from_swapped(&mut self, source: &[u8]) {
        assert!(source.len() <= self.len);
        assert_eq!(source.len() % 2, 0);
        assert_eq!(
            source.as_ptr().align_offset(core::mem::align_of::<u32>()),
            0
        );
        assert_eq!(self.ptr.align_offset(core::mem::align_of::<u32>()), 0);

        let words = source.len() / 4;
        for index in 0..words {
            let value = unsafe { source.as_ptr().cast::<u32>().add(index).read() };
            let swapped = ((value & 0x00FF_00FF) << 8) | ((value & 0xFF00_FF00) >> 8);
            unsafe { self.ptr.cast::<u32>().add(index).write(swapped) };
        }

        if source.len() % 4 != 0 {
            let index = words * 4;
            unsafe {
                self.ptr.add(index).write(source[index + 1]);
                self.ptr.add(index + 1).write(source[index]);
            }
        }
    }

    fn prepare_descriptors(&mut self, transfer_len: usize) -> *mut DmaDescriptor {
        assert!(transfer_len > 0 && transfer_len <= self.len);
        let descriptor_count = transfer_len.div_ceil(DMA_MAX_PAYLOAD);
        assert!(descriptor_count <= DMA_DESCRIPTOR_COUNT);

        let mut offset = 0usize;
        for index in 0..descriptor_count {
            let len = (transfer_len - offset).min(DMA_MAX_PAYLOAD);
            let is_last = index + 1 == descriptor_count;
            let descriptor = unsafe { self.descriptors.add(index) };
            let size = if is_last { (len + 3) & !3 } else { len };
            let control = size as u32 | ((len as u32) << 12) | ((is_last as u32) << 30) | (1 << 31);
            let next = if is_last {
                core::ptr::null_mut()
            } else {
                unsafe { self.descriptors.add(index + 1) }
            };
            unsafe {
                descriptor.write(DmaDescriptor {
                    control,
                    buffer: self.ptr.add(offset),
                    next,
                });
            }
            offset += len;
        }

        self.descriptors
    }
}

impl Drop for DmaBuffer {
    fn drop(&mut self) {
        unsafe {
            heap_caps_free(self.descriptors.cast());
            heap_caps_free(self.ptr.cast());
        };
    }
}

pub struct FastSpiInterface<'a, SPI, DC, CS> {
    spi: SPI,
    dc: DC,
    cs: CS,
    buffer: &'a mut [u8],
    raw_device: spi_device_handle_t,
    dma_channel: i32,
    dma_buffers: [DmaBuffer; DMA_BUFFER_COUNT],
}

unsafe impl<SPI: Send, DC: Send, CS: Send> Send for FastSpiInterface<'_, SPI, DC, CS> {}

impl<'a, SPI: SpiDevice, DC: OutputPin, CS: OutputPin> FastSpiInterface<'a, SPI, DC, CS> {
    pub fn new(
        spi: SPI,
        dc: DC,
        cs: CS,
        buffer: &'a mut [u8],
        raw_device: spi_device_handle_t,
    ) -> Self {
        let dma_channel = ((unsafe { DPORT_SPI_DMA_CHAN_SEL.read_volatile() } >> 2) & 3) as i32;
        assert!(dma_channel == 1 || dma_channel == 2);
        Self {
            spi,
            dc,
            cs,
            raw_device,
            dma_channel,
            dma_buffers: core::array::from_fn(|_| DmaBuffer::allocate(buffer.len())),
            buffer,
        }
    }
}

impl<SPI, DC, CS> FastSpiInterface<'_, SPI, DC, CS>
where
    SPI: SpiDevice<Error = HalSpiError>,
    DC: OutputPin,
    CS: OutputPin<Error = DC::Error>,
{
    pub fn send_frame_queued(
        &mut self,
        pixels: &[Rgb565],
    ) -> Result<(), SpiError<HalSpiError, DC::Error>> {
        let bytes =
            unsafe { core::slice::from_raw_parts(pixels.as_ptr().cast::<u8>(), pixels.len() * 2) };
        esp_result(unsafe { spi_device_acquire_bus(self.raw_device, u32::MAX) })
            .map_err(HalSpiError::from)
            .map_err(SpiError::Spi)?;
        self.cs.set_low().map_err(SpiError::Dc)?;
        unsafe {
            register(SPI_USER_OFFSET).write_volatile(SPI_USR_MOSI);
            register(SPI_PIN_OFFSET).write_volatile(SPI_DISABLE_ALL_CS);
            register(SPI_CLOCK_OFFSET).write_volatile(SPI_40MHZ_CLOCK_DIVIDER);
        }

        let transfer_result = (|| {
            self.write_raw(false, &[0x2A])?;
            self.write_raw(true, &[0, 0, 0x01, 0x3F])?;
            self.write_raw(false, &[0x2B])?;
            self.write_raw(true, &[0, 0, 0, 0xEF])?;
            self.write_raw(false, &[0x2C])?;
            self.wait_for_dma()
                .map_err(HalSpiError::from)
                .map_err(SpiError::Spi)?;
            self.dc.set_high().map_err(SpiError::Dc)?;
            self.queue_bytes_locked(bytes)
                .map_err(HalSpiError::from)
                .map_err(SpiError::Spi)
        })();

        unsafe { register(SPI_DMA_OUT_LINK_OFFSET).write_volatile(0) };
        unsafe { spicommon_dmaworkaround_idle(self.dma_channel) };
        unsafe { spi_device_release_bus(self.raw_device) };
        let cs_result = self.cs.set_high().map_err(SpiError::Dc);
        transfer_result.and(cs_result)
    }

    fn write_raw(
        &mut self,
        data_mode: bool,
        bytes: &[u8],
    ) -> Result<(), SpiError<HalSpiError, DC::Error>> {
        debug_assert!(!bytes.is_empty() && bytes.len() <= 4);
        self.wait_for_dma()
            .map_err(HalSpiError::from)
            .map_err(SpiError::Spi)?;
        if data_mode {
            self.dc.set_high().map_err(SpiError::Dc)?;
        } else {
            self.dc.set_low().map_err(SpiError::Dc)?;
        }

        let mut word = 0u32;
        for (shift, byte) in bytes.iter().enumerate() {
            word |= u32::from(*byte) << (shift * 8);
        }
        unsafe {
            register(SPI_MOSI_DLEN_OFFSET).write_volatile((bytes.len() as u32 * 8) - 1);
            register(SPI_W0_OFFSET).write_volatile(word);
            register(SPI_CMD_OFFSET).write_volatile(SPI_USR);
        }
        Ok(())
    }

    fn queue_bytes_locked(&mut self, bytes: &[u8]) -> Result<(), EspError> {
        // Ported from LovyanGFX/M5GFX Bus_SPI::writeBytes and
        // Bus_SPI::_setup_dma_desc_links. ESP-IDF owns bus allocation and
        // locking; this Rust implementation owns the acquired transfer window.
        self.wait_for_dma()?;
        let mut in_flight = false;
        for (chunk_index, source) in bytes.chunks(self.buffer.len()).enumerate() {
            let slot = chunk_index % DMA_BUFFER_COUNT;
            self.dma_buffers[slot].copy_from_swapped(source);
            let descriptors = self.dma_buffers[slot].prepare_descriptors(source.len());

            if in_flight {
                self.wait_for_dma()?;
            }
            self.start_dma(descriptors, source.len());
            in_flight = true;
        }

        self.wait_for_dma()
    }

    fn start_dma(&self, descriptors: *mut DmaDescriptor, len: usize) {
        let dma_conf = register(SPI_DMA_CONF_OFFSET);
        let out_link = register(SPI_DMA_OUT_LINK_OFFSET);
        let mosi_dlen = register(SPI_MOSI_DLEN_OFFSET);
        let command = register(SPI_CMD_OFFSET);

        unsafe {
            let mut conf = dma_conf.read_volatile()
                & !(SPI_OUT_DATA_BURST_EN | SPI_AHBM_RST | SPI_AHBM_FIFO_RST | SPI_OUT_RST);
            dma_conf.write_volatile(conf | SPI_AHBM_RST | SPI_AHBM_FIFO_RST | SPI_OUT_RST);
            conf |= SPI_OUTDSCR_BURST_EN;
            if len & 3 == 0 {
                conf |= SPI_OUT_DATA_BURST_EN;
            }
            dma_conf.write_volatile(conf);
            out_link.write_volatile(0);
            out_link
                .write_volatile(SPI_OUTLINK_START | (descriptors as u32 & SPI_OUTLINK_ADDR_MASK));
            mosi_dlen.write_volatile((len as u32 * 8) - 1);
            spicommon_dmaworkaround_transfer_active(self.dma_channel);
            command.write_volatile(SPI_USR);
        }
    }

    fn wait_for_dma(&self) -> Result<(), EspError> {
        let command = register(SPI_CMD_OFFSET);
        let started = unsafe { esp_timer_get_time() };
        while unsafe { command.read_volatile() } & SPI_USR != 0 {
            if unsafe { esp_timer_get_time() }.saturating_sub(started) >= DMA_TIMEOUT_US {
                return Err(EspError::from_infallible::<{ esp_idf_sys::ESP_ERR_TIMEOUT }>());
            }
            core::hint::spin_loop();
        }
        Ok(())
    }
}

fn register(offset: usize) -> *mut u32 {
    (SPI2_BASE + offset) as *mut u32
}

fn esp_result(code: i32) -> Result<(), EspError> {
    if code == esp_idf_sys::ESP_OK {
        Ok(())
    } else {
        Err(EspError::from(code).expect("ESP-IDF returned an invalid error code"))
    }
}

impl<SPI, DC, CS> Interface for FastSpiInterface<'_, SPI, DC, CS>
where
    SPI: SpiDevice,
    DC: OutputPin,
    CS: OutputPin<Error = DC::Error>,
{
    type Word = u8;
    type Error = SpiError<SPI::Error, DC::Error>;

    fn send_command(&mut self, command: u8, args: &[u8]) -> Result<(), Self::Error> {
        self.cs.set_low().map_err(SpiError::Dc)?;
        let transfer_result = (|| {
            self.dc.set_low().map_err(SpiError::Dc)?;
            self.spi.write(&[command]).map_err(SpiError::Spi)?;
            self.dc.set_high().map_err(SpiError::Dc)?;
            self.spi.write(args).map_err(SpiError::Spi)
        })();
        let cs_result = self.cs.set_high().map_err(SpiError::Dc);
        transfer_result.and(cs_result)
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
