#include "bmi160_motion.h"

#include <esp_log.h>
#include <esp_timer.h>

#include <math.h>
#include <utility>

#define TAG "RobotIMU"
#define EV_CALIB_DONE BIT0
#define BMI160_CHIP_ID 0xD1
#define CALIBRATION_SAMPLES 40
#define SAMPLE_PERIOD_MS 20
#define I2C_TIMEOUT_MS 100

bool Bmi160Motion::Init(i2c_master_bus_handle_t bus, const Config& cfg, EventCallback callback) {
    cfg_ = cfg;
    callback_ = std::move(callback);
    if (!bus) {
        ESP_LOGE(TAG, "no I2C bus");
        return false;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = cfg_.address,
        .scl_speed_hz = 400000,
        .scl_wait_us = 0,
        .flags = {.disable_ack_check = 0},
    };
    // BMI160 boards use either SA0 address. Try the configured address first,
    // then the alternate address so a menuconfig mismatch does not disable IMU.
    const uint8_t addresses[] = {cfg_.address, (uint8_t)(cfg_.address == 0x68 ? 0x69 : 0x68)};
    bool found = false;
    for (uint8_t address : addresses) {
        dev_cfg.device_address = address;
        esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &device_);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "cannot add BMI160 candidate at 0x%02x: %s", address, esp_err_to_name(err));
            continue;
        }

        uint8_t chip_id = 0;
        if (ReadRegister(0x00, &chip_id, 1) && chip_id == BMI160_CHIP_ID) {
            cfg_.address = address;
            found = true;
            break;
        }
        ESP_LOGW(TAG, "no BMI160 at 0x%02x (chip id 0x%02x); trying alternate address", address, chip_id);
        i2c_master_bus_rm_device(device_);
        device_ = nullptr;
    }
    if (!found) {
        ESP_LOGE(TAG, "BMI160 not found at either I2C address 0x%02x or 0x%02x",
                 addresses[0], addresses[1]);
        return false;
    }

    // Configure the accelerometer for 100 Hz, normal bandwidth, and +/-2 g.
    if (!WriteRegister(0x40, 0x28) || !WriteRegister(0x41, 0x03) ||
        !WriteRegister(0x7E, 0x11)) {
        ESP_LOGE(TAG, "BMI160 accelerometer configuration failed");
        i2c_master_bus_rm_device(device_);
        device_ = nullptr;
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    events_ = xEventGroupCreate();
    if (!events_) {
        ESP_LOGE(TAG, "cannot create calibration event group");
        i2c_master_bus_rm_device(device_);
        device_ = nullptr;
        return false;
    }

    calibration_requested_.store(true);
    calibration_success_.store(false);
    calibrating_.store(true);
    BaseType_t task_ok = xTaskCreate(TaskEntry, "robot_imu", 4096, this, 4, &task_);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "cannot create BMI160 task");
        calibrating_.store(false);
        vEventGroupDelete(events_);
        events_ = nullptr;
        i2c_master_bus_rm_device(device_);
        device_ = nullptr;
        return false;
    }
    present_.store(true);
    ESP_LOGI(TAG, "BMI160 ready at 0x%02x; calibrating stationary orientation", cfg_.address);
    return true;
}

bool Bmi160Motion::ReadRegister(uint8_t reg, uint8_t* data, size_t size) {
    return device_ && data &&
           i2c_master_transmit_receive(device_, &reg, 1, data, size, I2C_TIMEOUT_MS) == ESP_OK;
}

bool Bmi160Motion::WriteRegister(uint8_t reg, uint8_t value) {
    if (!device_) return false;
    uint8_t data[] = {reg, value};
    return i2c_master_transmit(device_, data, sizeof(data), I2C_TIMEOUT_MS) == ESP_OK;
}

bool Bmi160Motion::ReadAcceleration(robot::Acceleration& sample) {
    uint8_t data[6];
    if (!ReadRegister(0x12, data, sizeof(data))) return false;
    auto axis = [](uint8_t lo, uint8_t hi) {
        return static_cast<int16_t>((uint16_t)lo | ((uint16_t)hi << 8));
    };
    constexpr float kTwoGPerLsb = 2.0f / 32768.0f;
    sample.x = axis(data[0], data[1]) * kTwoGPerLsb;
    sample.y = axis(data[2], data[3]) * kTwoGPerLsb;
    sample.z = axis(data[4], data[5]) * kTwoGPerLsb;
    return true;
}

bool Bmi160Motion::Calibrate() {
    if (!present_.load() || !events_ || calibrating_.exchange(true)) return false;
    xEventGroupClearBits(events_, EV_CALIB_DONE);
    calibration_success_.store(false);
    calibration_requested_.store(true);
    EventBits_t bits = xEventGroupWaitBits(events_, EV_CALIB_DONE, pdTRUE, pdFALSE, pdMS_TO_TICKS(3000));
    return (bits & EV_CALIB_DONE) != 0 && calibration_success_.load();
}

bool Bmi160Motion::healthy() const {
    if (!present_.load()) return false;
    return (esp_timer_get_time() - last_sample_us_.load()) / 1000 < 500;
}

robot::MotionSnapshot Bmi160Motion::snapshot() const {
    robot::MotionSnapshot out;
    out.state = state_.load();
    out.pitch_deg = pitch_tenths_.load() / 10.0f;
    out.roll_deg = roll_tenths_.load() / 10.0f;
    return out;
}

void Bmi160Motion::Publish(const robot::MotionSnapshot& snapshot) {
    state_.store(snapshot.state);
    pitch_tenths_.store((int16_t)lroundf(snapshot.pitch_deg * 10.0f));
    roll_tenths_.store((int16_t)lroundf(snapshot.roll_deg * 10.0f));
    if (snapshot.event != robot::MotionEvent::kNone && callback_) callback_(snapshot.event);
}

void Bmi160Motion::TaskEntry(void* arg) {
    static_cast<Bmi160Motion*>(arg)->Loop();
    vTaskDelete(nullptr);
}

void Bmi160Motion::Loop() {
    TickType_t last = xTaskGetTickCount();
    robot::Acceleration calibration[CALIBRATION_SAMPLES];
    size_t calibration_count = 0;
    bool collecting = false;
    uint32_t calibration_elapsed_ms = 0;
    uint32_t errors = 0;

    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
        if (calibration_requested_.exchange(false)) {
            collecting = true;
            calibration_count = 0;
            calibration_elapsed_ms = 0;
        }

        if (collecting) calibration_elapsed_ms += SAMPLE_PERIOD_MS;
        robot::Acceleration sample;
        if (!ReadAcceleration(sample)) {
            errors++;
            if (errors == 1 || errors % 50 == 0) ESP_LOGW(TAG, "BMI160 read failed (%lu)", (unsigned long)errors);
            if (collecting && calibration_elapsed_ms >= 2500) {
                ESP_LOGE(TAG, "BMI160 calibration timed out while reading samples");
                collecting = false;
                calibrating_.store(false);
                calibration_success_.store(false);
                xEventGroupSetBits(events_, EV_CALIB_DONE);
            }
            continue;
        }
        errors = 0;
        last_sample_us_.store(esp_timer_get_time());

        if (collecting) {
            calibration[calibration_count++] = sample;
            if (calibration_count == CALIBRATION_SAMPLES) {
                bool ok = classifier_.Calibrate(calibration, calibration_count, cfg_.forward_axis,
                                                 cfg_.forward_sign, cfg_.lift_tilt_deg,
                                                 cfg_.release_impact_g, cfg_.release_delta_g);
                calibrating_.store(false);
                calibration_success_.store(ok);
                if (ok) {
                    ESP_LOGI(TAG, "BMI160 orientation calibrated");
                    Publish(robot::MotionSnapshot{robot::MotionState::kResting});
                } else {
                    ESP_LOGE(TAG, "BMI160 calibration failed; keep robot still and level, then retry");
                    state_.store(classifier_.state());
                }
                collecting = false;
                xEventGroupSetBits(events_, EV_CALIB_DONE);
            }
            continue;
        }

        robot::MotionSnapshot current =
            classifier_.Feed(sample, SAMPLE_PERIOD_MS, robot_stationary_.load(),
                              robot_on_calibrated_floor_.load());
        Publish(current);
    }
}
