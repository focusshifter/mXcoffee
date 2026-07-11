#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum ScaleType {
    Unknown = 0,
    LfSmartScale = 1,
}

pub fn decode_lf_smart_scale_weight(payload: &[u8]) -> Option<f32> {
    if payload.len() < 6 {
        return None;
    }

    let tenths = u16::from_le_bytes([payload[3], payload[4]]) as f32;
    let sign = if payload[5] == 1 { -1.0 } else { 1.0 };
    Some(sign * tenths / 10.0)
}

pub fn matches_lf_smart_scale_name(name: &[u8]) -> bool {
    const MATCH: &[u8] = b"lfsmart scale";
    name.windows(MATCH.len()).any(|window| {
        window
            .iter()
            .zip(MATCH)
            .all(|(left, right)| left.eq_ignore_ascii_case(right))
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn decodes_positive_tenths() {
        assert_eq!(
            decode_lf_smart_scale_weight(&[0, 0, 0, 0xd2, 0x04, 0]),
            Some(123.4)
        );
    }

    #[test]
    fn decodes_negative_sign_byte() {
        assert_eq!(
            decode_lf_smart_scale_weight(&[0, 0, 0, 0x2a, 0, 1]),
            Some(-4.2)
        );
    }

    #[test]
    fn rejects_short_packets() {
        assert_eq!(decode_lf_smart_scale_weight(&[0, 0, 0, 0x2a, 0]), None);
    }

    #[test]
    fn stored_type_ids_match_cpp() {
        assert_eq!(ScaleType::Unknown as u8, 0);
        assert_eq!(ScaleType::LfSmartScale as u8, 1);
    }

    #[test]
    fn matches_lf_scale_names_case_insensitively() {
        assert!(matches_lf_smart_scale_name(b"LFSMART SCALE"));
        assert!(matches_lf_smart_scale_name(b"My lfsmart scale 2"));
        assert!(!matches_lf_smart_scale_name(b"Coffee scale"));
    }
}
