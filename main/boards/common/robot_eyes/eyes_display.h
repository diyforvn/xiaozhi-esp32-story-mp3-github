// eyes_display.h
//
// Display cho fork xiaozhi-esp32 (LVGL 9, LvglDisplay -> LcdDisplay -> SpiLcdDisplay):
// thay emoji mặc định bằng mắt "Terminator", GIỮ NGUYÊN thanh trạng thái,
// thông báo và phụ đề của xiaozhi.
//
// Cách tự khởi động: ghi đè SetupUI() (Application::Initialize gọi sau khi
// board tạo display) -> không cần sửa application.cc.
// Tự bắt trạng thái nghe/nói qua SetStatus(Lang::Strings::...), và tạm dừng
// render khi SetPowerSaveMode(true).
//
// Không dùng với CONFIG_USE_EMOTE_MESSAGE_STYLE (kiểu hiển thị khác, không có LVGL).
#pragma once

#include <atomic>
#include <mutex>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#if __has_include("display/lcd_display.h")
#include "display/lcd_display.h"
#else
#include "lcd_display.h"
#endif

#include "eyes_render.h"

#ifndef EYES_TARGET_FPS
#define EYES_TARGET_FPS 30
#endif
#ifndef EYES_DOUBLE_BUFFER
// 1: 4 buffer x 32KB (chống xé hình). 0: 2 buffer x 32KB (board ít RAM).
#define EYES_DOUBLE_BUFFER 1
#endif
#ifndef EYES_TASK_PRIORITY
// Thấp hơn task audio và task LVGL để không làm giật tiếng.
#define EYES_TASK_PRIORITY 3
#endif
#ifndef EYES_TASK_CORE
#define EYES_TASK_CORE tskNO_AFFINITY
#endif
#ifndef EYES_Y_OFFSET
// Dịch dải mắt lên (âm) / xuống (dương) so với giữa màn hình, đơn vị pixel.
#define EYES_Y_OFFSET 0
#endif
#ifndef EYES_LOG_STATS
#define EYES_LOG_STATS 1  // log FPS / thời gian render mỗi 5 giây (để tinh chỉnh)
#endif

enum class EyesActivity { kIdle, kListening, kSpeaking };

class EyesDisplay : public SpiLcdDisplay {
 public:
  // Dùng lại nguyên constructor 9 tham số của SpiLcdDisplay.
  using SpiLcdDisplay::SpiLcdDisplay;
  ~EyesDisplay();

  // Dựng UI mặc định rồi chồng dải mắt lên.
  void SetupUI() override;

  // Giữ UI mặc định, đồng thời suy ra hoạt động (nghe/nói/rảnh) từ trạng thái.
  void SetStatus(const char* status) override;

  // Ghi đè: map cảm xúc xiaozhi -> biểu cảm mắt robot.
  void SetEmotion(const char* emotion) override;

  // Tiết kiệm điện: dừng render khi vào power save.
  void SetPowerSaveMode(bool on) override;

  // Gọi tay nếu cần (SetupUI đã tự gọi). Thất bại (thiếu RAM...) thì giữ emoji mặc định.
  void StartEyes();
  void StopEyes();

  void SetActivity(EyesActivity activity);
  void SetEyesPaused(bool paused) { paused_ = paused; }

 private:
  static constexpr int kBuffers = EYES_DOUBLE_BUFFER ? 2 : 1;
  static constexpr size_t kBufBytes = eyes::kEyeSize * eyes::kEyeSize * sizeof(uint16_t);

  bool AllocBuffers();
  void FreeBuffers();
  void BuildWidgets();
  void EyeLoop();
  static void EyeTaskThunk(void* arg);

  eyes::Engine engine_;

  std::mutex state_mu_;
  EyesActivity activity_ = EyesActivity::kIdle;
  char last_emotion_[24] = {0};

  uint16_t* buf_[2][2] = {{nullptr, nullptr}, {nullptr, nullptr}};  // [mắt][buffer]
  lv_obj_t* root_ = nullptr;
  lv_obj_t* canvas_[2] = {nullptr, nullptr};
  bool two_eyes_ = true;

  TaskHandle_t task_ = nullptr;
  SemaphoreHandle_t done_sem_ = nullptr;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_req_{false};
  std::atomic<bool> paused_{false};
};