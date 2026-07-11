use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

use esp32_nimble::utilities::mutex::Mutex as NimbleMutex;
use esp32_nimble::utilities::BleUuid;
use esp32_nimble::{
    uuid128, BLEAdvertisementData, BLECharacteristic, BLEDevice, BLEError, BLEScan,
    NimbleProperties,
};
use mxcoffee::ble_protocol::{clamp_battery_level, PressureState, DEVICE_NAME};
use mxcoffee::scale_protocol::{decode_lf_smart_scale_weight, matches_lf_smart_scale_name};

const LF_SERVICE_UUID: u16 = 0xfff0;
const LF_DATA_UUID: u16 = 0xfff4;

#[derive(Debug, Clone, Default)]
pub struct ScaleSnapshot {
    pub connected: bool,
    pub name: String,
    pub weight_grams: f32,
    pub samples: u64,
}

type Characteristic = Arc<NimbleMutex<BLECharacteristic>>;

pub struct MxBleServer {
    enabled: Arc<AtomicBool>,
    connected: Arc<AtomicBool>,
    pressure: Arc<Mutex<PressureState>>,
    battery_characteristic: Characteristic,
    log_characteristic: Characteristic,
    pressure_characteristic: Characteristic,
    scale: Arc<Mutex<ScaleSnapshot>>,
}

impl MxBleServer {
    pub fn new(enabled: bool) -> Result<Arc<Self>, BLEError> {
        let device = BLEDevice::take();
        BLEDevice::set_device_name(DEVICE_NAME)?;
        let advertising = device.get_advertising();
        let server = device.get_server();
        let enabled_flag = Arc::new(AtomicBool::new(enabled));
        let connected = Arc::new(AtomicBool::new(false));
        let pressure = Arc::new(Mutex::new(PressureState::default()));

        let connected_on_connect = connected.clone();
        let enabled_on_connect = enabled_flag.clone();
        server.on_connect(move |server, connection| {
            connected_on_connect.store(true, Ordering::Release);
            let _ = server.update_conn_params(connection.conn_handle(), 24, 48, 0, 60);
            if enabled_on_connect.load(Ordering::Acquire) {
                let _ = advertising.lock().start();
            }
        });

        let connected_on_disconnect = connected.clone();
        let enabled_on_disconnect = enabled_flag.clone();
        server.on_disconnect(move |_connection, _reason| {
            connected_on_disconnect.store(false, Ordering::Release);
            if enabled_on_disconnect.load(Ordering::Acquire) {
                let _ = advertising.lock().start();
            }
        });

        let battery_service = server.create_service(BleUuid::from_uuid16(0x180f));
        let battery_characteristic = battery_service.lock().create_characteristic(
            BleUuid::from_uuid16(0x2a19),
            NimbleProperties::READ | NimbleProperties::NOTIFY,
        );
        battery_characteristic.lock().set_value(&[100]);

        let log_service = server.create_service(uuid128!("873ae828-4c5a-4342-b539-9d900bf7ebd0"));
        let log_characteristic = log_service.lock().create_characteristic(
            uuid128!("873ae829-4c5a-4342-b539-9d900bf7ebd0"),
            NimbleProperties::READ | NimbleProperties::NOTIFY,
        );

        let pressure_service =
            server.create_service(uuid128!("873ae82a-4c5a-4342-b539-9d900bf7ebd0"));
        let pressure_characteristic = pressure_service.lock().create_characteristic(
            uuid128!("873ae82b-4c5a-4342-b539-9d900bf7ebd0"),
            NimbleProperties::READ | NimbleProperties::NOTIFY,
        );
        pressure_characteristic.lock().set_value(&[0, 0]);
        let zero_characteristic = pressure_service.lock().create_characteristic(
            uuid128!("873ae82c-4c5a-4342-b539-9d900bf7ebd0"),
            NimbleProperties::WRITE,
        );
        let pressure_for_zero = pressure.clone();
        zero_characteristic.lock().on_write(move |_| {
            pressure_for_zero.lock().unwrap().zero();
        });

        advertising.lock().set_data(
            BLEAdvertisementData::new()
                .name(DEVICE_NAME)
                .add_service_uuid(uuid128!("873ae82a-4c5a-4342-b539-9d900bf7ebd0")),
        )?;
        if enabled {
            advertising.lock().start()?;
        }

        let owner = Arc::new(Self {
            enabled: enabled_flag,
            connected,
            pressure,
            battery_characteristic,
            log_characteristic,
            pressure_characteristic,
            scale: Arc::new(Mutex::new(ScaleSnapshot::default())),
        });
        Self::spawn_scale_worker(owner.clone(), device);
        Ok(owner)
    }

    pub fn set_enabled(&self, enabled: bool) -> Result<(), BLEError> {
        self.enabled.store(enabled, Ordering::Release);
        if enabled {
            BLEDevice::take().get_advertising().lock().start()
        } else {
            self.scale.lock().unwrap().connected = false;
            BLEDevice::take().get_advertising().lock().stop()
        }
    }

    pub fn is_connected(&self) -> bool {
        self.connected.load(Ordering::Acquire)
    }

    pub fn notify_pressure(&self, raw_pressure: i16) -> bool {
        let payload = {
            let mut pressure = self.pressure.lock().unwrap();
            pressure.update(raw_pressure);
            pressure.encoded()
        };
        let mut characteristic = self.pressure_characteristic.lock();
        characteristic.set_value(&payload).notify();
        self.is_connected()
    }

    pub fn set_battery_level(&self, level: i32) {
        self.battery_characteristic
            .lock()
            .set_value(&[clamp_battery_level(level)])
            .notify();
    }

    pub fn log(&self, message: &[u8]) -> bool {
        self.log_characteristic.lock().set_value(message).notify();
        self.is_connected()
    }

    #[cfg(not(feature = "demo"))]
    pub fn scale_snapshot(&self) -> ScaleSnapshot {
        self.scale.lock().unwrap().clone()
    }

    fn spawn_scale_worker(owner: Arc<Self>, device: &'static BLEDevice) {
        thread::Builder::new()
            .name("ble-scale".into())
            .stack_size(8 * 1024)
            .spawn(move || loop {
                if !owner.enabled.load(Ordering::Acquire) {
                    thread::sleep(Duration::from_millis(250));
                    continue;
                }

                let result =
                    esp_idf_hal::task::block_on(Self::connect_scale_once(owner.clone(), device));
                if let Err(err) = result {
                    println!("BLE scale cycle failed: {err:?}");
                }
                owner.scale.lock().unwrap().connected = false;
                thread::sleep(Duration::from_secs(1));
            })
            .expect("failed to start BLE scale worker");
    }

    async fn connect_scale_once(
        owner: Arc<Self>,
        device: &'static BLEDevice,
    ) -> Result<(), BLEError> {
        let mut scan = BLEScan::new();
        let found = scan
            .active_scan(true)
            .interval(1_349)
            .window(449)
            .start(device, 2_000, |candidate, data| {
                let name = data.name()?;
                if matches_lf_smart_scale_name(name.as_ref()) {
                    Some((
                        candidate.addr(),
                        String::from_utf8_lossy(name.as_ref()).into_owned(),
                    ))
                } else {
                    None
                }
            })
            .await?;

        let Some((address, name)) = found else {
            return Ok(());
        };
        println!("BLE scale discovered: {name} ({address})");
        let mut client = device.new_client();
        client.set_connection_params(24, 48, 0, 60, 160, 159);
        client.connect(&address).await?;
        let service = client
            .get_service(BleUuid::from_uuid16(LF_SERVICE_UUID))
            .await?;
        let characteristic = service
            .get_characteristic(BleUuid::from_uuid16(LF_DATA_UUID))
            .await?;
        let scale_for_notify = owner.scale.clone();
        characteristic
            .on_notify(move |payload| {
                if let Some(weight) = decode_lf_smart_scale_weight(payload) {
                    let mut scale = scale_for_notify.lock().unwrap();
                    scale.weight_grams = weight;
                    scale.samples = scale.samples.saturating_add(1);
                }
            })
            .subscribe_notify(false)
            .await?;
        {
            let mut scale = owner.scale.lock().unwrap();
            scale.connected = true;
            scale.name = name;
        }
        println!("BLE scale subscribed: FFF0/FFF4");

        while owner.enabled.load(Ordering::Acquire) && client.connected() {
            thread::sleep(Duration::from_millis(100));
        }
        client.disconnect()?;
        println!("BLE scale disconnected");
        Ok(())
    }
}
