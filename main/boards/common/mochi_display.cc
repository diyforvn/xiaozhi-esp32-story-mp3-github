#include "mochi_display.h"
#include <esp_log.h>
#include <esp_random.h>
#include <algorithm>
#include <cmath>

static const char* TAG = "MochiDisplay";

// Tan suat animation tick (ms). 40ms ~ 25fps, du muot cho SSD1306 I2C.
static constexpr uint32_t kTickIntervalMs = 40;

MochiDisplay::MochiDisplay(esp_lcd_panel_io_handle_t io_handle,
                            esp_lcd_panel_handle_t panel_handle,
                            int width, int height,
                            bool mirror_x, bool mirror_y)
    : OledDisplay(io_handle, panel_handle, width, height, mirror_x, mirror_y) {
    // CHI duoc phep lam viec khong dinh toi LVGL o day (vi du tinh toan
    // hinh hoc). KHONG duoc tao lv_obj/lv_timer trong constructor - man
    // hinh LVGL chua chac da san sang, xem comment trong constructor cua
    // OledDisplay (oled_display.cc) giai thich ly do.

    // Scale hinh hoc mat/mieng theo kich thuoc man hinh thuc te,
    // gia tri mac dinh o tren tinh cho 128x64. width_/height_ o day la
    // bien ke thua tu Display (OledDisplay constructor da gan gia tri).
    float scale = std::min(width_ / 128.0f, height_ / 64.0f);
    eye_w_  = static_cast<int>(24 * scale);
    eye_h_  = static_cast<int>(30 * scale);
    eye_gap_ = static_cast<int>(20 * scale);
    eye_y_  = height_ / 2 - eye_h_ / 2 - 8;

    mouth_diameter_ = static_cast<int>(40 * scale);
    // Mieng nam ngay duoi day mat, cach 1 khoang nho - tinh truc tiep
    // theo toa do TUYET DOI tu dinh face_root_ (KHONG dung lv_obj_align
    // voi cong thuc tru height_/2 nhu ban truoc, do la nguyen nhan gay
    // bug mieng bi day len chong len mat / hien thanh "gach ngang tren
    // dau giua 2 mat").
    mouth_top_y_ = eye_y_ + eye_h_ + 4;
}

// ---------------------------------------------------------------------
// Day la noi THUC SU dung de tao UI - duoc Application::Initialize()
// goi sau khi display da khoi tao xong hoan toan (dung theo quy uoc cua
// framework, xem comment trong OledDisplay::OledDisplay()).
//
// LUU Y QUAN TRONG: ham nay KHONG goi OledDisplay::SetupUI(), nghia la
// toan bo layout goc (top bar wifi/pin, status bar chu, icon chip mac
// dinh, subtitle chat) SE KHONG duoc tao ra - danh toan bo man hinh
// 128x64 (hoac 128x32) cho mat Mochi. Day la danh doi co chu dich,
// giong cach ban mod tren Hackster.io da lam (bo status bar/emoji goc,
// thay bang animation ve tay). Neu ban muon giu lai icon wifi/pin, can
// tu viet them mot top bar rieng - hoi minh neu can ban nay.
// ---------------------------------------------------------------------
void MochiDisplay::SetupUI() {
    // Goi thang len Display::SetupUI() (bo qua OledDisplay::SetupUI())
    // de danh dau setup_ui_called_ dung quy uoc cua framework, tranh
    // canh bao "SetupUI() called multiple times" o noi khac neu co.
    Display::SetupUI();

    BuildFace();
    ScheduleNextBlink();

    anim_timer_ = lv_timer_create(OnAnimTick, kTickIntervalMs, this);

    ESP_LOGI(TAG, "Mochi face initialized: %dx%d", width_, height_);
}

MochiDisplay::~MochiDisplay() {
    if (anim_timer_) {
        lv_timer_del(anim_timer_);
        anim_timer_ = nullptr;
    }
}

// ---------------------------------------------------------------------
// Dung 3 object LVGL don gian: 2 hinh oval lam mat, 1 hinh chu nhat bo
// goc lam mieng. Tat ca deu mau trang tren nen den (hoac nguoc lai tuy
// theme man hinh cua ban), khong dung anh nen giam tai RAM.
// ---------------------------------------------------------------------
void MochiDisplay::BuildFace() {
    DisplayLockGuard lock(this);

    auto screen = lv_screen_active();
    // Dat nen man hinh mau den ro rang - vi khong con goi
    // OledDisplay::SetupUI() nua nen theme mac dinh khong con duoc ap
    // dung, phai tu dam bao nen den / net ve trang (chuan OLED don sac,
    // tranh truong hop nen trang lam mat/mieng trang bi "an bien").
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    face_root_ = lv_obj_create(screen);
    lv_obj_remove_style_all(face_root_);
    lv_obj_set_size(face_root_, width_, height_);
    lv_obj_center(face_root_);
    lv_obj_set_style_bg_opa(face_root_, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(face_root_, LV_OBJ_FLAG_SCROLLABLE);

    auto make_eye = [&]() {
        lv_obj_t* eye = lv_obj_create(face_root_);
        lv_obj_remove_style_all(eye);
        lv_obj_set_size(eye, eye_w_, eye_h_);
        lv_obj_set_style_bg_color(eye, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, 0);
        return eye;
    };

    eye_left_  = make_eye();
    eye_right_ = make_eye();

    int center_x = width_ / 2;
    lv_obj_set_pos(eye_left_,  center_x - eye_gap_ / 2 - eye_w_, eye_y_);
    lv_obj_set_pos(eye_right_, center_x + eye_gap_ / 2, eye_y_);

    // Mieng: dung lv_arc de ve mot cung tron o day - tao net cuoi cong
    // mem mai giong Mochi, thay vi thanh ngang cung nhu ban truoc.
    mouth_ = lv_arc_create(face_root_);
    lv_obj_remove_style_all(mouth_);
    lv_obj_set_size(mouth_, mouth_diameter_, mouth_diameter_);
    lv_obj_clear_flag(mouth_, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(mouth_, 0);
    lv_arc_set_bg_angles(mouth_, 0, 360);
    lv_obj_set_style_arc_opa(mouth_, LV_OPA_TRANSP, LV_PART_MAIN);   // an cung nen
    lv_obj_set_style_arc_width(mouth_, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(mouth_, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(mouth_, true, LV_PART_INDICATOR);
    lv_obj_remove_style(mouth_, nullptr, LV_PART_KNOB);              // bo nut keo mac dinh cua arc
    // Vi tri: dat KHUNG BAO cua arc ngay duoi mat, can giua theo truc x.
    // Dung lv_obj_set_pos (toa do tuyet doi tu goc tren-trai cua
    // face_root_) giong het cach dat mat, tranh tron 2 he toa do khac
    // nhau nhu bug truoc.
    lv_obj_set_pos(mouth_, center_x - mouth_diameter_ / 2, mouth_top_y_);

    SetMouthAngles(30, 150);  // mac dinh: net cuoi nhe o day
}

// ---------------------------------------------------------------------
// Helper: dat goc cung tron cho mieng. He quy uoc goc LVGL: 0do = vi
// tri 3h, tang dan THEO CHIEU KIM DONG HO. Vi du goc (30,150) ve cung
// phia duoi (di qua 90do = 6h) -> nhin giong net cuoi. Goc (210,330)
// ve cung phia tren (di qua 270do = 12h) -> nhin giong net nhan mieng
// (cau may).
// ---------------------------------------------------------------------
void MochiDisplay::SetMouthAngles(int start_deg, int end_deg) {
    lv_arc_set_angles(mouth_, start_deg, end_deg);
}

// ---------------------------------------------------------------------
// Diem noi voi Xiaozhi: application.cc goi SetEmotion("listening"),
// SetEmotion("speaking"), SetEmotion("idle")... moi khi doi trang thai.
// Map ten trang thai sang MochiState o day.
// ---------------------------------------------------------------------
void MochiDisplay::SetEmotion(const char* emotion) {
    if (emotion == nullptr) {
        ApplyState(MochiState::kIdle);
        return;
    }

    // Chuyen sang std::string de so sanh NOI DUNG chuoi, vi "==" tren
    // const char* chi so sanh dia chi con tro chu khong so sanh chuoi.
    std::string e(emotion);

    MochiState next = MochiState::kIdle;

    if (e == "listening") {
        next = MochiState::kListening;
    } else if (e == "speaking") {
        next = MochiState::kSpeaking;
    } else if (e == "thinking" || e == "connecting") {
        next = MochiState::kThinking;
    } else if (e == "happy" || e == "laughing" || e == "smile") {
        next = MochiState::kHappy;
    } else if (e == "sad" || e == "crying") {
        next = MochiState::kSad;
    } else {
        next = MochiState::kIdle;
    }

    ApplyState(next);
}

void MochiDisplay::ApplyState(MochiState state) {
    if (state_ == state) return;
    state_ = state;

    DisplayLockGuard lock(this);

    switch (state_) {
        case MochiState::kHappy:
            // mat cong lai kieu cuoi (nham bot) + mieng cuoi rong hon
            lv_obj_set_height(eye_left_, eye_h_ * 0.5f);
            lv_obj_set_height(eye_right_, eye_h_ * 0.5f);
            SetMouthAngles(10, 170);
            break;

        case MochiState::kSad:
            // mat hoi nham + mieng cong nguoc len tren (cau may)
            lv_obj_set_height(eye_left_, eye_h_ * 0.7f);
            lv_obj_set_height(eye_right_, eye_h_ * 0.7f);
            SetMouthAngles(210, 330);
            break;

        default:
            // cac state con lai (idle/listening/speaking/thinking)
            // duoc animation timer xu ly lien tuc, o day chi reset
            // ve kich thuoc mat mac dinh + net cuoi nhe truoc khi tick
            // tiep quan.
            lv_obj_set_height(eye_left_, eye_h_);
            lv_obj_set_height(eye_right_, eye_h_);
            SetMouthAngles(30, 150);
            break;
    }
}

// ---------------------------------------------------------------------
// Vong lap animation - chay tren LVGL timer, KHONG block task chinh,
// nen khong anh huong toi luong audio/ASR/LLM/TTS.
// ---------------------------------------------------------------------
void MochiDisplay::OnAnimTick(lv_timer_t* timer) {
    // LVGL v9: khong duoc truy cap truc tiep timer->user_data vi
    // lv_timer_t la incomplete type o phia public API. Phai dung
    // ham lv_timer_get_user_data() de lay lai con tro da luu khi
    // tao timer (tham so cuoi cua lv_timer_create).
    auto* self = static_cast<MochiDisplay*>(lv_timer_get_user_data(timer));
    self->tick_count_++;

    DisplayLockGuard lock(self);

    switch (self->state_) {
        case MochiState::kListening:
            self->TickListening();
            break;
        case MochiState::kSpeaking:
            self->TickSpeaking();
            break;
        case MochiState::kThinking:
            self->TickThinking();
            break;
        case MochiState::kIdle:
        default:
            self->TickIdle();
            break;
    }

    // Chop mat dinh ky bat ke dang o state nao (tru luc dang chop dang do)
    if (!self->blinking_ && self->tick_count_ >= self->next_blink_at_) {
        self->PlayBlink();
    }
}

void MochiDisplay::TickIdle() {
    // Mat di chuyen qua lai nhe trong bien do +-4px, doi huong khi cham bien
    idle_offset_x_ += idle_dir_;
    if (idle_offset_x_ > 4 || idle_offset_x_ < -4) {
        idle_dir_ = -idle_dir_;
    }

    int center_x = width_ / 2;
    lv_obj_set_x(eye_left_,  center_x - eye_gap_ / 2 - eye_w_ + idle_offset_x_);
    lv_obj_set_x(eye_right_, center_x + eye_gap_ / 2 + idle_offset_x_);

    // net cuoi nhe, on dinh - trang thai nghi
    SetMouthAngles(35, 145);
}

void MochiDisplay::TickListening() {
    // Mat lech bat doi xung: mat trai to hon mat phai mot chut de tao
    // cam giac "dang chu y lang nghe"
    lv_obj_set_height(eye_left_, eye_h_);
    lv_obj_set_height(eye_right_, static_cast<int>(eye_h_ * 0.75f));
    SetMouthAngles(50, 130);  // mieng thu nho lai, trung lap
}

void MochiDisplay::TickSpeaking() {
    // Gia lap nhip noi: do rong cung tron dao dong theo song sin ngan
    // quanh tam 90do (day cung), tao cam giac mieng dang mo/khep khi
    // noi - khong can phan tich am thanh that.
    speak_phase_ = (speak_phase_ + 1) % 20;
    float t = 0.5f + 0.5f * sinf(speak_phase_ * 0.31f);   // 0..1
    int half_span = 20 + static_cast<int>(90 * t);         // 20..110 do
    SetMouthAngles(90 - half_span, 90 + half_span);

    // mat mo binh thuong khi dang noi
    lv_obj_set_height(eye_left_, eye_h_);
    lv_obj_set_height(eye_right_, eye_h_);
}

void MochiDisplay::TickThinking() {
    // Mat nhin len tren + lac nhe trai phai, mieng khep nho lai
    idle_offset_x_ += idle_dir_;
    if (idle_offset_x_ > 3 || idle_offset_x_ < -3) {
        idle_dir_ = -idle_dir_;
    }
    lv_obj_set_y(eye_left_,  eye_y_ - 3);
    lv_obj_set_y(eye_right_, eye_y_ - 3);
    SetMouthAngles(75, 105);
}

// ---------------------------------------------------------------------
// Chop mat: dung lv_anim de thu chieu cao mat ve gan 0 roi phinh lai,
// khong block timer chinh.
// ---------------------------------------------------------------------
void MochiDisplay::ScheduleNextBlink() {
    // random 2-6 giay giua 2 lan chop mat (tinh theo so tick)
    uint32_t rand_ms = 2000 + (esp_random() % 4000);
    next_blink_at_ = tick_count_ + (rand_ms / kTickIntervalMs);
}

void MochiDisplay::PlayBlink() {
    blinking_ = true;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, this);
    lv_anim_set_values(&a, eye_h_, 2);
    lv_anim_set_time(&a, 90);
    lv_anim_set_playback_time(&a, 90);
    lv_anim_set_exec_cb(&a, OnBlinkAnim);
    lv_anim_set_ready_cb(&a, OnBlinkFinished);
    lv_anim_start(&a);
}

void MochiDisplay::OnBlinkAnim(void* var, int32_t value) {
    auto* self = static_cast<MochiDisplay*>(var);
    lv_obj_set_height(self->eye_left_, value);
    lv_obj_set_height(self->eye_right_, value);
}

void MochiDisplay::OnBlinkFinished(lv_anim_t* anim) {
    auto* self = static_cast<MochiDisplay*>(anim->var);
    self->blinking_ = false;
    self->ScheduleNextBlink();
}