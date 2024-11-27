# m×coffee

## Hardware

- M5Stack Core2 v1.1 - [AliExpress](https://aliexpress.com/item/1005006246734313.html)
- M5Stack Official VH3.96 - 4Pin Transfer Module Unit - [AliExpress](https://aliexpress.com/item/1005003297393440.html)
- WNK Pressure Sensor WNK80MA G-B-C3-3-P3 (gauge, 1% accuracy, 0-20bar, I2C, cable outlet, G1/8 male)- [AliExpress](https://aliexpress.com/item/1005003185769463.html)
- G1/8 FFM Fitting Tee - [AliExpress](https://aliexpress.com/item/1005005989816620.html?sku_id=12000035202091339)
- PTFE plumbing tape roll - see local store.

## How to flash

1. Download the latest `firmware.bin`:
   - Visit the [Releases page](https://github.com/focusshifter/mXcoffee/releases/latest)
   - Download `firmware.bin` from the assets

2. Flash using M5Burner:
   - Download [M5Burner](https://docs.m5stack.com/en/download)
   - Connect M5Stack Core2 via USB
   - Open M5Burner
   - Click "Browse" and select `firmware.bin`
   - Select your device's COM port
   - Click "Burn"

### Troubleshooting

- Device not recognized?
  - Linux: Add user to dialout group
    ```bash
    sudo usermod -a -G dialout $USER
    # Log out and back in
    ```
  - Windows: Install CP210x USB driver

- Flash failed?
  1. Hold button A (leftmost)
  2. Press RST button (side)
  3. Release button A
  4. Retry flash