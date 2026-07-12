pub const DISPLAY_WIDTH: usize = 320;
pub const DISPLAY_HEIGHT: usize = 240;
pub const BYTES_PER_PIXEL: usize = 2;
pub const FRAME_BYTES: usize = DISPLAY_WIDTH * DISPLAY_HEIGHT * BYTES_PER_PIXEL;
pub const FRAME_PIXELS: usize = DISPLAY_WIDTH * DISPLAY_HEIGHT;

// ESP32 DMA descriptors have 12-bit size and length fields. ESP-IDF keeps
// non-final payloads four-byte aligned and caps them at 4092 bytes.
pub const MAX_DMA_PAYLOAD: usize = 4092;

pub const CASET: u8 = 0x2A;
pub const PASET: u8 = 0x2B;
pub const RAMWR: u8 = 0x2C;
pub const FULL_WIDTH_ARGS: [u8; 4] = [0x00, 0x00, 0x01, 0x3F];
pub const FULL_HEIGHT_ARGS: [u8; 4] = [0x00, 0x00, 0x00, 0xEF];

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DmaSegment {
    pub offset: usize,
    pub len: usize,
    pub eof: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TransferPlanError {
    Empty,
    OutputTooSmall,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct InvalidFrameLength {
    pub expected: usize,
    pub actual: usize,
}

#[derive(Debug)]
pub enum FrameTransferError<T> {
    InvalidFrameLength(InvalidFrameLength),
    Transport(T),
}

pub fn validate_frame_length(pixel_count: usize) -> Result<(), InvalidFrameLength> {
    if pixel_count == FRAME_PIXELS {
        Ok(())
    } else {
        Err(InvalidFrameLength {
            expected: FRAME_PIXELS,
            actual: pixel_count,
        })
    }
}

pub fn descriptor_count(total_len: usize) -> usize {
    total_len.div_ceil(MAX_DMA_PAYLOAD)
}

pub fn plan_segments(
    total_len: usize,
    output: &mut [DmaSegment],
) -> Result<usize, TransferPlanError> {
    if total_len == 0 {
        return Err(TransferPlanError::Empty);
    }

    let count = descriptor_count(total_len);
    if output.len() < count {
        return Err(TransferPlanError::OutputTooSmall);
    }

    let mut offset = 0;
    for (index, segment) in output[..count].iter_mut().enumerate() {
        let len = (total_len - offset).min(MAX_DMA_PAYLOAD);
        *segment = DmaSegment {
            offset,
            len,
            eof: index + 1 == count,
        };
        offset += len;
    }

    Ok(count)
}

pub const fn rgb565_wire_bytes(pixel: u16) -> [u8; 2] {
    pixel.to_be_bytes()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn full_frame_descriptor_plan_is_contiguous_and_bounded() {
        let mut segments = [DmaSegment {
            offset: 0,
            len: 0,
            eof: false,
        }; 40];
        let count = plan_segments(FRAME_BYTES, &mut segments).unwrap();

        assert_eq!(count, 38);
        assert_eq!(segments[0].offset, 0);
        assert_eq!(segments[0].len, MAX_DMA_PAYLOAD);
        assert_eq!(segments[count - 1].offset, 151_404);
        assert_eq!(segments[count - 1].len, 2_196);
        assert_eq!(
            segments
                .iter()
                .take(count)
                .map(|item| item.len)
                .sum::<usize>(),
            FRAME_BYTES
        );

        for pair in segments[..count].windows(2) {
            assert_eq!(pair[0].offset + pair[0].len, pair[1].offset);
            assert!(!pair[0].eof);
        }
        assert!(segments[count - 1].eof);
    }

    #[test]
    fn descriptor_boundaries_handle_exact_and_partial_payloads() {
        let mut segments = [DmaSegment {
            offset: 0,
            len: 0,
            eof: false,
        }; 2];

        assert_eq!(plan_segments(1, &mut segments), Ok(1));
        assert_eq!(segments[0].len, 1);
        assert!(segments[0].eof);

        assert_eq!(plan_segments(MAX_DMA_PAYLOAD, &mut segments), Ok(1));
        assert_eq!(segments[0].len, MAX_DMA_PAYLOAD);

        assert_eq!(plan_segments(MAX_DMA_PAYLOAD + 1, &mut segments), Ok(2));
        assert_eq!(segments[0].len, MAX_DMA_PAYLOAD);
        assert_eq!(segments[1].offset, MAX_DMA_PAYLOAD);
        assert_eq!(segments[1].len, 1);
        assert!(segments[1].eof);
    }

    #[test]
    fn rejects_empty_and_undersized_plans() {
        let mut no_segments = [];
        assert_eq!(
            plan_segments(0, &mut no_segments),
            Err(TransferPlanError::Empty)
        );
        assert_eq!(
            plan_segments(MAX_DMA_PAYLOAD + 1, &mut no_segments),
            Err(TransferPlanError::OutputTooSmall)
        );
    }

    #[test]
    fn rgb565_uses_display_wire_byte_order() {
        assert_eq!(rgb565_wire_bytes(0xF800), [0xF8, 0x00]);
        assert_eq!(rgb565_wire_bytes(0x07E0), [0x07, 0xE0]);
        assert_eq!(rgb565_wire_bytes(0x001F), [0x00, 0x1F]);
    }

    #[test]
    fn full_frame_window_matches_320_by_240_panel() {
        assert_eq!(FULL_WIDTH_ARGS, [0, 0, 1, 63]);
        assert_eq!(FULL_HEIGHT_ARGS, [0, 0, 0, 239]);
        assert_eq!(CASET, 0x2A);
        assert_eq!(PASET, 0x2B);
        assert_eq!(RAMWR, 0x2C);
    }

    #[test]
    fn full_frame_length_is_exact() {
        assert_eq!(validate_frame_length(FRAME_PIXELS), Ok(()));
        assert_eq!(
            validate_frame_length(FRAME_PIXELS - 1),
            Err(InvalidFrameLength {
                expected: FRAME_PIXELS,
                actual: FRAME_PIXELS - 1,
            })
        );
        assert_eq!(
            validate_frame_length(FRAME_PIXELS + 1),
            Err(InvalidFrameLength {
                expected: FRAME_PIXELS,
                actual: FRAME_PIXELS + 1,
            })
        );
    }
}
