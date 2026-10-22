#pragma once

#include "board.h"

class SensorI2cLock {
public:
    explicit SensorI2cLock(uint32_t timeout_ms = 1000)
        : result_(board_sensor_i2c_lock(timeout_ms)), locked_(result_ == ESP_OK) {}

    ~SensorI2cLock()
    {
        if (locked_) {
            board_sensor_i2c_unlock();
        }
    }

    SensorI2cLock(const SensorI2cLock &) = delete;
    SensorI2cLock &operator=(const SensorI2cLock &) = delete;

    esp_err_t result() const { return result_; }

private:
    esp_err_t result_;
    bool locked_;
};
