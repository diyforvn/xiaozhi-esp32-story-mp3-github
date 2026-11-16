#include "motor_driver.h"

#include <esp_log.h>
#include <esp_timer.h>

#include "robot_logic.h"

#define TAG "RobotMotor"

// LEDC resources used by the robot base (backlight uses timer0/ch0, GpioLed timer1/ch0):
#define MOTOR_TIMER   LEDC_TIMER_2
#define MOTOR_RES     LEDC_TIMER_10_BIT
#define CH_L_IN1      LEDC_CHANNEL_2
#define CH_L_IN2      LEDC_CHANNEL_3
#define CH_R_IN1      LEDC_CHANNEL_4
#define CH_R_IN2      LEDC_CHANNEL_5

bool MotorDriver::Init(const Config& cfg) {
    cfg_ = cfg;
    mu_ = xSemaphoreCreateMutex();
    if (!mu_) return false;

    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.duty_resolution = MOTOR_RES;
    t.timer_num = MOTOR_TIMER;
    t.freq_hz = cfg_.pwm_hz;
    t.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&t) != ESP_OK) {
        ESP_LOGE(TAG, "ledc timer config failed");
        return false;
    }
    max_duty_ = 1u << MOTOR_RES;

    struct Map { ledc_channel_t ch; gpio_num_t pin; };
    const Map maps[] = {
        {CH_L_IN1, cfg_.left_in1}, {CH_L_IN2, cfg_.left_in2},
        {CH_R_IN1, cfg_.right_in1}, {CH_R_IN2, cfg_.right_in2},
    };
    for (const auto& m : maps) {
        ledc_channel_config_t c = {};
        c.gpio_num = m.pin;
        c.speed_mode = LEDC_LOW_SPEED_MODE;
        c.channel = m.ch;
        c.intr_type = LEDC_INTR_DISABLE;
        c.timer_sel = MOTOR_TIMER;
        c.duty = 0;
        c.hpoint = 0;
        if (ledc_channel_config(&c) != ESP_OK) {
            ESP_LOGE(TAG, "ledc channel %d (gpio %d) failed", (int)m.ch, (int)m.pin);
            return false;
        }
    }
    ready_ = true;
    Coast();
    ESP_LOGI(TAG, "DRV8833 ready: L=%d/%d R=%d/%d pwm=%luHz duty %d..%d%%", (int)cfg_.left_in1,
             (int)cfg_.left_in2, (int)cfg_.right_in1, (int)cfg_.right_in2, (unsigned long)cfg_.pwm_hz,
             cfg_.min_start_pct, cfg_.max_pct);
    return true;
}

void MotorDriver::SetDuty(ledc_channel_t ch, uint32_t duty) {
    if (duty > max_duty_) duty = max_duty_;
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, duty);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC set duty failed: channel=%d duty=%lu: %s", (int)ch,
                 (unsigned long)duty, esp_err_to_name(err));
        return;
    }
    err = ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC update duty failed: channel=%d duty=%lu: %s", (int)ch,
                 (unsigned long)duty, esp_err_to_name(err));
    }
}

void MotorDriver::SetSide(ledc_channel_t a, ledc_channel_t b, int pct, bool invert) {
    if (invert) pct = -pct;
    if (pct == 0) {                      // brake: both inputs high (fast stop)
        SetDuty(a, max_duty_);
        SetDuty(b, max_duty_);
        return;
    }
    uint32_t duty = (uint32_t)((pct < 0 ? -pct : pct) * max_duty_ / 100);
    if (pct > 0) {
        SetDuty(a, duty);
        SetDuty(b, 0);
    } else {
        SetDuty(a, 0);
        SetDuty(b, duty);
    }
}

void MotorDriver::ApplyLocked(int l, int r) {
    const int requested_l = l;
    const int requested_r = r;
    if (motion_inhibit_.load()) {
        l = 0;
        r = 0;
    }
    if (inhibit_.load()) robot::RemoveNetForward(l, r);
    int dl = robot::ToDutyPercent(l, cfg_.min_start_pct, cfg_.max_pct);
    int dr = robot::ToDutyPercent(r, cfg_.min_start_pct, cfg_.max_pct);
    const int64_t now_us = esp_timer_get_time();
    if ((requested_l != 0 || requested_r != 0) && now_us - last_drive_log_us_ >= 500000) {
        ESP_LOGI(TAG, "drive request L=%d R=%d -> applied L=%d R=%d duty L=%d%% R=%d%% motion_lock=%d forward_lock=%d",
                 requested_l, requested_r, l, r, dl, dr,
                 motion_inhibit_.load(), inhibit_.load());
        last_drive_log_us_ = now_us;
    }
    SetSide(CH_L_IN1, CH_L_IN2, dl, cfg_.left_invert);
    SetSide(CH_R_IN1, CH_R_IN2, dr, cfg_.right_invert);
}

void MotorDriver::Drive(int left, int right) {
    if (!ready_) return;
    xSemaphoreTake(mu_, portMAX_DELAY);
    if (motion_inhibit_.load()) {
        cmd_l_ = cmd_r_ = 0;
        ApplyLocked(0, 0);
    } else {
        cmd_l_ = left;
        cmd_r_ = right;
        ApplyLocked(left, right);
    }
    xSemaphoreGive(mu_);
}

void MotorDriver::DriveTest(int left_duty_pct, int right_duty_pct) {
    if (!ready_) return;
    left_duty_pct = left_duty_pct > 100 ? 100 : (left_duty_pct < -100 ? -100 : left_duty_pct);
    right_duty_pct = right_duty_pct > 100 ? 100 : (right_duty_pct < -100 ? -100 : right_duty_pct);
    xSemaphoreTake(mu_, portMAX_DELAY);
    cmd_l_ = left_duty_pct;
    cmd_r_ = right_duty_pct;
    // Directly apply raw signed PWM duty. This intentionally ignores both safety
    // inhibits and cfg_.max_pct for a short, explicit hardware diagnostic.
    SetSide(CH_L_IN1, CH_L_IN2, left_duty_pct, cfg_.left_invert);
    SetSide(CH_R_IN1, CH_R_IN2, right_duty_pct, cfg_.right_invert);
    xSemaphoreGive(mu_);
}

void MotorDriver::Brake() { Drive(0, 0); }

void MotorDriver::Coast() {
    if (!ready_) return;
    xSemaphoreTake(mu_, portMAX_DELAY);
    cmd_l_ = cmd_r_ = 0;
    SetDuty(CH_L_IN1, 0); SetDuty(CH_L_IN2, 0);
    SetDuty(CH_R_IN1, 0); SetDuty(CH_R_IN2, 0);
    xSemaphoreGive(mu_);
}

void MotorDriver::SetForwardInhibit(bool inhibit) {
    inhibit_.store(inhibit);
    if (!ready_) return;
    xSemaphoreTake(mu_, portMAX_DELAY);
    ApplyLocked(cmd_l_, cmd_r_);   // re-apply the last command through the interlock right now
    xSemaphoreGive(mu_);
}

void MotorDriver::SetMotionInhibit(bool inhibit) {
    motion_inhibit_.store(inhibit);
    if (!ready_) return;
    xSemaphoreTake(mu_, portMAX_DELAY);
    if (inhibit) cmd_l_ = cmd_r_ = 0;
    ApplyLocked(cmd_l_, cmd_r_);
    xSemaphoreGive(mu_);
}
