#![allow(dead_code)] // Last-scale persistence is consumed by the upcoming scale manager.

use esp_idf_svc::nvs::{EspDefaultNvs, EspDefaultNvsPartition, EspNvs};
use esp_idf_sys::EspError;

const NAMESPACE: &str = "mxcoffee";
const KEY_BLUETOOTH_ENABLED: &str = "bt_enabled";
const KEY_LAST_SCALE_ADDRESS: &str = "scale_addr";
const KEY_LAST_SCALE_ADDRESS_TYPE: &str = "scale_addr_typ";
const KEY_LAST_SCALE_TYPE: &str = "scale_type";
const KEY_LAST_SCALE_NAME: &str = "scale_name";

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LastScaleConfig {
    pub address: String,
    pub name: String,
    pub address_type: u8,
    pub scale_type: u8,
}

pub struct Settings {
    nvs: EspDefaultNvs,
}

impl Settings {
    pub fn new(partition: EspDefaultNvsPartition) -> Result<Self, EspError> {
        let nvs = EspNvs::new(partition, NAMESPACE, true)?;
        Ok(Self { nvs })
    }

    pub fn bluetooth_enabled(&self) -> Result<bool, EspError> {
        Ok(self.nvs.get_u8(KEY_BLUETOOTH_ENABLED)?.unwrap_or(0) != 0)
    }

    pub fn set_bluetooth_enabled(&self, enabled: bool) -> Result<(), EspError> {
        self.nvs.set_u8(KEY_BLUETOOTH_ENABLED, u8::from(enabled))
    }

    pub fn load_last_scale(&self) -> Result<Option<LastScaleConfig>, EspError> {
        let Some(address_len) = self.nvs.str_len(KEY_LAST_SCALE_ADDRESS)? else {
            return Ok(None);
        };
        let Some(name_len) = self.nvs.str_len(KEY_LAST_SCALE_NAME)? else {
            return Ok(None);
        };
        let Some(address_type) = self.nvs.get_u8(KEY_LAST_SCALE_ADDRESS_TYPE)? else {
            return Ok(None);
        };
        let Some(scale_type) = self.nvs.get_u8(KEY_LAST_SCALE_TYPE)? else {
            return Ok(None);
        };
        if scale_type == 0 {
            return Ok(None);
        }

        let mut address_buf = vec![0u8; address_len];
        let mut name_buf = vec![0u8; name_len];
        let Some(address) = self.nvs.get_str(KEY_LAST_SCALE_ADDRESS, &mut address_buf)? else {
            return Ok(None);
        };
        let Some(name) = self.nvs.get_str(KEY_LAST_SCALE_NAME, &mut name_buf)? else {
            return Ok(None);
        };

        Ok(Some(LastScaleConfig {
            address: address.to_owned(),
            name: name.to_owned(),
            address_type,
            scale_type,
        }))
    }

    pub fn save_last_scale(&mut self, scale: &LastScaleConfig) -> Result<(), EspError> {
        if scale.address.is_empty() || scale.scale_type == 0 {
            return Err(EspError::from_infallible::<
                { esp_idf_sys::ESP_ERR_INVALID_ARG },
            >());
        }

        self.nvs.set_str(KEY_LAST_SCALE_ADDRESS, &scale.address)?;
        self.nvs
            .set_u8(KEY_LAST_SCALE_ADDRESS_TYPE, scale.address_type)?;
        self.nvs.set_u8(KEY_LAST_SCALE_TYPE, scale.scale_type)?;
        self.nvs.set_str(KEY_LAST_SCALE_NAME, &scale.name)
    }

    pub fn clear_last_scale(&mut self) -> Result<(), EspError> {
        self.nvs.remove(KEY_LAST_SCALE_ADDRESS)?;
        self.nvs.remove(KEY_LAST_SCALE_ADDRESS_TYPE)?;
        self.nvs.remove(KEY_LAST_SCALE_TYPE)?;
        self.nvs.remove(KEY_LAST_SCALE_NAME)?;
        Ok(())
    }
}
