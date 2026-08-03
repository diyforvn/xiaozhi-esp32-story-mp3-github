#pragma once

// =====================================================================
// MochiDisplay v3 - lam DUNG theo trai nghiem FluxGarage RoboEyes: CHI
// 2 mat chu nhat bo goc, KHONG mieng, KHONG long may rieng. Toan bo
// bieu cam the hien qua: (1) ty le rong/cao cua tung mat, (2) cuong do
// "cat goc" (lid) o goc trong (kieu angry) hoac goc ngoai (kieu
// tired/sad) cua tung mat. Day la ky thuat chinh xac ma thu vien goc
// FluxGarage/RoboEyes su dung (Adafruit GFX + eyelid triangles), duoc
// dien lai bang LVGL o day.
//
// Kich thuoc mac dinh (36x36, cach nhau 10px) duoc tinh RIENG cho man
// hinh 128x64 - tong be rong 2 mat + khoang cach = 82px (con du le hai
// ben), chieu cao 36px nam gon trong 64px - KHONG con bug tran khung
// hinh nhu ban truoc (mieng cu vuot qua day man hinh 19px).
//
// Khop voi main/boards/bread-compact-wifi/compact_wifi_board.cc:
// base class la OledDisplay (display/oled_display.h), constructor
// nhan (panel_io, panel, width, height, mirror_x, mirror_y).
//
// TICH HOP: giong het cac ban truoc - copy de 2 file .h/.cc vao thu
// muc board, dam bao mochi_display.cc co trong CMakeLists SRCS, include
// "mochi_display.h" trong compact_wifi_board.cc, khoi tao bang
// new MochiDisplay(panel_io_, panel_, W, H, MIRROR_X, MIRROR_Y).
// =====================================================================

#include "display/oled_display.h"
#include <lvgl.h>
#include <string>
#include <cstdint>

// 14 trang thai bieu cam, khop voi bo emotion chuan cua xiaozhi-esp32
// (xem SetEmotion() trong .cc de biet chi tiet map tu chuoi -> state).
enum class MochiState {
    kIdle,          // neutral / mac dinh
    kListening,     // dang nghe
    kSpeaking,      // dang noi
    kThinking,      // dang xu ly / connecting
    kHappy,         // happy / delicious
    kLaughing,      // laughing / funny / silly
    kSad,           // sad
    kCrying,        // crying
    kAngry,         // angry
    kSurprised,     // surprised / shocked
    kLoving,        // loving / kissy
    kEmbarrassed,   // embarrassed
    kConfused,      // confused
    kCool,          // cool / confident / relaxed / winking
    kSleepy,        // sleepy
};

// Quirk ngau nhien, chi kich hoat khi dang o kIdle - tao cam giac "co
// song" thay vi chi phan ung theo lenh server.
enum class Quirk {
    kNone,
    kStretch,  // mat nhem lai roi mo bung ra (thay the cho "ngap" - vi
               // khong con mieng de the hien ngap truc tiep)
    kGlance,   // liec nhanh sang 1 ben roi ve giua
};

// Tap tham so hinh hoc cho 1 bieu cam - CHI con cac truong lien quan
// toi mat (khong con mouth/brow). TargetForState() trong .cc anh xa
// moi MochiState ve 1 bo FaceParams; moi tick, current_ duoc "chay"
// toi target_ bang mo hinh lo xo (spring, xem StepSpring trong .cc) de
// co do nay/overshoot nhe truoc khi on dinh.
struct FaceParams {
    // Ty le chieu cao / rong mat so voi eye_h_/eye_w_ goc (1.0 = binh
    // thuong, 0 = nham hoan toan). Tach rieng trai/phai de lam duoc
    // wink / bat doi xung (vd "cool" nham 1 mat, "confused" lech mat).
    float eye_l_h = 1.0f, eye_r_h = 1.0f;
    float eye_l_w = 1.0f, eye_r_w = 1.0f;
    // Dich chuyen doc chung cho ca 2 mat (px), am = len tren.
    float eye_y_off = 0.0f;

    // Cuong do "cat goc TRONG" (gan song mui) cua tung mat, 0..1. Day
    // la ky thuat "eyelid" cua FluxGarage cho bieu cam ANGRY - 2 mat
    // nhu bi "chau" vao nhau o giua.
    float lid_inner_l = 0.0f, lid_inner_r = 0.0f;
    // Cuong do "cat goc NGOAI" cua tung mat, 0..1. Dung cho bieu cam
    // TIRED/SAD - mi mat nhu ru xuong o phia ngoai.
    float lid_outer_l = 0.0f, lid_outer_r = 0.0f;
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

    // Ham nay duoc application.cc goi moi khi trang thai thiet bi doi.
    // Day la diem noi duy nhat can quan tam khi tich hop.
    void SetEmotion(const char* emotion) override;

private:
    void BuildFace();
    void BuildLids();

    void ApplyState(MochiState state);
    // Bang tra cuu trung tam: 1 trang thai -> 1 bo tham so hinh hoc
    // muc tieu. Sua so o day la du de tinh chinh bieu cam.
    FaceParams TargetForState(MochiState state) const;

    void StepSpringToTarget();
    void ApplyCurrentToObjects();

    static void OnAnimTick(lv_timer_t* timer);
    static void OnBlinkAnim(void* var, int32_t value);
    static void OnBlinkFinished(lv_anim_t* anim);

    // Cac trang thai co animation thoi gian thuc rieng (mo phong hanh
    // vi lien tuc theo tung frame, ghi de truc tiep len lv_obj sau khi
    // he spring chung da chay xong trong tick do).
    void TickIdle();
    void TickListening();
    void TickSpeaking();
    void TickThinking();

    void ScheduleNextBlink();
    void PlayBlink();

    void ScheduleNextQuirk();
    void TriggerRandomQuirk();

    // width_/height_ da duoc ke thua tu Display (base class), OledDisplay
    // gan gia tri cho no trong constructor - khong khai bao lai o day
    // de tranh shadow bien va gay nham lan.

    lv_obj_t* face_root_ = nullptr;
    lv_obj_t* eye_left_ = nullptr;
    lv_obj_t* eye_right_ = nullptr;
    // 4 "lid" - hinh vuong xoay 45 do mau nen, dat o 4 goc tren cua 2
    // mat (trong-trai, ngoai-trai, trong-phai, ngoai-phai) de gia lap
    // ky thuat eyelid-cut cua FluxGarage. An mac dinh (opa 0).
    lv_obj_t* lid_inner_left_ = nullptr;
    lv_obj_t* lid_outer_left_ = nullptr;
    lv_obj_t* lid_inner_right_ = nullptr;
    lv_obj_t* lid_outer_right_ = nullptr;

    lv_timer_t* anim_timer_ = nullptr;

    MochiState state_ = MochiState::kIdle;
    FaceParams current_;   // gia tri dang hien thi (duoc spring dan toi target_)
    FaceParams target_;    // gia tri muc tieu ung voi state_
    FaceParams velocity_;  // van toc cho tung truong, dung boi mo hinh lo xo

    // Hinh hoc mat - CHU NHAT BO GOC, kich thuoc mac dinh 36x36 cach
    // nhau 10px (chuan FluxGarage cho man 128x64): tong be rong
    // 36+10+36=82px, chieu cao 36px - vua khung 128x64 co du le, khong
    // con tran man hinh nhu bug mieng o ban truoc.
    int eye_w_ = 36;
    int eye_h_ = 36;
    int eye_gap_ = 10;
    int eye_y_ = 0;
    int eye_radius_ = 10;

    uint32_t tick_count_ = 0;
    uint32_t next_blink_at_ = 0;
    bool blinking_ = false;
    bool double_blink_pending_ = false;

    // bien dem cho TickSpeaking (nhip "tho" nhe khi dang noi, thay the
    // cho animation mieng vi khong con mieng)
    int speak_phase_ = 0;

    // bien dem cho idle/thinking (mat di chuyen nhe + look-around)
    int idle_offset_x_ = 0;
    int idle_dir_ = 1;
    // offset x hien tai cho animation (duoc TickIdle/TickThinking cap nhat)
    int current_x_offset_ = 0;

    // --- Quirk state (chi dung khi state_ == kIdle) ---
    Quirk quirk_ = Quirk::kNone;
    uint32_t quirk_ticks_left_ = 0;
    uint32_t quirk_ticks_total_ = 0;
    uint32_t idle_ticks_ = 0;
    uint32_t next_quirk_at_ = 0;
    int glance_dir_ = 1;
};