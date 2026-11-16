// Host-side tests for the robot logic. Build: g++ -std=c++17 -I../main/robot_base test_robot.cc ../main/robot_base/robot_logic.cc ../main/robot_base/robot_behaviors.cc ../main/robot_base/robot_controller.cc -o t
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include "robot_controller.h"
using namespace robot;

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s  ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

static ControllerInput In(Hazard h = Hazard::kNone, bool idle = false, bool listening = false, bool ok = true) {
    ControllerInput i; i.hazard = h; i.idle = idle; i.listening = listening; i.sensor_ok = ok; return i;
}

// ---------- HazardClassifier ----------
static void test_classifier() {
    HazardClassifier c; c.SetFloor(90);
    CHECK(c.current() == Hazard::kUnknown, "starts unknown");
    c.Feed(90, true); c.Feed(92, true);
    CHECK(c.current() == Hazard::kUnknown, "needs 3 clean samples");
    c.Feed(91, true);
    CHECK(c.current() == Hazard::kNone, "clear after 3");
    c.Feed(200, true);
    CHECK(c.current() == Hazard::kNone, "single outlier ignored");
    c.Feed(200, true);
    CHECK(c.current() == Hazard::kCliff, "2 samples > 150 => cliff");
    c.Feed(90, true); c.Feed(90, true);
    CHECK(c.current() == Hazard::kCliff, "still cliff until 3 clean");
    c.Feed(90, true);
    CHECK(c.current() == Hazard::kNone, "cleared");
    c.Feed(40, true); c.Feed(40, true);
    CHECK(c.current() == Hazard::kObstacle, "40 < 90-30 => obstacle");
    c.Feed(8190, true); c.Feed(8190, true);
    CHECK(c.current() == Hazard::kCliff, "no echo => cliff (fail safe)");
    c.Feed(0, false); c.Feed(0, false);
    CHECK(c.current() == Hazard::kCliff, "2 io fails not yet a fault");
    c.Feed(0, false);
    CHECK(c.current() == Hazard::kSensorFault, "3 io fails => sensor fault");
    c.Feed(150, true); c.Feed(150, true); c.Feed(150, true);
    CHECK(c.current() == Hazard::kNone, "exactly 150 mm is NOT a cliff (user rule: >15cm)");
    c.Feed(151, true); c.Feed(151, true);
    CHECK(c.current() == Hazard::kCliff, "151 mm => cliff");
    // uncalibrated: obstacle detection off, cliff still on
    HazardClassifier u; u.SetFloor(0);
    for (int i = 0; i < 3; i++) u.Feed(60, true);
    CHECK(u.current() == Hazard::kNone, "no floor => no obstacle detection");
    for (int i = 0; i < 2; i++) u.Feed(300, true);
    CHECK(u.current() == Hazard::kCliff, "cliff works without calibration");
}

static void test_floor() {
    uint16_t a[15], f = 0;
    for (int i = 0; i < 15; i++) a[i] = 88 + (i % 5);
    CHECK(ComputeFloor(a, 15, 150, &f) && f >= 88 && f <= 92, "good floor %d", f);
    for (int i = 0; i < 15; i++) a[i] = 80 + i * 4;   // noisy
    CHECK(!ComputeFloor(a, 15, 150, &f), "noisy rejected");
    for (int i = 0; i < 15; i++) a[i] = 148;
    CHECK(!ComputeFloor(a, 15, 150, &f), "floor too close to cliff threshold rejected");
    for (int i = 0; i < 5; i++) a[i] = 90;
    CHECK(!ComputeFloor(a, 5, 150, &f), "too few samples");
}

// ---------- helpers ----------
static void test_helpers() {
    int l = 28, r = 28; RemoveNetForward(l, r); CHECK(l == 0 && r == 0, "straight => stop");
    l = 55; r = -55; RemoveNetForward(l, r); CHECK(l == 55 && r == -55, "spin kept");
    l = 40; r = 22; RemoveNetForward(l, r); CHECK(l + r == 0 && l > 0 && r < 0, "arc => rotation (%d,%d)", l, r);
    l = -30; r = -30; RemoveNetForward(l, r); CHECK(l == -30 && r == -30, "reverse kept");
    l = 41; r = 20; RemoveNetForward(l, r); CHECK(l + r == 0, "odd sum (%d,%d)", l, r);
    CHECK(ToDutyPercent(0, 30, 70) == 0, "zero");
    CHECK(ToDutyPercent(100, 30, 70) == 70, "max");
    CHECK(ToDutyPercent(-100, 30, 70) == -70, "neg max");
    CHECK(ToDutyPercent(1, 30, 70) >= 30, "min start");
    CHECK(fabsf(RampToward(0, 100, 0.02f, 300, 800) - 6) < 1e-3, "ramp up");
    CHECK(fabsf(RampToward(50, 0, 0.02f, 300, 800) - 34) < 1e-3, "ramp down fast");
    CHECK(RampToward(5, 0, 0.1f, 300, 800) == 0, "ramp reaches target");
}

// run controller for ms with constant input; collect stats
struct Stats {
    int max_fwd = -999; int min_fwd = 999; int rev_ms = 0; int spin_ms = 0; int fwd_ms = 0; int still_ms = 0;
    float head_min = 999, head_max = -999; int max_contig_rev = 0; int nonzero_ms = 0;
    int faces = 0; std::vector<std::string> face_list;
    std::vector<std::string> modes;
};
static Stats Run(RobotController& c, ControllerInput in, int ms, std::function<void(int)> hook = nullptr) {
    Stats s; int contig = 0;
    for (int t = 0; t < ms; t += in.dt_ms) {
        if (hook) hook(t);
        ControllerOutput o = c.Tick(in);
        int fwd = o.left + o.right;
        s.max_fwd = std::max(s.max_fwd, fwd); s.min_fwd = std::min(s.min_fwd, fwd);
        if (fwd < 0) { s.rev_ms += in.dt_ms; contig += in.dt_ms; s.max_contig_rev = std::max(s.max_contig_rev, contig); } else contig = 0;
        if (fwd > 0) s.fwd_ms += in.dt_ms;
        if (o.left * o.right < 0) s.spin_ms += in.dt_ms;
        if (o.left == 0 && o.right == 0) s.still_ms += in.dt_ms; else s.nonzero_ms += in.dt_ms;
        s.head_min = std::min(s.head_min, o.head); s.head_max = std::max(s.head_max, o.head);
        if (o.face) { s.faces++; s.face_list.push_back(o.face); }
    }
    return s;
}

static void test_emotions() {
    ControllerConfig cfg; cfg.wander = true;
    { // HAPPY: spins until emotion changes
        RobotController c(cfg, 1); c.SetEmotion("happy");
        Stats s = Run(c, In(Hazard::kNone, false, false), 12000);
        CHECK(s.spin_ms >= 8000, "happy spin_ms=%d", s.spin_ms);
        CHECK(s.head_max - s.head_min >= 40, "happy head bobbing range %.1f", s.head_max - s.head_min);
        c.SetEmotion("neutral");
        Stats s2 = Run(c, In(Hazard::kNone, false, false), 1000);
        CHECK(s2.nonzero_ms == 0, "neutral stops motors (non-idle) nz=%d", s2.nonzero_ms);
    }
    { // SAD: reverses (limited), head down
        RobotController c(cfg, 2); c.SetEmotion("sad");
        Stats s = Run(c, In(Hazard::kNone, false, false), 15000);
        CHECK(s.rev_ms > 500 && s.rev_ms <= 1900, "sad reverse total %d ms (budget 1800)", s.rev_ms);
        CHECK(s.max_contig_rev <= 700, "sad max contiguous reverse %d", s.max_contig_rev);
        CHECK(s.head_max < -55, "sad head stays down max=%.1f", s.head_max);
        CHECK(s.fwd_ms == 0, "sad never forward");
    }
    { // ANGRY: slow advance, head up
        RobotController c(cfg, 3); c.SetEmotion("angry");
        Stats s = Run(c, In(Hazard::kNone, false, false), 8000);
        CHECK(s.fwd_ms >= 2000, "angry advances fwd_ms=%d", s.fwd_ms);
        CHECK(s.max_fwd <= 70, "angry slow max_fwd=%d", s.max_fwd);
        CHECK(s.head_max >= 70, "angry head up max=%.1f", s.head_max);
    }
    { // SLEEPY: back and forth slowly, head nodding
        RobotController c(cfg, 4); c.SetEmotion("sleepy");
        Stats s = Run(c, In(Hazard::kNone, false, false), 12000);
        CHECK(s.fwd_ms > 500 && s.rev_ms > 500, "sleepy fwd=%d rev=%d", s.fwd_ms, s.rev_ms);
        CHECK(s.max_fwd <= 40 && s.min_fwd >= -40, "sleepy slow [%d,%d]", s.min_fwd, s.max_fwd);
        CHECK(s.head_max - s.head_min >= 50, "sleepy nod range %.1f", s.head_max - s.head_min);
    }
    { // unknown emotion ignored, known list all resolve
        RobotController c(cfg, 5);
        c.SetEmotion("happy"); c.SetEmotion("banana");
        CHECK(strcmp(c.BehaviorName(), "happy") == 0 || strcmp(c.BehaviorName(), "") == 0, "unknown label ignored");
        const char* all[] = {"happy","laughing","funny","silly","sad","crying","angry","sleepy","surprised","shocked","confused","embarrassed","loving","kissy","thinking","cool","confident","relaxed","winking","delicious"};
        for (auto e : all) { bool k; const Behavior* b = FindEmotionBehavior(e, &k); CHECK(k && b, "emotion %s mapped", e); }
        bool k; CHECK(FindEmotionBehavior("neutral", &k) == nullptr && k, "neutral known & null");
    }
    { // same emotion repeated keeps phase (not restarted)
        RobotController c(cfg, 6); c.SetEmotion("happy"); Run(c, In(), 1000);
        const char* before = c.BehaviorName(); c.SetEmotion("happy");
        CHECK(strcmp(before, c.BehaviorName()) == 0, "repeat ok");
    }
    { // emotion times out
        ControllerConfig c2 = cfg; c2.emotion_max_ms = 3000;
        RobotController c(c2, 7); c.SetEmotion("happy");
        Stats s = Run(c, In(Hazard::kNone, false, false), 6000);
        CHECK(s.still_ms >= 2500, "emotion expires, still=%d", s.still_ms);
        CHECK(s.faces >= 1 && s.face_list.back() == "neutral", "face neutral restored on timeout");
    }
}

static void test_safety() {
    ControllerConfig cfg;
    { // angry advance + cliff appears -> never net forward while hazard, reflex runs
        RobotController c(cfg, 11); c.SetEmotion("angry");
        Run(c, In(Hazard::kNone), 600);                          // already advancing
        int bad = 0; bool saw_reflex = false; int rev = 0, spin = 0;
        for (int t = 0; t < 4000; t += 20) {
            ControllerInput in = In(t < 100 ? Hazard::kCliff : Hazard::kNone);  // single hazard event
            ControllerOutput o = c.Tick(in);
            if (in.hazard != Hazard::kNone && o.left + o.right > 0) bad++;
            if (strcmp(c.ModeName(), "reflex") == 0) saw_reflex = true;
            if (o.left + o.right < 0) rev += 20;
            if (o.left * o.right < 0) spin += 20;
        }
        CHECK(bad == 0, "net-forward ticks while cliff = %d", bad);
        CHECK(saw_reflex, "reflex engaged");
        CHECK(rev > 0 && rev <= 450, "reflex reverse short: %d", rev);
        CHECK(spin > 0, "reflex turns away");
    }
    { // very first tick with a cliff already present and intent forward: no forward output at all
        RobotController c(cfg, 12); c.SetEmotion("angry");
        int bad = 0; ControllerInput in = In(Hazard::kCliff);
        for (int t = 0; t < 3000; t += 20) { auto o = c.Tick(in); if (o.left + o.right > 0) bad++; }
        CHECK(bad == 0, "cold start at cliff, forward ticks %d", bad);
    }
    { // hazard kUnknown (not calibrated): no forward
        RobotController c(cfg, 13); c.ManualMove(60, 60, 3000);
        int bad = 0; ControllerInput in = In(Hazard::kUnknown);
        for (int t = 0; t < 3000; t += 20) { auto o = c.Tick(in); if (o.left + o.right > 0) bad++; }
        CHECK(bad == 0, "unknown hazard blocks forward (%d)", bad);
    }
    { // manual forward into obstacle: cancelled by reflex
        RobotController c(cfg, 14); c.ManualMove(60, 60, 5000);
        ControllerInput in = In(Hazard::kObstacle); int bad = 0;
        for (int t = 0; t < 2000; t += 20) { auto o = c.Tick(in); if (o.left + o.right > 0) bad++; }
        CHECK(bad == 0, "manual forward blocked");
    }
    { // manual forward OK when clear
        RobotController c(cfg, 15); c.ManualMove(60, 60, 1000);
        Stats s = Run(c, In(Hazard::kNone), 1000);
        CHECK(s.fwd_ms >= 900, "manual forward works when clear (%d)", s.fwd_ms);
    }
    { // sensor dead => wheels never move (default config), head still works
        RobotController c(cfg, 16); c.SetEmotion("happy"); c.ManualMove(60, 60, 3000);
        Stats s = Run(c, In(Hazard::kSensorFault, true, false, false), 5000);
        CHECK(s.nonzero_ms == 0, "no motion without guard (%d)", s.nonzero_ms);
        CHECK(s.head_max - s.head_min >= 30, "head still animates without guard");
    }
    { // allow_no_sensor lets wheels move (bench test)
        ControllerConfig c2 = cfg; c2.allow_no_sensor = true;
        RobotController c(c2, 17); c.ManualMove(60, 60, 1000);
        Stats s = Run(c, In(Hazard::kSensorFault, false, false, false), 1000);
        CHECK(s.fwd_ms > 500, "bench mode drives (%d)", s.fwd_ms);
    }
    { // listening gate
        RobotController c(cfg, 18); c.SetEmotion("happy");
        Stats s = Run(c, In(Hazard::kNone, false, true), 4000);
        CHECK(s.nonzero_ms == 0, "silent while listening (%d)", s.nonzero_ms);
        CHECK(s.head_max - s.head_min >= 30, "head still moves while listening");
        ControllerConfig c2 = cfg; c2.stop_when_listening = false;
        RobotController d(c2, 18); d.SetEmotion("happy");
        Stats s2 = Run(d, In(Hazard::kNone, false, true), 4000);
        CHECK(s2.nonzero_ms > 3000, "gate can be disabled");
    }
    { // blind reverse disabled
        ControllerConfig c2 = cfg; c2.blind_reverse = false;
        RobotController c(c2, 19); c.SetEmotion("sad");
        Stats s = Run(c, In(), 8000);
        CHECK(s.rev_ms == 0, "no reverse when disabled (%d)", s.rev_ms);
    }
    { // contiguous reverse cap via manual
        RobotController c(cfg, 20); c.ManualMove(-60, -60, 5000);
        Stats s = Run(c, In(), 5000);
        CHECK(s.max_contig_rev <= 720, "manual reverse capped: %d", s.max_contig_rev);
    }
    { // stuck: persistent cliff + wander => eventually stuck / still
        RobotController c(cfg, 21); c.SetEmotion("angry");
        ControllerInput in = In(Hazard::kCliff);
        bool stuck = false; int bad = 0;
        for (int t = 0; t < 60000; t += 20) {
            auto o = c.Tick(in); if (o.left + o.right > 0) bad++;
            if (strcmp(c.ModeName(), "stuck") == 0) stuck = true;
        }
        CHECK(bad == 0, "persistent cliff never forward (%d)", bad);
        CHECK(stuck, "stuck state reached after repeated reflexes");
    }
    { // face reaction
        RobotController c(cfg, 22); c.SetEmotion("angry");
        Stats s = Run(c, In(Hazard::kCliff), 100);
        Stats s_b = Run(c, In(Hazard::kNone), 3500);
        s.faces += s_b.faces; for (auto& f : s_b.face_list) s.face_list.push_back(f);
        CHECK(s.faces >= 2, "face on reflex start and end (%d)", s.faces);
        CHECK(s.face_list.front() == "shocked", "shocked face first, got %s", s.face_list.front().c_str());
        CHECK(s.face_list.back() == "angry", "restore last emotion, got %s", s.face_list.back().c_str());
    }
}

static void test_wander() {
    ControllerConfig cfg;
    RobotController c(cfg, 31);
    Stats s = Run(c, In(Hazard::kNone, true), 60000);
    CHECK(s.fwd_ms > 2000, "wander drives forward (%d)", s.fwd_ms);
    CHECK(s.spin_ms > 1500, "wander spins (%d)", s.spin_ms);
    CHECK(s.rev_ms > 100, "wander shuffles back (%d)", s.rev_ms);
    CHECK(s.still_ms > 10000, "wander pauses (%d)", s.still_ms);
    // not idle => no wander
    RobotController d(cfg, 32);
    Stats s2 = Run(d, In(Hazard::kNone, false), 20000);
    CHECK(s2.nonzero_ms == 0, "no wander when not idle");
    // wander disabled
    ControllerConfig c3 = cfg; c3.wander = false; RobotController e(c3, 33);
    Stats s3 = Run(e, In(Hazard::kNone, true), 20000);
    CHECK(s3.nonzero_ms == 0, "wander disabled");
    // wander with cliff never goes forward
    RobotController f(cfg, 34); int bad = 0; ControllerInput in = In(Hazard::kCliff, true);
    for (int t = 0; t < 60000; t += 20) { auto o = f.Tick(in); if (o.left + o.right > 0) bad++; }
    CHECK(bad == 0, "wander + cliff never forward (%d)", bad);
}

static void test_head() {
    ControllerConfig cfg; RobotController c(cfg, 41);
    c.ManualHead(-80, 1000);
    ControllerOutput o = c.Tick(In());
    CHECK(o.head == -80, "head override");
    for (int i = 0; i < 60; i++) o = c.Tick(In());
    CHECK(o.head == 0, "head returns to rest after hold (%.1f)", o.head);
    o = c.Tick(In(Hazard::kNone, false, true));
    CHECK(o.head == 15, "listening head pose");
}

int main() {
    test_classifier(); test_floor(); test_helpers(); test_emotions(); test_safety(); test_wander(); test_head();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
