// eyes_render.h
//
// Adapter cho TerminatorEyeFX:
//   - Đầu ra là buffer RGB565 128x128 cho mỗi mắt (không đụng tới SPI/panel).
//   - Không chặn: mỗi lần gọi Step() chỉ tính 1 frame.
//   - Điều khiển cảm xúc và trạng thái nghe từ lớp Display.
//
// Lưu ý license: code gốc và repo port có license riêng (repo ghi GPL-3.0).
// Kiểm tra trước khi phát hành firmware.
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

namespace eyes {

constexpr int kEyeSize = 128;  // mỗi mắt 128x128 pixel (cố định theo bộ dữ liệu)

class Engine {
 public:
  Engine();
  ~Engine();

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // An toàn gọi từ task bất kỳ.
  void SetEmotion(const char* emotion);
  void SetAttention(bool attention);
  void TriggerBlink();

  // Tính 1 frame. `left` bắt buộc; `right` = nullptr nếu chỉ dùng 1 mắt.
  // Mỗi buffer có kEyeSize*kEyeSize phần tử uint16_t.
  // Chỉ gọi từ 1 task duy nhất.
  void Step(int64_t now_us, uint16_t* left, uint16_t* right);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  std::mutex state_mu_;
  char emotion_[24] = "neutral";
  bool attention_ = false;
  bool force_blink_ = false;
};

}  // namespace eyes
