#ifndef CW2017_BATTERY_H_
#define CW2017_BATTERY_H_

#include <driver/i2c_master.h>
#include <esp_err.h>
#include <cstdint>

// CW2017 fuel gauge driver sharing the board I2C0 bus (0x63), with the exact
// Wanli 500mAh battery profile from FoloToy/ai-passport bsp_battery.c.
class Cw2017Battery {
public:
    Cw2017Battery() = default;
    ~Cw2017Battery();

    esp_err_t Initialize(i2c_master_bus_handle_t bus, uint8_t addr = 0x63);
    int ReadSoc() const;        // 0..100, or -1 if unavailable
    int ReadVoltageMv() const;  // mV, or -1 if unavailable
    bool IsPresent() const { return dev_ != nullptr; }

private:
    esp_err_t ReadReg(uint8_t reg, uint8_t* buf, size_t len) const;
    esp_err_t WriteReg(uint8_t reg, uint8_t val) const;
    bool EnterSleep() const;
    bool EnterActive() const;
    bool ProfileMatches(bool* matches) const;
    bool UpdateProfile() const;

    i2c_master_dev_handle_t dev_ = nullptr;
};

#endif  // CW2017_BATTERY_H_
