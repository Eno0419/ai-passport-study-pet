#include "cw2017_battery.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Cw2017Battery"

namespace {

constexpr uint8_t kRegVersion = 0x00;
constexpr uint8_t kRegVcellH = 0x02;
constexpr uint8_t kRegSocH = 0x04;
constexpr uint8_t kRegConfig = 0x08;
constexpr uint8_t kRegSocAlert = 0x0B;
constexpr uint8_t kRegProfile = 0x10;

constexpr uint8_t kCfgActive = 0x00;
constexpr uint8_t kCfgRestart = 0x30;
constexpr uint8_t kCfgSleep = 0xF0;
constexpr uint8_t kUpdateFlag = 0x80;
constexpr size_t kProfileSize = 80;

// Wanli New Energy 500 mAh, profile2 / XIL40X / 2026-10-20
static const uint8_t kBatteryProfile[kProfileSize] = {
    0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAF, 0xCA, 0xCA, 0xC0, 0xB4, 0xA8, 0xCB, 0xAA,
    0x9F, 0xCF, 0xB5, 0x8A, 0x70, 0x56, 0x4B, 0x40,
    0x34, 0x2B, 0x22, 0x55, 0x41, 0xD2, 0x30, 0xD7,
    0xC9, 0xBE, 0xAE, 0xC8, 0xAE, 0x97, 0x9D, 0x98,
    0x94, 0x9F, 0xB6, 0x89, 0x73, 0x65, 0x57, 0x49,
    0x3D, 0x50, 0x74, 0x87, 0xA3, 0x9B, 0x5E, 0x38,
    0x00, 0x00, 0xAB, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB7,
};

}  // namespace

Cw2017Battery::~Cw2017Battery() {
    if (dev_) {
        i2c_master_bus_rm_device(dev_);
        dev_ = nullptr;
    }
}

esp_err_t Cw2017Battery::ReadReg(uint8_t reg, uint8_t* buf, size_t len) const {
    if (!dev_) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(dev_, &reg, 1, buf, len, 100);
}

esp_err_t Cw2017Battery::WriteReg(uint8_t reg, uint8_t val) const {
    if (!dev_) return ESP_ERR_INVALID_STATE;
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(dev_, b, 2, 100);
}

bool Cw2017Battery::EnterSleep() const {
    if (WriteReg(kRegConfig, kCfgRestart) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (WriteReg(kRegConfig, kCfgSleep) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
    return true;
}

bool Cw2017Battery::EnterActive() const {
    if (WriteReg(kRegConfig, kCfgRestart) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (WriteReg(kRegConfig, kCfgActive) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
    return true;
}

bool Cw2017Battery::ProfileMatches(bool* matches) const {
    *matches = false;
    uint8_t val = 0;
    if (ReadReg(kRegSocAlert, &val, 1) != ESP_OK) return false;
    if ((val & kUpdateFlag) == 0) return true;
    for (size_t i = 0; i < kProfileSize; ++i) {
        if (ReadReg(static_cast<uint8_t>(kRegProfile + i), &val, 1) != ESP_OK) return false;
        if (val != kBatteryProfile[i]) return true;
    }
    *matches = true;
    return true;
}

bool Cw2017Battery::UpdateProfile() const {
    if (!EnterSleep()) return false;
    for (size_t i = 0; i < kProfileSize; ++i) {
        if (WriteReg(static_cast<uint8_t>(kRegProfile + i), kBatteryProfile[i]) != ESP_OK) {
            return false;
        }
    }
    uint8_t val = 0;
    for (size_t i = 0; i < kProfileSize; ++i) {
        if (ReadReg(static_cast<uint8_t>(kRegProfile + i), &val, 1) != ESP_OK ||
            val != kBatteryProfile[i]) {
            return false;
        }
    }
    if (ReadReg(kRegSocAlert, &val, 1) != ESP_OK) return false;
    if (WriteReg(kRegSocAlert, static_cast<uint8_t>(val | kUpdateFlag)) != ESP_OK) return false;
    return EnterActive();
}

esp_err_t Cw2017Battery::Initialize(i2c_master_bus_handle_t bus, uint8_t addr) {
    if (dev_) return ESP_OK;
    if (!bus) return ESP_ERR_INVALID_ARG;

    i2c_device_config_t dc = {};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = addr;
    dc.scl_speed_hz = 100000;
    esp_err_t err = i2c_master_bus_add_device(bus, &dc, &dev_);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t ver = 0;
    if (ReadReg(kRegVersion, &ver, 1) != ESP_OK) {
        ESP_LOGW(TAG, "CW2017 not detected at 0x%02X (continuing without fuel gauge)", addr);
        i2c_master_bus_rm_device(dev_);
        dev_ = nullptr;
        return ESP_ERR_NOT_FOUND;
    }

    bool matches = false;
    if (ProfileMatches(&matches)) {
        if (!matches) {
            ESP_LOGI(TAG, "Writing Wanli 500mAh CW2017 profile...");
            (void)UpdateProfile();
        } else {
            uint8_t cfg = 0;
            if (ReadReg(kRegConfig, &cfg, 1) == ESP_OK && cfg != kCfgActive) {
                (void)EnterActive();
            }
        }
    }

    ESP_LOGI(TAG, "CW2017 detected (ver=0x%02X, SOC=%d%%, V=%dmV)",
             ver, ReadSoc(), ReadVoltageMv());
    return ESP_OK;
}

int Cw2017Battery::ReadSoc() const {
    uint8_t b[2] = {};
    if (ReadReg(kRegSocH, b, 2) != ESP_OK) return -1;
    if (b[0] > 100) return -1;
    return static_cast<int>(b[0]);
}

int Cw2017Battery::ReadVoltageMv() const {
    uint8_t b[2] = {};
    if (ReadReg(kRegVcellH, b, 2) != ESP_OK) return -1;
    uint32_t raw = ((static_cast<uint32_t>(b[0]) << 8) | b[1]) & 0x3FFFu;
    return static_cast<int>((raw * 3125u) / 10000u);
}
