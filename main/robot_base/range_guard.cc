#include "range_guard.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <string.h>

#define TAG "RobotRange"
#define EV_CALIB_DONE BIT0
#define CAL_SAMPLES 15

void IRAM_ATTR RangeGuard::IsrHandler(void* arg) {
    auto* self = static_cast<RangeGuard*>(arg);
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(self->drdy_, &woken);
    if (woken) portYIELD_FROM_ISR();
}

bool RangeGuard::Init(i2c_master_bus_handle_t bus, const Config& cfg) {
    cfg_ = cfg;
    if (bus == nullptr) {
        ESP_LOGE(TAG, "no I2C bus");
        return false;
    }
    if (!tof_.attach(bus, cfg_.i2c_addr, 400000)) {
        ESP_LOGE(TAG, "cannot add I2C device 0x%02x", cfg_.i2c_addr);
        return false;
    }
    tof_.setTimeout(100);
    if (!tof_.init(true)) {
        ESP_LOGE(TAG, "VL53L0X init failed (wiring? address 0x%02x)", cfg_.i2c_addr);
        return false;
    }
    // Fast ranging: 20 ms budget, back-to-back continuous mode, interrupt on every sample.
    if (!tof_.setMeasurementTimingBudget(20000)) {
        ESP_LOGE(TAG, "cannot configure VL53L0X measurement timing");
        return false;
    }
    tof_.startContinuous(0);
    tof_.setTimeout(30);
    present_ = true;

    robot::HazardConfig hc;
    hc.cliff_mm = cfg_.cliff_mm;
    hc.obstacle_margin_mm = cfg_.obstacle_margin_mm;
    cls_.Configure(hc);
    cls_.SetFloor(0);

    events_ = xEventGroupCreate();
    drdy_ = xSemaphoreCreateBinary();
    if (!events_ || !drdy_) {
        ESP_LOGE(TAG, "cannot create range task synchronization objects");
        if (events_) {
            vEventGroupDelete(events_);
            events_ = nullptr;
        }
        if (drdy_) {
            vSemaphoreDelete(drdy_);
            drdy_ = nullptr;
        }
        present_ = false;
        return false;
    }

    if (cfg_.int_pin != GPIO_NUM_NC) {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << cfg_.int_pin;
        io.mode = GPIO_MODE_INPUT;
        io.pull_up_en = GPIO_PULLUP_ENABLE;
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io.intr_type = GPIO_INTR_NEGEDGE;
        esp_err_t gpio_err = gpio_config(&io);
        if (gpio_err != ESP_OK) {
            ESP_LOGW(TAG, "cannot configure data-ready GPIO%d: %s; polling instead",
                     (int)cfg_.int_pin, esp_err_to_name(gpio_err));
            cfg_.int_pin = GPIO_NUM_NC;
        }
    }
    if (cfg_.int_pin != GPIO_NUM_NC) {
        esp_err_t e = gpio_install_isr_service(0);   // ESP_ERR_INVALID_STATE = already installed, fine
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "isr service: %s; polling instead", esp_err_to_name(e));
            cfg_.int_pin = GPIO_NUM_NC;
        } else {
            e = gpio_isr_handler_add(cfg_.int_pin, IsrHandler, this);
            if (e != ESP_OK) {
                ESP_LOGW(TAG, "cannot attach data-ready interrupt: %s; polling instead", esp_err_to_name(e));
                cfg_.int_pin = GPIO_NUM_NC;
            }
        }
    }

    last_ok_us_.store(esp_timer_get_time());
    calib_req_.store(true);   // calibrate the floor automatically at boot (robot must stand on the floor)
    calibrating_.store(true);
    BaseType_t ok = xTaskCreate(TaskEntry, "robot_range", 4096, this, 6, &task_);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "cannot create range task");
        present_ = false;
        if (cfg_.int_pin != GPIO_NUM_NC) gpio_isr_handler_remove(cfg_.int_pin);
        vEventGroupDelete(events_);
        vSemaphoreDelete(drdy_);
        events_ = nullptr;
        drdy_ = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "VL53L0X ready (INT=GPIO%d), cliff > %u mm", (int)cfg_.int_pin, cfg_.cliff_mm);
    return true;
}

bool RangeGuard::healthy() const {
    if (!present_) return false;
    int64_t age_ms = (esp_timer_get_time() - last_ok_us_.load()) / 1000;
    return age_ms < 300 && hazard_.load() != robot::Hazard::kSensorFault;
}

bool RangeGuard::Calibrate() {
    if (!present_ || !events_ || calibrating_.exchange(true)) return false;
    xEventGroupClearBits(events_, EV_CALIB_DONE);
    calib_ok_.store(false);
    calib_req_.store(true);
    EventBits_t b = xEventGroupWaitBits(events_, EV_CALIB_DONE, pdTRUE, pdFALSE, pdMS_TO_TICKS(3000));
    bool ok = (b & EV_CALIB_DONE) && calib_ok_.load();
    calibrating_.store(false);
    return ok;
}

void RangeGuard::TaskEntry(void* arg) {
    static_cast<RangeGuard*>(arg)->Loop();
    vTaskDelete(nullptr);
}

void RangeGuard::Loop() {
    uint16_t cal[CAL_SAMPLES];
    size_t cal_n = 0;
    bool calibrating = false;
    robot::Hazard prev = robot::Hazard::kUnknown;
    uint32_t warned = 0;

    for (;;) {
        // wait for the data-ready interrupt; fall back to polling if INT is not wired
        bool got_int = xSemaphoreTake(drdy_, pdMS_TO_TICKS(40)) == pdTRUE;
        if (!got_int && cfg_.int_pin != GPIO_NUM_NC && (warned++ % 100) == 0) {
            ESP_LOGW(TAG, "no data-ready interrupt on GPIO%d, polling instead", (int)cfg_.int_pin);
        }

        uint16_t mm = tof_.readRangeContinuousMillimeters();
        bool io_ok = !tof_.timeoutOccurred() && tof_.last_status == 0;
        if (io_ok) last_ok_us_.store(esp_timer_get_time());
        last_mm_.store(io_ok ? mm : 0);

        if (calib_req_.exchange(false)) {
            calibrating = true;
            calibrating_.store(true);
            cal_n = 0;
        }
        if (calibrating) {
            if (io_ok && cal_n < CAL_SAMPLES) cal[cal_n++] = mm;
            if (cal_n >= CAL_SAMPLES) {
                uint16_t floor = 0;
                bool ok = robot::ComputeFloor(cal, cal_n, cfg_.cliff_mm, &floor);
                if (ok) {
                    floor_mm_.store(floor);
                    cls_.SetFloor(floor);
                    cls_.Reset();
                    hazard_.store(robot::Hazard::kUnknown);
                    ESP_LOGI(TAG, "floor calibrated: %u mm (cliff > %u, obstacle < %u)", floor, cfg_.cliff_mm,
                             floor > cfg_.obstacle_margin_mm ? floor - cfg_.obstacle_margin_mm : 0);
                } else {
                    ESP_LOGW(TAG, "floor calibration failed (noisy, too close to cliff, or not on the floor); "
                                  "keeping previous calibration");
                }
                calib_ok_.store(ok);
                calibrating = false;
                calibrating_.store(false);
                xEventGroupSetBits(events_, EV_CALIB_DONE);
            }
        }

        robot::Hazard h = cls_.Feed(mm, io_ok);
        hazard_.store(h);
        if (h != prev) {
            ESP_LOGI(TAG, "hazard %s -> %s (range %u mm)", robot::HazardName(prev), robot::HazardName(h), mm);
            prev = h;
            if (cb_) cb_(h);
        }
        if (!got_int) vTaskDelay(pdMS_TO_TICKS(5));   // never hog the CPU when polling
    }
}
