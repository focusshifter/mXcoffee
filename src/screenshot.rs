const BASE64_ALPHABET: &[u8; 64] =
    b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Base64Error {
    OutputTooSmall,
}

pub fn encode_base64(input: &[u8], output: &mut [u8]) -> Result<usize, Base64Error> {
    let required = input.len().div_ceil(3) * 4;
    if output.len() < required {
        return Err(Base64Error::OutputTooSmall);
    }

    let mut input_offset = 0;
    let mut output_offset = 0;
    while input_offset + 3 <= input.len() {
        let value = (u32::from(input[input_offset]) << 16)
            | (u32::from(input[input_offset + 1]) << 8)
            | u32::from(input[input_offset + 2]);
        output[output_offset] = BASE64_ALPHABET[((value >> 18) & 0x3f) as usize];
        output[output_offset + 1] = BASE64_ALPHABET[((value >> 12) & 0x3f) as usize];
        output[output_offset + 2] = BASE64_ALPHABET[((value >> 6) & 0x3f) as usize];
        output[output_offset + 3] = BASE64_ALPHABET[(value & 0x3f) as usize];
        input_offset += 3;
        output_offset += 4;
    }

    let remaining = input.len() - input_offset;
    if remaining > 0 {
        let first = u32::from(input[input_offset]);
        let second = if remaining == 2 {
            u32::from(input[input_offset + 1])
        } else {
            0
        };
        let value = (first << 16) | (second << 8);
        output[output_offset] = BASE64_ALPHABET[((value >> 18) & 0x3f) as usize];
        output[output_offset + 1] = BASE64_ALPHABET[((value >> 12) & 0x3f) as usize];
        output[output_offset + 2] = if remaining == 2 {
            BASE64_ALPHABET[((value >> 6) & 0x3f) as usize]
        } else {
            b'='
        };
        output[output_offset + 3] = b'=';
    }

    Ok(required)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn encoded(input: &[u8]) -> String {
        let mut output = [0u8; 32];
        let len = encode_base64(input, &mut output).unwrap();
        core::str::from_utf8(&output[..len]).unwrap().into()
    }

    #[test]
    fn encodes_complete_and_padded_groups() {
        assert_eq!(encoded(b""), "");
        assert_eq!(encoded(b"f"), "Zg==");
        assert_eq!(encoded(b"fo"), "Zm8=");
        assert_eq!(encoded(b"foo"), "Zm9v");
        assert_eq!(encoded(b"foobar"), "Zm9vYmFy");
    }

    #[test]
    fn rejects_short_output() {
        assert_eq!(
            encode_base64(b"four", &mut [0u8; 7]),
            Err(Base64Error::OutputTooSmall)
        );
    }
}
