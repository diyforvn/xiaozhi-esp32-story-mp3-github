#include "mochi_display.h"
#include <esp_log.h>
#include <esp_random.h>
#include <algorithm>
#include <cmath>

static const char* TAG = "MochiDisplay";

// true = ve ca mieng (nhu v4, mat nam o nua tren). false = chi con 2 mat
// kieu RoboEyes thuan tuy, can giua doc man hinh.
static constexpr bool kEnableMouth = true;

// 40ms ~ 25fps, du muot cho SSD1306 I2C.
static constexpr uint32_t kTickIntervalMs = 40;
static constexpr float kDt = kTickIntervalMs / 1000.0f;

// Lo xo dung chung (giu nguyen v4): nay nhe 1 nhip roi on dinh ~250-300ms.
static constexpr float kSpringStiffness = 180.0f;
static constexpr float kSpringDamping = 16.0f;

static constexpr float kMouthOverlapRatio = 0.55f;
static constexpr uint32_t kFlickForever = 0xFFFFFFFFu;

namespace {
inline void StepSpring(float& current, float& velocity, float target) {
    float accel = (target - current) * kSpringStiffness - velocity * kSpringDamping;
    velocity += accel * kDt;
    current += velocity * kDt;
}
inline float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
inline int Round(float v) { return static_cast<int>(std::lround(v)); }
inline int RandRange(int lo, int hi) {  // [lo, hi]
    return lo + static_cast<int>(esp_random() % static_cast<uint32_t>(hi - lo + 1));
}
}  // namespace

MochiDisplay::MochiDisplay(esp_lcd_panel_io_handle_t io_handle,
                            esp_lcd_panel_handle_t panel_handle,
                            int width, int height,
                            bool mirror_x, bool mirror_y)
    : OledDisplay(io_handle, panel_handle, width, height, mirror_x, mirror_y) {
    // Chi tinh toan hinh hoc o day. KHONG tao lv_obj/lv_timer.
    scale_ = std::min(width_ / 128.0f, height_ / 64.0f);

    eye_w_ = std::max(8, static_cast<int>(34 * scale_));
    eye_h_ = std::max(8, static_cast<int>(28 * scale_));
    eye_gap_ = static_cast<int>(10 * scale_);
    // RoboEyes: bo goc co dinh (LVGL tu kep ve nua canh ngan khi mat bi bop).
    eye_radius_ = std::max(2, static_cast<int>(8 * scale_));
    eye_y_ = std::max(2, static_cast<int>(4 * scale_));
    eye_cy_ = kEnableMouth ? (eye_y_ + eye_h_ / 2) : (height_ / 2);

    amp_x_ = 14.0f * scale_;
    amp_y_ = kEnableMouth ? 3.0f * scale_
                          : std::min(10.0f * scale_, (height_ - eye_h_) / 2.0f - 2.0f);
    cross_amp_ = 6.0f * scale_;

    // --- Mieng (giu nguyen v4.1) ---
    mouth_span_ = static_cast<int>(44 * scale_);
    {
        float denom = 1.0f + kMouthOverlapRatio * (kMouthSegCount - 1);
        mouth_seg_w_ = std::max(3, static_cast<int>(mouth_span_ / denom));
    }
    mouth_base_h_ = std::max(2, static_cast<int>(5 * scale_));
    mouth_open_amp_ = static_cast<int>(8 * scale_);
    mouth_curve_amp_ = static_cast<int>(7 * scale_);
    mouth_tilt_amp_ = static_cast<int>(5 * scale_);

    int mouth_top_gap = std::max(3, static_cast<int>(7 * scale_));
    mouth_y_ = eye_y_ + eye_h_ + mouth_top_gap + mouth_base_h_ / 2;
    int mouth_max_extent = mouth_base_h_ / 2 + mouth_open_amp_ + mouth_curve_amp_ + mouth_tilt_amp_;
    if (mouth_y_ + mouth_max_extent > height_ - 1) {
        mouth_y_ = std::max(eye_y_ + eye_h_ + 1, height_ - 1 - mouth_max_extent);
    }

    std::fill_n(velocity_.data(), kFaceParamCount, 0.0f);
}

void MochiDisplay::SetupUI() {
    Display::SetupUI();

    BuildFace();
    if (kEnableMouth) {
        BuildMouth();
    }

    target_ = TargetForState(state_);
    current_ = target_;
    ScheduleNextBlink();
    ScheduleNextQuirk();

    anim_timer_ = lv_timer_create(OnAnimTick, kTickIntervalMs, this);

    ESP_LOGI(TAG, "Mochi face v5 (RoboEyes style) initialized: %dx%d", width_, height_);
}

MochiDisplay::~MochiDisplay() {
    if (anim_timer_) {
        lv_timer_del(anim_timer_);
        anim_timer_ = nullptr;
    }
}

// ---------------------------------------------------------------------
// Tao shape: lv_obj chu nhat, an san. Thu tu tao = thu tu z (tao sau nam
// tren): mat -> ma -> mi phang -> mi bac thang -> mieng -> giot.
// ---------------------------------------------------------------------
MochiShape MochiDisplay::MakeShape(bool white, int radius) {
    MochiShape s;
    s.obj = lv_obj_create(face_root_);
    lv_obj_remove_style_all(s.obj);
    lv_obj_set_style_bg_color(s.obj, white ? lv_color_white() : lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s.obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s.obj, radius, 0);
    lv_obj_add_flag(s.obj, LV_OBJ_FLAG_HIDDEN);
    s.hidden = true;
    s.radius = radius;
    return s;
}

void MochiDisplay::SetRadius(MochiShape& s, int radius) {
    if (s.radius != radius) {
        lv_obj_set_style_radius(s.obj, radius, 0);
        s.radius = radius;
    }
}

void MochiDisplay::Hide(MochiShape& s) {
    if (!s.hidden) {
        lv_obj_add_flag(s.obj, LV_OBJ_FLAG_HIDDEN);
        s.hidden = true;
    }
}

void MochiDisplay::Place(MochiShape& s, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) {
        Hide(s);
        return;
    }
    if (s.hidden) {
        lv_obj_clear_flag(s.obj, LV_OBJ_FLAG_HIDDEN);
        s.hidden = false;
    }
    if (w != s.w || h != s.h) {
        lv_obj_set_size(s.obj, w, h);
        s.w = w;
        s.h = h;
    }
    if (x != s.x || y != s.y) {
        lv_obj_set_pos(s.obj, x, y);
        s.x = x;
        s.y = y;
    }
}

void MochiDisplay::BuildFace() {
    DisplayLockGuard lock(this);

    auto screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    face_root_ = lv_obj_create(screen);
    lv_obj_remove_style_all(face_root_);
    lv_obj_set_size(face_root_, width_, height_);
    lv_obj_center(face_root_);
    lv_obj_set_style_bg_opa(face_root_, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(face_root_, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 2; ++i) eye_[i] = MakeShape(true, eye_radius_);
    for (int i = 0; i < 2; ++i) cheek_[i] = MakeShape(false, eye_radius_);
    for (int i = 0; i < 2; ++i) lid_top_[i] = MakeShape(false, 0);
    for (int i = 0; i < 2; ++i) {
        for (int k = 0; k < kStairCount; ++k) lid_in_[i][k] = MakeShape(false, 0);
        for (int k = 0; k < kStairCount; ++k) lid_out_[i][k] = MakeShape(false, 0);
    }
    // Pool glyph trang (mat tim / mat X), nam tren mi, duoi giot.
    for (int i = 0; i < 2; ++i) {
        for (int k = 0; k < kGlyphCount; ++k) glyph_[i][k] = MakeShape(true, 0);
    }
    // 2 giot (mo hoi / nuoc mat), trang tren nen den.
    for (int i = 0; i < 2; ++i) drop_[i] = MakeShape(true, LV_RADIUS_CIRCLE);
}

void MochiDisplay::BuildMouth() {
    DisplayLockGuard lock(this);
    for (int i = 0; i < kMouthSegCount; ++i) {
        mouth_[i] = MakeShape(true, LV_RADIUS_CIRCLE);
    }
}

// ---------------------------------------------------------------------
// Diem noi voi Xiaozhi. Chuoi khong nhan ra -> kIdle.
// ---------------------------------------------------------------------
void MochiDisplay::SetEmotion(const char* emotion) {
    if (emotion == nullptr) {
        talking_session_active_ = false;
        ApplyState(MochiState::kIdle);
        return;
    }

    std::string e(emotion);
    MochiState next = MochiState::kIdle;

    if (e == "happy") {
        next = MochiState::kHappy;
    } else if (e == "delicious") {
        next = MochiState::kDelicious;
    } else if (e == "laughing" || e == "funny") {
        next = MochiState::kLaughing;
    } else if (e == "silly") {
        next = MochiState::kSilly;
    } else if (e == "sad") {
        next = MochiState::kSad;
    } else if (e == "crying") {
        next = MochiState::kCrying;
    } else if (e == "angry") {
        next = MochiState::kAngry;
    } else if (e == "surprised") {
        next = MochiState::kSurprised;
    } else if (e == "shocked") {
        next = MochiState::kShocked;
    } else if (e == "loving") {
        next = MochiState::kLoving;
    } else if (e == "kissy") {
        next = MochiState::kKissy;
    } else if (e == "embarrassed") {
        next = MochiState::kEmbarrassed;
    } else if (e == "confused") {
        next = MochiState::kConfused;
    } else if (e == "cool") {
        next = MochiState::kCool;
    } else if (e == "confident") {
        next = MochiState::kConfident;
    } else if (e == "relaxed") {
        next = MochiState::kRelaxed;
    } else if (e == "winking") {
        next = MochiState::kWinking;
    } else if (e == "sleepy") {
        next = MochiState::kSleepy;
    } else if (e == "worried" || e == "nervous" || e == "anxious") {
        next = MochiState::kWorried;
    } else if (e == "scared" || e == "afraid" || e == "fear") {
        next = MochiState::kScared;
    } else if (e == "suspicious" || e == "skeptical") {
        next = MochiState::kSuspicious;
    } else if (e == "bored") {
        next = MochiState::kBored;
    } else if (e == "excited") {
        next = MochiState::kExcited;
    } else if (e == "dizzy") {
        next = MochiState::kDizzy;
    } else if (e == "mischievous" || e == "naughty") {
        next = MochiState::kMischievous;
    } else if (e == "determined") {
        next = MochiState::kDetermined;
    } else if (e == "pleading" || e == "begging") {
        next = MochiState::kPleading;
    } else if (e == "proud") {
        next = MochiState::kProud;
    } else if (e == "jealous") {
        next = MochiState::kJealous;
    } else if (e == "shy") {
        next = MochiState::kShy;
    } else if (e == "sick" || e == "ill") {
        next = MochiState::kSick;
    } else if (e == "cold" || e == "freezing") {
        next = MochiState::kCold;
    } else if (e == "heart_eyes" || e == "heart-eyes" || e == "hearteyes") {
        next = MochiState::kHeartEyes;
    } else if (e == "x_eyes" || e == "xeyes" || e == "fainted") {
        next = MochiState::kXEyes;
    } else if (e == "sneeze") {
        next = MochiState::kSneeze;
    } else if (e == "glitch") {
        next = MochiState::kGlitch;
    } else if (e == "thinking" || e == "connecting" || e == "microchip_ai") {
        next = MochiState::kThinking;
    } else if (e == "listening") {
        next = MochiState::kListening;
    } else if (e == "speaking") {
        next = MochiState::kSpeaking;
    } else {
        next = MochiState::kIdle;
    }

    // "Phien noi" tu dong (giu nguyen logic v4): bat khi nhan "speaking",
    // tat khi nhan idle/listening/thinking/sleepy, cac emotion phan ung
    // khac giu nguyen de mieng khong dung nhap nhay giua cau.
    switch (next) {
        case MochiState::kSpeaking:
            talking_session_active_ = true;
            break;
        case MochiState::kIdle:
        case MochiState::kListening:
        case MochiState::kThinking:
        case MochiState::kSleepy:
            talking_session_active_ = false;
            break;
        default:
            break;
    }

    ApplyState(next);
}

// ---------------------------------------------------------------------
// Bang tra cuu trung tam: 1 trang thai -> 1 bo tham so muc tieu.
// SUA BIEU CAM O DAY. (px tinh tren man 128x64)
// ---------------------------------------------------------------------
FaceParams MochiDisplay::TargetForState(MochiState state) const {
    FaceParams p;

    switch (state) {
        case MochiState::kHappy:
            // RoboEyes HAPPY: ma day tu duoi len -> mat thanh vom "^".
            p.eye_l_w = p.eye_r_w = 1.05f;
            p.cheek_l = p.cheek_r = 1.0f;
            p.mouth_w = 1.2f;
            p.mouth_curve = 0.85f;
            p.mouth_open = 0.2f;
            break;

        case MochiState::kLaughing:
            p.eye_l_h = p.eye_r_h = 0.95f;
            p.cheek_l = p.cheek_r = 1.0f;
            p.flick_y = 2.0f;               // rung doc (RoboEyes laugh)
            p.mouth_w = 1.32f;
            p.mouth_curve = 0.95f;
            p.mouth_open = 0.75f;
            break;

        case MochiState::kSad:
            // RoboEyes TIRED: mi cat goc NGOAI, nhin xuong.
            p.eye_l_h = p.eye_r_h = 0.9f;
            p.lid_outer_l = p.lid_outer_r = 1.0f;
            p.look_y = 0.8f;
            p.eye_y_off = 1.0f;
            p.mouth_w = 0.85f;
            p.mouth_curve = -0.65f;
            p.mouth_y_off = 1.0f;
            break;

        case MochiState::kCrying:
            p.eye_l_h = p.eye_r_h = 0.85f;
            p.lid_outer_l = p.lid_outer_r = 1.0f;
            p.lid_inner_l = p.lid_inner_r = 0.3f;
            p.look_y = 1.0f;
            p.tears = 1.0f;
            p.flick_y = 1.0f;               // nuc no (lien tuc)
            p.mouth_w = 0.85f;
            p.mouth_curve = -0.75f;
            p.mouth_open = 0.4f;
            p.mouth_y_off = 1.5f;
            break;

        case MochiState::kAngry:
            // RoboEyes ANGRY: mi cat goc TRONG.
            p.eye_l_h = p.eye_r_h = 0.95f;
            p.lid_inner_l = p.lid_inner_r = 1.0f;
            p.eye_y_off = 0.5f;
            p.mouth_w = 0.85f;
            p.mouth_curve = -0.5f;
            p.mouth_open = 0.15f;
            break;

        case MochiState::kSurprised:
            p.eye_l_h = p.eye_r_h = 1.2f;
            p.eye_y_off = -1.5f;
            p.mouth_w = 0.6f;
            p.mouth_open = 1.0f;
            break;

        case MochiState::kShocked:
            p.eye_l_h = p.eye_r_h = 1.2f;
            p.eye_l_w = p.eye_r_w = 0.9f;
            p.eye_y_off = -2.0f;
            p.flick_x = 1.0f;               // run
            p.sweat = 1.0f;
            p.mouth_w = 0.55f;
            p.mouth_curve = -0.1f;
            p.mouth_open = 0.9f;
            p.mouth_y_off = 1.5f;
            break;

        case MochiState::kLoving:
            p.eye_l_h = p.eye_r_h = 1.05f;
            p.eye_l_w = p.eye_r_w = 1.1f;
            p.cheek_l = p.cheek_r = 0.55f;
            p.mouth_w = 1.05f;
            p.mouth_curve = 0.7f;
            p.mouth_open = 0.25f;
            break;

        case MochiState::kKissy:
            p.eye_l_w = p.eye_r_w = 1.05f;
            p.cheek_l = p.cheek_r = 0.8f;
            p.mouth_w = 0.35f;              // chum moi: "o" nho
            p.mouth_open = 0.55f;
            p.mouth_y_off = 1.0f;
            break;

        case MochiState::kEmbarrassed:
            p.eye_l_h = p.eye_r_h = 0.9f;
            p.lid_outer_l = p.lid_outer_r = 0.5f;
            p.lid_inner_l = p.lid_inner_r = 0.15f;
            p.cheek_l = p.cheek_r = 0.25f;
            p.look_x = -0.5f;
            p.look_y = 0.5f;
            p.sweat = 1.0f;
            p.mouth_w = 0.75f;
            p.mouth_curve = 0.25f;
            p.mouth_tilt = 0.45f;
            break;

        case MochiState::kConfused:
            p.eye_l_h = 0.85f;
            p.eye_r_h = 1.2f;
            p.eye_l_w = 0.92f;
            p.eye_r_w = 1.08f;
            p.lid_inner_l = 0.5f;
            p.lid_outer_r = 0.45f;
            p.flick_x = 2.0f;               // rung ngang 1 lan (RoboEyes confused)
            p.mouth_w = 0.8f;
            p.mouth_curve = -0.1f;
            p.mouth_tilt = 0.5f;
            break;

        case MochiState::kCool:
            p.lid_top_l = p.lid_top_r = 0.4f;
            p.mouth_w = 0.95f;
            p.mouth_curve = 0.5f;
            p.mouth_tilt = 0.55f;
            break;

        case MochiState::kConfident:
            p.lid_top_l = p.lid_top_r = 0.2f;
            p.cheek_l = p.cheek_r = 0.2f;
            p.look_y = -0.4f;
            p.mouth_curve = 0.6f;
            p.mouth_tilt = 0.4f;
            break;

        case MochiState::kRelaxed:
            p.lid_top_l = p.lid_top_r = 0.15f;
            p.cheek_l = p.cheek_r = 0.5f;
            p.mouth_curve = 0.35f;
            p.mouth_open = 0.08f;
            break;

        case MochiState::kWinking:
            p.eye_r_h = 0.12f;              // nhem mat phai (vach ngang)
            p.eye_r_w = 1.05f;
            p.mouth_w = 1.1f;
            p.mouth_curve = 0.7f;
            p.mouth_tilt = 0.3f;
            p.mouth_open = 0.15f;
            break;

        case MochiState::kSleepy:
            // TickSleepy() dao dong lid_top / eye_y_off quanh gia tri nay.
            p.eye_l_h = p.eye_r_h = 0.9f;
            p.lid_top_l = p.lid_top_r = 0.55f;
            p.lid_outer_l = p.lid_outer_r = 0.4f;
            p.eye_y_off = 1.5f;
            p.look_y = 0.5f;
            p.mouth_w = 0.7f;
            p.mouth_curve = -0.15f;
            p.mouth_open = 0.12f;
            p.mouth_y_off = 1.0f;
            break;

        case MochiState::kWorried:
            p.lid_outer_l = p.lid_outer_r = 0.6f;
            p.look_y = 0.3f;
            p.sweat = 1.0f;
            p.mouth_w = 0.75f;
            p.mouth_curve = -0.3f;
            p.mouth_tilt = 0.15f;
            p.mouth_open = 0.05f;
            break;

        case MochiState::kScared:
            p.eye_l_h = p.eye_r_h = 1.2f;
            p.eye_l_w = p.eye_r_w = 0.95f;
            p.eye_y_off = -1.0f;
            p.flick_x = 1.2f;               // run bay bay
            p.sweat = 1.0f;
            p.mouth_w = 0.6f;
            p.mouth_curve = -0.3f;
            p.mouth_open = 0.6f;
            break;

        case MochiState::kSuspicious:
            p.lid_top_l = 0.45f;
            p.lid_top_r = 0.55f;
            p.mouth_w = 0.7f;
            p.mouth_curve = -0.05f;
            p.mouth_tilt = 0.2f;
            break;

        case MochiState::kBored:
            p.eye_l_h = p.eye_r_h = 0.95f;
            p.lid_top_l = p.lid_top_r = 0.5f;
            p.look_y = 0.3f;
            p.mouth_w = 0.7f;
            p.mouth_curve = -0.1f;
            p.mouth_tilt = -0.2f;
            break;

        case MochiState::kExcited:
            p.eye_l_h = p.eye_r_h = 1.15f;
            p.eye_l_w = p.eye_r_w = 1.1f;
            p.look_y = -0.2f;
            p.flick_y = 1.5f;               // nhun nhay 1 luc
            p.mouth_w = 1.3f;
            p.mouth_curve = 0.9f;
            p.mouth_open = 0.6f;
            break;

        case MochiState::kDelicious:
            // TickDelicious() lac mieng trai-phai (liem moi).
            p.eye_l_w = p.eye_r_w = 1.05f;
            p.cheek_l = p.cheek_r = 0.9f;
            p.mouth_w = 1.1f;
            p.mouth_curve = 0.8f;
            p.mouth_open = 0.5f;
            p.mouth_tilt = 0.3f;
            break;

        case MochiState::kSilly:
            p.eye_l_h = 0.9f;
            p.eye_r_h = 1.15f;
            p.eye_l_w = p.eye_r_w = 0.95f;
            p.cross = 0.5f;                 // mat lac (0.5 = 3px, mat khong dinh nhau)
            p.mouth_w = 1.2f;
            p.mouth_curve = 0.6f;
            p.mouth_open = 0.55f;
            p.mouth_tilt = 0.5f;
            break;

        case MochiState::kDizzy:
            // TickDizzy() quay huong nhin theo vong tron.
            p.mouth_w = 0.8f;
            p.mouth_open = 0.3f;
            break;

        case MochiState::kMischievous:
            p.eye_l_h = 0.85f;
            p.eye_r_h = 1.1f;
            p.lid_top_l = 0.4f;
            p.lid_inner_l = 0.35f;
            p.look_x = 0.5f;
            p.mouth_w = 1.1f;
            p.mouth_curve = 0.75f;
            p.mouth_tilt = 0.5f;
            p.mouth_open = 0.1f;
            break;

        case MochiState::kDetermined:
            p.eye_l_w = p.eye_r_w = 1.05f;
            p.lid_inner_l = p.lid_inner_r = 0.55f;
            p.look_y = -0.2f;
            p.mouth_w = 0.8f;
            p.mouth_curve = 0.05f;
            break;

        case MochiState::kPleading:
            // TickPleading() lam mat "tho" nhe (mat cun con).
            p.eye_l_h = p.eye_r_h = 1.2f;
            p.eye_l_w = p.eye_r_w = 1.1f;
            p.lid_outer_l = p.lid_outer_r = 0.35f;
            p.look_y = -0.3f;
            p.eye_y_off = -1.0f;
            p.mouth_w = 0.6f;
            p.mouth_curve = -0.25f;
            p.mouth_open = 0.1f;
            break;

        case MochiState::kProud:
            p.cheek_l = p.cheek_r = 0.75f;
            p.lid_top_l = p.lid_top_r = 0.1f;
            p.look_y = -1.0f;
            p.mouth_w = 1.1f;
            p.mouth_curve = 0.8f;
            p.mouth_open = 0.1f;
            break;

        case MochiState::kJealous:
            p.eye_l_h = p.eye_r_h = 0.95f;
            p.lid_top_l = p.lid_top_r = 0.4f;
            p.lid_inner_l = p.lid_inner_r = 0.3f;
            p.look_x = 1.0f;
            p.look_y = 0.2f;
            p.mouth_w = 0.7f;
            p.mouth_curve = -0.3f;
            p.mouth_tilt = 0.3f;
            break;

        case MochiState::kShy:
            // TickGaze() luan phien: nhin tron 1 cai roi lai ne di.
            p.eye_l_h = p.eye_r_h = 0.9f;
            p.lid_outer_l = p.lid_outer_r = 0.3f;
            p.cheek_l = p.cheek_r = 0.3f;
            p.look_x = -0.8f;
            p.look_y = 0.7f;
            p.mouth_w = 0.6f;
            p.mouth_curve = 0.3f;
            p.mouth_tilt = 0.2f;
            p.mouth_y_off = 0.5f;
            break;

        case MochiState::kSick:
            // TickSick() lac nhe huong nhin; run lien tuc.
            p.eye_l_h = p.eye_r_h = 0.8f;
            p.eye_l_w = p.eye_r_w = 0.95f;
            p.lid_top_l = p.lid_top_r = 0.35f;
            p.lid_outer_l = p.lid_outer_r = 0.3f;
            p.look_y = 0.4f;
            p.flick_x = 0.6f;
            p.sweat = 1.0f;
            p.mouth_w = 0.8f;
            p.mouth_curve = -0.35f;
            p.mouth_tilt = -0.25f;
            p.mouth_open = 0.1f;
            break;

        case MochiState::kCold:
            // TickCold() lam mieng danh lap cap.
            p.eye_l_h = p.eye_r_h = 0.85f;
            p.lid_top_l = p.lid_top_r = 0.2f;
            p.lid_outer_l = p.lid_outer_r = 0.3f;
            p.look_y = 0.3f;
            p.flick_x = 1.4f;
            p.mouth_w = 0.7f;
            p.mouth_curve = -0.2f;
            p.mouth_open = 0.15f;
            break;

        case MochiState::kHeartEyes:
            // TickHeart() lam tim dap.
            p.heart = 1.0f;
            p.mouth_w = 1.15f;
            p.mouth_curve = 0.8f;
            p.mouth_open = 0.3f;
            break;

        case MochiState::kXEyes:
            p.xeyes = 1.0f;
            p.mouth_w = 0.8f;
            p.mouth_open = 0.35f;
            p.mouth_tilt = -0.2f;
            p.mouth_y_off = 1.0f;
            break;

        case MochiState::kSneeze:
            // Tu the nghi; TickSneeze() chay chu ky "ah... ah... CHOO!".
            p.eye_l_h = p.eye_r_h = 0.95f;
            p.lid_outer_l = p.lid_outer_r = 0.15f;
            p.look_y = 0.1f;
            p.mouth_w = 0.7f;
            p.mouth_curve = -0.05f;
            p.mouth_open = 0.05f;
            break;

        case MochiState::kGlitch:
            // TickGlitch() thinh thoang "nhieu" ngau nhien (bo qua lo xo).
            p.mouth_curve = 0.1f;
            break;

        case MochiState::kListening:
            p.eye_r_h = 1.1f;
            p.look_y = -0.5f;
            p.mouth_w = 0.9f;
            p.mouth_curve = 0.15f;
            p.mouth_open = 0.05f;
            break;

        case MochiState::kThinking:
            // TickGaze() luan phien nhin len-trai / len-phai.
            p.eye_l_h = 0.95f;
            p.eye_r_h = 1.1f;
            p.lid_inner_l = 0.4f;
            p.curiosity = 1.0f;
            p.look_y = -1.0f;
            p.mouth_w = 0.8f;
            p.mouth_curve = -0.08f;
            p.mouth_open = 0.05f;
            p.mouth_tilt = 0.3f;
            break;

        case MochiState::kSpeaking:
            p.eye_l_h = p.eye_r_h = 1.05f;
            p.eye_l_w = p.eye_r_w = 1.03f;
            p.mouth_w = 1.02f;
            p.mouth_curve = 0.25f;
            p.mouth_open = 0.35f;
            break;

        case MochiState::kIdle:
        default:
            p.curiosity = 1.0f;
            p.mouth_curve = 0.15f;
            break;
    }

    return p;
}

// So tick rung moi trang thai (0 = khong rung).
uint32_t MochiDisplay::FlickTicksFor(MochiState state) {
    switch (state) {
        case MochiState::kLaughing:  return 20;   // ~0.8s
        case MochiState::kConfused:  return 12;   // ~0.5s
        case MochiState::kExcited:   return 30;   // ~1.2s
        case MochiState::kCrying:
        case MochiState::kShocked:
        case MochiState::kScared:
        case MochiState::kSick:
        case MochiState::kCold:
        case MochiState::kSneeze:    return kFlickForever;   // Sneeze tu quan ly flick_y
        default:                     return 0;
    }
}

void MochiDisplay::ApplyState(MochiState state) {
    if (state_ == state) return;
    quirk_ = Quirk::kNone;
    quirk_ticks_left_ = 0;
    idle_ticks_ = 0;
    mouth_flap_ticks_left_ = 0;
    consecutive_low_picks_ = 0;
    anim_phase_ = 0.0f;
    gaze_ticks_left_ = 0;
    sneeze_t_ = 0;
    glitch_ticks_left_ = 0;

    state_ = state;
    target_ = TargetForState(state_);
    gaze_x_ = target_.look_x;
    gaze_y_ = target_.look_y;
    flick_ticks_left_ = FlickTicksFor(state_);
    // current_ khong gan lai: lo xo dan toi target_ moi (co "nay").
}

void MochiDisplay::StepSpringToTarget() {
    float* cur = current_.data();
    float* vel = velocity_.data();
    const float* tgt = target_.data();
    for (int i = 0; i < kFaceParamCount; ++i) {
        StepSpring(cur[i], vel[i], tgt[i]);
    }
}

// ---------------------------------------------------------------------
// Mi tam giac bang chuoi dai chu nhat (bac thang). anchor_left = true:
// canh dai nam sat mep TRAI cua mat (tam giac vuong goc o goc tren-trai,
// dinh nhon huong xuong o mep trai); false: doi xung ben phai.
//   tired/sad : mat trai anchor_left=true,  mat phai anchor_left=false
//   angry     : mat trai anchor_left=false, mat phai anchor_left=true
// ---------------------------------------------------------------------
void MochiDisplay::ApplyStairLid(MochiShape* strips, int ex, int top, int w, int h,
                                 float intensity, bool anchor_left) {
    int depth = static_cast<int>(Clamp01(intensity) * 0.55f * h);
    if (depth < 2) {
        for (int k = 0; k < kStairCount; ++k) Hide(strips[k]);
        return;
    }
    for (int k = 0; k < kStairCount; ++k) {
        int y0 = top + depth * k / kStairCount;
        int y1 = top + depth * (k + 1) / kStairCount;
        if (k == 0) y0 = top - 1;  // che luon vien tren cua mat
        int sh = y1 - y0;
        int wk = w * (2 * (kStairCount - k) - 1) / (2 * kStairCount) + 1;
        if (sh <= 0 || wk <= 0) {
            Hide(strips[k]);
            continue;
        }
        int x = anchor_left ? (ex - 1) : (ex + w + 1 - wk);
        Place(strips[k], x, y0, wk, sh);
    }
}

void MochiDisplay::ApplyCurrentToObjects() {
    const int cx = width_ / 2;

    // Rung: doi dau moi 2 tick (~6Hz). Bien do da duoc lo xo lam muot.
    const int sign = ((tick_count_ >> 1) & 1) ? 1 : -1;
    const int flick_dx = Round(current_.flick_x * scale_ * sign);
    const int flick_dy = Round(current_.flick_y * scale_ * sign);

    const int dx = Round(current_.look_x * amp_x_) + flick_dx;
    const int cross_px = Round(current_.cross * cross_amp_);
    const float cur_k = Clamp01(current_.curiosity);

    int ex[2], top[2], w[2], h[2];
    for (int i = 0; i < 2; ++i) {
        float hs = (i == 0) ? current_.eye_l_h : current_.eye_r_h;
        float ws = (i == 0) ? current_.eye_l_w : current_.eye_r_w;
        // Curiosity: mat PHIA NGOAI cao them khi liec ngang.
        bool outer = (i == 0 && current_.look_x < 0.0f) || (i == 1 && current_.look_x > 0.0f);
        if (outer) {
            hs *= 1.0f + 0.3f * cur_k * std::min(1.0f, std::fabs(current_.look_x));
        }
        h[i] = std::max(2, static_cast<int>(eye_h_ * hs * (1.0f - blink_amt_)));
        w[i] = std::max(2, static_cast<int>(eye_w_ * ws));
    }
    ex[0] = cx - eye_gap_ / 2 - w[0] + dx + cross_px;
    ex[1] = cx + eye_gap_ / 2 + dx - cross_px;

    const bool heart = current_.heart > 0.5f;
    const bool xeyes = current_.xeyes > 0.5f;

    const float cy = eye_cy_ + current_.eye_y_off * scale_ + current_.look_y * amp_y_ + flick_dy;
    for (int i = 0; i < 2; ++i) {
        top[i] = std::clamp(Round(cy - h[i] / 2.0f), 0, std::max(0, height_ - h[i]));
        if (heart || xeyes) {
            Hide(eye_[i]);
        } else {
            Place(eye_[i], ex[i], top[i], w[i], h[i]);
        }
        ApplyGlyph(i, ex[i], top[i], w[i], h[i], heart, xeyes);
    }

    for (int i = 0; i < 2; ++i) {
        const bool left = (i == 0);

        // Ma HAPPY: chu nhat bo goc mau nen day tu day mat len.
        float c = Clamp01(left ? current_.cheek_l : current_.cheek_r);
        int off = static_cast<int>(c * 0.72f * h[i]);
        if (off >= 1) {
            Place(cheek_[i], ex[i] - 1, top[i] + h[i] - off, w[i] + 2, h[i]);
        } else {
            Hide(cheek_[i]);
        }

        // Mi tren phang.
        float lt = Clamp01(left ? current_.lid_top_l : current_.lid_top_r);
        int lh = static_cast<int>(lt * h[i]);
        if (lh >= 1) {
            Place(lid_top_[i], ex[i] - 1, top[i] - 1, w[i] + 2, lh + 1);
        } else {
            Hide(lid_top_[i]);
        }

        // Mi tam giac: ngoai (tired/sad) va trong (angry).
        float outer_k = left ? current_.lid_outer_l : current_.lid_outer_r;
        float inner_k = left ? current_.lid_inner_l : current_.lid_inner_r;
        ApplyStairLid(lid_out_[i], ex[i], top[i], w[i], h[i], outer_k, /*anchor_left=*/left);
        ApplyStairLid(lid_in_[i], ex[i], top[i], w[i], h[i], inner_k, /*anchor_left=*/!left);
    }

    if (kEnableMouth) {
        int mdx = Round(current_.look_x * amp_x_ * 0.5f) + flick_dx;
        ApplyMouthToObjects(mdx, flick_dy);
    }
    ApplyDrops(ex, top, w, h);
}

// ---------------------------------------------------------------------
// Mat bieu tuong ghep tu pool glyph_ (hinh chu nhat trang):
//  - Tim: 2 thuy (hinh tron) + 7 dai bac thang thu nho dan thanh mui nhon.
//  - X: 7 dai x 2 net cheo, moi dai dich ngang nen thanh duong cheo.
// ---------------------------------------------------------------------
void MochiDisplay::ApplyGlyph(int i, int ex, int top, int w, int h, bool heart, bool xeyes) {
    MochiShape* g = glyph_[i];
    if (!heart && !xeyes) {
        for (int k = 0; k < kGlyphCount; ++k) Hide(g[k]);
        return;
    }

    if (heart) {
        int d = std::max(4, std::min(static_cast<int>(w * 0.56f), h));
        int ty = top + d / 2;                       // hang rong nhat cua 2 thuy
        int th = std::max(0, top + h - ty);
        SetRadius(g[0], LV_RADIUS_CIRCLE);
        SetRadius(g[1], LV_RADIUS_CIRCLE);
        Place(g[0], ex, top, d, d);
        Place(g[1], ex + w - d, top, d, d);
        for (int k = 0; k < kStairCount; ++k) {
            int y0 = ty + th * k / kStairCount;
            int y1 = ty + th * (k + 1) / kStairCount;
            int wk = w * (2 * (kStairCount - k) - 1) / (2 * kStairCount) + 1;
            SetRadius(g[2 + k], 0);
            Place(g[2 + k], ex + (w - wk) / 2, y0, wk, y1 - y0);
        }
        for (int k = 2 + kStairCount; k < kGlyphCount; ++k) Hide(g[k]);
        return;
    }

    // Mat X
    int s = std::max(4, static_cast<int>(w * 0.3f));   // net day -> duong cheo lien tuc
    for (int k = 0; k < kStairCount; ++k) {
        int y0 = top + h * k / kStairCount;
        int y1 = top + h * (k + 1) / kStairCount;
        int xl = ex + (w - s) * k / (kStairCount - 1);
        int xr = ex + (w - s) * (kStairCount - 1 - k) / (kStairCount - 1);
        SetRadius(g[2 * k], 0);
        SetRadius(g[2 * k + 1], 0);
        Place(g[2 * k], xl, y0, s, y1 - y0);
        Place(g[2 * k + 1], xr, y0, s, y1 - y0);
    }
}

// ---------------------------------------------------------------------
// Giot mo hoi (o 2 mep man) hoac nuoc mat (roi tu mep ngoai moi mat),
// roi deu theo tick_count_, 2 giot lech pha nua chu ky.
// ---------------------------------------------------------------------
void MochiDisplay::ApplyDrops(const int* ex, const int* top, const int* w, const int* h) {
    const bool tears = current_.tears > 0.5f;
    const bool sweat = current_.sweat > 0.5f;
    if (!tears && !sweat) {
        Hide(drop_[0]);
        Hide(drop_[1]);
        return;
    }
    const int dw = std::max(2, static_cast<int>(3 * scale_));
    const int dh = std::max(3, static_cast<int>(5 * scale_));
    constexpr int kPeriod = 40;  // ~1.6s

    for (int d = 0; d < 2; ++d) {
        int ph = static_cast<int>((tick_count_ + d * (kPeriod / 2)) % kPeriod);
        int x, y0, y1;
        if (tears) {
            // Nuoc mat: tu day mat, mep NGOAI (tranh vung mieng o giua).
            x = (d == 0) ? (ex[0] + 2) : (ex[1] + w[1] - 2 - dw);
            y0 = top[d] + h[d];
            y1 = height_ - dh - 1;
        } else {
            x = (d == 0) ? 2 : (width_ - 2 - dw);
            y0 = 1;
            y1 = height_ / 2;
        }
        int y = y0 + (std::max(0, y1 - y0)) * ph / kPeriod;
        Place(drop_[d], x, y, dw, dh);
    }
}

// ---------------------------------------------------------------------
// Mieng: ghep kMouthSegCount capsule chong lan (giu nguyen v4.1), them
// dx/dy de mieng di theo huong nhin va rung theo mat.
// ---------------------------------------------------------------------
void MochiDisplay::ApplyMouthToObjects(int dx, int dy) {
    int center_x = width_ / 2 + dx;

    int seg_w = std::max(3, static_cast<int>(mouth_seg_w_ * current_.mouth_w));
    int step = std::max(1, static_cast<int>(seg_w * kMouthOverlapRatio));
    int total_span = (kMouthSegCount - 1) * step + seg_w;
    int start_x = center_x - total_span / 2;

    for (int i = 0; i < kMouthSegCount; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(kMouthSegCount - 1);
        float center_weight = std::max(0.0f, 1.0f - 4.0f * (t - 0.5f) * (t - 0.5f));

        float curve_dy = current_.mouth_curve * mouth_curve_amp_ * center_weight;
        curve_dy += current_.mouth_tilt * mouth_tilt_amp_ * (t - 0.5f) * 2.0f;
        curve_dy += current_.mouth_y_off * scale_;

        int seg_h = mouth_base_h_ + static_cast<int>(current_.mouth_open * mouth_open_amp_ * center_weight);
        seg_h = std::max(2, seg_h);

        float width_weight = 0.6f + 0.4f * center_weight;
        int this_seg_w = std::max(3, static_cast<int>(seg_w * width_weight));
        int seg_x = start_x + i * step + (seg_w - this_seg_w) / 2;
        int seg_y = mouth_y_ + static_cast<int>(curve_dy) - seg_h / 2 + dy;

        int lo = eye_y_ + eye_h_;
        int hi = std::max(lo, height_ - seg_h - 1);
        seg_y = std::clamp(seg_y, lo, hi);

        Place(mouth_[i], seg_x, seg_y, this_seg_w, seg_h);
    }
}

// ---------------------------------------------------------------------
// Vong lap animation tren LVGL timer.
// ---------------------------------------------------------------------
void MochiDisplay::OnAnimTick(lv_timer_t* timer) {
    auto* self = static_cast<MochiDisplay*>(lv_timer_get_user_data(timer));
    self->tick_count_++;

    DisplayLockGuard lock(self);

    switch (self->state_) {
        case MochiState::kIdle:
            self->TickIdle();
            break;
        case MochiState::kListening:
            self->TickListening();
            break;
        case MochiState::kSpeaking:
            self->TickSpeaking();
            break;
        case MochiState::kThinking:
        case MochiState::kWorried:
        case MochiState::kSuspicious:
        case MochiState::kBored:
        case MochiState::kExcited:
        case MochiState::kRelaxed:
        case MochiState::kShy:
            self->TickGaze();
            break;
        case MochiState::kSleepy:
            self->TickSleepy();
            break;
        case MochiState::kDizzy:
            self->TickDizzy();
            break;
        case MochiState::kDelicious:
            self->TickDelicious();
            break;
        case MochiState::kPleading:
            self->TickPleading();
            break;
        case MochiState::kSick:
            self->TickSick();
            break;
        case MochiState::kCold:
            self->TickCold();
            break;
        case MochiState::kHeartEyes:
            self->TickHeart();
            break;
        case MochiState::kSneeze:
            self->TickSneeze();
            break;
        case MochiState::kGlitch:
            self->TickGlitch();
            break;
        default:
            break;
    }

    // Mieng noi doc lap voi emotion (giu nguyen v4).
    if ((self->talking_override_ || self->talking_session_active_) &&
        self->state_ != MochiState::kSpeaking) {
        self->TickMouthFlap();
    }

    self->TickFlick();
    self->TickBlink();
    self->StepSpringToTarget();
    self->ApplyCurrentToObjects();
}

// Huong nhin tu dong, kieu RoboEyes idle: den gio thi chon huong moi.
void MochiDisplay::TickGaze() {
    if (gaze_ticks_left_ > 0) {
        --gaze_ticks_left_;
    } else {
        static const float kX[] = {0, 0, -1, 1, -1, 1, 0, -0.6f, 0.6f};
        static const float kY[] = {0, 0, 0, 0, -1, -1, 1, 0.6f, 0.6f};
        static const float kBoredX[] = {0, 1, -1, 0};
        static const float kBoredY[] = {-1, -0.6f, -0.6f, 0.3f};
        switch (state_) {
            case MochiState::kIdle:
            case MochiState::kExcited: {
                int i = RandRange(0, 8);
                gaze_x_ = kX[i];
                gaze_y_ = kY[i];
                gaze_ticks_left_ = (state_ == MochiState::kIdle) ? RandRange(50, 125)
                                                                  : RandRange(12, 25);
                break;
            }
            case MochiState::kThinking:
                gaze_toggle_ = !gaze_toggle_;
                gaze_x_ = gaze_toggle_ ? 1.0f : -1.0f;
                gaze_y_ = -1.0f;
                gaze_ticks_left_ = RandRange(40, 70);
                break;
            case MochiState::kWorried:
                gaze_x_ = (esp_random() & 1) ? 0.7f : -0.7f;
                gaze_y_ = 0.3f;
                gaze_ticks_left_ = RandRange(10, 25);
                break;
            case MochiState::kSuspicious:
                gaze_toggle_ = !gaze_toggle_;
                gaze_x_ = gaze_toggle_ ? 1.0f : -1.0f;
                gaze_y_ = 0.0f;
                gaze_ticks_left_ = RandRange(60, 110);
                break;
            case MochiState::kBored: {
                int i = RandRange(0, 3);
                gaze_x_ = kBoredX[i];
                gaze_y_ = kBoredY[i];
                gaze_ticks_left_ = RandRange(70, 120);
                break;
            }
            case MochiState::kShy:
                // Nhin tron 1 cai (toggle=false) roi lai ne di (toggle=true).
                gaze_toggle_ = !gaze_toggle_;
                if (gaze_toggle_) {
                    gaze_x_ = -0.8f;
                    gaze_y_ = 0.7f;
                    gaze_ticks_left_ = RandRange(50, 90);
                } else {
                    gaze_x_ = 0.0f;
                    gaze_y_ = -0.1f;
                    gaze_ticks_left_ = RandRange(10, 18);
                }
                break;
            case MochiState::kRelaxed:
                gaze_x_ = RandRange(-5, 5) / 10.0f;
                gaze_y_ = 0.0f;
                gaze_ticks_left_ = RandRange(90, 150);
                break;
            default:
                gaze_ticks_left_ = 50;
                break;
        }
    }
    target_.look_x = gaze_x_;
    target_.look_y = gaze_y_;
}

void MochiDisplay::TickIdle() {
    if (quirk_ == Quirk::kStretch) {
        // Ngap: nua dau nham mat + khep mieng, nua sau mo to + ha mieng.
        bool first_half = quirk_ticks_left_ > quirk_ticks_total_ / 2;
        target_.eye_l_h = target_.eye_r_h = first_half ? 0.15f : 1.25f;
        target_.mouth_open = first_half ? 0.05f : 0.55f;
        target_.mouth_w = first_half ? 0.75f : 1.15f;

        if (quirk_ticks_left_ > 0) {
            quirk_ticks_left_--;
        }
        if (quirk_ticks_left_ == 0) {
            target_ = TargetForState(MochiState::kIdle);
            quirk_ = Quirk::kNone;
            idle_ticks_ = 0;
            ScheduleNextQuirk();
        }
    } else {
        idle_ticks_++;
        if (idle_ticks_ >= next_quirk_at_) {
            TriggerStretch();
        }
    }
    TickGaze();  // sau cung de ghi de look_x/look_y neu target_ vua bi reset
}

void MochiDisplay::ScheduleNextQuirk() {
    uint32_t rand_ms = 12000 + (esp_random() % 12000);  // 12-24s
    next_quirk_at_ = rand_ms / kTickIntervalMs;
}

void MochiDisplay::TriggerStretch() {
    quirk_ = Quirk::kStretch;
    quirk_ticks_total_ = 40;  // ~1.6s
    quirk_ticks_left_ = quirk_ticks_total_;
}

void MochiDisplay::TickListening() {
    speak_phase_ = (speak_phase_ + 1) % 60;
    float pulse = sinf(speak_phase_ * 0.105f);
    target_.eye_r_h = 1.1f + 0.06f * pulse;
    target_.mouth_open = 0.05f + 0.05f * pulse;
}

void MochiDisplay::TickSpeaking() {
    speak_phase_ = (speak_phase_ + 1) % 40;
    float pulse = 1.02f + 0.08f * sinf(speak_phase_ * 0.157f);
    target_.eye_l_h = target_.eye_r_h = pulse;
    TickMouthFlap();
}

// Giu nguyen v4: chon muc mo/khep theo tung am tiet.
void MochiDisplay::TickMouthFlap() {
    if (mouth_flap_ticks_left_ == 0) {
        bool force_open = consecutive_low_picks_ >= 2;
        bool pick_low = !force_open && (esp_random() % 100) < 22;

        if (pick_low) {
            mouth_flap_target_ = 0.1f + (esp_random() % 10) / 100.0f;
            consecutive_low_picks_++;
        } else {
            mouth_flap_target_ = 0.4f + (esp_random() % 55) / 100.0f;
            consecutive_low_picks_ = 0;
        }
        mouth_flap_ticks_left_ = pick_low ? 4 + (esp_random() % 3)
                                           : 6 + (esp_random() % 5);
    }
    mouth_flap_ticks_left_--;

    target_.mouth_open = mouth_flap_target_;
    target_.mouth_curve = 0.2f;
}

void MochiDisplay::SetMouthTalking(bool active) {
    talking_override_ = active;
    if (!active) {
        mouth_flap_ticks_left_ = 0;
        consecutive_low_picks_ = 0;
    }
}

// Buon ngu: mi phang sut xuong roi mo len cham (chu ky ~3.6s), dau gat nhe.
void MochiDisplay::TickSleepy() {
    anim_phase_ += 0.07f;
    float s = 0.5f - 0.5f * cosf(anim_phase_);  // 0..1
    target_.lid_top_l = target_.lid_top_r = 0.45f + 0.45f * s;
    target_.eye_y_off = 1.0f + 2.0f * s;
}

// Chong mat: huong nhin quay vong tron, mat lac nhip nhang.
void MochiDisplay::TickDizzy() {
    anim_phase_ += 0.3f;
    target_.look_x = cosf(anim_phase_);
    target_.look_y = sinf(anim_phase_);
    target_.cross = 0.4f * sinf(anim_phase_ * 0.5f);
    target_.mouth_curve = 0.3f * sinf(anim_phase_);
    target_.mouth_tilt = 0.5f * cosf(anim_phase_);
}

// Liem moi: mieng lac trai-phai.
void MochiDisplay::TickDelicious() {
    anim_phase_ += 0.22f;
    target_.mouth_tilt = 0.55f * sinf(anim_phase_);
}

// Mat cun con: "tho" cham.
void MochiDisplay::TickPleading() {
    anim_phase_ += 0.1f;
    target_.eye_l_h = target_.eye_r_h = 1.2f + 0.05f * sinf(anim_phase_);
}

// Om: huong nhin troi cham sang 2 ben.
void MochiDisplay::TickSick() {
    anim_phase_ += 0.08f;
    target_.look_x = 0.3f * sinf(anim_phase_);
}

// Lanh: mieng danh lap cap (doi muc moi 2 tick).
void MochiDisplay::TickCold() {
    target_.mouth_open = ((tick_count_ >> 1) & 1) ? 0.3f : 0.08f;
}

// Mat tim dap ~1Hz.
void MochiDisplay::TickHeart() {
    anim_phase_ += 0.26f;
    float s = 1.0f + 0.12f * (0.5f + 0.5f * sinf(anim_phase_));
    target_.eye_l_h = target_.eye_r_h = s;
    target_.eye_l_w = target_.eye_r_w = s;
}

// Hat hoi, chu ky 120 tick (~4.8s): 30 "ah..." (ngua mat len, ha mieng) ->
// 10 "CHOO!" (nham mat, mieng to, rung doc, dau gat xuong) -> 20 xi mui
// (mat long lanh) -> nghi.
void MochiDisplay::TickSneeze() {
    uint32_t t = sneeze_t_++;
    FaceParams p = TargetForState(MochiState::kSneeze);
    if (t < 30) {
        float k = t / 30.0f;
        p.look_y = -1.0f;
        p.eye_l_h = p.eye_r_h = 1.0f + 0.2f * k;
        p.eye_y_off = -2.0f * k;
        p.mouth_w = 0.7f;
        p.mouth_open = 0.3f + 0.5f * k;
        p.mouth_curve = 0.0f;
    } else if (t < 40) {
        p.eye_l_h = p.eye_r_h = 0.1f;
        p.eye_y_off = 3.0f;
        p.look_y = 1.0f;
        p.flick_y = 2.5f;
        p.mouth_w = 1.1f;
        p.mouth_open = 1.0f;
        p.mouth_curve = -0.2f;
    } else if (t < 60) {
        p.eye_l_h = p.eye_r_h = 0.85f;
        p.lid_outer_l = p.lid_outer_r = 0.35f;
        p.look_y = 0.4f;
        p.mouth_w = 0.6f;
    } else if (t >= 120) {
        sneeze_t_ = 0;
    }
    target_ = p;
}

void MochiDisplay::SnapParam(float FaceParams::*field, float value) {
    current_.*field = value;
    target_.*field = value;
    velocity_.*field = 0.0f;
}

// Nhieu kieu robot: thinh thoang (~5%/tick) vao dot nhieu 2-5 tick, moi
// tick dat NGAY (khong qua lo xo) kich thuoc/vi tri mat + mieng ngau nhien,
// het dot thi dat lai nguyen trang thai goc.
void MochiDisplay::TickGlitch() {
    auto rnd = [](float lo, float hi) {
        return lo + (hi - lo) * static_cast<float>(esp_random() % 1000) / 999.0f;
    };
    if (glitch_ticks_left_ > 0) {
        --glitch_ticks_left_;
        if (glitch_ticks_left_ == 0) {
            const FaceParams b = TargetForState(MochiState::kGlitch);
            static float FaceParams::* const kFields[] = {
                &FaceParams::eye_l_h, &FaceParams::eye_r_h, &FaceParams::eye_l_w,
                &FaceParams::eye_r_w, &FaceParams::look_x, &FaceParams::eye_y_off,
                &FaceParams::cross, &FaceParams::mouth_open, &FaceParams::mouth_curve,
                &FaceParams::mouth_tilt};
            for (auto f : kFields) SnapParam(f, b.*f);
        } else {
            SnapParam(&FaceParams::eye_l_w, rnd(0.3f, 1.6f));
            SnapParam(&FaceParams::eye_r_w, rnd(0.3f, 1.6f));
            SnapParam(&FaceParams::eye_l_h, rnd(0.1f, 1.2f));
            SnapParam(&FaceParams::eye_r_h, (esp_random() % 100) < 30 ? 0.05f : rnd(0.1f, 1.2f));
            SnapParam(&FaceParams::look_x, rnd(-1.0f, 1.0f));
            SnapParam(&FaceParams::eye_y_off, rnd(-3.0f, 3.0f));
            SnapParam(&FaceParams::cross, rnd(-0.5f, 0.5f));
            SnapParam(&FaceParams::mouth_open, rnd(0.0f, 1.0f));
            SnapParam(&FaceParams::mouth_curve, rnd(-1.0f, 1.0f));
            SnapParam(&FaceParams::mouth_tilt, rnd(-1.0f, 1.0f));
        }
    } else if ((esp_random() % 100) < 5) {
        glitch_ticks_left_ = RandRange(2, 5);
    }
}

// Het thoi gian rung thi dua bien do ve 0 (lo xo se lam tat dan).
void MochiDisplay::TickFlick() {
    if (flick_ticks_left_ == kFlickForever) return;
    if (flick_ticks_left_ > 0) {
        if (--flick_ticks_left_ == 0) {
            target_.flick_x = 0.0f;
            target_.flick_y = 0.0f;
        }
    }
}

void MochiDisplay::ScheduleNextBlink() {
    uint32_t rand_ms = 2000 + (esp_random() % 4000);
    next_blink_at_ = tick_count_ + (rand_ms / kTickIntervalMs);
}

// Chop mat trong tick (khong con lv_anim): dong ~80ms, mo ~120ms.
void MochiDisplay::TickBlink() {
    if (blink_phase_ == 0) {
        if (tick_count_ >= next_blink_at_) {
            blink_phase_ = 1;
        } else {
            return;
        }
    }
    if (blink_phase_ == 1) {
        blink_amt_ += 0.5f;
        if (blink_amt_ >= 1.0f) {
            blink_amt_ = 1.0f;
            blink_phase_ = 2;
        }
    } else if (blink_phase_ == 2) {
        blink_amt_ -= 0.34f;
        if (blink_amt_ <= 0.0f) {
            blink_amt_ = 0.0f;
            blink_phase_ = 0;
            // ~12% chop doi.
            if (!double_blink_pending_ && (esp_random() % 100) < 12) {
                double_blink_pending_ = true;
                next_blink_at_ = tick_count_ + (150 + (esp_random() % 150)) / kTickIntervalMs;
            } else {
                double_blink_pending_ = false;
                ScheduleNextBlink();
            }
        }
    }
}