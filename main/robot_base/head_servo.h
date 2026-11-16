// SG90 head servo on one LEDC channel (50 Hz). Pose -100..+100 maps to center +/- range degrees.
#pragma once
#include <driver/gpio.h>
#include <driver/ledc.h>

class HeadServo {
public:
    struct Config {
        gpio_num_t pin;
        int center_deg = 90;
        int range_deg = 35;     // pose +-100 => center +- range_deg
        bool invert = false;    // flip direction if "up" moves the head down
        int min_us = 500;       // SG90: ~500..2400 us
        int max_us = 2400;
    };
    bool Init(const Config& cfg);
    void SetPose(int pose);          // -100..100
    void SetAngleDeg(float deg);     // absolute, clamped to 0..180
    int pose() const { return pose_; }
    float angle_deg() const { return angle_; }
    bool ready() const { return ready_; }

private:
    Config cfg_{};
    bool ready_ = false;
    int pose_ = 0;
    float angle_ = 90;
};
