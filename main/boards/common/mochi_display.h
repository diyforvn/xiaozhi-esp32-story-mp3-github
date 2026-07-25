#pragma once

// =====================================================================
// MochiDisplay - mat Mochi ve bang LVGL vector, thay the he emoji tinh
// mac dinh cua Xiaozhi. Dung cho man hinh OLED don sac SSD1306.
//
// Khop voi main/boards/bread-compact-wifi/compact_wifi_board.cc:
// base class la OledDisplay (display/oled_display.h), constructor
// nhan (panel_io, panel, width, height, mirror_x, mirror_y).
//
// CACH TICH HOP VAO PROJECT XIAOZHI-ESP32:
//   1. Copy 2 file mochi_display.h / mochi_display.cc vao
//      main/boards/bread-compact-wifi/ (hoac board tuong ung cua ban)
//   2. Trong CMakeLists.txt cua board, them mochi_display.cc vao SRCS
//   3. Trong compact_wifi_board.cc, THEM dong include o dau file:
//         #include "mochi_display.h"
//      (day chinh la nguyen nhan gay loi "expected type-specifier
//      before 'MochiDisplay'" - compiler chua biet MochiDisplay la gi)
//   4. Doi dong khoi tao display tu:
//         display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH,
//                                     DISPLAY_HEIGHT, DISPLAY_MIRROR_X,
//                                     DISPLAY_MIRROR_Y);
//      thanh:
//         display_ = new MochiDisplay(panel_io_, panel_, DISPLAY_WIDTH,
//                                      DISPLAY_HEIGHT, DISPLAY_MIRROR_X,
//                                      DISPLAY_MIRROR_Y);
// =====================================================================

#include "display/oled_display.h"
#include <lvgl.h>
#include <string>
#include <cstdint>

enum class MochiState {
    kIdle,
    kListening,
    kSpeaking,
    kThinking,
    kHappy,
    kSad,
    kConnecting,
};

class MochiDisplay : public OledDisplay {
public:
    MochiDisplay(esp_lcd_panel_io_handle_t io_handle,
                 esp_lcd_panel_handle_t panel_handle,
                 int width, int height,
                 bool mirror_x, bool mirror_y);
    ~MochiDisplay() override;

    // QUAN TRONG: KHONG duoc tao LVGL object trong constructor - man
    // hinh chua san sang. Phai lam trong SetupUI(), duoc framework goi
    // dung luc (Application::Initialize()) sau khi display da init xong.
    void SetupUI() override;

    // Ham nay duoc application.cc goi moi khi trang thai thiet bi doi
    // (idle / listening / speaking / thinking ...). Day la diem noi
    // duy nhat can quan tam khi tich hop.
    void SetEmotion(const char* emotion) override;

private:
    void BuildFace();
    void ApplyState(MochiState state);
    void SetMouthAngles(int start_deg, int end_deg);

    static void OnAnimTick(lv_timer_t* timer);
    static void OnBlinkAnim(void* var, int32_t value);
    static void OnBlinkFinished(lv_anim_t* anim);

    void TickIdle();
    void TickListening();
    void TickSpeaking();
    void TickThinking();
    void ScheduleNextBlink();
    void PlayBlink();

    // width_/height_ da duoc ke thua tu Display (base class), OledDisplay
    // gan gia tri cho no trong constructor - khong khai bao lai o day
    // de tranh shadow bien va gay nham lan.

    lv_obj_t* face_root_ = nullptr;
    lv_obj_t* eye_left_ = nullptr;
    lv_obj_t* eye_right_ = nullptr;
    lv_obj_t* mouth_ = nullptr;   // lv_arc - ve duong cong cuoi thay vi thanh ngang

    lv_timer_t* anim_timer_ = nullptr;

    MochiState state_ = MochiState::kIdle;

    // Hinh hoc mat - to tron hon ban truoc de giong Mochi hon
    int eye_w_ = 24;
    int eye_h_ = 30;
    int eye_gap_ = 20;
    int eye_y_ = 0;

    // Hinh hoc mieng (lv_arc hinh tron, chi hien 1 cung ben duoi de tao
    // net cuoi cong). mouth_diameter_ la duong kinh khung bao cua arc,
    // mouth_top_y_ la toa do Y dinh khung bao (tinh tu dinh face_root_).
    int mouth_diameter_ = 40;
    int mouth_top_y_ = 0;

    uint32_t tick_count_ = 0;
    uint32_t next_blink_at_ = 0;
    bool blinking_ = false;

    // bien dem cho animation noi (mieng mo dan theo song sin)
    int speak_phase_ = 0;

    // bien dem cho idle (mat di chuyen nhe + look-around)
    int idle_offset_x_ = 0;
    int idle_dir_ = 1;
};