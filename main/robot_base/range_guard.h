// VL53L0X forward/down laser guard. One sensor, mounted at the front looking diagonally
// at the floor: a shorter reading than the calibrated floor distance = obstacle,
// a longer reading (or no echo) = drop / table edge.
#pragma once
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <functional>
#include <memory>

#include "robot_logic.h"
#include "vl53l0x.h"

class RangeGuard {
public:
    struct Config {
        gpio_num_t int_pin = GPIO_NUM_NC;   // VL53L0X GPIO1 (active low, "new sample ready")
        uint16_t cliff_mm = 150;
        uint16_t obstacle_margin_mm = 30;
        uint8_t i2c_addr = 0x29;
    };
    using HazardCallback = std::function<void(robot::Hazard)>;

    // Shares an existing i2c_master bus (the OLED bus on the OLED build).
    bool Init(i2c_master_bus_handle_t bus, const Config& cfg);
    // Re-measure the floor distance. The robot must stand on flat floor. Blocks up to ~3 s.
    bool Calibrate();

    robot::Hazard hazard() const { return hazard_.load(); }
    uint16_t distance_mm() const { return last_mm_.load(); }
    uint16_t floor_mm() const { return floor_mm_.load(); }
    bool present() const { return present_; }
    // true when samples are fresh (< 300 ms old) and there is no I2C fault
    bool healthy() const;
    bool calibration_active() const { return calibrating_.load(); }
    // called from the sensor task on every hazard change: keep it short
    void SetHazardCallback(HazardCallback cb) { cb_ = std::move(cb); }

private:
    static void TaskEntry(void* arg);
    void Loop();
    static void IRAM_ATTR IsrHandler(void* arg);

    Config cfg_{};
    VL53L0X tof_;
    bool present_ = false;
    robot::HazardClassifier cls_;
    std::atomic<robot::Hazard> hazard_{robot::Hazard::kUnknown};
    std::atomic<uint16_t> last_mm_{0};
    std::atomic<uint16_t> floor_mm_{0};
    std::atomic<int64_t> last_ok_us_{0};
    std::atomic<bool> calib_req_{false};
    std::atomic<bool> calib_ok_{false};
    std::atomic<bool> calibrating_{false};
    EventGroupHandle_t events_ = nullptr;
    SemaphoreHandle_t drdy_ = nullptr;
    HazardCallback cb_;
    TaskHandle_t task_ = nullptr;
};
