// eyes_display.cc
#include "eyes_display.h"

#include <cstring>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

#if __has_include("assets/lang_config.h")
#include "assets/lang_config.h"
#define EYES_HAS_LANG 1
#else
#define EYES_HAS_LANG 0
#endif

static const char* TAG = "EyesDisplay";

// --- Shim LVGL 8 / 9 (fork này dùng LVGL 9) --------------------------------
#if LVGL_VERSION_MAJOR >= 9
#define EYES_SCR() lv_screen_active()
#define EYES_CANVAS_SET(c, b) \
  lv_canvas_set_buffer((c), (b), eyes::kEyeSize, eyes::kEyeSize, LV_COLOR_FORMAT_RGB565)
#define EYES_CANVAS_SCALE(c, sx, sy) \
  do { \
    lv_image_set_scale_x((c), (sx)); \
    lv_image_set_scale_y((c), (sy)); \
  } while (0)
#define EYES_NO_SCROLL(o) lv_obj_remove_flag((o), LV_OBJ_FLAG_SCROLLABLE)
#define EYES_DEL(o) lv_obj_delete(o)
#else
#define EYES_SCR() lv_scr_act()
#define EYES_CANVAS_SET(c, b) \
  lv_canvas_set_buffer((c), (b), eyes::kEyeSize, eyes::kEyeSize, LV_IMG_CF_TRUE_COLOR)
#define EYES_CANVAS_SCALE(c, sx, sy) lv_img_set_zoom((c), ((sx) + (sy)) / 2)
#define EYES_NO_SCROLL(o) lv_obj_clear_flag((o), LV_OBJ_FLAG_SCROLLABLE)
#define EYES_DEL(o) lv_obj_del(o)
#endif

// ---------------------------------------------------------------------------

EyesDisplay::~EyesDisplay() { StopEyes(); }

void EyesDisplay::SetupUI() {
  SpiLcdDisplay::SetupUI();  // dựng status bar, phụ đề, emoji mặc định...
  StartEyes();               // rồi chồng dải mắt lên
}

void EyesDisplay::SetStatus(const char* status) {
  SpiLcdDisplay::SetStatus(status);
#if EYES_HAS_LANG
  if (!running_ || status == nullptr) return;
  if (std::strcmp(status, Lang::Strings::LISTENING) == 0) {
    SetActivity(EyesActivity::kListening);
  } else if (std::strcmp(status, Lang::Strings::SPEAKING) == 0) {
    SetActivity(EyesActivity::kSpeaking);
  } else if (std::strcmp(status, Lang::Strings::STANDBY) == 0) {
    SetActivity(EyesActivity::kIdle);
  }
#endif
}

void EyesDisplay::SetPowerSaveMode(bool on) {
  SpiLcdDisplay::SetPowerSaveMode(on);
  paused_ = on;
}

bool EyesDisplay::AllocBuffers() {
  const int eyes_n = 2;
  for (int e = 0; e < eyes_n; ++e) {
    for (int b = 0; b < kBuffers; ++b) {
      void* m = heap_caps_aligned_alloc(64, kBufBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (m == nullptr) {
        m = heap_caps_aligned_alloc(64, kBufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      }
      if (m == nullptr) {
        ESP_LOGE(TAG, "Khong du RAM cho buffer mat (%u byte)", (unsigned)kBufBytes);
        FreeBuffers();
        return false;
      }
      std::memset(m, 0, kBufBytes);  // 0 = den
      buf_[e][b] = static_cast<uint16_t*>(m);
    }
  }
  return true;
}

void EyesDisplay::FreeBuffers() {
  for (auto& eye : buf_) {
    for (auto& b : eye) {
      if (b != nullptr) {
        heap_caps_free(b);
        b = nullptr;
      }
    }
  }
}

void EyesDisplay::BuildWidgets() {
  // Gọi khi đang giữ khoá LVGL, sau khi LcdDisplay::SetupUI() đã chạy.
  lv_obj_t* scr = EYES_SCR();
  const int32_t W = lv_obj_get_width(scr);
  const int32_t H = lv_obj_get_height(scr);
  two_eyes_ = (W >= 2 * eyes::kEyeSize);  // ILI9341 320x240 -> 2 mắt

  // Ẩn emoji mặc định, giữ nguyên status bar / thông báo / phụ đề.
  for (lv_obj_t* o : {emoji_box_, emoji_label_, emoji_image_}) {
    if (o != nullptr) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  }

  // Dải đen full-width, cao đúng bằng 1 mắt, đặt giữa màn hình.
  root_ = lv_obj_create(scr);
  lv_obj_remove_style_all(root_);
  lv_obj_set_size(root_, W, H);
  lv_obj_align(root_, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
  EYES_NO_SCROLL(root_);

  const int n = two_eyes_ ? 2 : 1;
  for (int i = 0; i < n; ++i) {
    canvas_[i] = lv_canvas_create(root_);
    EYES_CANVAS_SET(canvas_[i], buf_[i][0]);
    const int32_t eye_w = two_eyes_ ? (W + 1) / 2 : W;
    const uint32_t scale_x = (uint32_t)((eye_w * 256 + eyes::kEyeSize / 2) / eyes::kEyeSize);
    const uint32_t scale_y = (uint32_t)((H * 256 + eyes::kEyeSize / 2) / eyes::kEyeSize);
    EYES_CANVAS_SCALE(canvas_[i], scale_x, scale_y);
    const int32_t center_offset = two_eyes_ ? (i == 0 ? -W / 4 : W / 4) : 0;
    lv_obj_align(canvas_[i], LV_ALIGN_CENTER, center_offset, EYES_Y_OFFSET);
  }

  // Popup pin yếu phải luôn nằm trên dải mắt.
  if (low_battery_popup_ != nullptr) lv_obj_move_foreground(low_battery_popup_);
}

void EyesDisplay::StartEyes() {
  if (running_) return;
  if (!AllocBuffers()) return;  // giữ emoji mặc định

  done_sem_ = xSemaphoreCreateBinary();
  {
    DisplayLockGuard lock(this);
    BuildWidgets();
  }
  {
    std::lock_guard<std::mutex> g(state_mu_);
    if (last_emotion_[0] == '\0') {
      std::strncpy(last_emotion_, "neutral", sizeof(last_emotion_) - 1);
    }
    engine_.SetEmotion(last_emotion_);
    engine_.SetAttention(activity_ == EyesActivity::kListening);
  }

  stop_req_ = false;
  running_ = true;
  if (xTaskCreatePinnedToCore(&EyesDisplay::EyeTaskThunk, "eyes", 6144, this,
                              EYES_TASK_PRIORITY, &task_, EYES_TASK_CORE) != pdPASS) {
    ESP_LOGE(TAG, "Khong tao duoc task mat");
    running_ = false;
    DisplayLockGuard lock(this);
    EYES_DEL(root_);
    root_ = nullptr;
    canvas_[0] = canvas_[1] = nullptr;
    for (lv_obj_t* o : {emoji_box_, emoji_label_, emoji_image_}) {
      if (o != nullptr) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    FreeBuffers();
  }
}

void EyesDisplay::StopEyes() {
  if (!running_) return;
  stop_req_ = true;
  if (done_sem_ != nullptr) xSemaphoreTake(done_sem_, pdMS_TO_TICKS(1000));
  running_ = false;
  task_ = nullptr;
  {
    DisplayLockGuard lock(this);
    if (root_ != nullptr) EYES_DEL(root_);
    root_ = nullptr;
    canvas_[0] = canvas_[1] = nullptr;
    for (lv_obj_t* o : {emoji_box_, emoji_label_, emoji_image_}) {
      if (o != nullptr) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
  }
  FreeBuffers();
  if (done_sem_ != nullptr) {
    vSemaphoreDelete(done_sem_);
    done_sem_ = nullptr;
  }
}

void EyesDisplay::SetEmotion(const char* emotion) {
  if (!running_) {  // mắt chưa chạy -> dùng UI emoji mặc định
    SpiLcdDisplay::SetEmotion(emotion);
    return;
  }
  if (emotion == nullptr) emotion = "neutral";
  bool changed;
  {
    std::lock_guard<std::mutex> g(state_mu_);
    changed = std::strncmp(last_emotion_, emotion, sizeof(last_emotion_) - 1) != 0;
    std::strncpy(last_emotion_, emotion, sizeof(last_emotion_) - 1);
    last_emotion_[sizeof(last_emotion_) - 1] = '\0';
    engine_.SetEmotion(emotion);
  }
  if (changed) engine_.TriggerBlink();  // chớp 1 cái cho chuyển cảm xúc tự nhiên
}

void EyesDisplay::SetActivity(EyesActivity activity) {
  std::lock_guard<std::mutex> g(state_mu_);
  activity_ = activity;
  engine_.SetAttention(activity_ == EyesActivity::kListening);
}

void EyesDisplay::EyeTaskThunk(void* arg) {
  static_cast<EyesDisplay*>(arg)->EyeLoop();
}

void EyesDisplay::EyeLoop() {
  const int64_t period_ms = 1000 / EYES_TARGET_FPS;
  int front = 0;
  const int n = two_eyes_ ? 2 : 1;

#if EYES_LOG_STATS
  int64_t stat_t0 = esp_timer_get_time();
  int64_t busy_us = 0;
  int frames = 0;
#endif

  while (!stop_req_) {
    if (paused_) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    const int64_t t0 = esp_timer_get_time();
    const int back = (front + 1) % kBuffers;

    // Render ngoài khoá LVGL: không giữ mutex lâu.
    engine_.Step(t0, buf_[0][back], two_eyes_ ? buf_[1][back] : nullptr);

    {
      DisplayLockGuard lock(this);
      if (root_ != nullptr) {
        for (int i = 0; i < n; ++i) {
          if (kBuffers > 1) EYES_CANVAS_SET(canvas_[i], buf_[i][back]);
          lv_obj_invalidate(canvas_[i]);
        }
      }
    }
    front = back;

    const int64_t spent_us = esp_timer_get_time() - t0;
#if EYES_LOG_STATS
    busy_us += spent_us;
    ++frames;
    const int64_t now = esp_timer_get_time();
    if (now - stat_t0 >= 5000000) {
      ESP_LOGI(TAG, "mat: %.1f fps, %.1f ms/frame (render+lock)",
               frames * 1e6 / (double)(now - stat_t0), busy_us / 1000.0 / frames);
      stat_t0 = now;
      busy_us = 0;
      frames = 0;
    }
#endif
    // Luôn nhường CPU >= 1 tick để không đói IDLE task / watchdog.
    int64_t wait_ms = period_ms - spent_us / 1000;
    TickType_t ticks = pdMS_TO_TICKS(wait_ms > 0 ? wait_ms : 0);
    vTaskDelay(ticks > 0 ? ticks : 1);
  }

  xSemaphoreGive(done_sem_);
  vTaskDelete(nullptr);
}
