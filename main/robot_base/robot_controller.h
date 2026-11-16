// Decision layer: turns emotions / commands / idle wandering into wheel + head targets,
// and enforces every safety rule. Pure C++ (no ESP-IDF) so it can be tested on a PC.
#pragma once
#include "robot_logic.h"

namespace robot {

struct ControllerConfig {
    bool wander = true;               // idle wandering when device is idle
    bool stop_when_listening = true;  // wheels silent while the device is listening to the user
    bool blind_reverse = true;        // allow reversing (there is no rear sensor)
    uint16_t reverse_max_ms = 700;    // max contiguous reverse time
    uint32_t emotion_max_ms = 90000;  // an emotion behavior falls back to neutral after this
    bool face_reaction = true;        // ask the display for surprised/shocked on reflex
    int listening_head = 15;          // head pose while listening
};

struct ControllerInput {
    uint32_t dt_ms = 20;
    Hazard hazard = Hazard::kUnknown;
    bool sensor_ok = false;  // laser guard alive: fresh samples and no I2C fault
    bool listening = false;  // DeviceState == listening
    bool speaking = false;   // DeviceState == speaking
    bool idle = false;       // DeviceState == idle
    bool motion_allowed = true;  // false during connecting, setup, upgrade, and calibration
};

struct ControllerOutput {
    int left = 0;                 // percent -100..100, already safety-filtered
    int right = 0;
    float head = 0;               // pose -100..100
    const char* face = nullptr;   // non-null: ask the display to show this emotion now
    bool manual = false;          // explicit self.robot.move request; may run while listening
    bool wander = false;          // enabled autonomous wandering; may run while listening
};

enum class Mode : uint8_t { kRest, kWander, kEmotion, kManual, kReflex, kStuck, kHeld };

class RobotController {
public:
    explicit RobotController(const ControllerConfig& cfg, uint32_t seed = 7);

    void SetEmotion(const char* name, uint8_t intensity = 100);  // "neutral" ends the current emotion
    void SetInteraction(bool greeting);
    void ManualMove(int left, int right, uint32_t duration_ms);
    void ManualHead(int pose, uint32_t hold_ms);
    void Stop();                                       // cancel manual / emotion / reflex
    void SetMotionHeld(bool held);
    bool motion_held() const { return motion_held_; }
    void SetWander(bool on) { cfg_.wander = on; if (!on) wan_.Stop(); }
    bool wander_enabled() const { return cfg_.wander; }

    ControllerOutput Tick(const ControllerInput& in);

    Mode mode() const { return mode_; }
    const char* ModeName() const;
    const char* BehaviorName() const;
    const char* last_emotion() const { return last_emotion_; }
    uint8_t emotion_intensity() const { return emotion_intensity_; }

private:
    void StartReflex(Hazard h);
    void EndEmotion();
    const Behavior* PickWander();

    ControllerConfig cfg_;
    BehaviorPlayer emo_, wan_, ref_;
    bool emo_active_ = false, reflex_active_ = false;
    uint8_t emotion_intensity_ = 100;
    uint32_t emo_age_ = 0;
    int man_l_ = 0, man_r_ = 0;
    uint32_t man_ms_ = 0;
    int head_pose_ = 0;
    uint32_t head_hold_ms_ = 0;
    int32_t wander_pause_ms_ = 1500;
    uint32_t stuck_ms_ = 0;
    uint32_t now_ms_ = 0;
    uint32_t reflex_hist_[6] = {0};
    uint8_t reflex_hist_n_ = 0;
    uint32_t rev_run_ms_ = 0;
    int32_t rev_block_ms_ = 0;
    uint32_t speech_phase_ms_ = 0;
    uint16_t interaction_ms_ = 0;
    bool interaction_greeting_ = false;
    bool motion_held_ = false;
    const char* pending_face_ = nullptr;
    char last_emotion_[24] = "neutral";
    Mode mode_ = Mode::kRest;
};

}  // namespace robot
