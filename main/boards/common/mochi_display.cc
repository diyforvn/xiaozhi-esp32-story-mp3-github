#include "mochi_display.h"
#include <esp_log.h>
#include <esp_random.h>
#include <algorithm>
#include <cmath>

static const char* TAG = "MochiDisplay";

// Tan suat animation tick (ms). 40ms ~ 25fps, du muot cho SSD1306 I2C.
static constexpr uint32_t kTickIntervalMs = 40;
static constexpr float kDt = kTickIntervalMs / 1000.0f;

// Tham so lo xo dung chung cho toan bo FaceParams. Ty le damping/critical
// ~0.6 -> nay nhe 1 nhip roi on dinh trong ~250-300ms.
static constexpr float kSpringStiffness = 180.0f;
static constexpr float kSpringDamping = 16.0f;

namespace {
inline void StepSpring(float& current, float& velocity, float target) {
    float accel = (target - current) * kSpringStiffness - velocity * kSpringDamping;
    velocity += accel * kDt;
    current += velocity * kDt;
}
}  // namespace

MochiDisplay::MochiDisplay(esp_lcd_panel_io_handle_t io_handle,
                            esp_lcd_panel_handle_t panel_handle,
                            int width, int height,
                            bool mirror_x, bool mirror_y)
    : OledDisplay(io_handle, panel_handle, width, height, mirror_x, mirror_y),
      current_x_offset_(0) {
    // CHI duoc phep lam viec khong dinh toi LVGL o day (tinh toan hinh
    // hoc). KHONG duoc tao lv_obj/lv_timer trong constructor - man hinh
    // LVGL chua chac da san sang.

    // Scale hinh hoc theo kich thuoc man hinh thuc te. Gia tri goc
    // (36x36, gap 10, radius 10) duoc tinh RIENG cho 128x64 - neu man
    // hinh cua ban khac ty le, scale se giu dung ty le tuong doi.
    float scale = std::min(width_ / 128.0f, height_ / 64.0f);
    eye_w_  = static_cast<int>(36 * scale);
    eye_h_  = static_cast<int>(36 * scale);
    eye_gap_ = static_cast<int>(10 * scale);
    eye_radius_ = std::max(2, static_cast<int>(10 * scale));
    // Can giua theo chieu doc - khong con phai chua cho cho mieng nhu
    // ban truoc.
    eye_y_ = (height_ - eye_h_) / 2;
}

void MochiDisplay::SetupUI() {
    Display::SetupUI();

    BuildFace();
    BuildLids();
    ScheduleNextBlink();
    ScheduleNextQuirk();

    anim_timer_ = lv_timer_create(OnAnimTick, kTickIntervalMs, this);

    ESP_LOGI(TAG, "Mochi face v3 (FluxGarage-style) initialized: %dx%d", width_, height_);
}

MochiDisplay::~MochiDisplay() {
    if (anim_timer_) {
        lv_timer_del(anim_timer_);
        anim_timer_ = nullptr;
    }
}

// ---------------------------------------------------------------------
// 2 mat CHU NHAT BO GOC tren nen den - dung ky thuat cua FluxGarage
// RoboEyes. Khong con mieng/long may.
// ---------------------------------------------------------------------
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

    auto make_eye = [&]() {
        lv_obj_t* eye = lv_obj_create(face_root_);
        lv_obj_remove_style_all(eye);
        lv_obj_set_size(eye, eye_w_, eye_h_);
        lv_obj_set_style_bg_color(eye, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(eye, eye_radius_, 0);
        return eye;
    };

    eye_left_  = make_eye();
    eye_right_ = make_eye();

    int center_x = width_ / 2;
    lv_obj_set_pos(eye_left_,  center_x - eye_gap_ / 2 - eye_w_, eye_y_);
    lv_obj_set_pos(eye_right_, center_x + eye_gap_ / 2, eye_y_);
}

// ---------------------------------------------------------------------
// 4 "lid" - hinh vuong xoay 45 do mau nen (den), dat tai 4 goc tren cua
// 2 mat. Kich thuoc + do mo tang theo cuong do bieu cam (0..1) de gia
// lap eyelid-cut: goc TRONG cho ANGRY, goc NGOAI cho TIRED/SAD. Tao
// SAU BuildFace() nen ve DE LEN TREN mat (thu tu z: con sinh sau nam
// tren con sinh truoc trong LVGL).
// ---------------------------------------------------------------------
void MochiDisplay::BuildLids() {
    DisplayLockGuard lock(this);

    auto make_lid = [&]() {
        lv_obj_t* lid = lv_obj_create(face_root_);
        lv_obj_remove_style_all(lid);
        lv_obj_set_style_bg_color(lid, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(lid, LV_OPA_TRANSP, 0);
        lv_obj_set_style_transform_rotation(lid, 450, 0);  // 45.0 do co dinh
        return lid;
    };

    lid_inner_left_  = make_lid();
    lid_outer_left_  = make_lid();
    lid_inner_right_ = make_lid();
    lid_outer_right_ = make_lid();
    // Kich thuoc/vi tri thuc te tinh moi tick trong ApplyCurrentToObjects()
    // vi phu thuoc cuong do bieu cam hien tai.
}

// ---------------------------------------------------------------------
// Diem noi voi Xiaozhi: application.cc goi SetEmotion(emotion_str) moi
// khi server gui truong "emotion" ve. Bo chuoi khop voi tap emotion
// chuan cua xiaozhi-esp32. Chuoi khong nhan dien duoc se roi ve kIdle.
// ---------------------------------------------------------------------
void MochiDisplay::SetEmotion(const char* emotion) {
    if (emotion == nullptr) {
        ApplyState(MochiState::kIdle);
        return;
    }

    std::string e(emotion);
    MochiState next = MochiState::kIdle;

    if (e == "happy" || e == "delicious") {
        next = MochiState::kHappy;
    } else if (e == "laughing" || e == "funny" || e == "silly") {
        next = MochiState::kLaughing;
    } else if (e == "sad") {
        next = MochiState::kSad;
    } else if (e == "crying") {
        next = MochiState::kCrying;
    } else if (e == "angry") {
        next = MochiState::kAngry;
    } else if (e == "surprised" || e == "shocked") {
        next = MochiState::kSurprised;
    } else if (e == "loving" || e == "kissy") {
        next = MochiState::kLoving;
    } else if (e == "embarrassed") {
        next = MochiState::kEmbarrassed;
    } else if (e == "confused") {
        next = MochiState::kConfused;
    } else if (e == "cool" || e == "confident" || e == "relaxed" || e == "winking") {
        next = MochiState::kCool;
    } else if (e == "sleepy") {
        next = MochiState::kSleepy;
    } else if (e == "thinking" || e == "connecting" || e == "microchip_ai") {
        next = MochiState::kThinking;
    } else if (e == "listening") {
        next = MochiState::kListening;
    } else if (e == "speaking") {
        next = MochiState::kSpeaking;
    } else {
        next = MochiState::kIdle;
    }

    ApplyState(next);
}

// ---------------------------------------------------------------------
// Bang tra cuu trung tam: 1 trang thai -> 1 bo tham so hinh hoc muc
// tieu. SUA BIEU CAM O DAY.
// Dieu chinh de tao hinh khuon mat tu nhien, can doi va dep hon cho Xiaozhi
// ---------------------------------------------------------------------
FaceParams MochiDisplay::TargetForState(MochiState state) const {
    FaceParams p;  // mac dinh = neutral

    switch (state) {
        case MochiState::kHappy:
            // Vui ve: mat hoi nhem, to ra hieu thuan thai
            p.eye_l_h = p.eye_r_h = 0.55f;
            p.eye_l_w = p.eye_r_w = 1.05f;
            break;

        case MochiState::kLaughing:
            // Cuoi lon: mat gan nhem hoan toan, hep hon happy
            p.eye_l_h = p.eye_r_h = 0.22f;
            p.eye_l_w = p.eye_r_w = 0.95f;
            p.eye_y_off = 1.0f;  // mat dich len nhat dinh
            break;

        case MochiState::kSad:
            // Buon: mat hoi mo rong, mi mat ru xuong (lid_outer)
            p.eye_l_h = p.eye_r_h = 0.65f;
            p.lid_outer_l = p.lid_outer_r = 0.9f;
            p.eye_y_off = 1.5f;  // mat dich len mot chut de to ra su buon
            break;

        case MochiState::kCrying:
            // Khoc: mat nhem hon sad, mi sat nhau nhieu hon
            p.eye_l_h = p.eye_r_h = 0.45f;
            p.lid_outer_l = p.lid_outer_r = 1.0f;
            p.lid_inner_l = p.lid_inner_r = 0.3f;  // them cat goc trong
            p.eye_y_off = 2.0f;  // mat dich len nhieu hon
            break;

        case MochiState::kAngry:
            // Tuc gian: mat hep nhan, cat goc trong (lid_inner) de tao cam giac mat chau
            p.eye_l_h = p.eye_r_h = 0.35f;
            p.eye_l_w = p.eye_r_w = 0.9f;
            p.lid_inner_l = p.lid_inner_r = 0.9f;
            p.eye_y_off = -1.0f;  // mat dich xuong de to ra su cuc khoan
            break;

        case MochiState::kSurprised:
            // Ngac nhien: mat to, tron xoe, dich len
            p.eye_l_h = p.eye_r_h = 1.35f;
            p.eye_l_w = p.eye_r_w = 1.1f;
            p.eye_y_off = -3.0f;
            break;

        case MochiState::kLoving:
            // Yeu thuong: mat hep, dai ra (hieu ung nheo mat)
            p.eye_l_h = p.eye_r_h = 0.38f;
            p.eye_l_w = p.eye_r_w = 1.15f;
            p.lid_outer_l = p.lid_outer_r = 0.2f;  // mi mat huong ngoai nhat dinh
            break;

        case MochiState::kEmbarrassed:
            // Ngan ngung: mat nhem, mi huong ngoai
            p.eye_l_h = p.eye_r_h = 0.55f;
            p.lid_outer_l = p.lid_outer_r = 0.5f;
            p.lid_inner_l = p.lid_inner_r = 0.2f;
            break;

        case MochiState::kConfused:
            // Boi roi: mat lech, nhan may
            p.eye_l_h = 0.85f; 
            p.eye_r_h = 1.15f;
            p.lid_inner_l = 0.5f;
            p.lid_outer_r = 0.4f;
            p.eye_l_w = 0.95f;  // mat trai hep hon
            p.eye_r_w = 1.05f;  // mat phai rong hon
            break;

        case MochiState::kCool:
            // Cool: mot mat nham (winking effect)
            p.eye_l_h = 1.0f;  // mat trai binh thuong
            p.eye_r_h = 0.12f; // mat phai nhem
            p.eye_r_w = 1.05f; // mat phai co the rong hon mot chut
            break;

        case MochiState::kSleepy:
            // Buon ngu: mat nho, mi ru xuong, mat dich xuong
            p.eye_l_h = p.eye_r_h = 0.35f;
            p.eye_y_off = 2.5f;
            p.lid_outer_l = p.lid_outer_r = 0.6f;
            p.lid_inner_l = p.lid_inner_r = 0.3f;
            break;

        case MochiState::kListening:
            // Tap trung nghe: mat hoi nhem, to ra su tap trung
            p.eye_l_h = 0.65f;
            p.eye_r_h = 0.7f;
            p.lid_outer_l = p.lid_outer_r = 0.2f;
            break;

        case MochiState::kThinking:
            // Dang suy nghi: mat hoi mo rong, nhin xa xoi
            p.eye_l_h = p.eye_r_h = 0.95f;
            p.eye_l_w = p.eye_r_w = 0.98f;
            p.eye_y_off = -1.0f;  // nhin len truoc
            break;

        case MochiState::kSpeaking:
            // Dang noi: mat to ra, to ra su hoat bong
            p.eye_l_h = p.eye_r_h = 1.05f;
            p.eye_l_w = p.eye_r_w = 1.02f;
            break;

        case MochiState::kIdle:
        default:
            // neutral: gia tri mac dinh cua FaceParams la du
            break;
    }

    return p;
}

void MochiDisplay::ApplyState(MochiState state) {
    if (state_ == state) return;
    // Roi khoi kIdle: huy quirk dang chay, reset dem idle.
    quirk_ = Quirk::kNone;
    quirk_ticks_left_ = 0;
    idle_ticks_ = 0;
    // Reset offset x khi doi sang bieu cam khac
    current_x_offset_ = 0;

    state_ = state;
    target_ = TargetForState(state_);
    // current_ KHONG gan lai o day - de OnAnimTick tu chay lo xo dan
    // toi target_ moi, tao chuyen dong co "nay" giua 2 bieu cam.
}

void MochiDisplay::StepSpringToTarget() {
    StepSpring(current_.eye_l_h, velocity_.eye_l_h, target_.eye_l_h);
    StepSpring(current_.eye_r_h, velocity_.eye_r_h, target_.eye_r_h);
    StepSpring(current_.eye_l_w, velocity_.eye_l_w, target_.eye_l_w);
    StepSpring(current_.eye_r_w, velocity_.eye_r_w, target_.eye_r_w);
    StepSpring(current_.eye_y_off, velocity_.eye_y_off, target_.eye_y_off);

    StepSpring(current_.lid_inner_l, velocity_.lid_inner_l, target_.lid_inner_l);
    StepSpring(current_.lid_inner_r, velocity_.lid_inner_r, target_.lid_inner_r);
    StepSpring(current_.lid_outer_l, velocity_.lid_outer_l, target_.lid_outer_l);
    StepSpring(current_.lid_outer_r, velocity_.lid_outer_r, target_.lid_outer_r);
}

void MochiDisplay::ApplyCurrentToObjects() {
    int center_x = width_ / 2;

    int eye_l_h = std::max(2, static_cast<int>(eye_h_ * current_.eye_l_h));
    int eye_r_h = std::max(2, static_cast<int>(eye_h_ * current_.eye_r_h));
    int eye_l_w = std::max(2, static_cast<int>(eye_w_ * current_.eye_l_w));
    int eye_r_w = std::max(2, static_cast<int>(eye_w_ * current_.eye_r_w));
    int y_off = static_cast<int>(current_.eye_y_off);

    lv_obj_set_size(eye_left_, eye_l_w, eye_l_h);
    lv_obj_set_size(eye_right_, eye_r_w, eye_r_h);
    // Can lai mat theo tam DUOI (khong phai tam tren) khi chieu cao
    // doi, de mat "nham tu duoi len" tu nhien.
    // Them current_x_offset_ vao vi tri x de xu ly animation idle/thinking
    int eye_left_x  = center_x - eye_gap_ / 2 - eye_l_w + current_x_offset_;
    int eye_right_x = center_x + eye_gap_ / 2 + current_x_offset_;
    int eye_bottom  = eye_y_ + eye_h_;
    int eye_l_top = eye_bottom - eye_l_h + y_off;
    int eye_r_top = eye_bottom - eye_r_h + y_off;
    lv_obj_set_pos(eye_left_,  eye_left_x, eye_l_top);
    lv_obj_set_pos(eye_right_, eye_right_x, eye_r_top);

    // --- Lid (eyelid-cut) ---
    // QUAN TRONG: size tinh theo chieu cao mat THUC TE dang hien thi
    // (eye_ref_h, da nhan voi ty le eye_l_h/eye_r_h), KHONG theo eye_h_
    // co dinh. Ly do: cac bieu cam nhu angry/sad/crying da tu co mat
    // lai (scale 0.5-0.75) roi; neu tinh theo eye_h_ goc thi vet cat se
    // qua lon so voi mat da co nho, "nuot" gan het mat trong tren OLED
    // that (du tren canvas mo phong co khu rang cua nen trong do hon).
    auto lid_size_for = [&](float intensity, int eye_ref_h) {
        return static_cast<int>(eye_ref_h * (0.12f + 0.22f * std::clamp(intensity, 0.0f, 1.0f)));
    };
    auto place_lid = [&](lv_obj_t* lid, int corner_x, int corner_y, float intensity, int eye_ref_h) {
        int size = lid_size_for(intensity, eye_ref_h);
        lv_obj_set_size(lid, size, size);
        lv_obj_set_pos(lid, corner_x - size / 2, corner_y - size / 2);
        lv_opa_t opa = static_cast<lv_opa_t>(std::clamp(intensity, 0.0f, 1.0f) * 255.0f);
        lv_obj_set_style_bg_opa(lid, opa, 0);
    };
    // Goc TRONG cua mat trai = canh PHAI cua no (gan tam man hinh);
    // goc TRONG cua mat phai = canh TRAI cua no. Goc NGOAI la canh con lai.
    place_lid(lid_inner_left_,  eye_left_x + eye_l_w, eye_l_top, current_.lid_inner_l, eye_l_h);
    place_lid(lid_outer_left_,  eye_left_x,           eye_l_top, current_.lid_outer_l, eye_l_h);
    place_lid(lid_inner_right_, eye_right_x,          eye_r_top, current_.lid_inner_r, eye_r_h);
    place_lid(lid_outer_right_, eye_right_x + eye_r_w, eye_r_top, current_.lid_outer_r, eye_r_h);
}

// ---------------------------------------------------------------------
// Vong lap animation - chay tren LVGL timer, KHONG block task chinh.
// ---------------------------------------------------------------------
void MochiDisplay::OnAnimTick(lv_timer_t* timer) {
    auto* self = static_cast<MochiDisplay*>(lv_timer_get_user_data(timer));
    self->tick_count_++;

    DisplayLockGuard lock(self);

    // Xử lý các animation đặc biệt trước (có thể cập nhật target_)
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
            self->TickThinking();
            break;
        default:
            // Happy/Laughing/Sad/Angry/Surprised/Loving/Embarrassed/
            // Confused/Cool/Sleepy/Crying: chi can spring toi target
            // roi giu nguyen, khong can dao dong lien tuc.
            break;
    }

    // Sau đó chạy spring và áp dụng lên object
    self->StepSpringToTarget();
    self->ApplyCurrentToObjects();

    if (!self->blinking_ && self->tick_count_ >= self->next_blink_at_) {
        self->PlayBlink();
    }
}

void MochiDisplay::TickIdle() {
    // Xử lý quirk (stretch, glance)
    int extra_x = 0;
    
    if (quirk_ != Quirk::kNone) {
        if (quirk_ == Quirk::kGlance) {
            float progress = 1.0f - static_cast<float>(quirk_ticks_left_) /
                                         static_cast<float>(quirk_ticks_total_);
            extra_x = static_cast<int>(glance_dir_ * 10.0f * sinf(progress * 3.14159f));
        } else if (quirk_ == Quirk::kStretch) {
            // 2 pha: nua dau nhem mat lai that nho, nua sau mo bung ra
            // to hon binh thuong, roi tro ve idle
            bool first_half = quirk_ticks_left_ > quirk_ticks_total_ / 2;
            target_.eye_l_h = target_.eye_r_h = first_half ? 0.12f : 1.3f;
        }
        
        if (quirk_ticks_left_ > 0) {
            quirk_ticks_left_--;
        }
        if (quirk_ticks_left_ == 0) {
            if (quirk_ == Quirk::kStretch) {
                target_ = TargetForState(MochiState::kIdle);
            }
            quirk_ = Quirk::kNone;
            idle_ticks_ = 0;
            ScheduleNextQuirk();
        }
    } else {
        idle_ticks_++;
        if (idle_ticks_ >= next_quirk_at_) {
            TriggerRandomQuirk();
        }
    }
    
    // Di chuyen mat sang hai ben nhe nhe (idle movement)
    idle_offset_x_ += idle_dir_;
    if (idle_offset_x_ > 4 || idle_offset_x_ < -4) {
        idle_dir_ = -idle_dir_;
    }
    
    // Luu offset x vao target de ApplyCurrentToObjects su dung
    // Note: ApplyCurrentToObjects se tinh toan vi tri x base tren eye_gap_
    // va eye_w_, sau do them offset. Ta can phai truyen offset nay vao.
    // Hien tai, ta se su dung mot cach don gian: luu offset vao bien tam
    // va ApplyCurrentToObjects se su dung no.
    current_x_offset_ = idle_offset_x_ + extra_x;
}

void MochiDisplay::ScheduleNextQuirk() {
    uint32_t rand_ms = 12000 + (esp_random() % 12000);  // 12-24s
    next_quirk_at_ = rand_ms / kTickIntervalMs;
}

void MochiDisplay::TriggerRandomQuirk() {
    bool do_stretch = (esp_random() % 100) < 60;

    if (do_stretch) {
        quirk_ = Quirk::kStretch;
        quirk_ticks_total_ = 40;  // ~1.6s, 2 pha 0.8s moi pha
        quirk_ticks_left_ = quirk_ticks_total_;
    } else {
        quirk_ = Quirk::kGlance;
        quirk_ticks_total_ = 25;  // ~1s
        quirk_ticks_left_ = quirk_ticks_total_;
        glance_dir_ = (esp_random() % 2 == 0) ? 1 : -1;
    }
}

void MochiDisplay::TickListening() {
    // target_ da dat san bat doi xung nhe trong TargetForState()
    // Them mot chut animation de mat phai nhap nhay nhe, to ra su tap trung
    speak_phase_ = (speak_phase_ + 1) % 60;  // Cham hon Speaking
    float listen_pulse = 0.65f + 0.08f * sinf(speak_phase_ * 0.105f);
    target_.eye_r_h = listen_pulse;
    target_.eye_l_h = 0.65f;  // mat trai on dinh
}

void MochiDisplay::TickSpeaking() {
    // Khong con mieng de the hien dang noi - thay bang nhip "tho" nhe
    // (pulse) chieu cao 2 mat, tao cam giac dang hoat dong/phan hoi.
    // Su dung target_ de he spring xu ly, tranh ghi de truc tiep len lv_obj
    speak_phase_ = (speak_phase_ + 1) % 40;
    float pulse = 0.95f + 0.12f * sinf(speak_phase_ * 0.157f);
    target_.eye_l_h = target_.eye_r_h = pulse;
}

void MochiDisplay::TickThinking() {
    // Dang suy nghi: mat di chuyen nhe sang hai ben va nhin len truoc
    idle_offset_x_ += idle_dir_;
    if (idle_offset_x_ > 3 || idle_offset_x_ < -3) {
        idle_dir_ = -idle_dir_;
    }
    // Cap nhat offset x va eye_y_off target de ApplyCurrentToObjects su dung
    current_x_offset_ = idle_offset_x_;
    target_.eye_y_off = -2.5f;  // nhin len truoc
}

void MochiDisplay::ScheduleNextBlink() {
    uint32_t rand_ms = 2000 + (esp_random() % 4000);
    next_blink_at_ = tick_count_ + (rand_ms / kTickIntervalMs);
}

void MochiDisplay::PlayBlink() {
    blinking_ = true;

    // Bat dau tu chieu cao HIEN TAI cua mat trai (co the dang la 0.5x
    // neu dang happy chang han), khong dung eye_h_ co dinh - tranh
    // giat hinh khi chop mat luc dang trong 1 bieu cam khac neutral.
    int start_h = std::max(2, static_cast<int>(eye_h_ * current_.eye_l_h));

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, this);
    lv_anim_set_values(&a, start_h, 2);
    lv_anim_set_time(&a, 90);
    lv_anim_set_playback_time(&a, 90);
    lv_anim_set_exec_cb(&a, OnBlinkAnim);
    lv_anim_set_ready_cb(&a, OnBlinkFinished);
    lv_anim_start(&a);
}

void MochiDisplay::OnBlinkAnim(void* var, int32_t value) {
    auto* self = static_cast<MochiDisplay*>(var);
    int eye_bottom = self->eye_y_ + self->eye_h_;
    int y_off = static_cast<int>(self->current_.eye_y_off);
    lv_obj_set_height(self->eye_left_, value);
    lv_obj_set_height(self->eye_right_, value);
    lv_obj_set_y(self->eye_left_,  eye_bottom - value + y_off);
    lv_obj_set_y(self->eye_right_, eye_bottom - value + y_off);
}

void MochiDisplay::OnBlinkFinished(lv_anim_t* anim) {
    auto* self = static_cast<MochiDisplay*>(anim->var);
    self->blinking_ = false;

    // ~12% co hoi chop them 1 lan gan ngay sau do (double-blink), gioi
    // han 1 lan/chuoi bang double_blink_pending_ de khong lap vo han.
    if (!self->double_blink_pending_ && (esp_random() % 100) < 12) {
        self->double_blink_pending_ = true;
        uint32_t rand_ms = 150 + (esp_random() % 150);
        self->next_blink_at_ = self->tick_count_ + (rand_ms / kTickIntervalMs);
    } else {
        self->double_blink_pending_ = false;
        self->ScheduleNextBlink();
    }
}