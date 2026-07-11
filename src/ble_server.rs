use std::sync::{Arc, Mutex};

use enumset::enum_set;
use esp_idf_hal::modem::Modem;
use esp_idf_svc::bt::ble::gap::{AdvConfiguration, BleGapEvent, EspBleGap};
use esp_idf_svc::bt::ble::gatt::server::{ConnectionId, EspGatts, GattsEvent};
use esp_idf_svc::bt::ble::gatt::{
    AutoResponse, GattCharacteristic, GattId, GattInterface, GattServiceId, GattStatus, Handle,
    Permission, Property,
};
use esp_idf_svc::bt::{Ble, BtDriver, BtStatus, BtUuid};
use esp_idf_svc::nvs::EspDefaultNvsPartition;
use esp_idf_sys::EspError;
use mxcoffee::ble_protocol::{clamp_battery_level, PressureState, DEVICE_NAME};

const APP_ID: u16 = 0;
const BATTERY_SERVICE_UUID: u16 = 0x180f;
const BATTERY_CHARACTERISTIC_UUID: u16 = 0x2a19;
const LOG_SERVICE_UUID: u128 = 0x873ae8284c5a4342b5399d900bf7ebd0;
const LOG_CHARACTERISTIC_UUID: u128 = 0x873ae8294c5a4342b5399d900bf7ebd0;
const PRESSURE_SERVICE_UUID: u128 = 0x873ae82a4c5a4342b5399d900bf7ebd0;
const PRESSURE_CHARACTERISTIC_UUID: u128 = 0x873ae82b4c5a4342b5399d900bf7ebd0;
const PRESSURE_ZERO_CHARACTERISTIC_UUID: u128 = 0x873ae82c4c5a4342b5399d900bf7ebd0;

type Driver = BtDriver<'static, Ble>;
type Gap = Arc<EspBleGap<'static, Ble, Arc<Driver>>>;
type Gatts = Arc<EspGatts<'static, Ble, Arc<Driver>>>;

#[derive(Default)]
struct State {
    enabled: bool,
    advertising_configured: bool,
    gatt_if: Option<GattInterface>,
    connection_id: Option<ConnectionId>,
    battery_service: Option<Handle>,
    log_service: Option<Handle>,
    pressure_service: Option<Handle>,
    battery_handle: Option<Handle>,
    log_handle: Option<Handle>,
    pressure_handle: Option<Handle>,
    zero_handle: Option<Handle>,
    pressure: PressureState,
}

pub struct MxBleServer {
    gap: Gap,
    gatts: Gatts,
    state: Mutex<State>,
}

impl MxBleServer {
    pub fn new(
        modem: Modem,
        nvs: EspDefaultNvsPartition,
        enabled: bool,
    ) -> Result<Arc<Self>, EspError> {
        let driver = Arc::new(BtDriver::new(modem, Some(nvs))?);
        let server = Arc::new(Self {
            gap: Arc::new(EspBleGap::new(driver.clone())?),
            gatts: Arc::new(EspGatts::new(driver)?),
            state: Mutex::new(State {
                enabled,
                ..State::default()
            }),
        });

        let gap_server = server.clone();
        server.gap.subscribe(move |event| {
            if let Err(err) = gap_server.on_gap_event(event) {
                println!("BLE GAP event failed: {err:?}");
            }
        })?;

        let gatts_server = server.clone();
        server.gatts.subscribe(move |(gatt_if, event)| {
            if let Err(err) = gatts_server.on_gatts_event(gatt_if, event) {
                println!("BLE GATT event failed: {err:?}");
            }
        })?;
        server.gatts.register_app(APP_ID)?;

        Ok(server)
    }

    pub fn set_enabled(&self, enabled: bool) -> Result<(), EspError> {
        let configured = {
            let mut state = self.state.lock().unwrap();
            state.enabled = enabled;
            state.advertising_configured
        };
        if configured {
            if enabled {
                self.gap.start_advertising()
            } else {
                self.gap.stop_advertising()
            }
        } else {
            Ok(())
        }
    }

    pub fn is_connected(&self) -> bool {
        self.state.lock().unwrap().connection_id.is_some()
    }

    pub fn notify_pressure(&self, raw_pressure: i16) -> Result<bool, EspError> {
        let (gatt_if, conn_id, handle, payload) = {
            let mut state = self.state.lock().unwrap();
            state.pressure.update(raw_pressure);
            let Some(gatt_if) = state.gatt_if else {
                return Ok(false);
            };
            let Some(conn_id) = state.connection_id else {
                return Ok(false);
            };
            let Some(handle) = state.pressure_handle else {
                return Ok(false);
            };
            (gatt_if, conn_id, handle, state.pressure.encoded())
        };
        self.gatts.notify(gatt_if, conn_id, handle, &payload)?;
        Ok(true)
    }

    pub fn set_battery_level(&self, level: i32) -> Result<(), EspError> {
        let level = [clamp_battery_level(level)];
        let (gatt_if, connection_id, handle) = {
            let state = self.state.lock().unwrap();
            (state.gatt_if, state.connection_id, state.battery_handle)
        };
        if let Some(handle) = handle {
            self.gatts.set_attr(handle, &level)?;
            if let (Some(gatt_if), Some(connection_id)) = (gatt_if, connection_id) {
                self.gatts.notify(gatt_if, connection_id, handle, &level)?;
            }
        }
        Ok(())
    }

    pub fn log(&self, message: &[u8]) -> Result<bool, EspError> {
        let (gatt_if, connection_id, handle) = {
            let state = self.state.lock().unwrap();
            (state.gatt_if, state.connection_id, state.log_handle)
        };
        let (Some(gatt_if), Some(connection_id), Some(handle)) = (gatt_if, connection_id, handle)
        else {
            return Ok(false);
        };
        self.gatts.notify(gatt_if, connection_id, handle, message)?;
        Ok(true)
    }

    fn on_gap_event(&self, event: BleGapEvent) -> Result<(), EspError> {
        if let BleGapEvent::AdvertisingConfigured(status) = event {
            if status != BtStatus::Success {
                println!("BLE advertising configuration failed: {status:?}");
                return Ok(());
            }
            let enabled = {
                let mut state = self.state.lock().unwrap();
                state.advertising_configured = true;
                state.enabled
            };
            if enabled {
                self.gap.start_advertising()?;
            }
        }
        Ok(())
    }

    fn on_gatts_event(&self, gatt_if: GattInterface, event: GattsEvent) -> Result<(), EspError> {
        match event {
            GattsEvent::ServiceRegistered { status, app_id }
                if status == GattStatus::Ok && app_id == APP_ID =>
            {
                self.configure_and_create_services(gatt_if)?;
            }
            GattsEvent::ServiceCreated {
                status: GattStatus::Ok,
                service_handle,
                service_id,
            } => {
                self.configure_service(service_handle, service_id.id.uuid)?;
            }
            GattsEvent::CharacteristicAdded {
                status: GattStatus::Ok,
                attr_handle,
                char_uuid,
                ..
            } => self.register_characteristic(attr_handle, char_uuid),
            GattsEvent::PeerConnected { conn_id, .. } => {
                self.state.lock().unwrap().connection_id = Some(conn_id);
            }
            GattsEvent::PeerDisconnected { .. } => {
                let enabled = {
                    let mut state = self.state.lock().unwrap();
                    state.connection_id = None;
                    state.enabled
                };
                if enabled {
                    self.gap.start_advertising()?;
                }
            }
            GattsEvent::Write { handle, .. } => {
                let mut state = self.state.lock().unwrap();
                if state.zero_handle == Some(handle) {
                    state.pressure.zero();
                }
            }
            _ => {}
        }
        Ok(())
    }

    fn configure_and_create_services(&self, gatt_if: GattInterface) -> Result<(), EspError> {
        self.state.lock().unwrap().gatt_if = Some(gatt_if);
        self.gap.set_device_name(DEVICE_NAME)?;
        self.gap.set_adv_conf(&AdvConfiguration {
            include_name: true,
            include_txpower: true,
            flag: 2,
            service_uuid: Some(BtUuid::uuid128(PRESSURE_SERVICE_UUID)),
            ..AdvConfiguration::default()
        })?;

        for uuid in [
            BtUuid::uuid16(BATTERY_SERVICE_UUID),
            BtUuid::uuid128(LOG_SERVICE_UUID),
            BtUuid::uuid128(PRESSURE_SERVICE_UUID),
        ] {
            self.gatts.create_service(
                gatt_if,
                &GattServiceId {
                    id: GattId { uuid, inst_id: 0 },
                    is_primary: true,
                },
                8,
            )?;
        }
        Ok(())
    }

    fn configure_service(&self, service_handle: Handle, uuid: BtUuid) -> Result<(), EspError> {
        self.gatts.start_service(service_handle)?;
        let characteristics = if uuid == BtUuid::uuid16(BATTERY_SERVICE_UUID) {
            self.state.lock().unwrap().battery_service = Some(service_handle);
            vec![(
                BtUuid::uuid16(BATTERY_CHARACTERISTIC_UUID),
                enum_set!(Permission::Read),
                enum_set!(Property::Read | Property::Notify),
                1,
                vec![100],
            )]
        } else if uuid == BtUuid::uuid128(LOG_SERVICE_UUID) {
            self.state.lock().unwrap().log_service = Some(service_handle);
            vec![(
                BtUuid::uuid128(LOG_CHARACTERISTIC_UUID),
                enum_set!(Permission::Read),
                enum_set!(Property::Notify),
                200,
                Vec::new(),
            )]
        } else if uuid == BtUuid::uuid128(PRESSURE_SERVICE_UUID) {
            self.state.lock().unwrap().pressure_service = Some(service_handle);
            vec![
                (
                    BtUuid::uuid128(PRESSURE_CHARACTERISTIC_UUID),
                    enum_set!(Permission::Read),
                    enum_set!(Property::Read | Property::Notify),
                    2,
                    vec![0, 0],
                ),
                (
                    BtUuid::uuid128(PRESSURE_ZERO_CHARACTERISTIC_UUID),
                    enum_set!(Permission::Write),
                    enum_set!(Property::Write),
                    1,
                    Vec::new(),
                ),
            ]
        } else {
            Vec::new()
        };

        for (uuid, permissions, properties, max_len, value) in characteristics {
            self.gatts.add_characteristic(
                service_handle,
                &GattCharacteristic {
                    uuid,
                    permissions,
                    properties,
                    max_len,
                    auto_rsp: AutoResponse::ByGatt,
                },
                &value,
            )?;
        }
        Ok(())
    }

    fn register_characteristic(&self, handle: Handle, uuid: BtUuid) {
        let mut state = self.state.lock().unwrap();
        if uuid == BtUuid::uuid16(BATTERY_CHARACTERISTIC_UUID) {
            state.battery_handle = Some(handle);
        } else if uuid == BtUuid::uuid128(LOG_CHARACTERISTIC_UUID) {
            state.log_handle = Some(handle);
        } else if uuid == BtUuid::uuid128(PRESSURE_CHARACTERISTIC_UUID) {
            state.pressure_handle = Some(handle);
        } else if uuid == BtUuid::uuid128(PRESSURE_ZERO_CHARACTERISTIC_UUID) {
            state.zero_handle = Some(handle);
        }
    }
}
