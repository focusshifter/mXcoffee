#include "m5u_shim.h"

#include <M5Unified.h>
#ifdef M5UNIFIED_RS_USE_REAL_M5CARDPUTER
#include <M5Cardputer.h>
#endif
#if defined(M5UNIFIED_RS_USE_ARDUINO_GPIO) || defined(M5UNIFIED_RS_USE_ARDUINO_SERIAL)
#include <Arduino.h>
#endif
#ifdef M5UNIFIED_RS_USE_ARDUINO_WIRE
#include <Wire.h>
#endif
#ifdef M5UNIFIED_RS_USE_ARDUINO_IRREMOTE
#ifndef DISABLE_CODE_FOR_RECEIVER
#define DISABLE_CODE_FOR_RECEIVER
#endif
#ifndef SEND_PWM_BY_TIMER
#define SEND_PWM_BY_TIMER
#endif
#include <IRremote.hpp>
#endif
#include <utility/PI4IOE5V6408_Class.hpp>
#include <utility/imu/AK8963_Class.hpp>
#include <utility/imu/BMI270_Class.hpp>
#include <utility/imu/BMM150_Class.hpp>
#include <utility/imu/MPU6886_Class.hpp>
#include <utility/imu/SH200Q_Class.hpp>
#include <utility/led/LED_PowerHub_Class.hpp>
#include <utility/led/LED_Strip_Class.hpp>
#include <utility/rtc/PCF8563_Class.hpp>
#include <utility/rtc/RTC_PowerHub_Class.hpp>
#include <utility/rtc/RX8130_Class.hpp>
#include <driver/gpio.h>
#if __has_include(<driver/i2s_std.h>)
#include <driver/i2s_std.h>
#include <hal/i2s_ll.h>
#include <soc/i2s_struct.h>
#endif
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

static std::string s_m5u_log_suffixes[3];
static m5u_log_callback_t s_m5u_log_callback = nullptr;
static void* s_m5u_log_callback_user_data = nullptr;
static constexpr const char* M5U_SD_MOUNT_POINT = "/sdcard";
static sdmmc_card_t* s_m5u_sd_card = nullptr;
static spi_host_device_t s_m5u_sd_host = SPI2_HOST;
static bool s_m5u_sd_owns_bus = false;

namespace m5 {
void calcClockDiv(uint32_t* div_a, uint32_t* div_b, uint32_t* div_n, uint32_t baseClock, uint32_t targetFreq);
}

#if __has_include(<driver/i2s_std.h>)
static i2s_chan_handle_t s_m5u_audio_capture_rx = nullptr;
static uint8_t s_m5u_audio_capture_channels = 0;

static bool m5u_audio_capture_set_mic_enabled(bool enabled) {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    auto cfg = M5.Mic.config();
    if (cfg.pin_bck != GPIO_NUM_34) {
        return true;
    }

    static constexpr uint8_t es7210_i2c_addr = 0x40;
    bool ok = M5.In_I2C.writeRegister8(es7210_i2c_addr, 0x00, 0xFF, 400000);
    if (!enabled) {
        return ok;
    }

    struct reg_data_t {
        uint8_t reg;
        uint8_t value;
    };
    static constexpr reg_data_t data[] = {
        { 0x00, 0x41 }, // RESET_CTL
        { 0x01, 0x1f }, // CLK_ON_OFF
        { 0x06, 0x00 }, // DIGITAL_PDN
        { 0x07, 0x20 }, // ADC_OSR
        { 0x08, 0x10 }, // MODE_CFG
        { 0x09, 0x30 }, // TCT0_CHPINI
        { 0x0A, 0x30 }, // TCT1_CHPINI
        { 0x20, 0x0a }, // ADC34_HPF2
        { 0x21, 0x2a }, // ADC34_HPF1
        { 0x22, 0x0a }, // ADC12_HPF2
        { 0x23, 0x2a }, // ADC12_HPF1
        { 0x02, 0xC1 },
        { 0x04, 0x01 },
        { 0x05, 0x00 },
        { 0x11, 0x60 },
        { 0x40, 0x42 }, // ANALOG_SYS
        { 0x41, 0x70 }, // MICBIAS12
        { 0x42, 0x70 }, // MICBIAS34
        { 0x43, 0x1B }, // MIC1_GAIN
        { 0x44, 0x1B }, // MIC2_GAIN
        { 0x45, 0x00 }, // MIC3_GAIN
        { 0x46, 0x00 }, // MIC4_GAIN
        { 0x47, 0x00 }, // MIC1_LP
        { 0x48, 0x00 }, // MIC2_LP
        { 0x49, 0x00 }, // MIC3_LP
        { 0x4A, 0x00 }, // MIC4_LP
        { 0x4B, 0x00 }, // MIC12_PDN
        { 0x4C, 0xFF }, // MIC34_PDN
        { 0x01, 0x14 }, // CLK_ON_OFF
    };

    for (const auto& d : data) {
        ok = M5.In_I2C.writeRegister8(es7210_i2c_addr, d.reg, d.value, 400000) && ok;
    }
    return ok;
#else
    (void)enabled;
    return true;
#endif
}

static void m5u_audio_capture_apply_rx_clock(i2s_port_t port, uint32_t sample_rate_hz, uint32_t over_sampling, bool use_pdm) {
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32P4)
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    static constexpr uint32_t pll_d2_clk = 20 * 1000 * 1000;
#else
    static constexpr uint32_t pll_d2_clk = 120 * 1000 * 1000;
#endif
    uint32_t oversampling = over_sampling;
    if (oversampling < 1) {
        oversampling = 1;
    } else if (oversampling > 8) {
        oversampling = 8;
    }

    uint32_t bits = 16;
    uint32_t div_m = 8;
    if (use_pdm) {
        bits = 64;
        div_m = 2;
    }

    uint32_t div_a = 0;
    uint32_t div_b = 0;
    uint32_t div_n = 0;
    m5::calcClockDiv(&div_a, &div_b, &div_n, pll_d2_clk / (bits * div_m), sample_rate_hz * oversampling);

    auto dev = &I2S0;
#if SOC_I2S_NUM >= 2
    if (port == I2S_NUM_1) {
        dev = &I2S1;
    }
#if SOC_I2S_NUM >= 3
    else if (port == I2S_NUM_2) {
        dev = &I2S2;
    }
#if SOC_I2S_NUM >= 4
    else if (port == I2S_NUM_3) {
        dev = &I2S3;
    }
#endif
#endif
#endif

    dev->rx_conf.rx_pdm_en = use_pdm;
    dev->rx_conf.rx_tdm_en = !use_pdm;
#if defined(I2S_RX_PDM2PCM_CONF_REG)
    dev->rx_pdm2pcm_conf.rx_pdm2pcm_en = use_pdm;
    dev->rx_pdm2pcm_conf.rx_pdm_sinc_dsr_16_en = 1;
#elif defined(I2S_RX_PDM2PCM_EN)
    dev->rx_conf.rx_pdm2pcm_en = use_pdm;
    dev->rx_conf.rx_pdm_sinc_dsr_16_en = 1;
#endif
    dev->rx_conf.rx_update = 1;

#if defined(CONFIG_IDF_TARGET_ESP32P4)
    dev->rx_conf.rx_bck_div_num = div_m - 1;
#else
    dev->rx_conf1.rx_bck_div_num = div_m - 1;
#endif

    bool yn1 = div_b > (div_a >> 1);
    if (yn1) {
        div_b = div_a - div_b;
    }

    int div_x = 0;
    int div_y = 1;
    if (div_b) {
        div_x = div_a / div_b - 1;
        div_y = div_a % div_b;
        if (div_y == 0) {
            div_y = 1;
            div_b = 511;
        }
    }

    i2s_ll_rx_set_raw_clk_div(dev, div_n, div_x, div_y, div_b, yn1);

#if defined(I2S_RX_CLKM_DIV_X)
    dev->rx_clkm_div_conf.rx_clkm_div_x = div_x;
    dev->rx_clkm_div_conf.rx_clkm_div_y = div_y;
    dev->rx_clkm_div_conf.rx_clkm_div_z = div_b;
    dev->rx_clkm_div_conf.rx_clkm_div_yn1 = yn1;
    dev->rx_clkm_conf.rx_clkm_div_num = div_n;
    dev->rx_clkm_conf.rx_clk_sel = 1;
    dev->tx_clkm_conf.clk_en = 1;
    dev->rx_clkm_conf.rx_clk_active = 1;
    dev->rx_conf.rx_update = 1;
    dev->rx_conf.rx_update = 0;
#endif
#else
    (void)port;
    (void)sample_rate_hz;
    (void)over_sampling;
    (void)use_pdm;
#endif
}
#endif

#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32P4)
#define M5U_HAS_AXP2101 0
#else
#define M5U_HAS_AXP2101 1
#endif

#if !defined(CONFIG_IDF_TARGET) || defined(CONFIG_IDF_TARGET_ESP32)
#define M5U_HAS_AXP192 1
#else
#define M5U_HAS_AXP192 0
#endif

#if defined(CONFIG_IDF_TARGET_ESP32C6)
#define M5U_HAS_AW32001 1
#define M5U_HAS_BQ27220 1
#else
#define M5U_HAS_AW32001 0
#define M5U_HAS_BQ27220 0
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
#define M5U_HAS_INA226 1
#else
#define M5U_HAS_INA226 0
#endif

#if !defined(CONFIG_IDF_TARGET) || defined(CONFIG_IDF_TARGET_ESP32)
#define M5U_HAS_IP5306 1
#define M5U_HAS_INA3221 1
#else
#define M5U_HAS_IP5306 0
#define M5U_HAS_INA3221 0
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define M5U_HAS_PY32PMIC 1
#else
#define M5U_HAS_PY32PMIC 0
#endif

#if defined(M5UNIFIED_RMT_VERSION) && M5UNIFIED_RMT_VERSION == 2
#define M5U_HAS_LED_STRIP_RMT 1
#else
#define M5U_HAS_LED_STRIP_RMT 0
#endif

extern "C" {

static auto m5u_apply_config(const m5u_config_t* src) {
    auto cfg = M5.config();
    if (!src) {
        return cfg;
    }
#if defined(ARDUINO)
    cfg.serial_baudrate = src->serial_baudrate;
#endif
    cfg.external_speaker_value = src->external_speaker_value;
    cfg.external_display_value = src->external_display_value;
    cfg.clear_display = src->clear_display != 0;
    cfg.output_power = src->output_power != 0;
    cfg.pmic_button = src->pmic_button != 0;
    cfg.internal_imu = src->internal_imu != 0;
    cfg.internal_rtc = src->internal_rtc != 0;
    cfg.internal_mic = src->internal_mic != 0;
    cfg.internal_spk = src->internal_spk != 0;
    cfg.external_imu = src->external_imu != 0;
    cfg.external_rtc = src->external_rtc != 0;
    cfg.disable_rtc_irq = src->disable_rtc_irq != 0;
    cfg.led_brightness = src->led_brightness;
    if (src->fallback_board >= 0) {
        cfg.fallback_board = (m5::board_t)src->fallback_board;
    }
    return cfg;
}

bool m5u_begin(void) {
    auto cfg = m5u_apply_config(nullptr);
    M5.begin(cfg);
    return true;
}

bool m5u_begin_with_config(const m5u_config_t* config) {
    auto cfg = m5u_apply_config(config);
    M5.begin(cfg);
    return true;
}

void m5u_update(void) {
    M5.update();
}

void m5u_delay_ms(uint32_t ms) {
    M5.delay(ms);
}

uint32_t m5u_millis(void) {
    return m5::M5Unified::millis();
}

uint32_t m5u_micros(void) {
    return m5::M5Unified::micros();
}

uint32_t m5u_get_update_msec(void) {
    return M5.getUpdateMsec();
}

size_t m5u_heap_get_free_size(uint32_t caps) {
    return heap_caps_get_free_size(caps);
}

size_t m5u_heap_get_largest_free_block(uint32_t caps) {
    return heap_caps_get_largest_free_block(caps);
}

int m5u_get_board(void) {
    return (int)M5.getBoard();
}

int m5u_get_pin(int name) {
    return M5.getPin((m5::pin_name_t)name);
}

bool m5u_set_primary_display_index(size_t index) {
    return M5.setPrimaryDisplay(index);
}

bool m5u_set_primary_display_type(int kind) {
    return M5.setPrimaryDisplayType((m5gfx::board_t)kind);
}

bool m5u_set_primary_display_types(const int* kinds, size_t len) {
    if (!kinds) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        if (M5.setPrimaryDisplayType((m5gfx::board_t)kinds[i])) {
            return true;
        }
    }
    return false;
}

void m5u_set_log_display_index(size_t index) {
    M5.setLogDisplayIndex(index);
}

void m5u_set_log_display_type(int kind) {
    M5.setLogDisplayType((m5gfx::board_t)kind);
}

void m5u_set_log_display_types(const int* kinds, size_t len) {
    if (!kinds) {
        return;
    }
    for (size_t i = 0; i < len; ++i) {
        int index = M5.getDisplayIndex((m5gfx::board_t)kinds[i]);
        if (index >= 0) {
            M5.setLogDisplayIndex((size_t)index);
            return;
        }
    }
}

void m5u_set_touch_button_height(uint16_t pixel) {
    M5.setTouchButtonHeight(pixel);
}

void m5u_set_touch_button_height_by_ratio(uint8_t ratio) {
    M5.setTouchButtonHeightByRatio(ratio);
}

uint16_t m5u_get_touch_button_height(void) {
    return M5.getTouchButtonHeight();
}

static bool m5u_io_expander_valid_index(size_t index) {
    switch (M5.getBoard()) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    case m5::board_t::board_M5Tab5:
        return index < 2;
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
    case m5::board_t::board_M5UnitC6L:
        return index == 0;
    case m5::board_t::board_ArduinoNessoN1:
        return index < 2;
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    case m5::board_t::board_M5StampPLC:
        return index == 0;
#endif
    default:
        return false;
    }
}

static m5::IOExpander_Base* m5u_io_expander(size_t index) {
    return m5u_io_expander_valid_index(index) ? &M5.getIOExpander(index) : nullptr;
}

bool m5u_io_expander_available(size_t index) {
    return m5u_io_expander_valid_index(index);
}

bool m5u_io_expander_set_direction(size_t index, uint8_t pin, bool output) {
    auto io_expander = m5u_io_expander(index);
    if (!io_expander || pin >= 8) {
        return false;
    }
    io_expander->setDirection(pin, output);
    return true;
}

bool m5u_io_expander_enable_pull(size_t index, uint8_t pin, bool enable) {
    auto io_expander = m5u_io_expander(index);
    if (!io_expander || pin >= 8) {
        return false;
    }
    io_expander->enablePull(pin, enable);
    return true;
}

bool m5u_io_expander_set_pull_mode(size_t index, uint8_t pin, bool pull_up) {
    auto io_expander = m5u_io_expander(index);
    if (!io_expander || pin >= 8) {
        return false;
    }
    io_expander->setPullMode(pin, pull_up);
    return true;
}

bool m5u_io_expander_set_high_impedance(size_t index, uint8_t pin, bool enable) {
    auto io_expander = m5u_io_expander(index);
    if (!io_expander || pin >= 8) {
        return false;
    }
    io_expander->setHighImpedance(pin, enable);
    return true;
}

bool m5u_io_expander_get_write_value(size_t index, uint8_t pin) {
    auto io_expander = m5u_io_expander(index);
    return io_expander && pin < 8 && io_expander->getWriteValue(pin);
}

bool m5u_io_expander_digital_write(size_t index, uint8_t pin, bool level) {
    auto io_expander = m5u_io_expander(index);
    if (!io_expander || pin >= 8) {
        return false;
    }
    io_expander->digitalWrite(pin, level);
    return true;
}

bool m5u_io_expander_digital_read(size_t index, uint8_t pin) {
    auto io_expander = m5u_io_expander(index);
    return io_expander && pin < 8 && io_expander->digitalRead(pin);
}

bool m5u_io_expander_reset_irq(size_t index) {
    auto io_expander = m5u_io_expander(index);
    if (!io_expander) {
        return false;
    }
    io_expander->resetIrq();
    return true;
}

bool m5u_io_expander_disable_irq(size_t index) {
    auto io_expander = m5u_io_expander(index);
    if (!io_expander) {
        return false;
    }
    io_expander->disableIrq();
    return true;
}

bool m5u_io_expander_enable_irq(size_t index) {
    auto io_expander = m5u_io_expander(index);
    if (!io_expander) {
        return false;
    }
    io_expander->enableIrq();
    return true;
}

static m5::PI4IOE5V6408_Class& m5u_pi4ioe5v6408(void) {
    static m5::PI4IOE5V6408_Class io_expander;
    return io_expander;
}

bool m5u_pi4ioe5v6408_begin(void) {
    return m5u_pi4ioe5v6408().begin();
}

bool m5u_pi4ioe5v6408_set_direction(uint8_t pin, bool output) {
    if (pin >= 8) {
        return false;
    }
    m5u_pi4ioe5v6408().setDirection(pin, output);
    return true;
}

bool m5u_pi4ioe5v6408_enable_pull(uint8_t pin, bool enable) {
    if (pin >= 8) {
        return false;
    }
    m5u_pi4ioe5v6408().enablePull(pin, enable);
    return true;
}

bool m5u_pi4ioe5v6408_set_pull_mode(uint8_t pin, bool pull_up) {
    if (pin >= 8) {
        return false;
    }
    m5u_pi4ioe5v6408().setPullMode(pin, pull_up);
    return true;
}

bool m5u_pi4ioe5v6408_set_high_impedance(uint8_t pin, bool enable) {
    if (pin >= 8) {
        return false;
    }
    m5u_pi4ioe5v6408().setHighImpedance(pin, enable);
    return true;
}

bool m5u_pi4ioe5v6408_get_write_value(uint8_t pin) {
    return pin < 8 && m5u_pi4ioe5v6408().getWriteValue(pin);
}

bool m5u_pi4ioe5v6408_digital_write(uint8_t pin, bool level) {
    if (pin >= 8) {
        return false;
    }
    m5u_pi4ioe5v6408().digitalWrite(pin, level);
    return true;
}

bool m5u_pi4ioe5v6408_digital_read(uint8_t pin) {
    return pin < 8 && m5u_pi4ioe5v6408().digitalRead(pin);
}

void m5u_pi4ioe5v6408_reset_irq(void) {
    m5u_pi4ioe5v6408().resetIrq();
}

void m5u_pi4ioe5v6408_disable_irq(void) {
    m5u_pi4ioe5v6408().disableIrq();
}

void m5u_pi4ioe5v6408_enable_irq(void) {
    m5u_pi4ioe5v6408().enableIrq();
}

static m5::I2C_Class* m5u_i2c_for_bus(int bus) {
    switch (bus) {
        case 0: return &M5.In_I2C;
        case 1: return &M5.Ex_I2C;
        default: return nullptr;
    }
}

void m5u_i2c_set_port(int bus, int port_num, int pin_sda, int pin_scl) {
    auto i2c = m5u_i2c_for_bus(bus);
    if (i2c) {
        i2c->setPort((i2c_port_t)port_num, pin_sda, pin_scl);
    }
}

bool m5u_i2c_begin(int bus) {
    auto i2c = m5u_i2c_for_bus(bus);
    return i2c && i2c->begin();
}

bool m5u_i2c_begin_with_port(int bus, int port_num, int pin_sda, int pin_scl) {
    auto i2c = m5u_i2c_for_bus(bus);
    return i2c && i2c->begin((i2c_port_t)port_num, pin_sda, pin_scl);
}

bool m5u_i2c_release(int bus) {
    auto i2c = m5u_i2c_for_bus(bus);
    return i2c && i2c->release();
}

bool m5u_i2c_is_enabled(int bus) {
    auto i2c = m5u_i2c_for_bus(bus);
    return i2c && i2c->isEnabled();
}

int m5u_i2c_get_port(int bus) {
    auto i2c = m5u_i2c_for_bus(bus);
    return i2c ? (int)i2c->getPort() : -1;
}

int m5u_i2c_get_sda(int bus) {
    auto i2c = m5u_i2c_for_bus(bus);
    return i2c ? i2c->getSDA() : -1;
}

int m5u_i2c_get_scl(int bus) {
    auto i2c = m5u_i2c_for_bus(bus);
    return i2c ? i2c->getSCL() : -1;
}

bool m5u_i2c_start(int bus, uint8_t address, bool read, uint32_t freq) {
    auto i2c = m5u_i2c_for_bus(bus);
    return i2c && i2c->start(address, read, freq);
}

bool m5u_i2c_restart(int bus, uint8_t address, bool read, uint32_t freq) {
    auto i2c = m5u_i2c_for_bus(bus);
    return i2c && i2c->restart(address, read, freq);
}

bool m5u_i2c_stop(int bus) {
    auto i2c = m5u_i2c_for_bus(bus);
    return i2c && i2c->stop();
}

bool m5u_i2c_write_byte(int bus, uint8_t data) {
    auto i2c = m5u_i2c_for_bus(bus);
    return i2c && i2c->write(data);
}

bool m5u_i2c_write(int bus, const uint8_t* data, size_t length) {
    auto i2c = m5u_i2c_for_bus(bus);
    return i2c && i2c->write(data, length);
}

bool m5u_i2c_read(int bus, uint8_t* result, size_t length, bool last_nack) {
    auto i2c = m5u_i2c_for_bus(bus);
    return i2c && i2c->read(result, length, last_nack);
}

bool m5u_i2c_write_register(int bus, uint8_t address, uint8_t reg, const uint8_t* data, size_t length, uint32_t freq) {
    auto i2c = m5u_i2c_for_bus(bus);
    return i2c && i2c->writeRegister(address, reg, data, length, freq);
}

bool m5u_i2c_read_register(int bus, uint8_t address, uint8_t reg, uint8_t* result, size_t length, uint32_t freq) {
    auto i2c = m5u_i2c_for_bus(bus);
    return i2c && i2c->readRegister(address, reg, result, length, freq);
}

bool m5u_i2c_write_register8(int bus, uint8_t address, uint8_t reg, uint8_t data, uint32_t freq) {
    auto i2c = m5u_i2c_for_bus(bus);
    return i2c && i2c->writeRegister8(address, reg, data, freq);
}

uint8_t m5u_i2c_read_register8(int bus, uint8_t address, uint8_t reg, uint32_t freq) {
    auto i2c = m5u_i2c_for_bus(bus);
    return i2c ? i2c->readRegister8(address, reg, freq) : 0;
}

bool m5u_i2c_bit_on(int bus, uint8_t address, uint8_t reg, uint8_t data, uint32_t freq) {
    auto i2c = m5u_i2c_for_bus(bus);
    return i2c && i2c->bitOn(address, reg, data, freq);
}

bool m5u_i2c_bit_off(int bus, uint8_t address, uint8_t reg, uint8_t data, uint32_t freq) {
    auto i2c = m5u_i2c_for_bus(bus);
    return i2c && i2c->bitOff(address, reg, data, freq);
}

void m5u_i2c_scan(int bus, bool* result, uint32_t freq) {
    auto i2c = m5u_i2c_for_bus(bus);
    if (i2c && result) {
        i2c->scanID(result, freq);
    }
}

bool m5u_i2c_scan_address(int bus, uint8_t address, uint32_t freq) {
    auto i2c = m5u_i2c_for_bus(bus);
    return i2c && i2c->scanID(address, freq);
}

int m5u_display_width(void) {
    return M5.Display.width();
}

int m5u_display_height(void) {
    return M5.Display.height();
}

void m5u_display_fill_screen(uint16_t color) {
    M5.Display.fillScreen(color);
}

void m5u_display_set_cursor(int x, int y) {
    M5.Display.setCursor(x, y);
}

void m5u_display_set_text_size(int size) {
    M5.Display.setTextSize(size);
}

void m5u_display_set_text_color(uint16_t fg, uint16_t bg) {
    M5.Display.setTextColor(fg, bg);
}

void m5u_display_print(const char* text) {
    M5.Display.print(text);
}

void m5u_display_println(const char* text) {
    M5.Display.println(text);
}

void m5u_display_draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    M5.Display.drawLine(x0, y0, x1, y1, color);
}

void m5u_display_draw_rect(int x, int y, int w, int h, uint16_t color) {
    M5.Display.drawRect(x, y, w, h, color);
}

void m5u_display_fill_rect(int x, int y, int w, int h, uint16_t color) {
    M5.Display.fillRect(x, y, w, h, color);
}

void m5u_display_draw_circle(int x, int y, int r, uint16_t color) {
    M5.Display.drawCircle(x, y, r, color);
}

void m5u_display_fill_circle(int x, int y, int r, uint16_t color) {
    M5.Display.fillCircle(x, y, r, color);
}

void m5u_display_set_rotation(int rotation) {
    M5.Display.setRotation(rotation);
}

bool m5u_btn_a_is_pressed(void) {
    return M5.BtnA.isPressed();
}

bool m5u_btn_a_was_pressed(void) {
    return M5.BtnA.wasPressed();
}

bool m5u_btn_a_was_released(void) {
    return M5.BtnA.wasReleased();
}

bool m5u_btn_b_is_pressed(void) {
    return M5.BtnB.isPressed();
}

bool m5u_btn_b_was_pressed(void) {
    return M5.BtnB.wasPressed();
}

bool m5u_btn_b_was_released(void) {
    return M5.BtnB.wasReleased();
}

bool m5u_btn_c_is_pressed(void) {
    return M5.BtnC.isPressed();
}

bool m5u_btn_c_was_pressed(void) {
    return M5.BtnC.wasPressed();
}

bool m5u_btn_c_was_released(void) {
    return M5.BtnC.wasReleased();
}

bool m5u_mic_begin(void) {
    return M5.Mic.begin();
}

bool m5u_mic_is_running(void) {
    return M5.Mic.isRunning();
}

bool m5u_mic_record_i16(int16_t* buffer, size_t samples) {
    return M5.Mic.record(buffer, samples);
}

bool m5u_mic_record_u8(uint8_t* buffer, size_t samples) {
    return M5.Mic.record(buffer, samples);
}

bool m5u_speaker_begin(void) {
    return M5.Speaker.begin();
}

bool m5u_speaker_is_running(void) {
    return M5.Speaker.isRunning();
}

void m5u_speaker_set_volume(uint8_t volume) {
    M5.Speaker.setVolume(volume);
}

bool m5u_speaker_tone(uint32_t frequency_hz, uint32_t duration_ms) {
    return M5.Speaker.tone(frequency_hz, duration_ms);
}

bool m5u_speaker_play_i16(const int16_t* samples, size_t len, uint32_t sample_rate_hz) {
    return M5.Speaker.playRaw(samples, len, sample_rate_hz, false, 1, 0);
}

bool m5u_imu_begin(void) {
    return M5.Imu.begin();
}

bool m5u_imu_begin_for_board(int board) {
    return M5.Imu.begin(nullptr, (m5::board_t)board);
}

bool m5u_imu_get_accel(float* x, float* y, float* z) {
    return M5.Imu.getAccel(x, y, z);
}

bool m5u_imu_get_gyro(float* x, float* y, float* z) {
    return M5.Imu.getGyro(x, y, z);
}

bool m5u_imu_get_mag(float* x, float* y, float* z) {
    return M5.Imu.getMag(x, y, z);
}

bool m5u_imu_get_data(m5u_imu_data_t* out) {
    if (!M5.Imu.isEnabled() || !out) {
        return false;
    }
    auto data = M5.Imu.getImuData();
    out->usec = data.usec;
    out->accel_x = data.accel.x;
    out->accel_y = data.accel.y;
    out->accel_z = data.accel.z;
    out->gyro_x = data.gyro.x;
    out->gyro_y = data.gyro.y;
    out->gyro_z = data.gyro.z;
    out->mag_x = data.mag.x;
    out->mag_y = data.mag.y;
    out->mag_z = data.mag.z;
    return true;
}

bool m5u_imu_get_temp_c(float* temp) {
    return M5.Imu.getTemp(temp);
}

int m5u_touch_count(void) {
    return M5.Touch.getCount();
}

bool m5u_touch_is_enabled(void) {
    return M5.Touch.isEnabled();
}

void m5u_touch_set_hold_thresh(uint16_t ms) {
    M5.Touch.setHoldThresh(ms);
}

void m5u_touch_set_flick_thresh(uint16_t distance) {
    M5.Touch.setFlickThresh(distance);
}

bool m5u_touch_get(int index, int* x, int* y) {
    auto detail = M5.Touch.getDetail(index);
    if (x) { *x = detail.x; }
    if (y) { *y = detail.y; }
    return detail.isPressed();
}

bool m5u_touch_get_raw(int index, int* x, int* y) {
    if (index < 0 || index >= M5.Touch.getCount()) {
        return false;
    }
    auto point = M5.Touch.getTouchPointRaw(index);
    if (x) { *x = point.x; }
    if (y) { *y = point.y; }
    return true;
}

bool m5u_rtc_begin(void) {
    return M5.Rtc.begin();
}

bool m5u_rtc_begin_for_board(int board) {
    return M5.Rtc.begin(nullptr, (m5::board_t)board);
}

bool m5u_rtc_get_datetime(int* year, int* month, int* day, int* hour, int* minute, int* second) {
    m5::rtc_datetime_t dt;
    if (!M5.Rtc.getDateTime(&dt)) { return false; }
    if (year) { *year = dt.date.year; }
    if (month) { *month = dt.date.month; }
    if (day) { *day = dt.date.date; }
    if (hour) { *hour = dt.time.hours; }
    if (minute) { *minute = dt.time.minutes; }
    if (second) { *second = dt.time.seconds; }
    return true;
}

static void m5u_rtc_to_raw(const m5::rtc_datetime_t& src, m5u_rtc_datetime_t* out) {
    out->year = src.date.year;
    out->month = src.date.month;
    out->day = src.date.date;
    out->weekday = src.date.weekDay;
    out->hour = src.time.hours;
    out->minute = src.time.minutes;
    out->second = src.time.seconds;
}

static void m5u_rtc_date_to_raw(const m5::rtc_date_t& src, m5u_rtc_datetime_t* out) {
    out->year = src.year;
    out->month = src.month;
    out->day = src.date;
    out->weekday = src.weekDay;
    out->hour = 0;
    out->minute = 0;
    out->second = 0;
}

static void m5u_rtc_time_to_raw(const m5::rtc_time_t& src, m5u_rtc_datetime_t* out) {
    out->year = 0;
    out->month = 0;
    out->day = 0;
    out->weekday = -1;
    out->hour = src.hours;
    out->minute = src.minutes;
    out->second = src.seconds;
}

static m5::rtc_datetime_t m5u_rtc_from_raw(const m5u_rtc_datetime_t* src) {
    m5::rtc_datetime_t dt = {};
    dt.date.year = src->year;
    dt.date.month = src->month;
    dt.date.date = src->day;
    dt.date.weekDay = src->weekday;
    dt.time.hours = src->hour;
    dt.time.minutes = src->minute;
    dt.time.seconds = src->second;
    return dt;
}

static m5::rtc_date_t m5u_rtc_date_from_raw(const m5u_rtc_datetime_t* src) {
    m5::rtc_date_t date = {};
    date.year = src->year;
    date.month = src->month;
    date.date = src->day;
    date.weekDay = src->weekday;
    return date;
}

static m5::rtc_time_t m5u_rtc_time_from_raw(const m5u_rtc_datetime_t* src) {
    m5::rtc_time_t time = {};
    time.hours = src->hour;
    time.minutes = src->minute;
    time.seconds = src->second;
    return time;
}

bool m5u_rtc_get_datetime_detail(m5u_rtc_datetime_t* out) {
    if (!out) {
        return false;
    }
    m5::rtc_datetime_t dt;
    if (!M5.Rtc.getDateTime(&dt)) {
        return false;
    }
    m5u_rtc_to_raw(dt, out);
    return true;
}

bool m5u_rtc_get_date_detail(m5u_rtc_datetime_t* out) {
    if (!out) {
        return false;
    }
    m5::rtc_date_t date;
    if (!M5.Rtc.getDate(&date)) {
        return false;
    }
    m5u_rtc_date_to_raw(date, out);
    return true;
}

bool m5u_rtc_get_time_detail(m5u_rtc_datetime_t* out) {
    if (!out) {
        return false;
    }
    m5::rtc_time_t time;
    if (!M5.Rtc.getTime(&time)) {
        return false;
    }
    m5u_rtc_time_to_raw(time, out);
    return true;
}

bool m5u_rtc_set_datetime(int year, int month, int day, int hour, int minute, int second) {
    m5::rtc_datetime_t dt;
    dt.date.year = year;
    dt.date.month = month;
    dt.date.date = day;
    dt.time.hours = hour;
    dt.time.minutes = minute;
    dt.time.seconds = second;
    M5.Rtc.setDateTime(&dt);
    return true;
}

bool m5u_rtc_set_datetime_detail(const m5u_rtc_datetime_t* datetime) {
    if (!datetime) {
        return false;
    }
    auto dt = m5u_rtc_from_raw(datetime);
    M5.Rtc.setDateTime(&dt);
    return true;
}

bool m5u_rtc_set_date_detail(const m5u_rtc_datetime_t* date) {
    if (!date) {
        return false;
    }
    auto raw = m5u_rtc_date_from_raw(date);
    M5.Rtc.setDate(&raw);
    return true;
}

bool m5u_rtc_set_time_detail(const m5u_rtc_datetime_t* time) {
    if (!time) {
        return false;
    }
    auto raw = m5u_rtc_time_from_raw(time);
    M5.Rtc.setTime(&raw);
    return true;
}

void m5u_rtc_set_system_time_from_rtc(void) {
    M5.Rtc.setSystemTimeFromRtc();
}

bool m5u_rtc_get_volt_low(void) {
    return M5.Rtc.getVoltLow();
}

uint32_t m5u_rtc_set_timer_irq(uint32_t timer_msec) {
    return M5.Rtc.setTimerIRQ(timer_msec);
}

int m5u_rtc_set_alarm_irq_after_seconds(int after_seconds) {
    return M5.Rtc.setAlarmIRQ(after_seconds);
}

int m5u_rtc_set_alarm_irq_datetime(const m5u_rtc_datetime_t* datetime) {
    if (!datetime) {
        return -1;
    }
    auto dt = m5u_rtc_from_raw(datetime);
    return M5.Rtc.setAlarmIRQ(&dt.date, &dt.time);
}

int m5u_rtc_set_alarm_irq_time(const m5u_rtc_datetime_t* time) {
    if (!time) {
        return -1;
    }
    auto raw = m5u_rtc_time_from_raw(time);
    return M5.Rtc.setAlarmIRQ((const m5::rtc_date_t*)nullptr, &raw);
}

bool m5u_rtc_get_irq_status(void) {
    return M5.Rtc.getIRQstatus();
}

void m5u_rtc_clear_irq(void) {
    M5.Rtc.clearIRQ();
}

void m5u_rtc_disable_irq(void) {
    M5.Rtc.disableIRQ();
}

static m5::RTC_Base* m5u_rtc_device_for_kind(int kind) {
    static m5::PCF8563_Class pcf8563;
    static m5::RX8130_Class rx8130;
    static m5::RTC_PowerHub_Class power_hub;

    switch (kind) {
        case 0: return &pcf8563;
        case 1: return &rx8130;
        case 2: return &power_hub;
        default: return nullptr;
    }
}

bool m5u_rtc_device_begin(int kind) {
    auto rtc = m5u_rtc_device_for_kind(kind);
    return rtc ? rtc->begin() : false;
}

bool m5u_rtc_device_get_datetime_detail(int kind, m5u_rtc_datetime_t* out) {
    auto rtc = m5u_rtc_device_for_kind(kind);
    if (!rtc || !out) {
        return false;
    }
    m5::rtc_date_t date;
    m5::rtc_time_t time;
    if (!rtc->getDateTime(&date, &time)) {
        return false;
    }
    m5u_rtc_to_raw(m5::rtc_datetime_t(date, time), out);
    return true;
}

bool m5u_rtc_device_get_date_detail(int kind, m5u_rtc_datetime_t* out) {
    auto rtc = m5u_rtc_device_for_kind(kind);
    if (!rtc || !out) {
        return false;
    }
    m5::rtc_date_t date;
    if (!rtc->getDateTime(&date, nullptr)) {
        return false;
    }
    m5u_rtc_date_to_raw(date, out);
    return true;
}

bool m5u_rtc_device_get_time_detail(int kind, m5u_rtc_datetime_t* out) {
    auto rtc = m5u_rtc_device_for_kind(kind);
    if (!rtc || !out) {
        return false;
    }
    m5::rtc_time_t time;
    if (!rtc->getDateTime(nullptr, &time)) {
        return false;
    }
    m5u_rtc_time_to_raw(time, out);
    return true;
}

bool m5u_rtc_device_set_datetime_detail(int kind, const m5u_rtc_datetime_t* datetime) {
    auto rtc = m5u_rtc_device_for_kind(kind);
    if (!rtc || !datetime) {
        return false;
    }
    auto raw = m5u_rtc_from_raw(datetime);
    return rtc->setDateTime(&raw.date, &raw.time);
}

bool m5u_rtc_device_set_date_detail(int kind, const m5u_rtc_datetime_t* date) {
    auto rtc = m5u_rtc_device_for_kind(kind);
    if (!rtc || !date) {
        return false;
    }
    auto raw = m5u_rtc_date_from_raw(date);
    return rtc->setDateTime(&raw, nullptr);
}

bool m5u_rtc_device_set_time_detail(int kind, const m5u_rtc_datetime_t* time) {
    auto rtc = m5u_rtc_device_for_kind(kind);
    if (!rtc || !time) {
        return false;
    }
    auto raw = m5u_rtc_time_from_raw(time);
    return rtc->setDateTime(nullptr, &raw);
}

bool m5u_rtc_device_get_volt_low(int kind) {
    auto rtc = m5u_rtc_device_for_kind(kind);
    return rtc ? rtc->getVoltLow() : false;
}

uint32_t m5u_rtc_device_set_timer_irq(int kind, uint32_t timer_msec) {
    auto rtc = m5u_rtc_device_for_kind(kind);
    return rtc ? rtc->setTimerIRQ(timer_msec) : 0;
}

int m5u_rtc_device_set_alarm_irq_datetime(int kind, const m5u_rtc_datetime_t* datetime) {
    auto rtc = m5u_rtc_device_for_kind(kind);
    if (!rtc || !datetime) {
        return -1;
    }
    auto raw = m5u_rtc_from_raw(datetime);
    return rtc->setAlarmIRQ(&raw.date, &raw.time);
}

int m5u_rtc_device_set_alarm_irq_time(int kind, const m5u_rtc_datetime_t* time) {
    auto rtc = m5u_rtc_device_for_kind(kind);
    if (!rtc || !time) {
        return -1;
    }
    auto raw = m5u_rtc_time_from_raw(time);
    return rtc->setAlarmIRQ(nullptr, &raw);
}

bool m5u_rtc_device_get_irq_status(int kind) {
    auto rtc = m5u_rtc_device_for_kind(kind);
    return rtc ? rtc->getIRQstatus() : false;
}

void m5u_rtc_device_clear_irq(int kind) {
    auto rtc = m5u_rtc_device_for_kind(kind);
    if (rtc) {
        rtc->clearIRQ();
    }
}

void m5u_rtc_device_disable_irq(int kind) {
    auto rtc = m5u_rtc_device_for_kind(kind);
    if (rtc) {
        rtc->disableIRQ();
    }
}

int m5u_battery_level(void) {
    return M5.Power.getBatteryLevel();
}

int m5u_battery_voltage_mv(void) {
    return M5.Power.getBatteryVoltage();
}

bool m5u_power_begin(void) {
    return M5.Power.begin();
}

int m5u_power_get_type(void) {
    return (int)M5.Power.getType();
}

int m5u_power_get_charge_state(void) {
    return (int)M5.Power.isCharging();
}

bool m5u_power_is_charging(void) {
    return M5.Power.isCharging() == m5::Power_Class::is_charging_t::is_charging;
}

void m5u_power_set_led(uint8_t brightness) {
    M5.Power.setLed(brightness);
}

void m5u_power_set_ext_output(bool enable, uint16_t port_mask) {
    M5.Power.setExtOutput(enable, (m5::ext_port_mask_t)port_mask);
}

bool m5u_power_get_ext_output(void) {
    return M5.Power.getExtOutput();
}

void m5u_power_set_usb_output(bool enable) {
    M5.Power.setUsbOutput(enable);
}

bool m5u_power_get_usb_output(void) {
    return M5.Power.getUsbOutput();
}

void m5u_power_set_battery_charge(bool enable) {
    M5.Power.setBatteryCharge(enable);
}

void m5u_power_set_charge_current(uint16_t max_ma) {
    M5.Power.setChargeCurrent(max_ma);
}

void m5u_power_set_charge_voltage(uint16_t max_mv) {
    M5.Power.setChargeVoltage(max_mv);
}

int m5u_power_get_vbus_voltage_mv(void) {
    return M5.Power.getVBUSVoltage();
}

int m5u_power_get_battery_current_ma(void) {
    return M5.Power.getBatteryCurrent();
}

float m5u_power_get_ext_voltage_mv(uint16_t port_mask) {
    return M5.Power.getExtVoltage((m5::ext_port_mask_t)port_mask);
}

float m5u_power_get_ext_current_ma(uint16_t port_mask) {
    return M5.Power.getExtCurrent((m5::ext_port_mask_t)port_mask);
}

uint8_t m5u_power_get_key_state(void) {
    return M5.Power.getKeyState();
}

void m5u_power_set_ext_port_bus_config(const m5u_power_ext_port_bus_t* config) {
    if (!config) {
        return;
    }
    m5::ext_port_bus_t bus_config;
    bus_config.voltage = config->voltage_mv;
    bus_config.currentLimit = config->current_limit_ma;
    bus_config.enable = config->enable;
    bus_config.direction = config->direction_output;
    M5.Power.setExtPortBusConfig(bus_config);
}

void m5u_power_set_vibration(uint8_t level) {
    M5.Power.setVibration(level);
}

void m5u_power_power_off(void) {
    M5.Power.powerOff();
}

void m5u_power_timer_sleep_seconds(int seconds) {
    M5.Power.timerSleep(seconds);
}

void m5u_power_timer_sleep_time(const m5u_rtc_datetime_t* time) {
    if (!time) {
        return;
    }
    auto raw = m5u_rtc_time_from_raw(time);
    M5.Power.timerSleep(raw);
}

void m5u_power_timer_sleep_date_time(const m5u_rtc_datetime_t* date, const m5u_rtc_datetime_t* time) {
    if (!date || !time) {
        return;
    }
    auto raw_date = m5u_rtc_date_from_raw(date);
    auto raw_time = m5u_rtc_time_from_raw(time);
    M5.Power.timerSleep(raw_date, raw_time);
}

void m5u_power_deep_sleep_us(uint64_t micro_seconds, bool touch_wakeup) {
    M5.Power.deepSleep(micro_seconds, touch_wakeup);
}

void m5u_power_light_sleep_us(uint64_t micro_seconds, bool touch_wakeup) {
    M5.Power.lightSleep(micro_seconds, touch_wakeup);
}

void m5u_log_println(const char* text) {
    M5.Log.println(text);
}

bool m5u_sd_begin(void) {
    m5u_sd_spi_config_t config = {};
    config.pin_sclk = M5.getPin(m5::pin_name_t::sd_spi_sclk);
    config.pin_mosi = M5.getPin(m5::pin_name_t::sd_spi_mosi);
    config.pin_miso = M5.getPin(m5::pin_name_t::sd_spi_miso);
    config.pin_cs = M5.getPin(m5::pin_name_t::sd_spi_cs);
    config.host_id = -1;
    config.frequency_khz = 20000;
    config.max_files = 5;
    config.format_if_mount_failed = 0;
    return m5u_sd_begin_spi(&config);
}

bool m5u_sd_begin_spi(const m5u_sd_spi_config_t* config) {
    if (s_m5u_sd_card) {
        return true;
    }
    if (!config || config->pin_sclk < 0 || config->pin_mosi < 0 || config->pin_miso < 0 || config->pin_cs < 0) {
        return false;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_host_device_t spi_host = static_cast<spi_host_device_t>(host.slot);
    if (config->host_id >= 0) {
        spi_host = static_cast<spi_host_device_t>(config->host_id);
        host.slot = static_cast<int>(spi_host);
    }
    if (config->frequency_khz > 0) {
        host.max_freq_khz = config->frequency_khz;
    }

    spi_bus_config_t bus_config = {};
    bus_config.mosi_io_num = config->pin_mosi;
    bus_config.miso_io_num = config->pin_miso;
    bus_config.sclk_io_num = config->pin_sclk;
    bus_config.quadwp_io_num = GPIO_NUM_NC;
    bus_config.quadhd_io_num = GPIO_NUM_NC;
    bus_config.max_transfer_sz = 4000;

    esp_err_t err = spi_bus_initialize(spi_host, &bus_config, SDSPI_DEFAULT_DMA);
    bool owns_bus = false;
    if (err == ESP_OK) {
        owns_bus = true;
    } else if (err != ESP_ERR_INVALID_STATE) {
        return false;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = (gpio_num_t)config->pin_cs;
    slot_config.host_id = spi_host;

    esp_vfs_fat_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = config->format_if_mount_failed != 0;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 0;
    mount_config.disk_status_check_enable = false;
    if (config->max_files > 0) {
        mount_config.max_files = config->max_files;
    }

    err = esp_vfs_fat_sdspi_mount(M5U_SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_m5u_sd_card);
    if (err != ESP_OK) {
        s_m5u_sd_card = nullptr;
        if (owns_bus) {
            spi_bus_free(spi_host);
        }
        return false;
    }

    s_m5u_sd_host = spi_host;
    s_m5u_sd_owns_bus = owns_bus;
    return true;
}

bool m5u_sd_is_mounted(void) {
    return s_m5u_sd_card != nullptr;
}

void m5u_sd_end(void) {
    if (!s_m5u_sd_card) {
        return;
    }
    esp_vfs_fat_sdcard_unmount(M5U_SD_MOUNT_POINT, s_m5u_sd_card);
    s_m5u_sd_card = nullptr;
    if (s_m5u_sd_owns_bus) {
        spi_bus_free(s_m5u_sd_host);
    }
    s_m5u_sd_owns_bus = false;
}


static m5::Button_Class* m5u_button_for_id(int button) {
    switch (button) {
    case 0: return &M5.BtnA;
    case 1: return &M5.BtnB;
    case 2: return &M5.BtnC;
    case 3: return &M5.BtnEXT;
    case 4: return &M5.BtnPWR;
    default: return nullptr;
    }
}

static bool m5u_button_state(int button, int query) {
    m5::Button_Class* btn = m5u_button_for_id(button);
    if (!btn) {
        return false;
    }

    switch (query) {
    case 0: return btn->isPressed();
    case 1: return btn->wasPressed();
    case 2: return btn->wasReleased();
    case 3: return btn->wasClicked();
    case 4: return btn->wasHold();
    case 5: return btn->isHolding();
    case 6: return btn->wasDecideClickCount();
    case 7: return btn->wasSingleClicked();
    case 8: return btn->wasDoubleClicked();
    case 9: return btn->wasChangePressed();
    case 10: return btn->isReleased();
    case 11: return btn->wasReleasedAfterHold();
    default: return false;
    }
}

int m5u_display_get_rotation(void) {
    return M5.Display.getRotation();
}

void m5u_display_set_brightness(uint8_t brightness) {
    M5.Display.setBrightness(brightness);
}

uint8_t m5u_display_get_brightness(void) {
    return M5.Display.getBrightness();
}

void m5u_display_sleep(void) {
    M5.Display.sleep();
}

void m5u_display_wakeup(void) {
    M5.Display.wakeup();
}

void m5u_display_power_save(bool enable) {
    M5.Display.powerSave(enable);
}

void m5u_display_invert_display(bool invert) {
    M5.Display.invertDisplay(invert);
}

bool m5u_display_get_invert(void) {
    return M5.Display.getInvert();
}

void m5u_display_set_swap_bytes(bool swap) {
    M5.Display.setSwapBytes(swap);
}

bool m5u_display_get_swap_bytes(void) {
    return M5.Display.getSwapBytes();
}

void m5u_display_set_color_depth(int depth) {
    M5.Display.setColorDepth((m5gfx::color_depth_t)depth);
}

int m5u_display_get_color_depth(void) {
    return (int)M5.Display.getColorDepth();
}

void m5u_display_set_addr_window(int x, int y, int w, int h) {
    M5.Display.setAddrWindow(x, y, w, h);
}

void m5u_display_set_window(int xs, int ys, int xe, int ye) {
    M5.Display.setWindow(xs, ys, xe, ye);
}

void m5u_display_begin_transaction(void) {
    M5.Display.beginTransaction();
}

void m5u_display_end_transaction(void) {
    M5.Display.endTransaction();
}

uint32_t m5u_display_get_start_count(void) {
    return M5.Display.getStartCount();
}

int m5u_display_get_scan_line(void) {
    return M5.Display.getScanLine();
}

void m5u_display_set_raw_color(uint32_t color) {
    M5.Display.setRawColor(color);
}

uint32_t m5u_display_get_raw_color(void) {
    return M5.Display.getRawColor();
}

void m5u_display_write_color(uint16_t color, uint32_t length) {
    M5.Display.writeColor(color, length);
}

void m5u_display_draw_pixel_current(int x, int y) {
    M5.Display.drawPixel(x, y);
}

void m5u_display_write_pixel_current(int x, int y) {
    M5.Display.writePixel(x, y);
}

void m5u_display_write_fill_rect(int x, int y, int w, int h, uint16_t color) {
    M5.Display.writeFillRect(x, y, w, h, color);
}

void m5u_display_write_fill_rect_preclipped(int x, int y, int w, int h, uint16_t color) {
    M5.Display.writeFillRectPreclipped(x, y, w, h, color);
}

void m5u_display_push_block(uint16_t color, uint32_t length) {
    M5.Display.pushBlock(color, length);
}

void m5u_display_progress_bar(int x, int y, int w, int h, uint8_t value) {
    M5.Display.progressBar(x, y, w, h, value);
}

void m5u_display_push_state(void) {
    M5.Display.pushState();
}

void m5u_display_pop_state(void) {
    M5.Display.popState();
}

void m5u_display_set_epd_fastest(void) {
    M5.Display.setEpdMode(m5gfx::epd_fastest);
}

void m5u_display_set_epd_mode(int mode) {
    switch (mode) {
    case m5gfx::epd_quality:
    case m5gfx::epd_text:
    case m5gfx::epd_fast:
    case m5gfx::epd_fastest:
        M5.Display.setEpdMode((m5gfx::epd_mode_t)mode);
        break;
    default:
        break;
    }
}

void m5u_display_set_text_scroll(bool scroll) {
    M5.Display.setTextScroll(scroll);
}

bool m5u_display_set_font(int font) {
    switch (font) {
    case 0:
        M5.Display.setFont(nullptr);
        return true;
    case 1:
        M5.Display.setFont(&fonts::AsciiFont8x16);
        return true;
    case 2:
        M5.Display.setFont(&fonts::lgfxJapanGothic_12);
        return true;
    case 3:
        M5.Display.setFont(&fonts::DejaVu18);
        return true;
    default:
        return false;
    }
}

void m5u_display_start_write(void) {
    M5.Display.startWrite();
}

void m5u_display_end_write(void) {
    M5.Display.endWrite();
}

void m5u_display_display(void) {
    M5.Display.display();
}

void m5u_display_display_region(int x, int y, int w, int h) {
    M5.Display.display(x, y, w, h);
}

bool m5u_display_display_busy(void) {
    return M5.Display.displayBusy();
}

void m5u_display_wait_display(void) {
    M5.Display.waitDisplay();
}

bool m5u_display_has_palette(void) {
    return M5.Display.hasPalette();
}

uint32_t m5u_display_get_palette_count(void) {
    return M5.Display.getPaletteCount();
}

bool m5u_display_is_readable(void) {
    return M5.Display.isReadable();
}

bool m5u_display_is_epd(void) {
    return M5.Display.isEPD();
}

bool m5u_display_is_bus_shared(void) {
    return M5.Display.isBusShared();
}

void m5u_display_set_auto_display(bool enable) {
    M5.Display.setAutoDisplay(enable);
}

void m5u_display_init_dma(void) {
    M5.Display.initDMA();
}

void m5u_display_wait_dma(void) {
    M5.Display.waitDMA();
}

bool m5u_display_dma_busy(void) {
    return M5.Display.dmaBusy();
}

int m5u_display_get_cursor_x(void) {
    return M5.Display.getCursorX();
}

int m5u_display_get_cursor_y(void) {
    return M5.Display.getCursorY();
}

int m5u_display_font_height(void) {
    return M5.Display.fontHeight();
}

uint32_t m5u_display_get_base_color(void) {
    return M5.Display.getBaseColor();
}

void m5u_display_set_base_color(uint32_t color) {
    M5.Display.setBaseColor(color);
}

void m5u_display_set_color(uint16_t color) {
    M5.Display.setColor(color);
}

void m5u_display_set_text_wrap(bool wrap_x, bool wrap_y) {
    M5.Display.setTextWrap(wrap_x, wrap_y);
}

void m5u_display_set_text_datum(int datum) {
    M5.Display.setTextDatum((textdatum_t)datum);
}

int m5u_display_draw_string(const char* text, int x, int y) {
    return text ? (int)M5.Display.drawString(text, x, y) : 0;
}

int m5u_display_draw_center_string(const char* text, int x, int y) {
    return text ? (int)M5.Display.drawCenterString(text, x, y) : 0;
}

int m5u_display_draw_right_string(const char* text, int x, int y) {
    return text ? (int)M5.Display.drawRightString(text, x, y) : 0;
}

int m5u_display_draw_number(int value, int x, int y) {
    return (int)M5.Display.drawNumber((long)value, x, y);
}

int m5u_display_draw_float(float value, uint8_t decimals, int x, int y) {
    return (int)M5.Display.drawFloat(value, decimals, x, y);
}

int m5u_display_draw_char(uint16_t codepoint, int x, int y) {
    return (int)M5.Display.drawChar(codepoint, x, y);
}

void m5u_display_draw_pixel(int x, int y, uint16_t color) {
    M5.Display.drawPixel(x, y, color);
}

void m5u_display_write_pixel(int x, int y, uint16_t color) {
    M5.Display.writePixel(x, y, color);
}

void m5u_display_draw_fast_hline(int x, int y, int w, uint16_t color) {
    M5.Display.drawFastHLine(x, y, w, color);
}

void m5u_display_write_fast_hline(int x, int y, int w, uint16_t color) {
    M5.Display.writeFastHLine(x, y, w, color);
}

void m5u_display_draw_fast_vline(int x, int y, int h, uint16_t color) {
    M5.Display.drawFastVLine(x, y, h, color);
}

void m5u_display_write_fast_vline(int x, int y, int h, uint16_t color) {
    M5.Display.writeFastVLine(x, y, h, color);
}

void m5u_display_draw_round_rect(int x, int y, int w, int h, int r, uint16_t color) {
    M5.Display.drawRoundRect(x, y, w, h, r, color);
}

void m5u_display_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color) {
    M5.Display.fillRoundRect(x, y, w, h, r, color);
}

void m5u_display_draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color) {
    M5.Display.drawTriangle(x0, y0, x1, y1, x2, y2, color);
}

void m5u_display_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color) {
    M5.Display.fillTriangle(x0, y0, x1, y1, x2, y2, color);
}

void m5u_display_draw_ellipse(int x, int y, int rx, int ry, uint16_t color) {
    M5.Display.drawEllipse(x, y, rx, ry, color);
}

void m5u_display_fill_ellipse(int x, int y, int rx, int ry, uint16_t color) {
    M5.Display.fillEllipse(x, y, rx, ry, color);
}

void m5u_display_draw_arc(int x, int y, int r0, int r1, float angle0, float angle1, uint16_t color) {
    M5.Display.drawArc(x, y, r0, r1, angle0, angle1, color);
}

void m5u_display_fill_arc(int x, int y, int r0, int r1, float angle0, float angle1, uint16_t color) {
    M5.Display.fillArc(x, y, r0, r1, angle0, angle1, color);
}

void m5u_display_draw_ellipse_arc(int x, int y, int r0x, int r1x, int r0y, int r1y, float angle0, float angle1, uint16_t color) {
    M5.Display.drawEllipseArc(x, y, r0x, r1x, r0y, r1y, angle0, angle1, color);
}

void m5u_display_fill_ellipse_arc(int x, int y, int r0x, int r1x, int r0y, int r1y, float angle0, float angle1, uint16_t color) {
    M5.Display.fillEllipseArc(x, y, r0x, r1x, r0y, r1y, angle0, angle1, color);
}

void m5u_display_draw_bezier3(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color) {
    M5.Display.drawBezier(x0, y0, x1, y1, x2, y2, color);
}

void m5u_display_draw_bezier4(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, uint16_t color) {
    M5.Display.drawBezier(x0, y0, x1, y1, x2, y2, x3, y3, color);
}

void m5u_display_draw_smooth_line(int x0, int y0, int x1, int y1, uint16_t color) {
    M5.Display.drawSmoothLine(x0, y0, x1, y1, color);
}

void m5u_display_draw_wide_line(int x0, int y0, int x1, int y1, float radius, uint16_t color) {
    M5.Display.drawWideLine(x0, y0, x1, y1, radius, color);
}

void m5u_display_draw_wedge_line(int x0, int y0, int x1, int y1, float r0, float r1, uint16_t color) {
    M5.Display.drawWedgeLine(x0, y0, x1, y1, r0, r1, color);
}

void m5u_display_draw_gradient_line(int x0, int y0, int x1, int y1, uint16_t start_color, uint16_t end_color) {
    M5.Display.drawGradientLine(x0, y0, x1, y1, start_color, end_color);
}

void m5u_display_draw_spot(int x, int y, float radius, uint16_t color) {
    M5.Display.drawSpot(x, y, radius, color);
}

void m5u_display_fill_smooth_circle(int x, int y, int r, uint16_t color) {
    M5.Display.fillSmoothCircle(x, y, r, color);
}

void m5u_display_fill_smooth_round_rect(int x, int y, int w, int h, int r, uint16_t color) {
    M5.Display.fillSmoothRoundRect(x, y, w, h, r, color);
}

static m5gfx::fill_style_t m5u_display_gradient_style(int style) {
    switch (style) {
    case 0:
        return m5gfx::HLINEAR;
    case 1:
        return m5gfx::VLINEAR;
    case 2:
    default:
        return m5gfx::RADIAL;
    }
}

void m5u_display_fill_gradient_rect(int x, int y, int w, int h, uint16_t start_color, uint16_t end_color, int style) {
    if (w <= 0 || h <= 0) {
        return;
    }
    M5.Display.fillGradientRect(x, y, (uint32_t)w, (uint32_t)h, start_color, end_color, m5u_display_gradient_style(style));
}

void m5u_display_flood_fill(int x, int y, uint16_t color) {
    M5.Display.floodFill(x, y, color);
}

void m5u_display_set_scroll_rect(int x, int y, int w, int h) {
    M5.Display.setScrollRect(x, y, w, h);
}

void m5u_display_set_scroll_rect_color(int x, int y, int w, int h, uint16_t color) {
    M5.Display.setScrollRect(x, y, w, h, color);
}

void m5u_display_get_scroll_rect(int* x, int* y, int* w, int* h) {
    int32_t rx = 0;
    int32_t ry = 0;
    int32_t rw = 0;
    int32_t rh = 0;
    M5.Display.getScrollRect(&rx, &ry, &rw, &rh);
    if (x) { *x = (int)rx; }
    if (y) { *y = (int)ry; }
    if (w) { *w = (int)rw; }
    if (h) { *h = (int)rh; }
}

void m5u_display_clear_scroll_rect(void) {
    M5.Display.clearScrollRect();
}

void m5u_display_scroll(int dx, int dy) {
    M5.Display.scroll(dx, dy);
}

int m5u_display_text_width(const char* text) {
    return text ? M5.Display.textWidth(text) : 0;
}

int m5u_display_text_length(const char* text, int width) {
    return text ? M5.Display.textLength(text, width) : 0;
}

int m5u_display_get_text_datum(void) {
    return (int)M5.Display.getTextDatum();
}

int m5u_display_font_width(void) {
    return M5.Display.fontWidth();
}

void m5u_display_set_text_padding(uint32_t padding) {
    M5.Display.setTextPadding(padding);
}

uint32_t m5u_display_get_text_padding(void) {
    return M5.Display.getTextPadding();
}

float m5u_display_get_text_size_x(void) {
    return M5.Display.getTextSizeX();
}

float m5u_display_get_text_size_y(void) {
    return M5.Display.getTextSizeY();
}

void m5u_display_set_clip_rect(int x, int y, int w, int h) {
    M5.Display.setClipRect(x, y, w, h);
}

void m5u_display_get_clip_rect(int* x, int* y, int* w, int* h) {
    int32_t rx = 0;
    int32_t ry = 0;
    int32_t rw = 0;
    int32_t rh = 0;
    M5.Display.getClipRect(&rx, &ry, &rw, &rh);
    if (x) { *x = (int)rx; }
    if (y) { *y = (int)ry; }
    if (w) { *w = (int)rw; }
    if (h) { *h = (int)rh; }
}

void m5u_display_clear_clip_rect(void) {
    M5.Display.clearClipRect();
}

uint32_t m5u_display_color888(uint8_t r, uint8_t g, uint8_t b) {
    return M5.Display.color888(r, g, b);
}

void m5u_display_set_pivot(float x, float y) {
    M5.Display.setPivot(x, y);
}

float m5u_display_get_pivot_x(void) {
    return M5.Display.getPivotX();
}

float m5u_display_get_pivot_y(void) {
    return M5.Display.getPivotY();
}

bool m5u_display_push_image_rgb565(int x, int y, int w, int h, const uint16_t* data) {
    if (!data || w <= 0 || h <= 0) {
        return false;
    }
    M5.Display.pushImage(x, y, w, h, data);
    return true;
}

bool m5u_display_push_image_rgb565_transparent(int x, int y, int w, int h, const uint16_t* data, uint16_t transparent) {
    if (!data || w <= 0 || h <= 0) {
        return false;
    }
    M5.Display.pushImage(x, y, w, h, data, transparent);
    return true;
}

uint16_t m5u_display_read_pixel(int x, int y) {
    return M5.Display.readPixel(x, y);
}

bool m5u_display_read_rect_rgb565(int x, int y, int w, int h, uint16_t* data) {
    if (!data || w <= 0 || h <= 0) {
        return false;
    }
    M5.Display.readRect(x, y, w, h, data);
    return true;
}

void m5u_display_copy_rect(int dst_x, int dst_y, int w, int h, int src_x, int src_y) {
    M5.Display.copyRect(dst_x, dst_y, w, h, src_x, src_y);
}

static m5u_image_options_t m5u_display_image_options(const m5u_image_options_t* options) {
    if (options) {
        return *options;
    }
    m5u_image_options_t defaults = {};
    defaults.scale_x = 1.0f;
    defaults.scale_y = 0.0f;
    defaults.datum = 0;
    return defaults;
}

static m5gfx::datum_t m5u_display_image_datum(int datum) {
    switch (datum) {
    case 0:
    case 1:
    case 2:
    case 4:
    case 5:
    case 6:
    case 8:
    case 9:
    case 10:
        return (m5gfx::datum_t)datum;
    default:
        return m5gfx::datum_t::top_left;
    }
}

bool m5u_display_draw_image(int format, const uint8_t* data, size_t len, const m5u_image_options_t* options) {
    if (!data || len == 0 || len > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    const auto opts = m5u_display_image_options(options);
    const auto datum = m5u_display_image_datum(opts.datum);
    const auto size = (uint32_t)len;
    switch (format) {
    case 0:
        return M5.Display.drawBmp(data, size, opts.x, opts.y, opts.max_width, opts.max_height, opts.off_x, opts.off_y, opts.scale_x, opts.scale_y, datum);
    case 1:
        return M5.Display.drawJpg(data, size, opts.x, opts.y, opts.max_width, opts.max_height, opts.off_x, opts.off_y, opts.scale_x, opts.scale_y, datum);
    case 2:
        return M5.Display.drawPng(data, size, opts.x, opts.y, opts.max_width, opts.max_height, opts.off_x, opts.off_y, opts.scale_x, opts.scale_y, datum);
    case 3:
        return M5.Display.drawQoi(data, size, opts.x, opts.y, opts.max_width, opts.max_height, opts.off_x, opts.off_y, opts.scale_x, opts.scale_y, datum);
    default:
        return false;
    }
}

bool m5u_display_draw_image_file(int format, const char* path, const m5u_image_options_t* options) {
    if (!path || !*path) {
        return false;
    }
    const auto opts = m5u_display_image_options(options);
    const auto datum = m5u_display_image_datum(opts.datum);
    switch (format) {
    case 0:
        return M5.Display.drawBmpFile(path, opts.x, opts.y, opts.max_width, opts.max_height, opts.off_x, opts.off_y, opts.scale_x, opts.scale_y, datum);
    case 1:
        return M5.Display.drawJpgFile(path, opts.x, opts.y, opts.max_width, opts.max_height, opts.off_x, opts.off_y, opts.scale_x, opts.scale_y, datum);
    case 2:
        return M5.Display.drawPngFile(path, opts.x, opts.y, opts.max_width, opts.max_height, opts.off_x, opts.off_y, opts.scale_x, opts.scale_y, datum);
    case 3:
        return M5.Display.drawQoiFile(path, opts.x, opts.y, opts.max_width, opts.max_height, opts.off_x, opts.off_y, opts.scale_x, opts.scale_y, datum);
    default:
        return false;
    }
}

void m5u_display_qrcode(const char* text, int x, int y, int width, uint8_t version, bool margin) {
    if (text) {
        M5.Display.qrcode(text, x, y, width, version, margin);
    }
}

int m5u_display_count(void) {
    return M5.getDisplayCount();
}

int m5u_display_index_for_kind(int kind) {
    return M5.getDisplayIndex((m5::board_t)kind);
}

int m5u_display_index_for_kinds(const int* kinds, size_t len) {
    if (!kinds) {
        return -1;
    }
    for (size_t i = 0; i < len; ++i) {
        int index = M5.getDisplayIndex((m5::board_t)kinds[i]);
        if (index >= 0) {
            return index;
        }
    }
    return -1;
}

int m5u_display_width_at(int index) {
    return M5.Displays(index).width();
}

int m5u_display_height_at(int index) {
    return M5.Displays(index).height();
}

void m5u_display_fill_screen_at(int index, uint16_t color) {
    M5.Displays(index).fillScreen(color);
}

void m5u_display_set_cursor_at(int index, int x, int y) {
    M5.Displays(index).setCursor(x, y);
}

void m5u_display_set_text_size_at(int index, int size) {
    M5.Displays(index).setTextSize(size);
}

void m5u_display_set_text_color_at(int index, uint16_t fg, uint16_t bg) {
    M5.Displays(index).setTextColor(fg, bg);
}

int m5u_display_get_rotation_at(int index) {
    return M5.Displays(index).getRotation();
}

void m5u_display_set_rotation_at(int index, int rotation) {
    M5.Displays(index).setRotation(rotation);
}

void m5u_display_set_brightness_at(int index, uint8_t brightness) {
    M5.Displays(index).setBrightness(brightness);
}

uint8_t m5u_display_get_brightness_at(int index) {
    return M5.Displays(index).getBrightness();
}

void m5u_display_sleep_at(int index) {
    M5.Displays(index).sleep();
}

void m5u_display_wakeup_at(int index) {
    M5.Displays(index).wakeup();
}

void m5u_display_power_save_at(int index, bool enable) {
    M5.Displays(index).powerSave(enable);
}

void m5u_display_invert_display_at(int index, bool invert) {
    M5.Displays(index).invertDisplay(invert);
}

bool m5u_display_get_invert_at(int index) {
    return M5.Displays(index).getInvert();
}

void m5u_display_set_swap_bytes_at(int index, bool swap) {
    M5.Displays(index).setSwapBytes(swap);
}

bool m5u_display_get_swap_bytes_at(int index) {
    return M5.Displays(index).getSwapBytes();
}

void m5u_display_set_color_depth_at(int index, int depth) {
    M5.Displays(index).setColorDepth((m5gfx::color_depth_t)depth);
}

int m5u_display_get_color_depth_at(int index) {
    return (int)M5.Displays(index).getColorDepth();
}

void m5u_display_set_addr_window_at(int index, int x, int y, int w, int h) {
    M5.Displays(index).setAddrWindow(x, y, w, h);
}

void m5u_display_set_window_at(int index, int xs, int ys, int xe, int ye) {
    M5.Displays(index).setWindow(xs, ys, xe, ye);
}

void m5u_display_begin_transaction_at(int index) {
    M5.Displays(index).beginTransaction();
}

void m5u_display_end_transaction_at(int index) {
    M5.Displays(index).endTransaction();
}

uint32_t m5u_display_get_start_count_at(int index) {
    return M5.Displays(index).getStartCount();
}

int m5u_display_get_scan_line_at(int index) {
    return M5.Displays(index).getScanLine();
}

void m5u_display_set_raw_color_at(int index, uint32_t color) {
    M5.Displays(index).setRawColor(color);
}

uint32_t m5u_display_get_raw_color_at(int index) {
    return M5.Displays(index).getRawColor();
}

void m5u_display_write_color_at(int index, uint16_t color, uint32_t length) {
    M5.Displays(index).writeColor(color, length);
}

void m5u_display_draw_pixel_current_at(int index, int x, int y) {
    M5.Displays(index).drawPixel(x, y);
}

void m5u_display_write_pixel_current_at(int index, int x, int y) {
    M5.Displays(index).writePixel(x, y);
}

void m5u_display_write_fill_rect_at(int index, int x, int y, int w, int h, uint16_t color) {
    M5.Displays(index).writeFillRect(x, y, w, h, color);
}

void m5u_display_write_fill_rect_preclipped_at(int index, int x, int y, int w, int h, uint16_t color) {
    M5.Displays(index).writeFillRectPreclipped(x, y, w, h, color);
}

void m5u_display_push_block_at(int index, uint16_t color, uint32_t length) {
    M5.Displays(index).pushBlock(color, length);
}

void m5u_display_progress_bar_at(int index, int x, int y, int w, int h, uint8_t value) {
    M5.Displays(index).progressBar(x, y, w, h, value);
}

void m5u_display_push_state_at(int index) {
    M5.Displays(index).pushState();
}

void m5u_display_pop_state_at(int index) {
    M5.Displays(index).popState();
}

void m5u_display_set_color_at(int index, uint16_t color) {
    M5.Displays(index).setColor(color);
}

uint32_t m5u_display_get_base_color_at(int index) {
    return M5.Displays(index).getBaseColor();
}

void m5u_display_set_base_color_at(int index, uint32_t color) {
    M5.Displays(index).setBaseColor(color);
}

int m5u_display_get_cursor_x_at(int index) {
    return M5.Displays(index).getCursorX();
}

int m5u_display_get_cursor_y_at(int index) {
    return M5.Displays(index).getCursorY();
}

void m5u_display_start_write_at(int index) {
    M5.Displays(index).startWrite();
}

void m5u_display_end_write_at(int index) {
    M5.Displays(index).endWrite();
}

void m5u_display_display_at(int index) {
    M5.Displays(index).display();
}

void m5u_display_display_region_at(int index, int x, int y, int w, int h) {
    M5.Displays(index).display(x, y, w, h);
}

bool m5u_display_display_busy_at(int index) {
    return M5.Displays(index).displayBusy();
}

void m5u_display_wait_display_at(int index) {
    M5.Displays(index).waitDisplay();
}

bool m5u_display_has_palette_at(int index) {
    return M5.Displays(index).hasPalette();
}

uint32_t m5u_display_get_palette_count_at(int index) {
    return M5.Displays(index).getPaletteCount();
}

bool m5u_display_is_readable_at(int index) {
    return M5.Displays(index).isReadable();
}

bool m5u_display_is_epd_at(int index) {
    return M5.Displays(index).isEPD();
}

bool m5u_display_is_bus_shared_at(int index) {
    return M5.Displays(index).isBusShared();
}

void m5u_display_set_auto_display_at(int index, bool enable) {
    M5.Displays(index).setAutoDisplay(enable);
}

void m5u_display_init_dma_at(int index) {
    M5.Displays(index).initDMA();
}

void m5u_display_wait_dma_at(int index) {
    M5.Displays(index).waitDMA();
}

bool m5u_display_dma_busy_at(int index) {
    return M5.Displays(index).dmaBusy();
}

void m5u_display_print_at(int index, const char* text) {
    M5.Displays(index).print(text);
}

void m5u_display_println_at(int index, const char* text) {
    M5.Displays(index).println(text);
}

int m5u_display_draw_string_at(int index, const char* text, int x, int y) {
    return text ? (int)M5.Displays(index).drawString(text, x, y) : 0;
}

int m5u_display_draw_center_string_at(int index, const char* text, int x, int y) {
    return text ? (int)M5.Displays(index).drawCenterString(text, x, y) : 0;
}

int m5u_display_draw_right_string_at(int index, const char* text, int x, int y) {
    return text ? (int)M5.Displays(index).drawRightString(text, x, y) : 0;
}

int m5u_display_draw_number_at(int index, int value, int x, int y) {
    return (int)M5.Displays(index).drawNumber((long)value, x, y);
}

int m5u_display_draw_float_at(int index, float value, uint8_t decimals, int x, int y) {
    return (int)M5.Displays(index).drawFloat(value, decimals, x, y);
}

int m5u_display_draw_char_at(int index, uint16_t codepoint, int x, int y) {
    return (int)M5.Displays(index).drawChar(codepoint, x, y);
}

void m5u_display_draw_line_at(int index, int x0, int y0, int x1, int y1, uint16_t color) {
    M5.Displays(index).drawLine(x0, y0, x1, y1, color);
}

void m5u_display_draw_rect_at(int index, int x, int y, int w, int h, uint16_t color) {
    M5.Displays(index).drawRect(x, y, w, h, color);
}

void m5u_display_fill_rect_at(int index, int x, int y, int w, int h, uint16_t color) {
    M5.Displays(index).fillRect(x, y, w, h, color);
}

void m5u_display_draw_circle_at(int index, int x, int y, int r, uint16_t color) {
    M5.Displays(index).drawCircle(x, y, r, color);
}

void m5u_display_fill_circle_at(int index, int x, int y, int r, uint16_t color) {
    M5.Displays(index).fillCircle(x, y, r, color);
}

void m5u_display_write_pixel_at(int index, int x, int y, uint16_t color) {
    M5.Displays(index).writePixel(x, y, color);
}

void m5u_display_draw_pixel_at(int index, int x, int y, uint16_t color) {
    M5.Displays(index).drawPixel(x, y, color);
}

void m5u_display_draw_fast_hline_at(int index, int x, int y, int w, uint16_t color) {
    M5.Displays(index).drawFastHLine(x, y, w, color);
}

void m5u_display_write_fast_hline_at(int index, int x, int y, int w, uint16_t color) {
    M5.Displays(index).writeFastHLine(x, y, w, color);
}

void m5u_display_draw_fast_vline_at(int index, int x, int y, int h, uint16_t color) {
    M5.Displays(index).drawFastVLine(x, y, h, color);
}

void m5u_display_write_fast_vline_at(int index, int x, int y, int h, uint16_t color) {
    M5.Displays(index).writeFastVLine(x, y, h, color);
}

void m5u_display_draw_round_rect_at(int index, int x, int y, int w, int h, int r, uint16_t color) {
    M5.Displays(index).drawRoundRect(x, y, w, h, r, color);
}

void m5u_display_fill_round_rect_at(int index, int x, int y, int w, int h, int r, uint16_t color) {
    M5.Displays(index).fillRoundRect(x, y, w, h, r, color);
}

void m5u_display_draw_triangle_at(int index, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color) {
    M5.Displays(index).drawTriangle(x0, y0, x1, y1, x2, y2, color);
}

void m5u_display_fill_triangle_at(int index, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color) {
    M5.Displays(index).fillTriangle(x0, y0, x1, y1, x2, y2, color);
}

void m5u_display_draw_ellipse_at(int index, int x, int y, int rx, int ry, uint16_t color) {
    M5.Displays(index).drawEllipse(x, y, rx, ry, color);
}

void m5u_display_fill_ellipse_at(int index, int x, int y, int rx, int ry, uint16_t color) {
    M5.Displays(index).fillEllipse(x, y, rx, ry, color);
}

void m5u_display_draw_arc_at(int index, int x, int y, int r0, int r1, float angle0, float angle1, uint16_t color) {
    M5.Displays(index).drawArc(x, y, r0, r1, angle0, angle1, color);
}

void m5u_display_fill_arc_at(int index, int x, int y, int r0, int r1, float angle0, float angle1, uint16_t color) {
    M5.Displays(index).fillArc(x, y, r0, r1, angle0, angle1, color);
}

void m5u_display_draw_ellipse_arc_at(int index, int x, int y, int r0x, int r1x, int r0y, int r1y, float angle0, float angle1, uint16_t color) {
    M5.Displays(index).drawEllipseArc(x, y, r0x, r1x, r0y, r1y, angle0, angle1, color);
}

void m5u_display_fill_ellipse_arc_at(int index, int x, int y, int r0x, int r1x, int r0y, int r1y, float angle0, float angle1, uint16_t color) {
    M5.Displays(index).fillEllipseArc(x, y, r0x, r1x, r0y, r1y, angle0, angle1, color);
}

void m5u_display_draw_bezier3_at(int index, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color) {
    M5.Displays(index).drawBezier(x0, y0, x1, y1, x2, y2, color);
}

void m5u_display_draw_bezier4_at(int index, int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, uint16_t color) {
    M5.Displays(index).drawBezier(x0, y0, x1, y1, x2, y2, x3, y3, color);
}

void m5u_display_draw_smooth_line_at(int index, int x0, int y0, int x1, int y1, uint16_t color) {
    M5.Displays(index).drawSmoothLine(x0, y0, x1, y1, color);
}

void m5u_display_draw_wide_line_at(int index, int x0, int y0, int x1, int y1, float radius, uint16_t color) {
    M5.Displays(index).drawWideLine(x0, y0, x1, y1, radius, color);
}

void m5u_display_draw_wedge_line_at(int index, int x0, int y0, int x1, int y1, float r0, float r1, uint16_t color) {
    M5.Displays(index).drawWedgeLine(x0, y0, x1, y1, r0, r1, color);
}

void m5u_display_draw_gradient_line_at(int index, int x0, int y0, int x1, int y1, uint16_t start_color, uint16_t end_color) {
    M5.Displays(index).drawGradientLine(x0, y0, x1, y1, start_color, end_color);
}

void m5u_display_draw_spot_at(int index, int x, int y, float radius, uint16_t color) {
    M5.Displays(index).drawSpot(x, y, radius, color);
}

void m5u_display_fill_smooth_circle_at(int index, int x, int y, int r, uint16_t color) {
    M5.Displays(index).fillSmoothCircle(x, y, r, color);
}

void m5u_display_fill_smooth_round_rect_at(int index, int x, int y, int w, int h, int r, uint16_t color) {
    M5.Displays(index).fillSmoothRoundRect(x, y, w, h, r, color);
}

void m5u_display_fill_gradient_rect_at(int index, int x, int y, int w, int h, uint16_t start_color, uint16_t end_color, int style) {
    if (w <= 0 || h <= 0) {
        return;
    }
    M5.Displays(index).fillGradientRect(x, y, (uint32_t)w, (uint32_t)h, start_color, end_color, m5u_display_gradient_style(style));
}

void m5u_display_flood_fill_at(int index, int x, int y, uint16_t color) {
    M5.Displays(index).floodFill(x, y, color);
}

void m5u_display_set_scroll_rect_at(int index, int x, int y, int w, int h) {
    M5.Displays(index).setScrollRect(x, y, w, h);
}

void m5u_display_set_scroll_rect_color_at(int index, int x, int y, int w, int h, uint16_t color) {
    M5.Displays(index).setScrollRect(x, y, w, h, color);
}

void m5u_display_get_scroll_rect_at(int index, int* x, int* y, int* w, int* h) {
    int32_t rx = 0;
    int32_t ry = 0;
    int32_t rw = 0;
    int32_t rh = 0;
    M5.Displays(index).getScrollRect(&rx, &ry, &rw, &rh);
    if (x) { *x = (int)rx; }
    if (y) { *y = (int)ry; }
    if (w) { *w = (int)rw; }
    if (h) { *h = (int)rh; }
}

void m5u_display_clear_scroll_rect_at(int index) {
    M5.Displays(index).clearScrollRect();
}

void m5u_display_scroll_at(int index, int dx, int dy) {
    M5.Displays(index).scroll(dx, dy);
}

int m5u_display_text_width_at(int index, const char* text) {
    return text ? M5.Displays(index).textWidth(text) : 0;
}

int m5u_display_text_length_at(int index, const char* text, int width) {
    return text ? M5.Displays(index).textLength(text, width) : 0;
}

int m5u_display_get_text_datum_at(int index) {
    return (int)M5.Displays(index).getTextDatum();
}

int m5u_display_font_width_at(int index) {
    return M5.Displays(index).fontWidth();
}

void m5u_display_set_text_padding_at(int index, uint32_t padding) {
    M5.Displays(index).setTextPadding(padding);
}

uint32_t m5u_display_get_text_padding_at(int index) {
    return M5.Displays(index).getTextPadding();
}

float m5u_display_get_text_size_x_at(int index) {
    return M5.Displays(index).getTextSizeX();
}

float m5u_display_get_text_size_y_at(int index) {
    return M5.Displays(index).getTextSizeY();
}

void m5u_display_set_clip_rect_at(int index, int x, int y, int w, int h) {
    M5.Displays(index).setClipRect(x, y, w, h);
}

void m5u_display_get_clip_rect_at(int index, int* x, int* y, int* w, int* h) {
    int32_t rx = 0;
    int32_t ry = 0;
    int32_t rw = 0;
    int32_t rh = 0;
    M5.Displays(index).getClipRect(&rx, &ry, &rw, &rh);
    if (x) { *x = (int)rx; }
    if (y) { *y = (int)ry; }
    if (w) { *w = (int)rw; }
    if (h) { *h = (int)rh; }
}

void m5u_display_clear_clip_rect_at(int index) {
    M5.Displays(index).clearClipRect();
}

void m5u_display_set_pivot_at(int index, float x, float y) {
    M5.Displays(index).setPivot(x, y);
}

float m5u_display_get_pivot_x_at(int index) {
    return M5.Displays(index).getPivotX();
}

float m5u_display_get_pivot_y_at(int index) {
    return M5.Displays(index).getPivotY();
}

bool m5u_display_push_image_rgb565_at(int index, int x, int y, int w, int h, const uint16_t* data) {
    if (!data || w <= 0 || h <= 0) {
        return false;
    }
    M5.Displays(index).pushImage(x, y, w, h, data);
    return true;
}

bool m5u_display_push_image_rgb565_transparent_at(int index, int x, int y, int w, int h, const uint16_t* data, uint16_t transparent) {
    if (!data || w <= 0 || h <= 0) {
        return false;
    }
    M5.Displays(index).pushImage(x, y, w, h, data, transparent);
    return true;
}

uint16_t m5u_display_read_pixel_at(int index, int x, int y) {
    return M5.Displays(index).readPixel(x, y);
}

bool m5u_display_read_rect_rgb565_at(int index, int x, int y, int w, int h, uint16_t* data) {
    if (!data || w <= 0 || h <= 0) {
        return false;
    }
    M5.Displays(index).readRect(x, y, w, h, data);
    return true;
}

void m5u_display_copy_rect_at(int index, int dst_x, int dst_y, int w, int h, int src_x, int src_y) {
    M5.Displays(index).copyRect(dst_x, dst_y, w, h, src_x, src_y);
}

bool m5u_display_draw_image_at(int index, int format, const uint8_t* data, size_t len, const m5u_image_options_t* options) {
    if (!data || len == 0 || len > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    const auto opts = m5u_display_image_options(options);
    const auto datum = m5u_display_image_datum(opts.datum);
    const auto size = (uint32_t)len;
    switch (format) {
    case 0:
        return M5.Displays(index).drawBmp(data, size, opts.x, opts.y, opts.max_width, opts.max_height, opts.off_x, opts.off_y, opts.scale_x, opts.scale_y, datum);
    case 1:
        return M5.Displays(index).drawJpg(data, size, opts.x, opts.y, opts.max_width, opts.max_height, opts.off_x, opts.off_y, opts.scale_x, opts.scale_y, datum);
    case 2:
        return M5.Displays(index).drawPng(data, size, opts.x, opts.y, opts.max_width, opts.max_height, opts.off_x, opts.off_y, opts.scale_x, opts.scale_y, datum);
    case 3:
        return M5.Displays(index).drawQoi(data, size, opts.x, opts.y, opts.max_width, opts.max_height, opts.off_x, opts.off_y, opts.scale_x, opts.scale_y, datum);
    default:
        return false;
    }
}

bool m5u_display_draw_image_file_at(int index, int format, const char* path, const m5u_image_options_t* options) {
    if (!path || !*path) {
        return false;
    }
    const auto opts = m5u_display_image_options(options);
    const auto datum = m5u_display_image_datum(opts.datum);
    switch (format) {
    case 0:
        return M5.Displays(index).drawBmpFile(path, opts.x, opts.y, opts.max_width, opts.max_height, opts.off_x, opts.off_y, opts.scale_x, opts.scale_y, datum);
    case 1:
        return M5.Displays(index).drawJpgFile(path, opts.x, opts.y, opts.max_width, opts.max_height, opts.off_x, opts.off_y, opts.scale_x, opts.scale_y, datum);
    case 2:
        return M5.Displays(index).drawPngFile(path, opts.x, opts.y, opts.max_width, opts.max_height, opts.off_x, opts.off_y, opts.scale_x, opts.scale_y, datum);
    case 3:
        return M5.Displays(index).drawQoiFile(path, opts.x, opts.y, opts.max_width, opts.max_height, opts.off_x, opts.off_y, opts.scale_x, opts.scale_y, datum);
    default:
        return false;
    }
}

void m5u_display_qrcode_at(int index, const char* text, int x, int y, int width, uint8_t version, bool margin) {
    if (text) {
        M5.Displays(index).qrcode(text, x, y, width, version, margin);
    }
}

bool m5u_button_is_pressed(int button) { return m5u_button_state(button, 0); }
bool m5u_button_was_pressed(int button) { return m5u_button_state(button, 1); }
bool m5u_button_was_released(int button) { return m5u_button_state(button, 2); }
bool m5u_button_was_clicked(int button) { return m5u_button_state(button, 3); }
bool m5u_button_was_hold(int button) { return m5u_button_state(button, 4); }
bool m5u_button_is_holding(int button) { return m5u_button_state(button, 5); }
bool m5u_button_was_decide_click_count(int button) { return m5u_button_state(button, 6); }
bool m5u_button_was_single_clicked(int button) { return m5u_button_state(button, 7); }
bool m5u_button_was_double_clicked(int button) { return m5u_button_state(button, 8); }
bool m5u_button_was_change_pressed(int button) { return m5u_button_state(button, 9); }
bool m5u_button_is_released(int button) { return m5u_button_state(button, 10); }
bool m5u_button_was_released_after_hold(int button) { return m5u_button_state(button, 11); }
int m5u_button_get_click_count(int button) {
    m5::Button_Class* btn = m5u_button_for_id(button);
    return btn ? btn->getClickCount() : 0;
}
bool m5u_button_was_release_for(int button, uint32_t ms) {
    m5::Button_Class* btn = m5u_button_for_id(button);
    return btn ? btn->wasReleaseFor(ms) : false;
}
bool m5u_button_pressed_for(int button, uint32_t ms) {
    m5::Button_Class* btn = m5u_button_for_id(button);
    return btn ? btn->pressedFor(ms) : false;
}
bool m5u_button_released_for(int button, uint32_t ms) {
    m5::Button_Class* btn = m5u_button_for_id(button);
    return btn ? btn->releasedFor(ms) : false;
}
void m5u_button_set_debounce_thresh(int button, uint32_t ms) {
    m5::Button_Class* btn = m5u_button_for_id(button);
    if (btn) {
        btn->setDebounceThresh(ms);
    }
}
void m5u_button_set_hold_thresh(int button, uint32_t ms) {
    m5::Button_Class* btn = m5u_button_for_id(button);
    if (btn) {
        btn->setHoldThresh(ms);
    }
}
void m5u_button_set_raw_state(int button, uint32_t msec, bool press) {
    m5::Button_Class* btn = m5u_button_for_id(button);
    if (btn) {
        btn->setRawState(msec, press);
    }
}
void m5u_button_set_state(int button, uint32_t msec, uint8_t state) {
    m5::Button_Class* btn = m5u_button_for_id(button);
    if (btn) {
        btn->setState(msec, static_cast<m5::Button_Class::button_state_t>(state));
    }
}
uint8_t m5u_button_get_state(int button) {
    m5::Button_Class* btn = m5u_button_for_id(button);
    return btn ? static_cast<uint8_t>(btn->getState()) : 0;
}
uint32_t m5u_button_last_change(int button) {
    m5::Button_Class* btn = m5u_button_for_id(button);
    return btn ? btn->lastChange() : 0;
}
uint32_t m5u_button_get_debounce_thresh(int button) {
    m5::Button_Class* btn = m5u_button_for_id(button);
    return btn ? btn->getDebounceThresh() : 0;
}
uint32_t m5u_button_get_hold_thresh(int button) {
    m5::Button_Class* btn = m5u_button_for_id(button);
    return btn ? btn->getHoldThresh() : 0;
}
uint32_t m5u_button_get_update_msec(int button) {
    m5::Button_Class* btn = m5u_button_for_id(button);
    return btn ? btn->getUpdateMsec() : 0;
}

bool m5u_mic_is_enabled(void) {
    return M5.Mic.isEnabled();
}

bool m5u_mic_is_recording(void) {
    return M5.Mic.isRecording();
}

size_t m5u_mic_recording_state(void) {
    return M5.Mic.isRecording();
}

void m5u_mic_end(void) {
    M5.Mic.end();
}

bool m5u_mic_record_i16_at(int16_t* buffer, size_t samples, uint32_t sample_rate_hz) {
    return M5.Mic.record(buffer, samples, sample_rate_hz);
}

bool m5u_mic_record_i16_ex(int16_t* buffer, size_t samples, uint32_t sample_rate_hz, bool stereo) {
    return M5.Mic.record(buffer, samples, sample_rate_hz, stereo);
}

bool m5u_mic_record_u8_ex(uint8_t* buffer, size_t samples, uint32_t sample_rate_hz, bool stereo) {
    return M5.Mic.record(buffer, samples, sample_rate_hz, stereo);
}

bool m5u_audio_capture_begin(uint32_t sample_rate_hz, size_t dma_frame_num, size_t dma_desc_num, uint8_t* out_channels) {
#if __has_include(<driver/i2s_std.h>)
    if (out_channels) {
        *out_channels = 0;
    }
    m5u_audio_capture_end();
    M5.Mic.end();
    M5.Speaker.end();

    auto cfg = M5.Mic.config();
    if (cfg.pin_data_in < 0 || cfg.pin_bck < 0 || cfg.pin_ws < 0) {
        return false;
    }

    if (!m5u_audio_capture_set_mic_enabled(true)) {
        return false;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(cfg.i2s_port, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = dma_desc_num ? dma_desc_num : cfg.dma_buf_count;
    chan_cfg.dma_frame_num = dma_frame_num ? dma_frame_num : cfg.dma_buf_len;

    esp_err_t err = i2s_new_channel(&chan_cfg, nullptr, &s_m5u_audio_capture_rx);
    if (err != ESP_OK) {
        s_m5u_audio_capture_rx = nullptr;
        return false;
    }

    const bool stereo = cfg.stereo;
    const bool use_pdm = cfg.pin_bck < 0 && !cfg.use_adc;
    i2s_std_config_t i2s_config;
    memset(&i2s_config, 0, sizeof(i2s_std_config_t));
    i2s_config.clk_cfg.clk_src = i2s_clock_src_t::I2S_CLK_SRC_PLL_160M;
    i2s_config.clk_cfg.sample_rate_hz = 48000;
    i2s_config.clk_cfg.mclk_multiple = i2s_mclk_multiple_t::I2S_MCLK_MULTIPLE_128;
    i2s_config.slot_cfg.data_bit_width = i2s_data_bit_width_t::I2S_DATA_BIT_WIDTH_16BIT;
    i2s_config.slot_cfg.slot_bit_width = i2s_slot_bit_width_t::I2S_SLOT_BIT_WIDTH_16BIT;
    i2s_config.slot_cfg.slot_mode = stereo ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO;
    i2s_config.slot_cfg.slot_mask = stereo
        ? I2S_STD_SLOT_BOTH
        : (cfg.left_channel ? I2S_STD_SLOT_LEFT : I2S_STD_SLOT_RIGHT);
    i2s_config.slot_cfg.ws_width = 16;
    i2s_config.slot_cfg.bit_shift = true;
#if SOC_I2S_HW_VERSION_1
    i2s_config.slot_cfg.msb_right = false;
#else
    i2s_config.slot_cfg.left_align = true;
    i2s_config.slot_cfg.big_endian = false;
    i2s_config.slot_cfg.bit_order_lsb = false;
#endif
    i2s_config.gpio_cfg.mclk = (gpio_num_t)cfg.pin_mck;
    i2s_config.gpio_cfg.bclk = (gpio_num_t)cfg.pin_bck;
    i2s_config.gpio_cfg.ws = (gpio_num_t)cfg.pin_ws;
    i2s_config.gpio_cfg.dout = I2S_GPIO_UNUSED;
    i2s_config.gpio_cfg.din = (gpio_num_t)cfg.pin_data_in;
    i2s_config.gpio_cfg.invert_flags.mclk_inv = false;
    i2s_config.gpio_cfg.invert_flags.bclk_inv = false;
    i2s_config.gpio_cfg.invert_flags.ws_inv = false;

    err = i2s_channel_init_std_mode(s_m5u_audio_capture_rx, &i2s_config);
    if (err != ESP_OK) {
        m5u_audio_capture_end();
        return false;
    }

    m5u_audio_capture_apply_rx_clock(cfg.i2s_port, sample_rate_hz, cfg.over_sampling, use_pdm);

    err = i2s_channel_enable(s_m5u_audio_capture_rx);
    if (err != ESP_OK) {
        m5u_audio_capture_end();
        return false;
    }

    s_m5u_audio_capture_channels = stereo ? 2 : 1;
    if (out_channels) {
        *out_channels = s_m5u_audio_capture_channels;
    }
    return true;
#else
    (void)sample_rate_hz;
    (void)dma_frame_num;
    (void)dma_desc_num;
    if (out_channels) {
        *out_channels = 0;
    }
    return false;
#endif
}

size_t m5u_audio_capture_read_i16(int16_t* buffer, size_t samples, uint32_t timeout_ms) {
#if __has_include(<driver/i2s_std.h>)
    if (!s_m5u_audio_capture_rx || !buffer || samples == 0) {
        return 0;
    }
    size_t bytes_read = 0;
    TickType_t ticks = timeout_ms == 0 ? 0 : pdMS_TO_TICKS(timeout_ms);
    esp_err_t err = i2s_channel_read(
        s_m5u_audio_capture_rx,
        buffer,
        samples * sizeof(int16_t),
        &bytes_read,
        ticks);
    return err == ESP_OK ? bytes_read / sizeof(int16_t) : 0;
#else
    (void)buffer;
    (void)samples;
    (void)timeout_ms;
    return 0;
#endif
}

void m5u_audio_capture_end(void) {
#if __has_include(<driver/i2s_std.h>)
    if (s_m5u_audio_capture_rx) {
        i2s_channel_disable(s_m5u_audio_capture_rx);
        i2s_del_channel(s_m5u_audio_capture_rx);
        s_m5u_audio_capture_rx = nullptr;
    }
    m5u_audio_capture_set_mic_enabled(false);
    s_m5u_audio_capture_channels = 0;
#endif
}

void m5u_mic_set_sample_rate(uint32_t sample_rate_hz) {
    M5.Mic.setSampleRate(sample_rate_hz);
}

bool m5u_mic_get_config(m5u_mic_config_t* out) {
    if (!out) {
        return false;
    }
    auto cfg = M5.Mic.config();
    out->pin_data_in = cfg.pin_data_in;
    out->pin_bck = cfg.pin_bck;
    out->pin_mck = cfg.pin_mck;
    out->pin_ws = cfg.pin_ws;
    out->sample_rate = cfg.sample_rate;
    out->left_channel = cfg.left_channel;
    out->stereo = cfg.stereo;
    out->over_sampling = cfg.over_sampling;
    out->magnification = cfg.magnification;
    out->noise_filter_level = cfg.noise_filter_level;
    out->use_adc = cfg.use_adc;
    out->dma_buf_len = cfg.dma_buf_len;
    out->dma_buf_count = cfg.dma_buf_count;
    out->task_priority = cfg.task_priority;
    out->task_pinned_core = cfg.task_pinned_core;
    out->i2s_port = (int)cfg.i2s_port;
    return true;
}

bool m5u_mic_set_config(const m5u_mic_config_t* config) {
    if (!config) {
        return false;
    }
    auto cfg = M5.Mic.config();
    cfg.pin_data_in = config->pin_data_in;
    cfg.pin_bck = config->pin_bck;
    cfg.pin_mck = config->pin_mck;
    cfg.pin_ws = config->pin_ws;
    cfg.sample_rate = config->sample_rate;
    cfg.left_channel = config->left_channel != 0;
    cfg.stereo = config->stereo != 0;
    cfg.over_sampling = config->over_sampling;
    cfg.magnification = config->magnification;
    cfg.noise_filter_level = config->noise_filter_level;
    cfg.use_adc = config->use_adc != 0;
    cfg.dma_buf_len = config->dma_buf_len;
    cfg.dma_buf_count = config->dma_buf_count;
    cfg.task_priority = config->task_priority;
    cfg.task_pinned_core = config->task_pinned_core;
    cfg.i2s_port = (i2s_port_t)config->i2s_port;
    M5.Mic.config(cfg);
    return true;
}

int m5u_mic_get_noise_filter_level(void) {
    return M5.Mic.config().noise_filter_level;
}

bool m5u_mic_set_noise_filter_level(int level) {
    auto cfg = M5.Mic.config();
    cfg.noise_filter_level = level;
    M5.Mic.config(cfg);
    return true;
}

bool m5u_speaker_is_enabled(void) {
    return M5.Speaker.isEnabled();
}

void m5u_speaker_end(void) {
    M5.Speaker.end();
}

uint8_t m5u_speaker_get_volume(void) {
    return M5.Speaker.getVolume();
}

bool m5u_speaker_get_config(m5u_speaker_config_t* out) {
    if (!out) {
        return false;
    }
    auto cfg = M5.Speaker.config();
    out->pin_data_out = cfg.pin_data_out;
    out->pin_bck = cfg.pin_bck;
    out->pin_mck = cfg.pin_mck;
    out->pin_ws = cfg.pin_ws;
    out->sample_rate = cfg.sample_rate;
    out->stereo = cfg.stereo;
    out->buzzer = cfg.buzzer;
    out->use_dac = cfg.use_dac;
    out->dac_zero_level = cfg.dac_zero_level;
    out->magnification = cfg.magnification;
    out->dma_buf_len = cfg.dma_buf_len;
    out->dma_buf_count = cfg.dma_buf_count;
    out->task_priority = cfg.task_priority;
    out->task_pinned_core = cfg.task_pinned_core;
    out->i2s_port = (int)cfg.i2s_port;
    return true;
}

bool m5u_speaker_set_config(const m5u_speaker_config_t* config) {
    if (!config) {
        return false;
    }
    auto cfg = M5.Speaker.config();
    cfg.pin_data_out = config->pin_data_out;
    cfg.pin_bck = config->pin_bck;
    cfg.pin_mck = config->pin_mck;
    cfg.pin_ws = config->pin_ws;
    cfg.sample_rate = config->sample_rate;
    cfg.stereo = config->stereo != 0;
    cfg.buzzer = config->buzzer != 0;
    cfg.use_dac = config->use_dac != 0;
    cfg.dac_zero_level = config->dac_zero_level;
    cfg.magnification = config->magnification;
    cfg.dma_buf_len = config->dma_buf_len;
    cfg.dma_buf_count = config->dma_buf_count;
    cfg.task_priority = config->task_priority;
    cfg.task_pinned_core = config->task_pinned_core;
    cfg.i2s_port = (i2s_port_t)config->i2s_port;
    M5.Speaker.config(cfg);
    return true;
}

bool m5u_speaker_tone_ex(float frequency_hz, uint32_t duration_ms, int channel) {
    return M5.Speaker.tone(frequency_hz, duration_ms, channel);
}

bool m5u_speaker_tone_options(float frequency_hz, uint32_t duration_ms, int channel, bool stop_current_sound) {
    return M5.Speaker.tone(frequency_hz, duration_ms, channel, stop_current_sound);
}

bool m5u_speaker_tone_full(float frequency_hz, uint32_t duration_ms, int channel, bool stop_current_sound, const uint8_t* raw_data, size_t len, bool stereo) {
    return raw_data && len ? M5.Speaker.tone(frequency_hz, duration_ms, channel, stop_current_sound, raw_data, len, stereo) : false;
}

bool m5u_speaker_play_u8(const uint8_t* samples, size_t len, uint32_t sample_rate_hz) {
    return M5.Speaker.playRaw(samples, len, sample_rate_hz, false, 1, 0);
}

bool m5u_speaker_play_u8_ex(const uint8_t* samples, size_t len, uint32_t sample_rate_hz, bool stereo, uint32_t repeat, int channel, bool stop_current_sound) {
    return M5.Speaker.playRaw(samples, len, sample_rate_hz, stereo, repeat, channel, stop_current_sound);
}

bool m5u_speaker_play_i8_ex(const int8_t* samples, size_t len, uint32_t sample_rate_hz, bool stereo, uint32_t repeat, int channel, bool stop_current_sound) {
    return M5.Speaker.playRaw(samples, len, sample_rate_hz, stereo, repeat, channel, stop_current_sound);
}

bool m5u_speaker_play_i16_ex(const int16_t* samples, size_t len, uint32_t sample_rate_hz, bool stereo, uint32_t repeat, int channel, bool stop_current_sound) {
    return M5.Speaker.playRaw(samples, len, sample_rate_hz, stereo, repeat, channel, stop_current_sound);
}

bool m5u_speaker_play_wav(const uint8_t* data, size_t len) {
    return M5.Speaker.playWav(data, len);
}

bool m5u_speaker_play_wav_ex(const uint8_t* data, size_t len, uint32_t repeat, int channel, bool stop_current_sound) {
    return M5.Speaker.playWav(data, len, repeat, channel, stop_current_sound);
}

bool m5u_speaker_is_playing(int channel) {
    return channel < 0 ? M5.Speaker.isPlaying() : M5.Speaker.isPlaying(channel);
}

size_t m5u_speaker_playing_channels(void) {
    return M5.Speaker.getPlayingChannels();
}

size_t m5u_speaker_channel_playing_state(int channel) {
    return channel >= 0 ? M5.Speaker.isPlaying(channel) : 0;
}

void m5u_speaker_stop(int channel) {
    if (channel < 0) { M5.Speaker.stop(); } else { M5.Speaker.stop(channel); }
}

uint8_t m5u_speaker_get_channel_volume(int channel) {
    return M5.Speaker.getChannelVolume(channel);
}

void m5u_speaker_set_channel_volume(int channel, uint8_t volume) {
    M5.Speaker.setChannelVolume(channel, volume);
}

void m5u_speaker_set_all_channel_volume(uint8_t volume) {
    M5.Speaker.setAllChannelVolume(volume);
}

bool m5u_imu_is_enabled(void) {
    return M5.Imu.isEnabled();
}

int m5u_imu_get_type(void) {
    return (int)M5.Imu.getType();
}

bool m5u_imu_update(void) {
    return M5.Imu.update();
}

int m5u_imu_update_mask(void) {
    return (int)M5.Imu.update();
}

bool m5u_imu_sleep(void) {
    return M5.Imu.sleep();
}

void m5u_imu_set_clock(uint32_t freq) {
    M5.Imu.setClock(freq);
}

bool m5u_imu_set_axis_order(int axis0, int axis1, int axis2) {
    return M5.Imu.setAxisOrder(
        (m5::IMU_Class::axis_t)axis0,
        (m5::IMU_Class::axis_t)axis1,
        (m5::IMU_Class::axis_t)axis2);
}

bool m5u_imu_set_axis_order_right_handed(int axis0, int axis1) {
    return M5.Imu.setAxisOrderRightHanded(
        (m5::IMU_Class::axis_t)axis0,
        (m5::IMU_Class::axis_t)axis1);
}

bool m5u_imu_set_axis_order_left_handed(int axis0, int axis1) {
    return M5.Imu.setAxisOrderLeftHanded(
        (m5::IMU_Class::axis_t)axis0,
        (m5::IMU_Class::axis_t)axis1);
}

bool m5u_imu_set_int_pin_active_logic(bool level) {
    return M5.Imu.setINTPinActiveLogic(level);
}

bool m5u_imu_load_offset_from_nvs(void) {
    return M5.Imu.loadOffsetFromNVS();
}

bool m5u_imu_save_offset_to_nvs(void) {
    return M5.Imu.saveOffsetToNVS();
}

float m5u_imu_get_offset_data(int index) {
    return M5.Imu.getOffsetData(index);
}

void m5u_imu_set_calibration(float x, float y, float z) {
    M5.Imu.setCalibration(x, y, z);
}

void m5u_imu_set_calibration_strength(uint8_t accel, uint8_t gyro, uint8_t mag) {
    M5.Imu.setCalibration(accel, gyro, mag);
}

void m5u_imu_clear_offset_data(void) {
    M5.Imu.clearOffsetData();
}

void m5u_imu_set_offset_data(size_t index, int32_t value) {
    M5.Imu.setOffsetData(index, value);
}

int32_t m5u_imu_get_offset_data_i32(size_t index) {
    return M5.Imu.getOffsetData(index);
}

int16_t m5u_imu_get_raw_data(size_t index) {
    return M5.Imu.getRawData(index);
}

static m5::IMU_Base* m5u_imu_device_for_kind(int kind) {
    static m5::AK8963_Class ak8963;
    static m5::BMM150_Class bmm150;
    static m5::BMI270_Class bmi270;
    static m5::MPU6886_Class mpu6886;
    static m5::SH200Q_Class sh200q;

    switch (kind) {
        case 0: return &ak8963;
        case 1: return &bmm150;
        case 2: return &bmi270;
        case 3: return &mpu6886;
        case 4: return &sh200q;
        default: return nullptr;
    }
}

int m5u_imu_device_begin(int kind) {
    auto imu = m5u_imu_device_for_kind(kind);
    return imu ? imu->begin() : 0;
}

bool m5u_imu_device_get_raw_data(int kind, m5u_imu_raw_data_t* out) {
    auto imu = m5u_imu_device_for_kind(kind);
    if (!imu || !out) {
        return false;
    }
    m5::IMU_Base::imu_raw_data_t raw = {};
    auto mask = imu->getImuRawData(&raw);
    out->accel_x = raw.accel.x;
    out->accel_y = raw.accel.y;
    out->accel_z = raw.accel.z;
    out->gyro_x = raw.gyro.x;
    out->gyro_y = raw.gyro.y;
    out->gyro_z = raw.gyro.z;
    out->mag_x = raw.mag.x;
    out->mag_y = raw.mag.y;
    out->mag_z = raw.mag.z;
    out->temp = raw.temp;
    out->sensor_mask = static_cast<uint8_t>(mask);
    return mask != m5::IMU_Base::imu_spec_none;
}

bool m5u_imu_device_get_convert_param(int kind, m5u_imu_convert_param_t* out) {
    auto imu = m5u_imu_device_for_kind(kind);
    if (!imu || !out) {
        return false;
    }
    m5::IMU_Base::imu_convert_param_t param;
    imu->getConvertParam(&param);
    out->accel_res = param.accel_res;
    out->gyro_res = param.gyro_res;
    out->mag_res = param.mag_res;
    out->temp_res = param.temp_res;
    out->temp_offset = param.temp_offset;
    return true;
}

bool m5u_imu_device_get_temp_adc(int kind, int16_t* adc) {
    auto imu = m5u_imu_device_for_kind(kind);
    return imu && adc ? imu->getTempAdc(adc) : false;
}

bool m5u_imu_device_sleep(int kind) {
    auto imu = m5u_imu_device_for_kind(kind);
    return imu ? imu->sleep() : false;
}

bool m5u_imu_device_set_int_pin_active_logic(int kind, bool level) {
    auto imu = m5u_imu_device_for_kind(kind);
    return imu ? imu->setINTPinActiveLogic(level) : false;
}

int m5u_imu_device_who_am_i(int kind) {
    switch (kind) {
        case 0: return static_cast<m5::AK8963_Class*>(m5u_imu_device_for_kind(kind))->WhoAmI();
        case 1: return static_cast<m5::BMM150_Class*>(m5u_imu_device_for_kind(kind))->WhoAmI();
        case 2: return static_cast<m5::BMI270_Class*>(m5u_imu_device_for_kind(kind))->WhoAmI();
        case 3: return static_cast<m5::MPU6886_Class*>(m5u_imu_device_for_kind(kind))->whoAmI();
        case 4: return static_cast<m5::SH200Q_Class*>(m5u_imu_device_for_kind(kind))->WhoAmI();
        default:
            return -1;
    }
}

bool m5u_touch_get_detail(int index, m5u_touch_detail_t* out) {
    if (!out) { return false; }
    auto d = M5.Touch.getDetail(index);
    out->x = d.x;
    out->y = d.y;
    out->prev_x = d.prev_x;
    out->prev_y = d.prev_y;
    out->base_x = d.base_x;
    out->base_y = d.base_y;
    out->base_msec = d.base_msec;
    out->state = static_cast<uint8_t>(d.state);
    out->is_pressed = d.isPressed();
    out->was_pressed = d.wasPressed();
    out->was_released = d.wasReleased();
    out->was_clicked = d.wasClicked();
    out->was_hold = d.wasHold();
    out->is_holding = d.isHolding();
    out->click_count = d.getClickCount();
    return true;
}

bool m5u_rtc_is_enabled(void) {
    return M5.Rtc.isEnabled();
}

static bool m5u_has_axp2101(void) {
#if M5U_HAS_AXP2101
    return M5.Power.getType() == m5::Power_Class::pmic_t::pmic_axp2101;
#else
    return false;
#endif
}

static bool m5u_has_axp192(void) {
#if M5U_HAS_AXP192
    return M5.Power.getType() == m5::Power_Class::pmic_t::pmic_axp192;
#else
    return false;
#endif
}

static bool m5u_has_aw32001(void) {
#if M5U_HAS_AW32001
    return M5.Power.getType() == m5::Power_Class::pmic_t::pmic_aw32001;
#else
    return false;
#endif
}

static bool m5u_has_bq27220(void) {
#if M5U_HAS_BQ27220
    return M5.Power.getType() == m5::Power_Class::pmic_t::pmic_aw32001;
#else
    return false;
#endif
}

static bool m5u_has_py32pmic(void) {
#if M5U_HAS_PY32PMIC
    return M5.Power.getType() == m5::Power_Class::pmic_t::pmic_py32pmic;
#else
    return false;
#endif
}

static bool m5u_has_ip5306(void) {
#if M5U_HAS_IP5306
    return M5.Power.getType() == m5::Power_Class::pmic_t::pmic_ip5306;
#else
    return false;
#endif
}

bool m5u_power_axp192_begin(void) {
#if M5U_HAS_AXP192
    return m5u_has_axp192() && M5.Power.Axp192.begin();
#else
    return false;
#endif
}

int m5u_power_axp192_get_battery_level(void) {
#if M5U_HAS_AXP192
    return m5u_has_axp192() ? M5.Power.Axp192.getBatteryLevel() : -1;
#else
    return -1;
#endif
}

bool m5u_power_axp192_set_battery_charge(bool enable) {
#if M5U_HAS_AXP192
    if (!m5u_has_axp192()) {
        return false;
    }
    M5.Power.Axp192.setBatteryCharge(enable);
    return true;
#else
    (void)enable;
    return false;
#endif
}

bool m5u_power_axp192_set_charge_current(uint16_t max_ma) {
#if M5U_HAS_AXP192
    if (!m5u_has_axp192()) {
        return false;
    }
    M5.Power.Axp192.setChargeCurrent(max_ma);
    return true;
#else
    (void)max_ma;
    return false;
#endif
}

bool m5u_power_axp192_set_charge_voltage(uint16_t max_mv) {
#if M5U_HAS_AXP192
    if (!m5u_has_axp192()) {
        return false;
    }
    M5.Power.Axp192.setChargeVoltage(max_mv);
    return true;
#else
    (void)max_mv;
    return false;
#endif
}

bool m5u_power_axp192_is_charging(void) {
#if M5U_HAS_AXP192
    return m5u_has_axp192() && M5.Power.Axp192.isCharging();
#else
    return false;
#endif
}

bool m5u_power_axp192_set_dcdc(uint8_t channel, int voltage_mv) {
#if M5U_HAS_AXP192
    if (!m5u_has_axp192()) {
        return false;
    }
    switch (channel) {
    case 1: M5.Power.Axp192.setDCDC1(voltage_mv); return true;
    case 2: M5.Power.Axp192.setDCDC2(voltage_mv); return true;
    case 3: M5.Power.Axp192.setDCDC3(voltage_mv); return true;
    default: return false;
    }
#else
    (void)channel;
    (void)voltage_mv;
    return false;
#endif
}

bool m5u_power_axp192_set_ldo(uint8_t channel, int voltage_mv) {
#if M5U_HAS_AXP192
    if (!m5u_has_axp192()) {
        return false;
    }
    switch (channel) {
    case 0: M5.Power.Axp192.setLDO0(voltage_mv); return true;
    case 2: M5.Power.Axp192.setLDO2(voltage_mv); return true;
    case 3: M5.Power.Axp192.setLDO3(voltage_mv); return true;
    default: return false;
    }
#else
    (void)channel;
    (void)voltage_mv;
    return false;
#endif
}

bool m5u_power_axp192_set_gpio(uint8_t gpio_num, bool state) {
#if M5U_HAS_AXP192
    if (!m5u_has_axp192() || gpio_num > 4) {
        return false;
    }
    M5.Power.Axp192.setGPIO(gpio_num, state);
    return true;
#else
    (void)gpio_num;
    (void)state;
    return false;
#endif
}

bool m5u_power_axp192_power_off(void) {
#if M5U_HAS_AXP192
    if (!m5u_has_axp192()) {
        return false;
    }
    M5.Power.Axp192.powerOff();
    return true;
#else
    return false;
#endif
}

bool m5u_power_axp192_set_adc_state(bool enable) {
#if M5U_HAS_AXP192
    if (!m5u_has_axp192()) {
        return false;
    }
    M5.Power.Axp192.setAdcState(enable);
    return true;
#else
    (void)enable;
    return false;
#endif
}

bool m5u_power_axp192_set_adc_rate(uint8_t rate) {
#if M5U_HAS_AXP192
    if (!m5u_has_axp192()) {
        return false;
    }
    M5.Power.Axp192.setAdcRate(rate);
    return true;
#else
    (void)rate;
    return false;
#endif
}

bool m5u_power_axp192_set_exten(bool enable) {
#if M5U_HAS_AXP192
    if (!m5u_has_axp192()) {
        return false;
    }
    M5.Power.Axp192.setEXTEN(enable);
    return true;
#else
    (void)enable;
    return false;
#endif
}

bool m5u_power_axp192_set_backup(bool enable) {
#if M5U_HAS_AXP192
    if (!m5u_has_axp192()) {
        return false;
    }
    M5.Power.Axp192.setBACKUP(enable);
    return true;
#else
    (void)enable;
    return false;
#endif
}

bool m5u_power_axp192_is_acin(void) {
#if M5U_HAS_AXP192
    return m5u_has_axp192() && M5.Power.Axp192.isACIN();
#else
    return false;
#endif
}

bool m5u_power_axp192_is_vbus(void) {
#if M5U_HAS_AXP192
    return m5u_has_axp192() && M5.Power.Axp192.isVBUS();
#else
    return false;
#endif
}

bool m5u_power_axp192_get_bat_state(void) {
#if M5U_HAS_AXP192
    return m5u_has_axp192() && M5.Power.Axp192.getBatState();
#else
    return false;
#endif
}

bool m5u_power_axp192_get_exten(void) {
#if M5U_HAS_AXP192
    return m5u_has_axp192() && M5.Power.Axp192.getEXTEN();
#else
    return false;
#endif
}

float m5u_power_axp192_get_battery_voltage_v(void) {
#if M5U_HAS_AXP192
    return m5u_has_axp192() ? M5.Power.Axp192.getBatteryVoltage() : 0.0f;
#else
    return 0.0f;
#endif
}

float m5u_power_axp192_get_battery_discharge_current_ma(void) {
#if M5U_HAS_AXP192
    return m5u_has_axp192() ? M5.Power.Axp192.getBatteryDischargeCurrent() : 0.0f;
#else
    return 0.0f;
#endif
}

float m5u_power_axp192_get_battery_charge_current_ma(void) {
#if M5U_HAS_AXP192
    return m5u_has_axp192() ? M5.Power.Axp192.getBatteryChargeCurrent() : 0.0f;
#else
    return 0.0f;
#endif
}

float m5u_power_axp192_get_battery_power_mw(void) {
#if M5U_HAS_AXP192
    return m5u_has_axp192() ? M5.Power.Axp192.getBatteryPower() : 0.0f;
#else
    return 0.0f;
#endif
}

float m5u_power_axp192_get_acin_voltage_v(void) {
#if M5U_HAS_AXP192
    return m5u_has_axp192() ? M5.Power.Axp192.getACINVoltage() : 0.0f;
#else
    return 0.0f;
#endif
}

float m5u_power_axp192_get_acin_current_ma(void) {
#if M5U_HAS_AXP192
    return m5u_has_axp192() ? M5.Power.Axp192.getACINCurrent() : 0.0f;
#else
    return 0.0f;
#endif
}

float m5u_power_axp192_get_vbus_voltage_v(void) {
#if M5U_HAS_AXP192
    return m5u_has_axp192() ? M5.Power.Axp192.getVBUSVoltage() : 0.0f;
#else
    return 0.0f;
#endif
}

float m5u_power_axp192_get_vbus_current_ma(void) {
#if M5U_HAS_AXP192
    return m5u_has_axp192() ? M5.Power.Axp192.getVBUSCurrent() : 0.0f;
#else
    return 0.0f;
#endif
}

float m5u_power_axp192_get_aps_voltage_v(void) {
#if M5U_HAS_AXP192
    return m5u_has_axp192() ? M5.Power.Axp192.getAPSVoltage() : 0.0f;
#else
    return 0.0f;
#endif
}

float m5u_power_axp192_get_internal_temperature_c(void) {
#if M5U_HAS_AXP192
    return m5u_has_axp192() ? M5.Power.Axp192.getInternalTemperature() : 0.0f;
#else
    return 0.0f;
#endif
}

uint8_t m5u_power_axp192_get_pek_press(void) {
#if M5U_HAS_AXP192
    return m5u_has_axp192() ? M5.Power.Axp192.getPekPress() : 0;
#else
    return 0;
#endif
}

bool m5u_power_aw32001_begin(void) {
#if M5U_HAS_AW32001
    return m5u_has_aw32001() && M5.Power.Aw32001.begin();
#else
    return false;
#endif
}

bool m5u_power_aw32001_set_battery_charge(bool enable) {
#if M5U_HAS_AW32001
    return m5u_has_aw32001() && M5.Power.Aw32001.setBatteryCharge(enable);
#else
    (void)enable;
    return false;
#endif
}

bool m5u_power_aw32001_set_charge_current(uint16_t max_ma) {
#if M5U_HAS_AW32001
    return m5u_has_aw32001() && M5.Power.Aw32001.setChargeCurrent(max_ma);
#else
    (void)max_ma;
    return false;
#endif
}

bool m5u_power_aw32001_set_charge_voltage(uint16_t max_mv) {
#if M5U_HAS_AW32001
    return m5u_has_aw32001() && M5.Power.Aw32001.setChargeVoltage(max_mv);
#else
    (void)max_mv;
    return false;
#endif
}

bool m5u_power_aw32001_is_charging(void) {
#if M5U_HAS_AW32001
    return m5u_has_aw32001() && M5.Power.Aw32001.isCharging();
#else
    return false;
#endif
}

uint16_t m5u_power_aw32001_get_charge_current(void) {
#if M5U_HAS_AW32001
    return m5u_has_aw32001() ? M5.Power.Aw32001.getChargeCurrent() : 0;
#else
    return 0;
#endif
}

uint16_t m5u_power_aw32001_get_charge_voltage(void) {
#if M5U_HAS_AW32001
    return m5u_has_aw32001() ? M5.Power.Aw32001.getChargeVoltage() : 0;
#else
    return 0;
#endif
}

int m5u_power_aw32001_get_charge_status(void) {
#if M5U_HAS_AW32001
    return m5u_has_aw32001() ? M5.Power.Aw32001.getChargeStatus() : -1;
#else
    return -1;
#endif
}

bool m5u_power_bq27220_begin(void) {
#if M5U_HAS_BQ27220
    return m5u_has_bq27220() && M5.Power.Bq27220.begin();
#else
    return false;
#endif
}

int16_t m5u_power_bq27220_get_current_ma(void) {
#if M5U_HAS_BQ27220
    return m5u_has_bq27220() ? M5.Power.Bq27220.getCurrent_mA() : 0;
#else
    return 0;
#endif
}

int16_t m5u_power_bq27220_get_voltage_mv(void) {
#if M5U_HAS_BQ27220
    return m5u_has_bq27220() ? M5.Power.Bq27220.getVoltage_mV() : 0;
#else
    return 0;
#endif
}

float m5u_power_bq27220_get_current_a(void) {
#if M5U_HAS_BQ27220
    return m5u_has_bq27220() ? M5.Power.Bq27220.getCurrent_F() : 0.0f;
#else
    return 0.0f;
#endif
}

float m5u_power_bq27220_get_voltage_v(void) {
#if M5U_HAS_BQ27220
    return m5u_has_bq27220() ? M5.Power.Bq27220.getVoltage_F() : 0.0f;
#else
    return 0.0f;
#endif
}

bool m5u_power_ina226_begin(void) {
#if M5U_HAS_INA226
    return M5.Power.Ina226.begin();
#else
    return false;
#endif
}

bool m5u_power_ina226_config(const m5u_power_ina226_config_t* config) {
#if M5U_HAS_INA226
    if (!config) {
        return false;
    }

    m5::INA226_Class::config_t cfg;
    cfg.shunt_res = config->shunt_res;
    cfg.max_expected_current = config->max_expected_current;
    cfg.sampling_rate = static_cast<m5::INA226_Class::Sampling>(config->sampling_rate & 0x07);
    cfg.shunt_conversion_time = static_cast<m5::INA226_Class::ConversionTime>(config->shunt_conversion_time & 0x07);
    cfg.bus_conversion_time = static_cast<m5::INA226_Class::ConversionTime>(config->bus_conversion_time & 0x07);
    cfg.mode = static_cast<m5::INA226_Class::Mode>(config->mode & 0x07);
    M5.Power.Ina226.config(cfg);
    return true;
#else
    (void)config;
    return false;
#endif
}

float m5u_power_ina226_get_bus_voltage_v(void) {
#if M5U_HAS_INA226
    return M5.Power.Ina226.getBusVoltage();
#else
    return 0.0f;
#endif
}

float m5u_power_ina226_get_shunt_voltage_v(void) {
#if M5U_HAS_INA226
    return M5.Power.Ina226.getShuntVoltage();
#else
    return 0.0f;
#endif
}

float m5u_power_ina226_get_shunt_current_a(void) {
#if M5U_HAS_INA226
    return M5.Power.Ina226.getShuntCurrent();
#else
    return 0.0f;
#endif
}

float m5u_power_ina226_get_power_w(void) {
#if M5U_HAS_INA226
    return M5.Power.Ina226.getPower();
#else
    return 0.0f;
#endif
}

static bool m5u_power_ina3221_valid_index(size_t index) {
#if M5U_HAS_INA3221
    return index < 2;
#else
    (void)index;
    return false;
#endif
}

bool m5u_power_ina3221_begin(size_t index) {
#if M5U_HAS_INA3221
    return m5u_power_ina3221_valid_index(index) && M5.Power.Ina3221[index].begin();
#else
    (void)index;
    return false;
#endif
}

float m5u_power_ina3221_get_bus_voltage_v(size_t index, uint8_t channel) {
#if M5U_HAS_INA3221
    return m5u_power_ina3221_valid_index(index) ? M5.Power.Ina3221[index].getBusVoltage(channel) : 0.0f;
#else
    (void)index;
    (void)channel;
    return 0.0f;
#endif
}

float m5u_power_ina3221_get_shunt_voltage_v(size_t index, uint8_t channel) {
#if M5U_HAS_INA3221
    return m5u_power_ina3221_valid_index(index) ? M5.Power.Ina3221[index].getShuntVoltage(channel) : 0.0f;
#else
    (void)index;
    (void)channel;
    return 0.0f;
#endif
}

float m5u_power_ina3221_get_current_a(size_t index, uint8_t channel) {
#if M5U_HAS_INA3221
    return m5u_power_ina3221_valid_index(index) ? M5.Power.Ina3221[index].getCurrent(channel) : 0.0f;
#else
    (void)index;
    (void)channel;
    return 0.0f;
#endif
}

int32_t m5u_power_ina3221_get_bus_voltage_mv(size_t index, uint8_t channel) {
#if M5U_HAS_INA3221
    return m5u_power_ina3221_valid_index(index) ? M5.Power.Ina3221[index].getBusMilliVoltage(channel) : 0;
#else
    (void)index;
    (void)channel;
    return 0;
#endif
}

int32_t m5u_power_ina3221_get_shunt_voltage_mv(size_t index, uint8_t channel) {
#if M5U_HAS_INA3221
    return m5u_power_ina3221_valid_index(index) ? M5.Power.Ina3221[index].getShuntMilliVoltage(channel) : 0;
#else
    (void)index;
    (void)channel;
    return 0;
#endif
}

bool m5u_power_ina3221_set_shunt_res(size_t index, uint8_t channel, uint32_t res) {
#if M5U_HAS_INA3221
    if (!m5u_power_ina3221_valid_index(index)) {
        return false;
    }
    M5.Power.Ina3221[index].setShuntRes(channel, res);
    return channel < 3;
#else
    (void)index;
    (void)channel;
    (void)res;
    return false;
#endif
}

bool m5u_power_ip5306_begin(void) {
#if M5U_HAS_IP5306
    return m5u_has_ip5306() && M5.Power.Ip5306.begin();
#else
    return false;
#endif
}

int m5u_power_ip5306_get_battery_level(void) {
#if M5U_HAS_IP5306
    return m5u_has_ip5306() ? M5.Power.Ip5306.getBatteryLevel() : -1;
#else
    return -1;
#endif
}

bool m5u_power_ip5306_set_battery_charge(bool enable) {
#if M5U_HAS_IP5306
    if (!m5u_has_ip5306()) {
        return false;
    }
    M5.Power.Ip5306.setBatteryCharge(enable);
    return true;
#else
    (void)enable;
    return false;
#endif
}

bool m5u_power_ip5306_set_charge_current(uint16_t max_ma) {
#if M5U_HAS_IP5306
    if (!m5u_has_ip5306()) {
        return false;
    }
    M5.Power.Ip5306.setChargeCurrent(max_ma);
    return true;
#else
    (void)max_ma;
    return false;
#endif
}

bool m5u_power_ip5306_set_charge_voltage(uint16_t max_mv) {
#if M5U_HAS_IP5306
    if (!m5u_has_ip5306()) {
        return false;
    }
    M5.Power.Ip5306.setChargeVoltage(max_mv);
    return true;
#else
    (void)max_mv;
    return false;
#endif
}

bool m5u_power_ip5306_is_charging(void) {
#if M5U_HAS_IP5306
    return m5u_has_ip5306() && M5.Power.Ip5306.isCharging();
#else
    return false;
#endif
}

bool m5u_power_ip5306_set_power_boost_keep_on(bool enable) {
#if M5U_HAS_IP5306
    return m5u_has_ip5306() && M5.Power.Ip5306.setPowerBoostKeepOn(enable);
#else
    (void)enable;
    return false;
#endif
}

bool m5u_power_py32pmic_begin(void) {
#if M5U_HAS_PY32PMIC
    return m5u_has_py32pmic() && M5.Power.PY32pmic.begin();
#else
    return false;
#endif
}

bool m5u_power_py32pmic_set_ext_output(bool enable) {
#if M5U_HAS_PY32PMIC
    return m5u_has_py32pmic() && M5.Power.PY32pmic.setExtOutput(enable);
#else
    (void)enable;
    return false;
#endif
}

bool m5u_power_py32pmic_set_battery_charge(bool enable) {
#if M5U_HAS_PY32PMIC
    return m5u_has_py32pmic() && M5.Power.PY32pmic.setBatteryCharge(enable);
#else
    (void)enable;
    return false;
#endif
}

bool m5u_power_py32pmic_set_charge_current(uint16_t max_ma) {
#if M5U_HAS_PY32PMIC
    return m5u_has_py32pmic() && M5.Power.PY32pmic.setChargeCurrent(max_ma);
#else
    (void)max_ma;
    return false;
#endif
}

bool m5u_power_py32pmic_set_charge_voltage(uint16_t max_mv) {
#if M5U_HAS_PY32PMIC
    return m5u_has_py32pmic() && M5.Power.PY32pmic.setChargeVoltage(max_mv);
#else
    (void)max_mv;
    return false;
#endif
}

bool m5u_power_py32pmic_is_charging(void) {
#if M5U_HAS_PY32PMIC
    return m5u_has_py32pmic() && M5.Power.PY32pmic.isCharging();
#else
    return false;
#endif
}

uint16_t m5u_power_py32pmic_get_charge_current(void) {
#if M5U_HAS_PY32PMIC
    return m5u_has_py32pmic() ? M5.Power.PY32pmic.getChargeCurrent() : 0;
#else
    return 0;
#endif
}

uint16_t m5u_power_py32pmic_get_charge_voltage(void) {
#if M5U_HAS_PY32PMIC
    return m5u_has_py32pmic() ? M5.Power.PY32pmic.getChargeVoltage() : 0;
#else
    return 0;
#endif
}

uint8_t m5u_power_py32pmic_get_pek_press(void) {
#if M5U_HAS_PY32PMIC
    return m5u_has_py32pmic() ? M5.Power.PY32pmic.getPekPress() : 0;
#else
    return 0;
#endif
}

bool m5u_power_py32pmic_power_off(void) {
#if M5U_HAS_PY32PMIC
    return m5u_has_py32pmic() && M5.Power.PY32pmic.powerOff();
#else
    return false;
#endif
}

bool m5u_power_axp2101_begin(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() && M5.Power.Axp2101.begin();
#else
    return false;
#endif
}

int m5u_power_axp2101_get_battery_level(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() ? M5.Power.Axp2101.getBatteryLevel() : -1;
#else
    return -1;
#endif
}

bool m5u_power_axp2101_set_battery_charge(bool enable) {
#if M5U_HAS_AXP2101
    if (!m5u_has_axp2101()) {
        return false;
    }
    M5.Power.Axp2101.setBatteryCharge(enable);
    return true;
#else
    (void)enable;
    return false;
#endif
}

bool m5u_power_axp2101_set_pre_charge_current(uint16_t max_ma) {
#if M5U_HAS_AXP2101
    if (!m5u_has_axp2101()) {
        return false;
    }
    M5.Power.Axp2101.setPreChargeCurrent(max_ma);
    return true;
#else
    (void)max_ma;
    return false;
#endif
}

bool m5u_power_axp2101_set_charge_current(uint16_t max_ma) {
#if M5U_HAS_AXP2101
    if (!m5u_has_axp2101()) {
        return false;
    }
    M5.Power.Axp2101.setChargeCurrent(max_ma);
    return true;
#else
    (void)max_ma;
    return false;
#endif
}

bool m5u_power_axp2101_set_charge_voltage(uint16_t max_mv) {
#if M5U_HAS_AXP2101
    if (!m5u_has_axp2101()) {
        return false;
    }
    M5.Power.Axp2101.setChargeVoltage(max_mv);
    return true;
#else
    (void)max_mv;
    return false;
#endif
}

int m5u_power_axp2101_get_charge_status(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() ? M5.Power.Axp2101.getChargeStatus() : -2;
#else
    return -2;
#endif
}

bool m5u_power_axp2101_is_charging(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() && M5.Power.Axp2101.isCharging();
#else
    return false;
#endif
}

bool m5u_power_axp2101_set_ldo(int kind, int channel, int voltage_mv) {
#if M5U_HAS_AXP2101
    if (!m5u_has_axp2101()) {
        return false;
    }

    switch (kind) {
    case 0:
        switch (channel) {
        case 1: M5.Power.Axp2101.setALDO1(voltage_mv); return true;
        case 2: M5.Power.Axp2101.setALDO2(voltage_mv); return true;
        case 3: M5.Power.Axp2101.setALDO3(voltage_mv); return true;
        case 4: M5.Power.Axp2101.setALDO4(voltage_mv); return true;
        default: return false;
        }
    case 1:
        switch (channel) {
        case 1: M5.Power.Axp2101.setBLDO1(voltage_mv); return true;
        case 2: M5.Power.Axp2101.setBLDO2(voltage_mv); return true;
        default: return false;
        }
    case 2:
        switch (channel) {
        case 1: M5.Power.Axp2101.setDLDO1(voltage_mv); return true;
        case 2: M5.Power.Axp2101.setDLDO2(voltage_mv); return true;
        default: return false;
        }
    default:
        return false;
    }
#else
    (void)kind;
    (void)channel;
    (void)voltage_mv;
    return false;
#endif
}

bool m5u_power_axp2101_get_ldo_enabled(int kind, int channel) {
#if M5U_HAS_AXP2101
    if (!m5u_has_axp2101()) {
        return false;
    }

    switch (kind) {
    case 0:
        switch (channel) {
        case 1: return M5.Power.Axp2101.getALDO1Enabled();
        case 2: return M5.Power.Axp2101.getALDO2Enabled();
        case 3: return M5.Power.Axp2101.getALDO3Enabled();
        case 4: return M5.Power.Axp2101.getALDO4Enabled();
        default: return false;
        }
    case 1:
        switch (channel) {
        case 1: return M5.Power.Axp2101.getBLDO1Enabled();
        case 2: return M5.Power.Axp2101.getBLDO2Enabled();
        default: return false;
        }
    default:
        return false;
    }
#else
    (void)kind;
    (void)channel;
    return false;
#endif
}

bool m5u_power_axp2101_power_off(void) {
#if M5U_HAS_AXP2101
    if (!m5u_has_axp2101()) {
        return false;
    }
    M5.Power.Axp2101.powerOff();
    return true;
#else
    return false;
#endif
}

bool m5u_power_axp2101_set_adc_state(bool enable) {
#if M5U_HAS_AXP2101
    if (!m5u_has_axp2101()) {
        return false;
    }
    M5.Power.Axp2101.setAdcState(enable);
    return true;
#else
    (void)enable;
    return false;
#endif
}

bool m5u_power_axp2101_set_adc_rate(uint8_t rate) {
#if M5U_HAS_AXP2101
    if (!m5u_has_axp2101()) {
        return false;
    }
    M5.Power.Axp2101.setAdcRate(rate);
    return true;
#else
    (void)rate;
    return false;
#endif
}

bool m5u_power_axp2101_set_backup(bool enable) {
#if M5U_HAS_AXP2101
    if (!m5u_has_axp2101()) {
        return false;
    }
    M5.Power.Axp2101.setBACKUP(enable);
    return true;
#else
    (void)enable;
    return false;
#endif
}

bool m5u_power_axp2101_is_acin(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() && M5.Power.Axp2101.isACIN();
#else
    return false;
#endif
}

bool m5u_power_axp2101_is_vbus(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() && M5.Power.Axp2101.isVBUS();
#else
    return false;
#endif
}

bool m5u_power_axp2101_get_bat_state(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() && M5.Power.Axp2101.getBatState();
#else
    return false;
#endif
}

float m5u_power_axp2101_get_battery_voltage_v(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() ? M5.Power.Axp2101.getBatteryVoltage() : 0.0f;
#else
    return 0.0f;
#endif
}

float m5u_power_axp2101_get_battery_discharge_current_ma(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() ? M5.Power.Axp2101.getBatteryDischargeCurrent() : 0.0f;
#else
    return 0.0f;
#endif
}

float m5u_power_axp2101_get_battery_charge_current_ma(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() ? M5.Power.Axp2101.getBatteryChargeCurrent() : 0.0f;
#else
    return 0.0f;
#endif
}

float m5u_power_axp2101_get_battery_power_mw(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() ? M5.Power.Axp2101.getBatteryPower() : 0.0f;
#else
    return 0.0f;
#endif
}

float m5u_power_axp2101_get_acin_voltage_v(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() ? M5.Power.Axp2101.getACINVoltage() : 0.0f;
#else
    return 0.0f;
#endif
}

float m5u_power_axp2101_get_acin_current_ma(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() ? M5.Power.Axp2101.getACINCurrent() : 0.0f;
#else
    return 0.0f;
#endif
}

float m5u_power_axp2101_get_vbus_voltage_v(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() ? M5.Power.Axp2101.getVBUSVoltage() : 0.0f;
#else
    return 0.0f;
#endif
}

float m5u_power_axp2101_get_vbus_current_ma(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() ? M5.Power.Axp2101.getVBUSCurrent() : 0.0f;
#else
    return 0.0f;
#endif
}

float m5u_power_axp2101_get_ts_voltage_v(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() ? M5.Power.Axp2101.getTSVoltage() : 0.0f;
#else
    return 0.0f;
#endif
}

float m5u_power_axp2101_get_aps_voltage_v(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() ? M5.Power.Axp2101.getAPSVoltage() : 0.0f;
#else
    return 0.0f;
#endif
}

float m5u_power_axp2101_get_internal_temperature_c(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() ? M5.Power.Axp2101.getInternalTemperature() : 0.0f;
#else
    return 0.0f;
#endif
}

uint8_t m5u_power_axp2101_get_pek_press(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() ? M5.Power.Axp2101.getPekPress() : 0;
#else
    return 0;
#endif
}

bool m5u_power_axp2101_disable_irq(uint64_t mask) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() && M5.Power.Axp2101.disableIRQ(mask);
#else
    (void)mask;
    return false;
#endif
}

bool m5u_power_axp2101_enable_irq(uint64_t mask) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() && M5.Power.Axp2101.enableIRQ(mask);
#else
    (void)mask;
    return false;
#endif
}

bool m5u_power_axp2101_clear_irq_statuses(void) {
#if M5U_HAS_AXP2101
    if (!m5u_has_axp2101()) {
        return false;
    }
    M5.Power.Axp2101.clearIRQStatuses();
    return true;
#else
    return false;
#endif
}

uint64_t m5u_power_axp2101_get_irq_statuses(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() ? M5.Power.Axp2101.getIRQStatuses() : 0;
#else
    return 0;
#endif
}

bool m5u_power_axp2101_is_bat_charger_under_temperature_irq(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() && M5.Power.Axp2101.isBatChargerUnderTemperatureIrq();
#else
    return false;
#endif
}

bool m5u_power_axp2101_is_bat_charger_over_temperature_irq(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() && M5.Power.Axp2101.isBatChargerOverTemperatureIrq();
#else
    return false;
#endif
}

bool m5u_power_axp2101_is_vbus_insert_irq(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() && M5.Power.Axp2101.isVbusInsertIrq();
#else
    return false;
#endif
}

bool m5u_power_axp2101_is_vbus_remove_irq(void) {
#if M5U_HAS_AXP2101
    return m5u_has_axp2101() && M5.Power.Axp2101.isVbusRemoveIrq();
#else
    return false;
#endif
}

bool m5u_led_begin(void) {
    return M5.Led.begin();
}

void m5u_led_display(void) {
    M5.Led.display();
}

void m5u_led_set_auto_display(bool enable) {
    M5.Led.setAutoDisplay(enable);
}

size_t m5u_led_count(void) {
    return M5.Led.getCount();
}

void m5u_led_set_brightness(uint8_t brightness) {
    M5.Led.setBrightness(brightness);
}

void m5u_led_set_color_rgb(size_t index, uint8_t r, uint8_t g, uint8_t b) {
    M5.Led.setColor(index, r, g, b);
}

void m5u_led_set_all_color_rgb(uint8_t r, uint8_t g, uint8_t b) {
    M5.Led.setAllColor(RGBColor{r, g, b});
}

void m5u_led_set_colors_rgb(const m5u_led_color_t* colors, size_t index, size_t length) {
    if (!colors || !length) {
        return;
    }
    std::vector<RGBColor> rgb;
    rgb.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        rgb.push_back(RGBColor{colors[i].r, colors[i].g, colors[i].b});
    }
    M5.Led.setColors(rgb.data(), index, length);
}

int m5u_led_get_type(size_t index) {
    return (int)M5.Led.getLedType(index);
}

bool m5u_led_is_enabled(void) {
    return M5.Led.isEnabled();
}

#if defined(CONFIG_IDF_TARGET_ESP32S3)
static m5::LED_PowerHub_Class& m5u_led_power_hub(void) {
    static m5::LED_PowerHub_Class led;
    return led;
}
#endif

bool m5u_led_power_hub_begin(void) {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    return m5u_led_power_hub().begin();
#else
    return false;
#endif
}

size_t m5u_led_power_hub_count(void) {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    return m5u_led_power_hub().getCount();
#else
    return 0;
#endif
}

void m5u_led_power_hub_set_brightness(uint8_t brightness) {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    m5u_led_power_hub().setBrightness(brightness);
#else
    (void)brightness;
#endif
}

void m5u_led_power_hub_set_color_rgb(size_t index, uint8_t r, uint8_t g, uint8_t b) {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    if (index < m5u_led_power_hub().getCount()) {
        RGBColor color{r, g, b};
        m5u_led_power_hub().setColors(&color, index, 1);
    }
#else
    (void)index; (void)r; (void)g; (void)b;
#endif
}

void m5u_led_power_hub_set_colors_rgb(const m5u_led_color_t* colors, size_t index, size_t length) {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    if (!colors || !length) {
        return;
    }
    std::vector<RGBColor> rgb;
    rgb.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        rgb.push_back(RGBColor{colors[i].r, colors[i].g, colors[i].b});
    }
    m5u_led_power_hub().setColors(rgb.data(), index, length);
#else
    (void)colors; (void)index; (void)length;
#endif
}

void m5u_led_power_hub_display(void) {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    m5u_led_power_hub().display();
#endif
}

int m5u_led_power_hub_get_type(size_t index) {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    return (int)m5u_led_power_hub().getLedType(index);
#else
    (void)index;
    return 0;
#endif
}

#if M5U_HAS_LED_STRIP_RMT
static m5::LED_Strip_Class& m5u_led_strip(void) {
    static m5::LED_Strip_Class led;
    return led;
}

static std::shared_ptr<m5::LedBus_RMT>& m5u_led_strip_rmt_bus(void) {
    static std::shared_ptr<m5::LedBus_RMT> bus;
    return bus;
}

static m5::LED_Strip_Class::config_t::color_order_t m5u_led_strip_color_order(int color_order) {
    using color_order_t = m5::LED_Strip_Class::config_t::color_order_t;
    switch (color_order) {
        case 0: return color_order_t::color_order_rgb;
        case 1: return color_order_t::color_order_rbg;
        case 2: return color_order_t::color_order_grb;
        case 3: return color_order_t::color_order_gbr;
        case 4: return color_order_t::color_order_brg;
        case 5: return color_order_t::color_order_bgr;
        default: return color_order_t::color_order_grb;
    }
}
#endif

bool m5u_led_strip_set_config(const m5u_led_strip_config_t* config) {
#if M5U_HAS_LED_STRIP_RMT
    if (!config) {
        return false;
    }
    m5::LED_Strip_Class::config_t cfg;
    cfg.led_count = config->led_count;
    cfg.color_order = m5u_led_strip_color_order(config->color_order);
    cfg.byte_per_led = config->byte_per_led;
    m5u_led_strip().setConfig(cfg);
    return true;
#else
    (void)config;
    return false;
#endif
}

bool m5u_led_strip_set_rmt_bus_config(const m5u_led_strip_rmt_config_t* config) {
#if M5U_HAS_LED_STRIP_RMT
    if (!config) {
        return false;
    }
    auto& bus = m5u_led_strip_rmt_bus();
    if (bus) {
        bus->release();
    }
    bus = std::make_shared<m5::LedBus_RMT>();
    auto cfg = bus->config();
    cfg.frequency = config->frequency;
    cfg.t0h_ns = config->t0h_ns;
    cfg.t0l_ns = config->t0l_ns;
    cfg.t1h_ns = config->t1h_ns;
    cfg.t1l_ns = config->t1l_ns;
    cfg.reset_us = config->reset_us;
    cfg.pin_data = config->pin_data;
    bus->setConfig(cfg);
    m5u_led_strip().setBus(bus);
    return true;
#else
    (void)config;
    return false;
#endif
}

bool m5u_led_strip_begin(void) {
#if M5U_HAS_LED_STRIP_RMT
    return m5u_led_strip().begin();
#else
    return false;
#endif
}

size_t m5u_led_strip_count(void) {
#if M5U_HAS_LED_STRIP_RMT
    return m5u_led_strip().getCount();
#else
    return 0;
#endif
}

void m5u_led_strip_set_brightness(uint8_t brightness) {
#if M5U_HAS_LED_STRIP_RMT
    m5u_led_strip().setBrightness(brightness);
#else
    (void)brightness;
#endif
}

void m5u_led_strip_set_color_rgb(size_t index, uint8_t r, uint8_t g, uint8_t b) {
#if M5U_HAS_LED_STRIP_RMT
    if (index < m5u_led_strip().getCount()) {
        RGBColor color{r, g, b};
        m5u_led_strip().setColors(&color, index, 1);
    }
#else
    (void)index; (void)r; (void)g; (void)b;
#endif
}

void m5u_led_strip_set_colors_rgb(const m5u_led_color_t* colors, size_t index, size_t length) {
#if M5U_HAS_LED_STRIP_RMT
    auto& led = m5u_led_strip();
    const size_t count = led.getCount();
    if (!colors || !length || index >= count) {
        return;
    }
    const size_t available = count - index;
    if (length > available) {
        length = available;
    }
    std::vector<RGBColor> rgb;
    rgb.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        rgb.push_back(RGBColor{colors[i].r, colors[i].g, colors[i].b});
    }
    led.setColors(rgb.data(), index, length);
#else
    (void)colors; (void)index; (void)length;
#endif
}

void m5u_led_strip_display(void) {
#if M5U_HAS_LED_STRIP_RMT
    m5u_led_strip().display();
#endif
}

int m5u_led_strip_get_type(size_t index) {
#if M5U_HAS_LED_STRIP_RMT
    return (int)m5u_led_strip().getLedType(index);
#else
    (void)index;
    return 0;
#endif
}

void m5u_log_print(const char* text) {
    M5.Log.print(text);
}

void m5u_log_println_empty(void) {
    M5.Log.println();
}

void m5u_log_level(int level, const char* text) {
    M5.Log((esp_log_level_t)level, "%s", text);
}

void m5u_log_dump(const void* addr, uint32_t len, int level) {
    M5.Log.dump(addr, len, (esp_log_level_t)level);
}

const char* m5u_log_path_to_file_name(const char* path) {
    return path ? m5::Log_Class::pathToFileName(path) : nullptr;
}

bool m5u_log_set_callback(m5u_log_callback_t callback, void* user_data) {
    s_m5u_log_callback = callback;
    s_m5u_log_callback_user_data = user_data;
    if (!callback) {
        M5.Log.setCallback(nullptr);
        return true;
    }
    M5.Log.setCallback([](esp_log_level_t level, bool use_color, const char* text) {
        if (s_m5u_log_callback) {
            s_m5u_log_callback((int)level, use_color, text, s_m5u_log_callback_user_data);
        }
    });
    return true;
}

static bool m5u_log_valid_target(int target) {
    return target >= m5::log_target_serial && target < m5::log_target_max;
}

static bool m5u_log_valid_level(int level) {
    return level >= ESP_LOG_NONE && level <= ESP_LOG_VERBOSE;
}

bool m5u_log_set_enable_color(int target, bool enable) {
    if (!m5u_log_valid_target(target)) {
        return false;
    }
    M5.Log.setEnableColor((m5::log_target_t)target, enable);
    return true;
}

bool m5u_log_get_enable_color(int target) {
    if (!m5u_log_valid_target(target)) {
        return false;
    }
    return M5.Log.getEnableColor((m5::log_target_t)target);
}

bool m5u_log_set_level(int target, int level) {
    if (!m5u_log_valid_target(target) || !m5u_log_valid_level(level)) {
        return false;
    }
    M5.Log.setLogLevel((m5::log_target_t)target, (esp_log_level_t)level);
    return true;
}

int m5u_log_get_level(int target) {
    if (!m5u_log_valid_target(target)) {
        return -1;
    }
    return (int)M5.Log.getLogLevel((m5::log_target_t)target);
}

bool m5u_log_set_suffix(int target, const char* suffix) {
    if (!m5u_log_valid_target(target) || !suffix) {
        return false;
    }
    s_m5u_log_suffixes[target] = suffix;
    M5.Log.setSuffix((m5::log_target_t)target, s_m5u_log_suffixes[target].c_str());
    return true;
}

// ─── Off-screen canvas (LGFX_Sprite) ────────────────────────────────────────

static LGFX_Sprite* s_canvas = nullptr;

bool m5u_canvas_create(int width, int height) {
    delete s_canvas;
    s_canvas = new LGFX_Sprite(&M5.Display);
    s_canvas->setColorDepth(16);
    s_canvas->setPsram(true);
    void* buf = s_canvas->createSprite(width, height);
    if (!buf) {
        s_canvas->deleteSprite();
        s_canvas->setPsram(false);
        buf = s_canvas->createSprite(width, height);
    }
    if (!buf) { delete s_canvas; s_canvas = nullptr; return false; }
    return true;
}

void m5u_canvas_push(int x, int y) {
    if (s_canvas) s_canvas->pushSprite(x, y);
}

void m5u_canvas_delete(void) {
    if (s_canvas) { s_canvas->deleteSprite(); delete s_canvas; s_canvas = nullptr; }
}

void m5u_canvas_fill_screen(uint16_t color) {
    if (s_canvas) s_canvas->fillScreen(color);
}
void m5u_canvas_fill_smooth_circle(int x, int y, int r, uint16_t color) {
    if (s_canvas) s_canvas->fillSmoothCircle(x, y, r, color);
}
void m5u_canvas_draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    if (s_canvas) s_canvas->drawLine(x0, y0, x1, y1, color);
}
void m5u_canvas_draw_circle(int x, int y, int r, uint16_t color) {
    if (s_canvas) s_canvas->drawCircle(x, y, r, color);
}
void m5u_canvas_fill_circle(int x, int y, int r, uint16_t color) {
    if (s_canvas) s_canvas->fillCircle(x, y, r, color);
}
void m5u_canvas_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (s_canvas) s_canvas->fillRect(x, y, w, h, color);
}
void m5u_canvas_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color) {
    if (s_canvas) s_canvas->fillTriangle(x0, y0, x1, y1, x2, y2, color);
}
void m5u_canvas_fill_smooth_round_rect(int x, int y, int w, int h, int r, uint16_t color) {
    if (s_canvas) s_canvas->fillSmoothRoundRect(x, y, w, h, r, color);
}
void m5u_canvas_fill_arc(int x, int y, int r0, int r1, float a0, float a1, uint16_t color) {
    if (s_canvas) s_canvas->fillArc(x, y, r0, r1, a0, a1, color);
}
void m5u_canvas_fill_ellipse(int x, int y, int rx, int ry, uint16_t color) {
    if (s_canvas) s_canvas->fillEllipse(x, y, rx, ry, color);
}
void m5u_canvas_draw_ellipse(int x, int y, int rx, int ry, uint16_t color) {
    if (s_canvas) s_canvas->drawEllipse(x, y, rx, ry, color);
}
void m5u_canvas_set_text_size(int size) {
    if (s_canvas) s_canvas->setTextSize(size);
}
void m5u_canvas_set_text_color(uint16_t fg, uint16_t bg) {
    if (s_canvas) s_canvas->setTextColor(fg, bg);
}
void m5u_canvas_set_text_datum(int datum) {
    if (s_canvas) s_canvas->setTextDatum((textdatum_t)datum);
}
int m5u_canvas_text_width(const char* text) {
    return (s_canvas && text) ? s_canvas->textWidth(text) : 0;
}
int m5u_canvas_draw_string(const char* text, int x, int y) {
    return (s_canvas && text) ? (int)s_canvas->drawString(text, x, y) : 0;
}

// ─── FEETECH SCSCL bus servo (StackChan head) ───────────────────────────────

#include "driver/uart.h"

#define M5U_SERVO_DEFAULT_TX   6
#define M5U_SERVO_DEFAULT_RX   7
#define M5U_SERVO_DEFAULT_BAUD 1000000
#define M5U_SERVO_UART_NUM     UART_NUM_1
#define M5U_SERVO_BUF_SIZE     512

// SCSCL instruction codes
#define SCSCL_WRITE   0x03
#define SCSCL_READ    0x02

// SCSCL SRAM register addresses
#define SCSCL_TORQUE_ENABLE  0x28   // 1 byte
#define SCSCL_GOAL_POS_L     0x2A   // 2 bytes: goal position
#define SCSCL_GOAL_TIME_L    0x2C   // 2 bytes: travel time
#define SCSCL_GOAL_SPEED_L   0x2E   // 2 bytes: max speed
#define SCSCL_PRESENT_POS_L  0x38   // 2 bytes: current position

static bool s_servo_initialized = false;

static uint8_t scscl_checksum(const uint8_t* p, int len) {
    uint8_t cs = 0;
    for (int i = 0; i < len; i++) cs += p[i];
    return static_cast<uint8_t>(~cs);
}

bool m5u_servo_init(int tx_pin, int rx_pin, int baud_rate) {
    if (s_servo_initialized) return true;
    if (tx_pin < 0)   tx_pin   = M5U_SERVO_DEFAULT_TX;
    if (rx_pin < 0)   rx_pin   = M5U_SERVO_DEFAULT_RX;
    if (baud_rate <= 0) baud_rate = M5U_SERVO_DEFAULT_BAUD;

    uart_config_t cfg = {};
    cfg.baud_rate  = baud_rate;
    cfg.data_bits  = UART_DATA_8_BITS;
    cfg.parity     = UART_PARITY_DISABLE;
    cfg.stop_bits  = UART_STOP_BITS_1;
    cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    if (uart_driver_install(M5U_SERVO_UART_NUM, M5U_SERVO_BUF_SIZE * 2, 0, 0, nullptr, 0) != ESP_OK)
        return false;
    if (uart_param_config(M5U_SERVO_UART_NUM, &cfg) != ESP_OK) {
        uart_driver_delete(M5U_SERVO_UART_NUM);
        return false;
    }
    if (uart_set_pin(M5U_SERVO_UART_NUM, tx_pin, rx_pin,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        uart_driver_delete(M5U_SERVO_UART_NUM);
        return false;
    }
    s_servo_initialized = true;
    return true;
}

bool m5u_servo_write_raw_pos(uint8_t id, uint16_t raw_pos, uint16_t time_ms, uint16_t speed) {
    if (!s_servo_initialized) return false;
    // Packet: FF FF ID LEN INSTR ADDR PL PH TL TH SL SH CS  (13 bytes)
    // LEN = INSTR(1) + ADDR(1) + DATA(6) + CS(1) = 9
    uint8_t pkt[13];
    pkt[0]  = 0xFF;
    pkt[1]  = 0xFF;
    pkt[2]  = id;
    pkt[3]  = 9;
    pkt[4]  = SCSCL_WRITE;
    pkt[5]  = SCSCL_GOAL_POS_L;
    pkt[6]  = raw_pos  & 0xFF;
    pkt[7]  = (raw_pos  >> 8) & 0xFF;
    pkt[8]  = time_ms & 0xFF;
    pkt[9]  = (time_ms >> 8) & 0xFF;
    pkt[10] = speed   & 0xFF;
    pkt[11] = (speed   >> 8) & 0xFF;
    pkt[12] = scscl_checksum(&pkt[2], 10); // covers ID..SPD_H
    uart_write_bytes(M5U_SERVO_UART_NUM, reinterpret_cast<const char*>(pkt), sizeof(pkt));
    return true;
}

int m5u_servo_read_raw_pos(uint8_t id) {
    if (!s_servo_initialized) return -1;
    uart_flush_input(M5U_SERVO_UART_NUM);

    // Request: FF FF ID 04 02 ADDR NUM CS  (8 bytes)
    // LEN = INSTR(1) + ADDR(1) + NUM(1) + CS(1) = 4
    uint8_t req[8];
    req[0] = 0xFF;
    req[1] = 0xFF;
    req[2] = id;
    req[3] = 4;
    req[4] = SCSCL_READ;
    req[5] = SCSCL_PRESENT_POS_L;
    req[6] = 2;
    req[7] = scscl_checksum(&req[2], 5);
    uart_write_bytes(M5U_SERVO_UART_NUM, reinterpret_cast<const char*>(req), sizeof(req));

    // Response: FF FF ID 04 ERR PL PH CS  (8 bytes)
    uint8_t resp[8] = {};
    int got = uart_read_bytes(M5U_SERVO_UART_NUM, resp, sizeof(resp),
                              pdMS_TO_TICKS(10));
    if (got < 8) return -1;
    if (resp[0] != 0xFF || resp[1] != 0xFF || resp[2] != id) return -1;
    if (resp[4] != 0) return -1; // error byte
    return static_cast<int>(resp[5]) | (static_cast<int>(resp[6]) << 8);
}

bool m5u_servo_enable_torque(uint8_t id, bool enable) {
    if (!s_servo_initialized) return false;
    // Packet: FF FF ID 04 03 28 ENABLE CS  (8 bytes)
    // LEN = INSTR(1) + ADDR(1) + DATA(1) + CS(1) = 4
    uint8_t pkt[8];
    pkt[0] = 0xFF;
    pkt[1] = 0xFF;
    pkt[2] = id;
    pkt[3] = 4;
    pkt[4] = SCSCL_WRITE;
    pkt[5] = SCSCL_TORQUE_ENABLE;
    pkt[6] = enable ? 1 : 0;
    pkt[7] = scscl_checksum(&pkt[2], 5);
    uart_write_bytes(M5U_SERVO_UART_NUM, reinterpret_cast<const char*>(pkt), sizeof(pkt));
    return true;
}

void m5u_servo_deinit(void) {
    if (!s_servo_initialized) return;
    uart_driver_delete(M5U_SERVO_UART_NUM);
    s_servo_initialized = false;
}

bool m5u_cardputer_begin(bool enable_keyboard) {
#ifdef M5UNIFIED_RS_USE_REAL_M5CARDPUTER
    M5Cardputer.begin(m5u_apply_config(nullptr), enable_keyboard);
    return true;
#else
    (void)enable_keyboard;
    return false;
#endif
}

bool m5u_cardputer_begin_with_config(const m5u_config_t* config, bool enable_keyboard) {
#ifdef M5UNIFIED_RS_USE_REAL_M5CARDPUTER
    M5Cardputer.begin(m5u_apply_config(config), enable_keyboard);
    return true;
#else
    (void)config;
    (void)enable_keyboard;
    return false;
#endif
}

void m5u_cardputer_update(void) {
#ifdef M5UNIFIED_RS_USE_REAL_M5CARDPUTER
    M5Cardputer.update();
#endif
}

void m5u_cardputer_keyboard_begin(void) {
#ifdef M5UNIFIED_RS_USE_REAL_M5CARDPUTER
    M5Cardputer.Keyboard.begin();
#endif
}

bool m5u_cardputer_keyboard_is_pressed(void) {
#ifdef M5UNIFIED_RS_USE_REAL_M5CARDPUTER
    return M5Cardputer.Keyboard.isPressed() != 0;
#else
    return false;
#endif
}

uint8_t m5u_cardputer_keyboard_pressed_count(void) {
#ifdef M5UNIFIED_RS_USE_REAL_M5CARDPUTER
    return M5Cardputer.Keyboard.isPressed();
#else
    return 0;
#endif
}

bool m5u_cardputer_keyboard_is_change(void) {
#ifdef M5UNIFIED_RS_USE_REAL_M5CARDPUTER
    return M5Cardputer.Keyboard.isChange();
#else
    return false;
#endif
}

bool m5u_cardputer_keyboard_is_key_pressed(uint8_t key) {
#ifdef M5UNIFIED_RS_USE_REAL_M5CARDPUTER
    return M5Cardputer.Keyboard.isKeyPressed((char)key);
#else
    (void)key;
    return false;
#endif
}

uint8_t m5u_cardputer_keyboard_get_key(uint8_t x, uint8_t y) {
#ifdef M5UNIFIED_RS_USE_REAL_M5CARDPUTER
    Point2D_t key_coor;
    key_coor.x = x;
    key_coor.y = y;
    return M5Cardputer.Keyboard.getKey(key_coor);
#else
    (void)x;
    (void)y;
    return 0;
#endif
}

bool m5u_cardputer_keyboard_get_key_value(uint8_t x, uint8_t y, m5u_cardputer_key_value_t* out) {
    if (!out) { return false; }
    memset(out, 0, sizeof(*out));
#ifdef M5UNIFIED_RS_USE_REAL_M5CARDPUTER
    if (x >= 14 || y >= 4) { return false; }
    Point2D_t key_coor;
    key_coor.x = x;
    key_coor.y = y;
    auto value = M5Cardputer.Keyboard.getKeyValue(key_coor);
    out->first = static_cast<uint8_t>(value.value_first);
    out->second = static_cast<uint8_t>(value.value_second);
    return true;
#else
    (void)x;
    (void)y;
    return false;
#endif
}

bool m5u_cardputer_keyboard_get_state(m5u_cardputer_keyboard_state_t* out) {
    if (!out) { return false; }
    memset(out, 0, sizeof(*out));
#ifdef M5UNIFIED_RS_USE_REAL_M5CARDPUTER
    M5Cardputer.Keyboard.updateKeysState();
    auto& state = M5Cardputer.Keyboard.keysState();
    out->tab = state.tab;
    out->fn_key = state.fn;
    out->shift = state.shift;
    out->ctrl = state.ctrl;
    out->opt = state.opt;
    out->alt = state.alt;
    out->del = state.del;
    out->enter = state.enter;
    out->space = state.space;
    out->modifiers = state.modifiers;

    out->word_len = state.word.size();
    if (out->word_len > M5U_CARDPUTER_KEYBOARD_WORD_CAPACITY) {
        out->word_len = M5U_CARDPUTER_KEYBOARD_WORD_CAPACITY;
    }
    for (size_t i = 0; i < out->word_len; ++i) {
        out->word[i] = static_cast<uint8_t>(state.word[i]);
    }

    out->hid_len = state.hid_keys.size();
    if (out->hid_len > M5U_CARDPUTER_KEYBOARD_HID_CAPACITY) {
        out->hid_len = M5U_CARDPUTER_KEYBOARD_HID_CAPACITY;
    }
    for (size_t i = 0; i < out->hid_len; ++i) {
        out->hid_keys[i] = static_cast<uint8_t>(state.hid_keys[i]);
    }

    out->modifier_len = state.modifier_keys.size();
    if (out->modifier_len > M5U_CARDPUTER_KEYBOARD_MODIFIER_CAPACITY) {
        out->modifier_len = M5U_CARDPUTER_KEYBOARD_MODIFIER_CAPACITY;
    }
    for (size_t i = 0; i < out->modifier_len; ++i) {
        out->modifier_keys[i] = static_cast<uint8_t>(state.modifier_keys[i]);
    }
#endif
    return true;
}

bool m5u_cardputer_keyboard_capslocked(void) {
#ifdef M5UNIFIED_RS_USE_REAL_M5CARDPUTER
    return M5Cardputer.Keyboard.capslocked();
#else
    return false;
#endif
}

void m5u_cardputer_keyboard_set_capslocked(bool locked) {
#ifdef M5UNIFIED_RS_USE_REAL_M5CARDPUTER
    M5Cardputer.Keyboard.setCapsLocked(locked);
#else
    (void)locked;
#endif
}

bool m5u_cardputer_ir_begin(int pin) {
#ifdef M5UNIFIED_RS_USE_ARDUINO_IRREMOTE
    IrSender.begin(DISABLE_LED_FEEDBACK);
    IrSender.setSendPin(pin);
    return true;
#else
    (void)pin;
    return false;
#endif
}

bool m5u_cardputer_ir_send_nec(uint16_t address, uint8_t command, uint8_t repeats) {
#ifdef M5UNIFIED_RS_USE_ARDUINO_IRREMOTE
    IrSender.sendNEC(address, command, repeats);
    return true;
#else
    (void)address;
    (void)command;
    (void)repeats;
    return false;
#endif
}

bool m5u_cardputer_grove_i2c_begin(int sda, int scl, uint32_t frequency_hz) {
#ifdef M5UNIFIED_RS_USE_ARDUINO_WIRE
    return Wire.begin(sda, scl, frequency_hz);
#else
    (void)sda;
    (void)scl;
    (void)frequency_hz;
    return false;
#endif
}

void m5u_cardputer_grove_i2c_end(void) {
#ifdef M5UNIFIED_RS_USE_ARDUINO_WIRE
    Wire.end();
#endif
}

bool m5u_cardputer_grove_i2c_probe(uint8_t address) {
#ifdef M5UNIFIED_RS_USE_ARDUINO_WIRE
    Wire.beginTransmission(address);
    return Wire.endTransmission() == 0;
#else
    (void)address;
    return false;
#endif
}

bool m5u_cardputer_grove_i2c_write(uint8_t address, const uint8_t* data, size_t len) {
#ifdef M5UNIFIED_RS_USE_ARDUINO_WIRE
    Wire.beginTransmission(address);
    if (data && len) {
        Wire.write(data, len);
    }
    return Wire.endTransmission() == 0;
#else
    (void)address;
    (void)data;
    (void)len;
    return false;
#endif
}

size_t m5u_cardputer_grove_i2c_read(uint8_t address, uint8_t* data, size_t len) {
#ifdef M5UNIFIED_RS_USE_ARDUINO_WIRE
    if (!data || !len) { return 0; }
    size_t requested = Wire.requestFrom((int)address, (int)len);
    size_t read_len = 0;
    while (Wire.available() && read_len < requested && read_len < len) {
        data[read_len++] = static_cast<uint8_t>(Wire.read());
    }
    return read_len;
#else
    (void)address;
    (void)data;
    (void)len;
    return 0;
#endif
}

bool m5u_cardputer_grove_gpio_pin_mode(int pin, int mode) {
#ifdef M5UNIFIED_RS_USE_ARDUINO_GPIO
    switch (mode) {
    case 0: pinMode(pin, INPUT); return true;
    case 1: pinMode(pin, OUTPUT); return true;
    case 2: pinMode(pin, INPUT_PULLUP); return true;
    case 3: pinMode(pin, INPUT_PULLDOWN); return true;
    default: return false;
    }
#else
    (void)pin;
    (void)mode;
    return false;
#endif
}

bool m5u_cardputer_grove_gpio_write(int pin, bool high) {
#ifdef M5UNIFIED_RS_USE_ARDUINO_GPIO
    digitalWrite(pin, high ? HIGH : LOW);
    return true;
#else
    (void)pin;
    (void)high;
    return false;
#endif
}

int m5u_cardputer_grove_gpio_read(int pin) {
#ifdef M5UNIFIED_RS_USE_ARDUINO_GPIO
    return digitalRead(pin) == HIGH ? 1 : 0;
#else
    (void)pin;
    return -1;
#endif
}

bool m5u_cardputer_grove_uart_begin(int rx, int tx, uint32_t baud) {
#ifdef M5UNIFIED_RS_USE_ARDUINO_SERIAL
    Serial1.begin(baud, SERIAL_8N1, rx, tx);
    return true;
#else
    (void)rx;
    (void)tx;
    (void)baud;
    return false;
#endif
}

void m5u_cardputer_grove_uart_end(void) {
#ifdef M5UNIFIED_RS_USE_ARDUINO_SERIAL
    Serial1.end();
#endif
}

size_t m5u_cardputer_grove_uart_available(void) {
#ifdef M5UNIFIED_RS_USE_ARDUINO_SERIAL
    int available = Serial1.available();
    return available > 0 ? static_cast<size_t>(available) : 0;
#else
    return 0;
#endif
}

size_t m5u_cardputer_grove_uart_read(uint8_t* data, size_t len) {
#ifdef M5UNIFIED_RS_USE_ARDUINO_SERIAL
    if (!data || !len) { return 0; }
    size_t read_len = 0;
    while (read_len < len && Serial1.available() > 0) {
        int value = Serial1.read();
        if (value < 0) { break; }
        data[read_len++] = static_cast<uint8_t>(value);
    }
    return read_len;
#else
    (void)data;
    (void)len;
    return 0;
#endif
}

size_t m5u_cardputer_grove_uart_write(const uint8_t* data, size_t len) {
#ifdef M5UNIFIED_RS_USE_ARDUINO_SERIAL
    if (!data || !len) { return 0; }
    return Serial1.write(data, len);
#else
    (void)data;
    (void)len;
    return 0;
#endif
}

void m5u_cardputer_grove_uart_flush(void) {
#ifdef M5UNIFIED_RS_USE_ARDUINO_SERIAL
    Serial1.flush();
#endif
}

// ─── NVS helpers ────────────────────────────────────────────────────────────

#include "nvs_flash.h"
#include "nvs.h"

bool m5u_nvs_read_i32(const char* ns, const char* key, int32_t* out_val) {
    nvs_handle_t handle;
    if (nvs_open(ns, NVS_READONLY, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_get_i32(handle, key, out_val);
    nvs_close(handle);
    return err == ESP_OK;
}

bool m5u_nvs_write_i32(const char* ns, const char* key, int32_t val) {
    nvs_handle_t handle;
    if (nvs_open(ns, NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_i32(handle, key, val);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

} // extern "C"
