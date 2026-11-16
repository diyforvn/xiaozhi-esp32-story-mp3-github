// Pure C++ logic for the xiaozhi robot base (no ESP-IDF dependency, unit-testable on a PC).
//  - HazardClassifier : VL53L0X distance -> {none, obstacle, cliff, fault}
//  - BehaviorPlayer   : plays a table of timed steps (wheels + head pose)
//  - helpers          : forward-inhibit filter, duty mapping, ramps
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace robot {

// ---------------------------------------------------------------------------
// Hazard classification
// ---------------------------------------------------------------------------
enum class Hazard : uint8_t {
    kUnknown = 0,   // not calibrated / no valid sample yet  -> forward is inhibited
    kNone,          // floor ahead looks normal
    kObstacle,      // something closer than the calibrated floor distance
    kCliff,         // floor farther than the cliff threshold, or no echo at all
    kSensorFault,   // I2C / sensor failure
};
const char* HazardName(Hazard h);

struct HazardConfig {
    uint16_t cliff_mm = 150;           // reading above this => drop / table edge
    uint16_t obstacle_margin_mm = 30;  // reading below (floor - margin) => obstacle
    uint16_t min_valid_mm = 30;        // below this the sensor is too close => obstacle
    uint16_t no_echo_mm = 8000;        // VL53L0X reports ~8190 / 65535 when nothing is seen
    uint8_t enter_count = 2;           // consecutive samples to enter a hazard
    uint8_t clear_count = 3;           // consecutive clean samples to clear it
    uint8_t fault_count = 3;           // consecutive I2C failures => kSensorFault
};

class HazardClassifier {
public:
    void Configure(const HazardConfig& c) { cfg_ = c; }
    const HazardConfig& config() const { return cfg_; }
    void SetFloor(uint16_t floor_mm) { floor_mm_ = floor_mm; }   // 0 = obstacle detection off
    uint16_t floor_mm() const { return floor_mm_; }
    void Reset();
    // raw_mm: sensor reading; io_ok=false when the I2C read failed / timed out
    Hazard Feed(uint16_t raw_mm, bool io_ok);
    Hazard current() const { return cur_; }

private:
    HazardConfig cfg_;
    uint16_t floor_mm_ = 0;
    Hazard cur_ = Hazard::kUnknown;
    Hazard pending_ = Hazard::kUnknown;
    uint8_t pend_n_ = 0;
};

// Median-based floor calibration. Returns false when the samples are too few,
// too noisy, or the floor is not clearly below the cliff threshold.
bool ComputeFloor(uint16_t* samples, size_t n, uint16_t cliff_mm, uint16_t* floor_out);

// ---------------------------------------------------------------------------
// BMI160 motion tracking
// ---------------------------------------------------------------------------
struct Acceleration {
    float x = 0, y = 0, z = 0;  // g
};

enum class MotionState : uint8_t { kUncalibrated, kResting, kPickedUp, kHeld };
enum class MotionEvent : uint8_t {
    kNone,
    kPickedUp,
    kHeld,
    kReleased,
    kShaken,
    kTiltForward,
    kTiltBackward,
    kTiltLeft,
    kTiltRight,
};

struct MotionSnapshot {
    MotionState state = MotionState::kUncalibrated;
    MotionEvent event = MotionEvent::kNone;
    float pitch_deg = 0;
    float roll_deg = 0;
};

class MotionClassifier {
public:
    // forward_axis selects sensor X or Y; sign maps that axis to robot-forward.
    bool Calibrate(const Acceleration* samples, size_t count, uint8_t forward_axis,
                   int8_t forward_sign, float lift_tilt_deg = 30,
                   float release_impact_g = 1.2f, float release_delta_g = 0.35f);
    MotionSnapshot Feed(const Acceleration& sample, uint32_t dt_ms, bool robot_stationary,
                        bool robot_on_calibrated_floor = false);
    MotionState state() const { return state_; }
    float pitch_deg() const { return pitch_deg_; }
    float roll_deg() const { return roll_deg_; }

private:
    Acceleration gravity_{};
    Acceleration forward_{};
    Acceleration right_{};
    Acceleration previous_{};
    float lift_tilt_deg_ = 30;
    float release_impact_g_ = 1.2f;
    float release_delta_g_ = 0.35f;
    float pitch_deg_ = 0, roll_deg_ = 0;
    uint32_t movement_ms_ = 0, tilt_ms_ = 0, stable_ms_ = 0, pickup_age_ms_ = 0;
    bool has_previous_ = false, impact_seen_ = false, left_floor_after_pickup_ = false;
    bool shake_reported_ = false;
    MotionState state_ = MotionState::kUncalibrated;
    MotionEvent direction_ = MotionEvent::kNone;
};

// ---------------------------------------------------------------------------
// Behaviors (timed step tables)
// ---------------------------------------------------------------------------
enum : uint8_t { kFlagMirror = 1 };  // randomly swap left/right at step start (random turn direction)

struct Step {
    int8_t left;        // wheel command, percent -100..100
    int8_t right;
    int8_t head;        // head pose -100 (down) .. +100 (up)
    uint8_t osc_amp;    // head oscillation amplitude (pose units)
    uint16_t osc_ms;    // oscillation period, 0 = none
    uint16_t dur_ms;
    uint16_t jitter_ms; // + random 0..jitter_ms
    uint8_t flags;
};

struct Behavior {
    const char* name;
    const Step* steps;
    uint8_t count;
    bool loop;
    uint16_t max_reverse_ms;  // total time allowed with net-reverse wheels per run (0 = unlimited)
    uint32_t max_total_ms;    // 0 = until stopped
};

struct PlayerOut {
    int left = 0;
    int right = 0;
    float head = 0;
    bool finished = false;
};

class BehaviorPlayer {
public:
    explicit BehaviorPlayer(uint32_t seed = 12345) : rng_(seed ? seed : 1) {}
    void Start(const Behavior* b);
    void Stop() { b_ = nullptr; }
    bool active() const { return b_ != nullptr; }
    const Behavior* behavior() const { return b_; }
    PlayerOut Tick(uint32_t dt_ms);
    uint32_t Rand();  // shared RNG (xorshift32)

private:
    void BeginStep(uint8_t idx);
    const Behavior* b_ = nullptr;
    uint8_t idx_ = 0;
    uint32_t step_elapsed_ = 0, step_dur_ = 0, total_ = 0, rev_used_ = 0;
    bool mirror_ = false, done_ = false;
    uint32_t rng_;
};

// ---------------------------------------------------------------------------
// Motor helpers
// ---------------------------------------------------------------------------
// Remove the net-forward component (keeps rotation). (l+r) > 0  =>  (l+r) == 0
void RemoveNetForward(int& l, int& r);
// Map -100..100 to signed duty percent with start-up deadzone compensation.
int ToDutyPercent(int s, int min_start_pct, int max_pct);
// Slew-rate limiter (units/s). Uses accel when speeding up, decel otherwise.
float RampToward(float cur, float target, float dt_s, float accel, float decel);

// ---------------------------------------------------------------------------
// Behavior tables
// ---------------------------------------------------------------------------
// nullptr for "neutral"/unknown. *known = false when the name is not recognised.
const Behavior* FindEmotionBehavior(const char* emotion, bool* known);
uint8_t DefaultEmotionIntensity(const char* emotion);
extern const Behavior kReflexCliff;
extern const Behavior kReflexObstacle;
extern const Behavior* const kWanderBehaviors[];
extern const uint8_t kWanderWeights[];
extern const size_t kWanderCount;

}  // namespace robot
