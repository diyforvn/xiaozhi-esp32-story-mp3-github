#pragma once

#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "robot_logic.h"

class Bmi160Motion {
public:
    struct Config {
        uint8_t address = 0x68;
        uint8_t forward_axis = 0;  // 0 = sensor X, 1 = sensor Y
        int8_t forward_sign = 1;
        float lift_tilt_deg = 30;
        float release_impact_g = 1.2f;
        float release_delta_g = 0.35f;
    };
    using EventCallback = std::function<void(robot::MotionEvent)>;

    bool Init(i2c_master_bus_handle_t bus, const Config& cfg, EventCallback callback);
    bool Calibrate();
    void SetRobotStationary(bool stationary) { robot_stationary_.store(stationary); }
    void SetRobotOnCalibratedFloor(bool on_floor) { robot_on_calibrated_floor_.store(on_floor); }

    bool present() const { return present_.load(); }
    bool healthy() const;
    bool calibrating() const { return calibrating_.load(); }
    robot::MotionSnapshot snapshot() const;

private:
    static void TaskEntry(void* arg);
    void Loop();
    bool ReadRegister(uint8_t reg, uint8_t* data, size_t size);
    bool WriteRegister(uint8_t reg, uint8_t value);
    bool ReadAcceleration(robot::Acceleration& sample);
    void Publish(const robot::MotionSnapshot& snapshot);

    Config cfg_{};
    i2c_master_dev_handle_t device_ = nullptr;
    EventGroupHandle_t events_ = nullptr;
    TaskHandle_t task_ = nullptr;
    robot::MotionClassifier classifier_;
    EventCallback callback_;
    std::atomic<bool> present_{false};
    std::atomic<bool> calibrating_{false};
    std::atomic<bool> calibration_requested_{false};
    std::atomic<bool> calibration_success_{false};
    std::atomic<bool> robot_stationary_{false};
    std::atomic<bool> robot_on_calibrated_floor_{false};
    std::atomic<int64_t> last_sample_us_{0};
    std::atomic<int16_t> pitch_tenths_{0};
    std::atomic<int16_t> roll_tenths_{0};
    std::atomic<robot::MotionState> state_{robot::MotionState::kUncalibrated};
};
