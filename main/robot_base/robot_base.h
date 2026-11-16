// Robot base for xiaozhi: 2 DC wheels (DRV8833) + VL53L0X laser guard + SG90 head servo.
// Works with every display type (OLED Mochi face / TFT robot eyes) because it follows the
// emotion string xiaozhi already sends to the display.
#pragma once
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <memory>
#include <atomic>

#include "device_state.h"
#include "head_servo.h"
#include "bmi160_motion.h"
#include "motor_driver.h"
#include "range_guard.h"
#include "robot_controller.h"

class RobotBase {
public:
    static RobotBase& GetInstance();

    // Creates the I2C bus on CONFIG_ROBOT_I2C_SDA/SCL (LCD build, which has no OLED bus).
    static i2c_master_bus_handle_t CreateI2cBus();

    // Call once from the board constructor. `i2c_bus` is shared with the OLED on the OLED build.
    bool Init(i2c_master_bus_handle_t i2c_bus);

    // follows xiaozhi emotions: "happy", "sad", "angry", "sleepy", ..., "neutral"
    void OnEmotion(const char* emotion);
    void OnDeviceState(DeviceState state);

private:
    RobotBase() = default;
    static void TaskEntry(void* arg);
    void Loop();
    void RegisterTools();
    void SetFace(const char* emotion);
    void OnMotionEvent(robot::MotionEvent event);
    bool SensorsReady() const;
    bool MotionHeld() const;
    bool LocomotionAllowed() const;

    MotorDriver motors_;
    HeadServo head_;
    RangeGuard guard_;
    Bmi160Motion motion_;
    std::unique_ptr<robot::RobotController> ctl_;
    SemaphoreHandle_t mu_ = nullptr;
    bool initialized_ = false;
    // Short, explicit diagnostic override for verifying motor wiring and PWM.
    std::atomic<int> motor_test_left_{0};
    std::atomic<int> motor_test_right_{0};
    std::atomic<int64_t> motor_test_until_ms_{0};
    bool stop_when_listening_ = true;
    std::atomic<bool> calibrating_floor_{false};
    DeviceState last_device_state_ = kDeviceStateUnknown;
    float cur_l_ = 0, cur_r_ = 0, cur_head_ = 0;
    int64_t perform_end_ms_ = 0;
};
