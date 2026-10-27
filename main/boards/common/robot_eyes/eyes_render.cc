// eyes_render.cc

#include "eyes_render.h"

#include <cmath>
#include <cstring>

#ifndef PROGMEM
#define PROGMEM
#endif

#ifndef pgm_read_byte
#define pgm_read_byte(address) (*reinterpret_cast<const uint8_t*>(address))
#endif
#ifndef pgm_read_word
#define pgm_read_word(address) (*reinterpret_cast<const uint16_t*>(address))
#endif

// Each image header defines the same global image symbols, so only one style
// may be included in this translation unit.
#if defined(EYES_STYLE_CAT)
#include "data/catEye.h"
#include "data/catEyeFX.h"
using EyeType = CatEye;
#elif defined(EYES_STYLE_DEFAULT)
#include "data/defaultEye.h"
#include "data/defaultEyeFX.h"
using EyeType = DefaultEye;
#elif defined(EYES_STYLE_DRAGON)
#include "data/dragonEye.h"
#include "data/dragonEyeFX.h"
using EyeType = DragonEye;
#elif defined(EYES_STYLE_GOAT)
#include "data/goatEye.h"
#include "data/goatEyeFX.h"
using EyeType = GoatEye;
#elif defined(EYES_STYLE_NAUGA)
#include "data/naugaEye.h"
#include "data/naugaEyeFX.h"
using EyeType = NaugaEye;
#elif defined(EYES_STYLE_NEWT)
#include "data/newtEye.h"
#include "data/newtEyeFX.h"
using EyeType = NewtEye;
#elif defined(EYES_STYLE_NOSCLERA)
#include "data/noScleraEye.h"
#include "data/noScleraEyeFX.h"
using EyeType = NoScleraEye;
#elif defined(EYES_STYLE_OWL)
#include "data/owlEye.h"
#include "data/owlEyeFX.h"
using EyeType = OwlEye;
#else
#include "data/terminatorEye.h"
#include "data/terminatorEyeFX.h"
using EyeType = TerminatorEye;
#endif

static_assert(SCREEN_WIDTH == eyes::kEyeSize && SCREEN_HEIGHT == eyes::kEyeSize,
              "Bo du lieu mat phai la 128x128");

namespace eyes {

struct Engine::Impl {
  EyeType left;
  EyeType right;
  EyeEmotion applied_emotion = EYE_NEUTRAL;
  bool applied_attention = false;
  bool initialized = false;
};

Engine::Engine() : impl_(std::make_unique<Impl>()) {}

Engine::~Engine() = default;

void Engine::SetEmotion(const char* emotion) {
  std::lock_guard<std::mutex> lock(state_mu_);
  if (emotion == nullptr) emotion = "neutral";
  std::strncpy(emotion_, emotion, sizeof(emotion_) - 1);
  emotion_[sizeof(emotion_) - 1] = '\0';
}

void Engine::SetAttention(bool attention) {
  std::lock_guard<std::mutex> lock(state_mu_);
  attention_ = attention;
}

void Engine::TriggerBlink() {
  std::lock_guard<std::mutex> lock(state_mu_);
  force_blink_ = true;
}

void Engine::Step(int64_t now_us, uint16_t* left, uint16_t* right) {
  if (left == nullptr) return;

  char emotion_name[sizeof(emotion_)];
  bool attention;
  bool force_blink;
  {
    std::lock_guard<std::mutex> lock(state_mu_);
    std::memcpy(emotion_name, emotion_, sizeof(emotion_name));
    attention = attention_;
    force_blink = force_blink_;
    force_blink_ = false;
  }

  EyeEmotion emotion = EYE_NEUTRAL;
  eyeEmotionFromName(emotion_name, emotion);
  const uint32_t now_ms = static_cast<uint32_t>(now_us / 1000);

  if (!impl_->initialized) {
    impl_->left.begin(false);
    impl_->right.begin(true);
    impl_->left.setWinkEye(true);
    impl_->right.setWinkEye(false);
    impl_->left.setEmotion(emotion);
    impl_->right.setEmotion(emotion);
    impl_->applied_emotion = emotion;
    impl_->initialized = true;
  } else if (impl_->applied_emotion != emotion) {
    impl_->left.setEmotion(emotion);
    impl_->right.setEmotion(emotion);
    impl_->applied_emotion = emotion;
  }

  if (impl_->applied_attention != attention) {
    impl_->left.setAutoLook(!attention);
    impl_->right.setAutoLook(!attention);
    impl_->applied_attention = attention;
  }

  if (attention) {
    impl_->left.lookAt(0.0f, 0.0f);
    impl_->right.lookAt(0.0f, 0.0f);
  }
  if (force_blink) {
    impl_->left.blink();
    impl_->right.blink();
  }

  impl_->left.update(now_ms);
  impl_->left.render(left);
  if (right != nullptr) {
    impl_->right.update(now_ms);
    impl_->right.render(right);
  }
}

}  // namespace eyes
