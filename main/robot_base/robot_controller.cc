#include "robot_controller.h"

#include <math.h>
#include <string.h>

namespace robot {

RobotController::RobotController(const ControllerConfig& cfg, uint32_t seed)
    : cfg_(cfg), emo_(seed * 2654435761u + 1), wan_(seed * 40503u + 7), ref_(seed * 69069u + 3) {}

void RobotController::SetEmotion(const char* name, uint8_t intensity) {
    bool known = false;
    const Behavior* b = FindEmotionBehavior(name, &known);
    if (!known) return;                                  // unknown label: keep what we are doing
    strncpy(last_emotion_, (name && *name) ? name : "neutral", sizeof(last_emotion_) - 1);
    last_emotion_[sizeof(last_emotion_) - 1] = '\0';
    if (!b) {                                            // neutral
        emo_.Stop();
        emo_active_ = false;
        emotion_intensity_ = 0;
        return;
    }
    if (intensity > 100) intensity = 100;
    emotion_intensity_ = intensity;
    if (emo_active_ && emo_.behavior() == b) {           // same emotion repeated: just refresh the timer
        emo_age_ = 0;
        return;
    }
    emo_.Start(b);
    emo_active_ = true;
    emo_age_ = 0;
    wan_.Stop();
    wander_pause_ms_ = 3000;
}

void RobotController::SetInteraction(bool greeting) {
    interaction_greeting_ = greeting;
    interaction_ms_ = 1200;
}

void RobotController::EndEmotion() {
    emo_.Stop();
    emo_active_ = false;
    emotion_intensity_ = 0;
    strcpy(last_emotion_, "neutral");
    pending_face_ = "neutral";
}

void RobotController::ManualMove(int l, int r, uint32_t dur_ms) {
    if (l > 100) l = 100;
    if (l < -100) l = -100;
    if (r > 100) r = 100;
    if (r < -100) r = -100;
    man_l_ = l;
    man_r_ = r;
    man_ms_ = dur_ms;
}

void RobotController::ManualHead(int pose, uint32_t hold_ms) {
    if (pose > 100) pose = 100;
    if (pose < -100) pose = -100;
    head_pose_ = pose;
    head_hold_ms_ = hold_ms;
}

void RobotController::Stop() {
    man_ms_ = 0;
    head_hold_ms_ = 0;
    emo_.Stop();
    emo_active_ = false;
    emotion_intensity_ = 0;
    strcpy(last_emotion_, "neutral");
    ref_.Stop();
    reflex_active_ = false;
    wan_.Stop();
    wander_pause_ms_ = 3000;
    interaction_ms_ = 0;
}

void RobotController::SetMotionHeld(bool held) {
    if (motion_held_ == held) return;
    motion_held_ = held;
    man_ms_ = 0;
    ref_.Stop();
    reflex_active_ = false;
    wan_.Stop();
    wander_pause_ms_ = held ? 0 : 2000;
}

const Behavior* RobotController::PickWander() {
    uint32_t total = 0;
    for (size_t i = 0; i < kWanderCount; i++) total += kWanderWeights[i];
    uint32_t pick = wan_.Rand() % total;
    for (size_t i = 0; i < kWanderCount; i++) {
        if (pick < kWanderWeights[i]) return kWanderBehaviors[i];
        pick -= kWanderWeights[i];
    }
    return kWanderBehaviors[0];
}

void RobotController::StartReflex(Hazard h) {
    ref_.Start(h == Hazard::kCliff ? &kReflexCliff : &kReflexObstacle);
    reflex_active_ = true;
    man_ms_ = 0;                 // a reflex cancels manual driving
    wan_.Stop();
    wander_pause_ms_ = 800;
    if (cfg_.face_reaction) pending_face_ = (h == Hazard::kCliff) ? "shocked" : "surprised";

    // stuck detection: 6 reflexes within 20 s => stop moving for a while
    for (int i = 0; i < 5; i++) reflex_hist_[i] = reflex_hist_[i + 1];
    reflex_hist_[5] = now_ms_;
    if (reflex_hist_n_ < 6) reflex_hist_n_++;
    if (reflex_hist_n_ == 6 && now_ms_ - reflex_hist_[0] <= 20000) {
        stuck_ms_ = 15000;
        reflex_hist_n_ = 0;
        if (cfg_.face_reaction) pending_face_ = "confused";
    }
}

ControllerOutput RobotController::Tick(const ControllerInput& in) {
    ControllerOutput out;
    const uint32_t dt = in.dt_ms;
    now_ms_ += dt;
    pending_face_ = nullptr;

    const bool loco_ok = !motion_held_ && in.motion_allowed && in.sensor_ok;
    const bool quiet = (cfg_.stop_when_listening && in.listening) || in.speaking;
    bool manual_intent = false;
    bool wander_intent = false;
    if (stuck_ms_ > 0) stuck_ms_ = stuck_ms_ > dt ? stuck_ms_ - dt : 0;
    if (in.speaking) speech_phase_ms_ += dt;
    else speech_phase_ms_ = 0;
    if (interaction_ms_ > 0) interaction_ms_ = interaction_ms_ > dt ? interaction_ms_ - dt : 0;

    auto gate = [&](int& a, int& b) {
        if (!loco_ok || (quiet && !manual_intent && !wander_intent) || stuck_ms_ > 0) { a = 0; b = 0; }
        if (!cfg_.blind_reverse && a + b < 0) { a = 0; b = 0; }
    };

    int l = 0, r = 0;
    float head = in.listening ? (float)cfg_.listening_head : 0.f;
    Mode mode = Mode::kRest;

    if (!reflex_active_) {
        // ---- intent from the normal sources: manual > emotion > wander > rest
        if (man_ms_ > 0) {
            l = man_l_;
            r = man_r_;
            manual_intent = true;
            // An explicit move can run while listening; pause only during speech
            // playback or another locomotion lock.
            if (!in.speaking && loco_ok) man_ms_ = man_ms_ > dt ? man_ms_ - dt : 0;
            mode = Mode::kManual;
        } else if (emo_active_) {
            PlayerOut po = emo_.Tick(dt);
            l = po.left * emotion_intensity_ / 100;
            r = po.right * emotion_intensity_ / 100;
            head = po.head * emotion_intensity_ / 100.0f;
            emo_age_ += dt;
            mode = Mode::kEmotion;
            if (po.finished) {
                l = r = 0;
                head = 0;
                EndEmotion();
            } else if (emo_age_ >= cfg_.emotion_max_ms) {
                l = r = 0;
                head = 0;
                EndEmotion();
            }
        } else if (cfg_.wander && (in.idle || in.listening)) {
            if (wan_.active()) {
                PlayerOut po = wan_.Tick(dt);
                l = po.left;
                r = po.right;
                head = po.head;
                mode = Mode::kWander;
                wander_intent = true;
                if (po.finished) {
                    wan_.Stop();
                    wander_pause_ms_ = 1500 + (int32_t)(wan_.Rand() % 3000u);
                }
            } else {
                wander_pause_ms_ -= (int32_t)dt;
                if (wander_pause_ms_ <= 0) wan_.Start(PickWander());
            }
        }
        gate(l, r);

        // ---- laser reflex: only when we are really about to drive forward into a hazard
        if ((in.hazard == Hazard::kCliff || in.hazard == Hazard::kObstacle) && in.sensor_ok &&
            (l + r) > 10) {
            StartReflex(in.hazard);
        }
    }

    if (reflex_active_) {
        PlayerOut po = ref_.Tick(dt);
        l = po.left;
        r = po.right;
        head = po.head;
        mode = Mode::kReflex;
        gate(l, r);
        if (po.finished) {
            reflex_active_ = false;
            ref_.Stop();
            if (cfg_.face_reaction && !pending_face_) pending_face_ = last_emotion_;
        }
    }
    if (stuck_ms_ > 0) mode = Mode::kStuck;

    if (in.speaking) {
        const float nod = 7.0f * sinf(6.2831853f * (float)(speech_phase_ms_ % 1100u) / 1100.0f);
        head += nod;
    }
    if (interaction_ms_ > 0) {
        const float progress = 1.0f - (float)interaction_ms_ / 1200.0f;
        const float envelope = sinf(3.1415927f * progress);
        const float gesture = (interaction_greeting_ ? 24.0f : -18.0f) * envelope;
        head += gesture;
    }
    if (head > 100) head = 100;
    if (head < -100) head = -100;
    if (motion_held_) {
        l = r = 0;
        mode = Mode::kHeld;
    }

    // ---- final safety stage -------------------------------------------------
    // 1) contiguous reverse cap (nothing looks behind the robot)
    bool want_rev = (l + r) < 0;
    if (rev_block_ms_ > 0) {
        rev_block_ms_ -= (int32_t)dt;
        if (want_rev) { l = 0; r = 0; want_rev = false; }
    }
    if (want_rev) {
        rev_run_ms_ += dt;
        if (rev_run_ms_ > cfg_.reverse_max_ms) {
            l = 0; r = 0;
            rev_block_ms_ = 600;
            rev_run_ms_ = 0;
        }
    } else {
        rev_run_ms_ = 0;
    }
    // 2) never drive net-forward while the guard reports anything but a clear floor
    if (in.sensor_ok && in.hazard != Hazard::kNone) RemoveNetForward(l, r);

    // head override from the MCP tool
    if (head_hold_ms_ > 0) {
        head = (float)head_pose_;
        head_hold_ms_ = head_hold_ms_ > dt ? head_hold_ms_ - dt : 0;
    }

    mode_ = mode;
    out.left = l;
    out.right = r;
    out.head = head;
    out.face = pending_face_;
    out.manual = mode_ == Mode::kManual;
    out.wander = mode_ == Mode::kWander;
    return out;
}

const char* RobotController::ModeName() const {
    switch (mode_) {
        case Mode::kRest: return "rest";
        case Mode::kWander: return "wander";
        case Mode::kEmotion: return "emotion";
        case Mode::kManual: return "manual";
        case Mode::kReflex: return "reflex";
        case Mode::kStuck: return "stuck";
        case Mode::kHeld: return "held";
    }
    return "?";
}

const char* RobotController::BehaviorName() const {
    if (motion_held_) return "held";
    if (reflex_active_ && ref_.behavior()) return ref_.behavior()->name;
    if (man_ms_ > 0) return "manual";
    if (emo_active_ && emo_.behavior()) return emo_.behavior()->name;
    if (wan_.active() && wan_.behavior()) return wan_.behavior()->name;
    return "";
}

}  // namespace robot
