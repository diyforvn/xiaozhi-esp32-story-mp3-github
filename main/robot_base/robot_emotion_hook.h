// Tiny hook so the robot base can follow the emotion that xiaozhi sends to the display,
// regardless of the display type (OLED Mochi face or TFT robot eyes).
// application.cc calls RobotEmotionHook::Notify(...) right after display->SetEmotion(...).
#pragma once
#include <functional>
#include <string>
#include <utility>

#include "device_state.h"

namespace RobotEmotionHook {
using Handler = std::function<void(const char*)>;
using StateHandler = std::function<void(DeviceState)>;
using EmotionResolver = std::function<std::string(const char*)>;
inline Handler& handler() {
    static Handler h;
    return h;
}
inline StateHandler& state_handler() {
    static StateHandler h;
    return h;
}
inline EmotionResolver& emotion_resolver() {
    static EmotionResolver resolver;
    return resolver;
}
inline void Set(Handler h) { handler() = std::move(h); }
inline void SetStateHandler(StateHandler h) { state_handler() = std::move(h); }
inline void SetEmotionResolver(EmotionResolver resolver) { emotion_resolver() = std::move(resolver); }
inline std::string ResolveEmotion(const char* emotion) {
    if (emotion_resolver()) return emotion_resolver()(emotion);
    return emotion ? emotion : "neutral";
}
inline void Notify(const char* emotion) {
    if (handler()) handler()(emotion);
}
inline void NotifyState(DeviceState state) {
    if (state_handler()) state_handler()(state);
}
}  // namespace RobotEmotionHook
