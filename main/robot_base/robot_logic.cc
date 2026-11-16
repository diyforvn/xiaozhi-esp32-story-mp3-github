#include "robot_logic.h"

#include <math.h>
#include <string.h>

namespace robot {

const char* HazardName(Hazard h) {
    switch (h) {
        case Hazard::kUnknown: return "unknown";
        case Hazard::kNone: return "none";
        case Hazard::kObstacle: return "obstacle";
        case Hazard::kCliff: return "cliff";
        case Hazard::kSensorFault: return "sensor_fault";
    }
    return "?";
}

// ---------------------------------------------------------------------------
void HazardClassifier::Reset() {
    cur_ = Hazard::kUnknown;
    pending_ = Hazard::kUnknown;
    pend_n_ = 0;
}

Hazard HazardClassifier::Feed(uint16_t raw_mm, bool io_ok) {
    Hazard cand;
    if (!io_ok) {
        cand = Hazard::kSensorFault;
    } else if (raw_mm >= cfg_.no_echo_mm) {
        cand = Hazard::kCliff;                       // no echo: fail safe, treat as a drop
    } else if (raw_mm > cfg_.cliff_mm) {
        cand = Hazard::kCliff;
    } else if (raw_mm < cfg_.min_valid_mm) {
        cand = Hazard::kObstacle;                    // touching / too close
    } else if (floor_mm_ > 0 && (uint32_t)raw_mm + cfg_.obstacle_margin_mm < floor_mm_) {
        cand = Hazard::kObstacle;
    } else {
        cand = Hazard::kNone;
    }

    if (cand == pending_) {
        if (pend_n_ < 255) pend_n_++;
    } else {
        pending_ = cand;
        pend_n_ = 1;
    }
    uint8_t need = cfg_.enter_count;
    if (cand == Hazard::kNone) need = cfg_.clear_count;
    else if (cand == Hazard::kSensorFault) need = cfg_.fault_count;
    if (pend_n_ >= need) cur_ = cand;
    return cur_;
}

bool ComputeFloor(uint16_t* s, size_t n, uint16_t cliff_mm, uint16_t* floor_out) {
    if (n < 10) return false;
    for (size_t i = 1; i < n; i++) {           // insertion sort
        uint16_t v = s[i];
        size_t j = i;
        while (j > 0 && s[j - 1] > v) { s[j] = s[j - 1]; j--; }
        s[j] = v;
    }
    uint16_t med = s[n / 2];
    uint16_t p10 = s[n / 10];
    uint16_t p90 = s[n - 1 - n / 10];
    if (p90 - p10 > 15) return false;                    // too noisy
    if (med + 15 > cliff_mm) return false;               // floor must be clearly below the cliff threshold
    if (med < 30) return false;                          // too close to be a floor
    *floor_out = med;
    return true;
}

static float Dot(const Acceleration& a, const Acceleration& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static float Norm(const Acceleration& a) {
    return sqrtf(Dot(a, a));
}

static Acceleration Scale(const Acceleration& a, float scale) {
    return {a.x * scale, a.y * scale, a.z * scale};
}

static Acceleration Subtract(const Acceleration& a, const Acceleration& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

bool MotionClassifier::Calibrate(const Acceleration* samples, size_t count, uint8_t forward_axis,
                                 int8_t forward_sign, float lift_tilt_deg,
                                 float release_impact_g, float release_delta_g) {
    if (!samples || count < 10 || forward_axis > 1 || (forward_sign != -1 && forward_sign != 1) ||
        lift_tilt_deg < 15 || lift_tilt_deg > 60 || release_impact_g < 1.0f ||
        release_impact_g > 2.5f || release_delta_g < 0.1f || release_delta_g > 1.0f) return false;

    Acceleration mean{};
    for (size_t i = 0; i < count; ++i) {
        float magnitude = Norm(samples[i]);
        if (magnitude < 0.75f || magnitude > 1.25f) return false;
        mean.x += samples[i].x;
        mean.y += samples[i].y;
        mean.z += samples[i].z;
    }
    mean = Scale(mean, 1.0f / (float)count);
    float mean_norm = Norm(mean);
    if (mean_norm < 0.85f || mean_norm > 1.15f) return false;
    gravity_ = Scale(mean, 1.0f / mean_norm);

    Acceleration axis = forward_axis == 0 ? Acceleration{1, 0, 0} : Acceleration{0, 1, 0};
    axis = Scale(axis, (float)forward_sign);
    forward_ = Subtract(axis, Scale(gravity_, Dot(axis, gravity_)));
    float forward_norm = Norm(forward_);
    if (forward_norm < 0.5f) return false;
    forward_ = Scale(forward_, 1.0f / forward_norm);
    right_ = {gravity_.y * forward_.z - gravity_.z * forward_.y,
              gravity_.z * forward_.x - gravity_.x * forward_.z,
              gravity_.x * forward_.y - gravity_.y * forward_.x};

    lift_tilt_deg_ = lift_tilt_deg;
    release_impact_g_ = release_impact_g;
    release_delta_g_ = release_delta_g;
    previous_ = mean;
    has_previous_ = true;
    movement_ms_ = tilt_ms_ = stable_ms_ = pickup_age_ms_ = 0;
    impact_seen_ = left_floor_after_pickup_ = shake_reported_ = false;
    pitch_deg_ = roll_deg_ = 0;
    direction_ = MotionEvent::kNone;
    state_ = MotionState::kResting;
    return true;
}

MotionSnapshot MotionClassifier::Feed(const Acceleration& sample, uint32_t dt_ms, bool robot_stationary,
                                      bool robot_on_calibrated_floor) {
    MotionSnapshot out;
    out.state = state_;
    if (state_ == MotionState::kUncalibrated) return out;

    const float magnitude = Norm(sample);
    if (magnitude > 0.01f) {
        Acceleration unit = Scale(sample, 1.0f / magnitude);
        const float up_component = Dot(unit, gravity_);
        pitch_deg_ = atan2f(Dot(unit, forward_), up_component) * 57.29578f;
        roll_deg_ = atan2f(Dot(unit, right_), up_component) * 57.29578f;
    }
    out.pitch_deg = pitch_deg_;
    out.roll_deg = roll_deg_;

    const bool stable_gravity = magnitude >= 0.82f && magnitude <= 1.18f;
    const float delta = has_previous_ ? Norm(Subtract(sample, previous_)) : 0;
    previous_ = sample;
    has_previous_ = true;
    const float tilt = fmaxf(fabsf(pitch_deg_), fabsf(roll_deg_));
    const bool tilted = tilt >= lift_tilt_deg_;

    MotionEvent direction = MotionEvent::kNone;
    if (pitch_deg_ >= 18) direction = MotionEvent::kTiltForward;
    else if (pitch_deg_ <= -18) direction = MotionEvent::kTiltBackward;
    else if (roll_deg_ >= 18) direction = MotionEvent::kTiltRight;
    else if (roll_deg_ <= -18) direction = MotionEvent::kTiltLeft;

    if (state_ == MotionState::kResting) {
        const bool strong_lift_impulse = (robot_stationary && delta >= 0.65f) ||
                                         fabsf(magnitude - 1.0f) >= 0.5f;
        if (robot_stationary && (delta >= 0.28f || fabsf(magnitude - 1.0f) >= 0.28f)) {
            movement_ms_ += dt_ms;
        } else {
            movement_ms_ = movement_ms_ > dt_ms * 2 ? movement_ms_ - dt_ms * 2 : 0;
        }
        tilt_ms_ = tilted ? tilt_ms_ + dt_ms : 0;
        const bool sustained_pickup = robot_stationary && movement_ms_ >= 120;
        if (strong_lift_impulse || sustained_pickup || tilt_ms_ >= 300) {
            state_ = MotionState::kPickedUp;
            movement_ms_ = stable_ms_ = pickup_age_ms_ = 0;
            impact_seen_ = false;
            left_floor_after_pickup_ = false;
            shake_reported_ = false;
            out.event = MotionEvent::kPickedUp;
        } else if (robot_stationary && movement_ms_ >= 40 && !shake_reported_) {
            shake_reported_ = true;
            out.event = MotionEvent::kShaken;
        } else if (movement_ms_ < 40) {
            shake_reported_ = false;
        }
    } else if (state_ == MotionState::kPickedUp) {
        pickup_age_ms_ += dt_ms;
        // Ignore the lift itself, then remember a placement jolt even if it happens
        // before the 600 ms transition from picked-up to held.
        if (pickup_age_ms_ >= 120 &&
            (magnitude >= release_impact_g_ || delta >= release_delta_g_)) impact_seen_ = true;
        if (!robot_on_calibrated_floor) left_floor_after_pickup_ = true;
        stable_ms_ = stable_gravity && delta < 0.12f ? stable_ms_ + dt_ms : 0;
        if (stable_ms_ >= 600) {
            state_ = MotionState::kHeld;
            stable_ms_ = 0;
            out.event = MotionEvent::kHeld;
        }
    } else if (state_ == MotionState::kHeld) {
        if (!robot_on_calibrated_floor) left_floor_after_pickup_ = true;
        if (magnitude >= release_impact_g_ || delta >= release_delta_g_) impact_seen_ = true;
        const bool returned_to_rest = (impact_seen_ || (left_floor_after_pickup_ && robot_on_calibrated_floor)) &&
                                      !tilted &&
                                      magnitude >= 0.82f && magnitude <= 1.18f;
        stable_ms_ = returned_to_rest && delta < 0.12f ? stable_ms_ + dt_ms : 0;
        if (stable_ms_ >= 900) {
            state_ = MotionState::kResting;
            movement_ms_ = tilt_ms_ = stable_ms_ = 0;
            pickup_age_ms_ = 0;
            impact_seen_ = false;
            left_floor_after_pickup_ = false;
            shake_reported_ = false;
            out.event = MotionEvent::kReleased;
        }
    }

    if (out.event == MotionEvent::kNone && direction != direction_) out.event = direction;
    direction_ = direction;
    out.state = state_;
    out.pitch_deg = pitch_deg_;
    out.roll_deg = roll_deg_;
    return out;
}

// ---------------------------------------------------------------------------
uint32_t BehaviorPlayer::Rand() {
    uint32_t x = rng_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_ = x ? x : 1;
    return rng_;
}

void BehaviorPlayer::BeginStep(uint8_t idx) {
    idx_ = idx;
    const Step& s = b_->steps[idx];
    step_elapsed_ = 0;
    step_dur_ = s.dur_ms + (s.jitter_ms ? (Rand() % (s.jitter_ms + 1u)) : 0u);
    mirror_ = (s.flags & kFlagMirror) && (Rand() & 1);
}

void BehaviorPlayer::Start(const Behavior* b) {
    b_ = b;
    total_ = 0;
    rev_used_ = 0;
    done_ = false;
    if (b_ && b_->count > 0) BeginStep(0);
    else b_ = nullptr;
}

PlayerOut BehaviorPlayer::Tick(uint32_t dt_ms) {
    PlayerOut out;
    if (!b_) { out.finished = true; return out; }

    total_ += dt_ms;
    if (b_->max_total_ms && total_ >= b_->max_total_ms) done_ = true;

    const Step& s = b_->steps[idx_];
    float head = (float)s.head;
    if (!done_ && s.osc_ms > 0 && s.osc_amp > 0) {
        head += (float)s.osc_amp * sinf(6.2831853f * (float)(total_ % 600000u) / (float)s.osc_ms);
    }
    if (head > 100) head = 100;
    if (head < -100) head = -100;
    out.head = head;

    if (done_) {                       // hold the last pose, wheels stopped
        out.finished = true;
        return out;
    }

    int l = s.left, r = s.right;
    if (mirror_) { int t = l; l = r; r = t; }
    if (l + r < 0) {                   // net reverse: spend the reverse budget
        rev_used_ += dt_ms;
        if (b_->max_reverse_ms && rev_used_ > b_->max_reverse_ms) { l = 0; r = 0; }
    }
    out.left = l;
    out.right = r;

    step_elapsed_ += dt_ms;
    if (step_elapsed_ >= step_dur_) {
        uint8_t next = idx_ + 1;
        if (next >= b_->count) {
            if (b_->loop) next = 0;
            else { done_ = true; return out; }
        }
        BeginStep(next);
    }
    return out;
}

// ---------------------------------------------------------------------------
void RemoveNetForward(int& l, int& r) {
    int s = l + r;
    if (s > 0) {
        l -= s / 2;
        r -= s - s / 2;
    }
}

int ToDutyPercent(int s, int min_start_pct, int max_pct) {
    if (s == 0) return 0;
    int a = s < 0 ? -s : s;
    if (a > 100) a = 100;
    if (max_pct < min_start_pct) max_pct = min_start_pct;
    int duty = min_start_pct + (max_pct - min_start_pct) * a / 100;
    return s < 0 ? -duty : duty;
}

float RampToward(float cur, float target, float dt_s, float accel, float decel) {
    bool speeding_up = fabsf(target) > fabsf(cur) && (target * cur >= 0);
    float step = (speeding_up ? accel : decel) * dt_s;
    float diff = target - cur;
    if (diff > step) return cur + step;
    if (diff < -step) return cur - step;
    return target;
}

}  // namespace robot
