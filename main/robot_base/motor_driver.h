// DRV8833 dual H-bridge, 2 PWM pins per wheel (IN1/IN2), no encoder.
//   forward : IN1 = PWM, IN2 = 0        reverse : IN1 = 0, IN2 = PWM
//   brake   : IN1 = IN2 = 1             coast   : IN1 = IN2 = 0
#pragma once
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <cstdint>

class MotorDriver {
public:
    struct Config {
        gpio_num_t left_in1, left_in2, right_in1, right_in2;
        bool left_invert = false;
        bool right_invert = false;
        int min_start_pct = 30;   // duty needed to overcome stiction
        int max_pct = 70;         // duty ceiling (protects motors on a 2S pack)
        uint32_t pwm_hz = 20000;
    };

    bool Init(const Config& cfg);
    // Wheel command -100..100 (percent). Applies the forward interlock.
    void Drive(int left, int right);
    // Diagnostic only: raw duty percent, bypasses both motor interlocks and configured duty cap.
    void DriveTest(int left_duty_pct, int right_duty_pct);
    void Brake();
    void Coast();
    void SetMotionInhibit(bool inhibit);
    // Hardware-level interlock, set from the laser task: removes the net-forward part
    // of whatever is being driven right now, immediately.
    void SetForwardInhibit(bool inhibit);
    bool forward_inhibited() const { return inhibit_.load(); }
    int left() const { return cmd_l_; }
    int right() const { return cmd_r_; }
    bool ready() const { return ready_; }

private:
    void ApplyLocked(int l, int r);
    void SetSide(ledc_channel_t ch_a, ledc_channel_t ch_b, int signed_pct, bool invert);
    void SetDuty(ledc_channel_t ch, uint32_t duty);

    Config cfg_{};
    bool ready_ = false;
    SemaphoreHandle_t mu_ = nullptr;
    std::atomic<bool> inhibit_{false};
    std::atomic<bool> motion_inhibit_{false};
    int cmd_l_ = 0, cmd_r_ = 0;   // last requested (pre-interlock) command
    int64_t last_drive_log_us_ = 0;
    uint32_t max_duty_ = 1024;
};
