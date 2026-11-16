// Emotion / idle / reflex behavior tables.
//
// Step = { left%, right%, head pose, osc amplitude, osc period ms, duration ms, random extra ms, flags }
//   wheels : -100..100 (negative = reverse)
//   head   : -100 (head down) .. +100 (head up);  osc = sinusoidal wobble around the pose
//   kFlagMirror : left/right swapped at random each time the step starts (random turn direction)
//
// Edit the numbers here to tune the personality. Everything goes through the safety layer
// (laser guard, reverse caps) in RobotController, so tables never bypass safety.
#include <string.h>

#include "robot_logic.h"

namespace robot {

#define M kFlagMirror

// ---- Emotions asked for ---------------------------------------------------
// HAPPY: face happy, spin in place until the emotion changes, head bobbing excitedly.
static const Step kHappySteps[] = {
    {+55, -55, +25, 30, 420, 2600, 800, M},   // spin (random direction)
    {+50, +50, +30, 30, 420, 260, 0, 0},      // little hop forward (laser-guarded)
    {-50, -50, +30, 30, 420, 260, 0, 0},      // hop back (blind, short)
};
// SAD: back away slowly, head bowed, sighing pauses.
static const Step kSadSteps[] = {
    {-30, -30, -70, 8, 1600, 650, 0, 0},
    {0, 0, -95, 0, 0, 1100, 400, 0},
    {-25, -25, -80, 6, 1800, 500, 0, 0},
    {0, 0, -100, 0, 0, 1600, 600, 0},
};
// ANGRY: advance slowly, chin up defiantly, glare, head slam, small stomp-turn.
static const Step kAngrySteps[] = {
    {+28, +28, +75, 5, 110, 1400, 400, 0},
    {0, 0, +90, 4, 100, 600, 300, 0},
    {0, 0, -40, 0, 0, 280, 0, 0},
    {0, 0, +85, 0, 0, 500, 0, 0},
    {+30, -30, +75, 0, 0, 250, 150, M},
};
// SLEEPY: creep back and forth slowly, head nodding off.
static const Step kSleepySteps[] = {
    {+18, +18, -20, 35, 2800, 800, 0, 0},
    {0, 0, -35, 35, 2800, 900, 300, 0},
    {-18, -18, -25, 35, 2800, 800, 0, 0},
    {0, 0, -45, 35, 2800, 1400, 500, 0},
};

// ---- Extra emotions -------------------------------------------------------
static const Step kLaughSteps[] = {
    {+60, -60, +30, 50, 300, 700, 300, M},    // fast shimmy-spin
    {+40, +40, +30, 50, 300, 200, 0, 0},
    {-40, -40, +30, 50, 300, 200, 0, 0},
};
static const Step kCrySteps[] = {
    {-25, -25, -80, 10, 140, 500, 0, 0},      // trembling head
    {0, 0, -90, 10, 140, 1500, 500, 0},
};
static const Step kSurprisedSteps[] = {
    {-60, -60, +100, 0, 0, 280, 0, 0},        // jump back
    {0, 0, +100, 0, 0, 700, 0, 0},            // freeze
    {0, 0, +60, 12, 260, 1200, 0, 0},         // look around, trembling
};
static const Step kConfusedSteps[] = {
    {+35, -35, +30, 0, 0, 350, 0, 0},
    {-35, +35, -10, 0, 0, 700, 0, 0},
    {+35, -35, +20, 0, 0, 350, 0, 0},
    {0, 0, +20, 25, 900, 900, 0, 0},
};
static const Step kEmbarrassedSteps[] = {
    {-25, -25, -60, 0, 0, 350, 0, 0},
    {+30, -30, -75, 0, 0, 450, 200, M},       // turn away
    {0, 0, -85, 8, 200, 1500, 0, 0},
};
static const Step kLovingSteps[] = {
    {+20, +20, +15, 15, 1800, 1300, 300, 0},  // approach softly, swaying
    {0, 0, +20, 20, 1200, 1200, 300, 0},
    {+30, -30, +10, 15, 1400, 500, 200, M},   // little twirl
};
static const Step kThinkingSteps[] = {
    {0, 0, +30, 10, 2200, 2500, 1000, 0},
};
static const Step kCoolSteps[] = {
    {+35, +35, +15, 0, 0, 1600, 400, 0},
    {+40, +22, +20, 0, 0, 1400, 300, M},
    {0, 0, +30, 0, 0, 700, 300, 0},
};
static const Step kRelaxedSteps[] = {
    {+22, +22, 0, 12, 2600, 2000, 500, 0},
    {0, 0, -5, 12, 2600, 1500, 500, 0},
};
static const Step kWinkSteps[] = {
    {0, 0, -35, 0, 0, 220, 0, 0},
    {0, 0, +15, 0, 0, 900, 0, 0},
};
static const Step kDeliciousSteps[] = {
    {+35, -35, +15, 25, 500, 1200, 400, M},
    {0, 0, +10, 25, 500, 800, 300, 0},
};
static const Step kSillySteps[] = {
    {+60, +20, +25, 40, 350, 420, 0, 0},      // zig-zag
    {+20, +60, +25, 40, 350, 420, 0, 0},
};

//                      name         steps              n  loop  max_rev  max_total
static const Behavior kHappy      = {"happy",      kHappySteps,      3, true,  0,    0};
static const Behavior kSad        = {"sad",        kSadSteps,        4, true,  1800, 0};
static const Behavior kAngry      = {"angry",      kAngrySteps,      5, true,  0,    0};
static const Behavior kSleepy     = {"sleepy",     kSleepySteps,     4, true,  1600, 0};
static const Behavior kLaugh      = {"laughing",   kLaughSteps,      3, true,  0,    0};
static const Behavior kCry        = {"crying",     kCrySteps,        2, true,  1000, 0};
static const Behavior kSurprised  = {"surprised",  kSurprisedSteps,  3, false, 300,  0};
static const Behavior kConfused   = {"confused",   kConfusedSteps,   4, true,  0,    0};
static const Behavior kEmbarrassed= {"embarrassed",kEmbarrassedSteps,3, false, 400,  0};
static const Behavior kLoving     = {"loving",     kLovingSteps,     3, true,  0,    0};
static const Behavior kThinking   = {"thinking",   kThinkingSteps,   1, true,  0,    0};
static const Behavior kCool       = {"cool",       kCoolSteps,       3, true,  0,    0};
static const Behavior kRelaxed    = {"relaxed",    kRelaxedSteps,    2, true,  0,    0};
static const Behavior kWink       = {"winking",    kWinkSteps,       2, false, 0,    0};
static const Behavior kDelicious  = {"delicious",  kDeliciousSteps,  2, true,  0,    0};
static const Behavior kSilly      = {"silly",      kSillySteps,      2, true,  0,    0};

struct EmotionEntry { const char* name; const Behavior* b; };
static const EmotionEntry kEmotionMap[] = {
    {"happy", &kHappy},        {"laughing", &kLaugh},     {"funny", &kSilly},
    {"silly", &kSilly},        {"sad", &kSad},            {"crying", &kCry},
    {"angry", &kAngry},        {"sleepy", &kSleepy},      {"surprised", &kSurprised},
    {"shocked", &kSurprised},  {"confused", &kConfused},  {"embarrassed", &kEmbarrassed},
    {"loving", &kLoving},      {"kissy", &kLoving},       {"thinking", &kThinking},
    {"cool", &kCool},          {"confident", &kCool},     {"relaxed", &kRelaxed},
    {"winking", &kWink},       {"delicious", &kDelicious},
};

const Behavior* FindEmotionBehavior(const char* emotion, bool* known) {
    if (known) *known = true;
    if (!emotion || !*emotion || strcmp(emotion, "neutral") == 0) return nullptr;
    for (const auto& e : kEmotionMap) {
        if (strcmp(e.name, emotion) == 0) return e.b;
    }
    if (known) *known = false;
    return nullptr;
}

uint8_t DefaultEmotionIntensity(const char* emotion) {
    if (!emotion) return 65;
    if (strcmp(emotion, "angry") == 0 || strcmp(emotion, "surprised") == 0 ||
        strcmp(emotion, "shocked") == 0 || strcmp(emotion, "laughing") == 0) return 90;
    if (strcmp(emotion, "happy") == 0 || strcmp(emotion, "silly") == 0 ||
        strcmp(emotion, "funny") == 0 || strcmp(emotion, "delicious") == 0) return 80;
    if (strcmp(emotion, "thinking") == 0 || strcmp(emotion, "relaxed") == 0 ||
        strcmp(emotion, "sleepy") == 0 || strcmp(emotion, "cool") == 0 ||
        strcmp(emotion, "confident") == 0) return 45;
    if (strcmp(emotion, "neutral") == 0) return 0;
    return 65;
}

// ---- Reflexes (laser guard) ----------------------------------------------
// Short blind back-up (<= 400 ms), then turn away in a random direction.
static const Step kReflexCliffSteps[] = {
    {-35, -35, +40, 0, 0, 380, 0, 0},
    {+45, -45, 0, 0, 0, 600, 500, M},
    {0, 0, 0, 0, 0, 150, 0, 0},
};
static const Step kReflexObstacleSteps[] = {
    {-30, -30, +20, 0, 0, 300, 0, 0},
    {+45, -45, 0, 0, 0, 400, 400, M},
    {0, 0, 0, 0, 0, 120, 0, 0},
};
const Behavior kReflexCliff = {"reflex_cliff", kReflexCliffSteps, 3, false, 450, 0};
const Behavior kReflexObstacle = {"reflex_obstacle", kReflexObstacleSteps, 3, false, 350, 0};

// ---- Idle wandering ("đi qua đi lại, xoay tròn ngẫu nhiên") ---------------
static const Step kWRollSteps[] = {{+30, +30, 0, 0, 0, 900, 1100, 0}};
static const Step kWTurnSteps[] = {{+38, -38, 0, 0, 0, 300, 500, M}};
static const Step kWSpinSteps[] = {{+45, -45, +10, 0, 0, 1000, 1000, M}};
static const Step kWShuffleSteps[] = {
    {+25, +25, 0, 0, 0, 400, 0, 0},
    {-25, -25, 0, 0, 0, 350, 0, 0},
};
static const Step kWLookSteps[] = {
    {0, 0, +25, 0, 0, 700, 400, 0},
    {0, 0, -20, 0, 0, 700, 400, 0},
    {0, 0, +5, 0, 0, 500, 0, 0},
};
static const Behavior kWRoll = {"wander_roll", kWRollSteps, 1, false, 0, 0};
static const Behavior kWTurn = {"wander_turn", kWTurnSteps, 1, false, 0, 0};
static const Behavior kWSpin = {"wander_spin", kWSpinSteps, 1, false, 0, 0};
static const Behavior kWShuffle = {"wander_shuffle", kWShuffleSteps, 2, false, 400, 0};
static const Behavior kWLook = {"wander_look", kWLookSteps, 3, false, 0, 0};

const Behavior* const kWanderBehaviors[] = {&kWRoll, &kWTurn, &kWSpin, &kWShuffle, &kWLook};
const uint8_t kWanderWeights[] = {3, 3, 2, 1, 2};
const size_t kWanderCount = sizeof(kWanderBehaviors) / sizeof(kWanderBehaviors[0]);

}  // namespace robot
