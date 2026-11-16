#include "head_servo.h"

#include <esp_log.h>

#define TAG "RobotHead"
#define SERVO_TIMER LEDC_TIMER_3
#define SERVO_CH    LEDC_CHANNEL_6
#define SERVO_RES   LEDC_TIMER_14_BIT
#define SERVO_HZ    50

bool HeadServo::Init(const Config& cfg) {
    cfg_ = cfg;
    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.duty_resolution = SERVO_RES;
    t.timer_num = SERVO_TIMER;
    t.freq_hz = SERVO_HZ;
    t.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&t) != ESP_OK) {
        ESP_LOGE(TAG, "ledc timer config failed");
        return false;
    }
    ledc_channel_config_t c = {};
    c.gpio_num = cfg_.pin;
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.channel = SERVO_CH;
    c.intr_type = LEDC_INTR_DISABLE;
    c.timer_sel = SERVO_TIMER;
    c.duty = 0;
    c.hpoint = 0;
    if (ledc_channel_config(&c) != ESP_OK) {
        ESP_LOGE(TAG, "ledc channel config failed");
        return false;
    }
    ready_ = true;
    SetPose(0);
    ESP_LOGI(TAG, "servo ready on GPIO%d, center %d deg, range +-%d deg", (int)cfg_.pin, cfg_.center_deg,
             cfg_.range_deg);
    return true;
}

void HeadServo::SetAngleDeg(float deg) {
    if (!ready_) return;
    if (deg < 0) deg = 0;
    if (deg > 180) deg = 180;
    angle_ = deg;
    float us = cfg_.min_us + (cfg_.max_us - cfg_.min_us) * (deg / 180.0f);
    uint32_t max_duty = (1u << SERVO_RES) - 1;
    uint32_t duty = (uint32_t)(us * SERVO_HZ * (float)(1u << SERVO_RES) / 1000000.0f);
    if (duty > max_duty) duty = max_duty;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, SERVO_CH, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, SERVO_CH);
}

void HeadServo::SetPose(int pose) {
    if (pose > 100) pose = 100;
    if (pose < -100) pose = -100;
    pose_ = pose;
    float sign = cfg_.invert ? -1.0f : 1.0f;
    SetAngleDeg((float)cfg_.center_deg + sign * (float)cfg_.range_deg * (float)pose / 100.0f);
}
