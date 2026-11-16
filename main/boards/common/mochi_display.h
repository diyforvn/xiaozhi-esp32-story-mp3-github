#pragma once


#include "display/oled_display.h"
#include <lvgl.h>
#include <string>
#include <cstdint>


enum class MochiState {
    kIdle, kListening, kSpeaking, kThinking,
    kHappy, kLaughing, kSad, kCrying, kAngry, kSurprised,
    kLoving, kEmbarrassed, kConfused, kCool, kSleepy,
    // --- MOI ---
    kWinking,      // "winking"
    kShocked,      // "shocked"
    kDelicious,    // "delicious" (liem moi)
    kSilly,        // "silly" (mat lac)
    kKissy,        // "kissy" (chum moi)
    kRelaxed,      // "relaxed"
    kConfident,    // "confident"
    kWorried,      // "worried" / "nervous" / "anxious"
    kScared,       // "scared" / "afraid" / "fear"
    kSuspicious,   // "suspicious" / "skeptical"
    kBored,        // "bored"
    kExcited,      // "excited"
    kDizzy,        // "dizzy"
    // --- MOI (dot 2) ---
    kMischievous,  // "mischievous" / "naughty"
    kDetermined,   // "determined"
    kPleading,     // "pleading" / "begging" (mat cun con)
    kProud,        // "proud"
    kJealous,      // "jealous"
    kShy,          // "shy"
    kSick,         // "sick" / "ill"
    kCold,         // "cold" / "freezing"
    kHeartEyes,    // "heart_eyes"
    kXEyes,        // "x_eyes" / "xeyes" / "fainted"
    kSneeze,       // "sneeze"
    kGlitch,       // "glitch"
};


enum class Quirk {
    kNone,
    kStretch,
};


struct FaceParams {
    // Ty le cao/rong tung mat (1.0 = mac dinh). Mat co dinh tam doc.
    float eye_l_h = 1.0f, eye_r_h = 1.0f;
    float eye_l_w = 1.0f, eye_r_w = 1.0f;
    float eye_y_off = 0.0f;   // px, am = len

    // Huong nhin -1..1 (x: trai->phai, y: tren->duoi).
    float look_x = 0.0f, look_y = 0.0f;
    // Mat lac: +1 = hai mat chum vao nhau, -1 = loi ra ngoai.
    float cross = 0.0f;
    // 0..1: khi liec ngang, mat PHIA NGOAI cao them (RoboEyes curiosity).
    float curiosity = 0.0f;

    // Cat mi hinh tam giac (0..1 = do sau 0..~55% chieu cao mat).
    float lid_inner_l = 0.0f, lid_inner_r = 0.0f;   // goc gan mui (ANGRY)
    float lid_outer_l = 0.0f, lid_outer_r = 0.0f;   // goc ngoai (TIRED/SAD)
    // Mi tren phang (0..1 = ty le chieu cao bi che tu tren xuong).
    float lid_top_l = 0.0f, lid_top_r = 0.0f;
    // "Ma" (HAPPY): che tu duoi len, 0..1 -> mat thanh hinh vom ^.
    float cheek_l = 0.0f, cheek_r = 0.0f;

    // Bien do rung (px). Chi co tac dung khi flick_ticks_left_ > 0.
    float flick_x = 0.0f, flick_y = 0.0f;
    // >0.5 thi hien 2 giot: sweat = mo hoi o 2 mep man, tears = nuoc mat
    // roi tu mep ngoai moi mat.
    float sweat = 0.0f, tears = 0.0f;

    // Mat bieu tuong (an mat thuong, ve bang glyph_): heart = mat trai
    // tim, xeyes = mat chu X. Dung 2 tham so rieng (0/1) thay vi 1 so
    // "chon kieu" de lo xo khong noi suy qua kieu khac khi doi trang thai.
    float heart = 0.0f, xeyes = 0.0f;

    // --- Mieng (giu nguyen y nghia v4) ---
    float mouth_w = 1.0f;
    float mouth_curve = 0.0f;
    float mouth_open = 0.0f;
    float mouth_tilt = 0.0f;
    float mouth_y_off = 0.0f;

    float* data() { return &eye_l_h; }
    const float* data() const { return &eye_l_h; }
};
static_assert(sizeof(FaceParams) == 28 * sizeof(float),
              "FaceParams chi duoc chua float (StepSpring duyet theo mang) - cap nhat so 28 khi them/bot truong");
constexpr int kFaceParamCount = static_cast<int>(sizeof(FaceParams) / sizeof(float));


struct MochiShape {
    lv_obj_t* obj = nullptr;
    int x = -10000, y = -10000, w = -1, h = -1;
    bool hidden = true;
    int radius = -1;   // cache style radius (heart/X dung chung 1 pool shape)
};

class MochiDisplay : public OledDisplay {
public:
    MochiDisplay(esp_lcd_panel_io_handle_t io_handle,
                 esp_lcd_panel_handle_t panel_handle,
                 int width, int height,
                 bool mirror_x, bool mirror_y);
    ~MochiDisplay() override;

    // Tao lv_obj trong SetupUI(), KHONG tao trong constructor.
    void SetupUI() override;

    // application.cc goi moi khi emotion doi. Chuoi la khong nhan ra -> kIdle.
    void SetEmotion(const char* emotion) override;

    // Giu nguyen y nghia v4: bat/tat mieng nhap nhay theo am thanh THAT.
    void SetMouthTalking(bool active);

private:
    static constexpr int kStairCount = 7;
    static constexpr int kMouthSegCount = 7;
    // Pool shape trang cho mat tim (2 thuy + 7 dai tam giac = 9) va mat X
    // (7 dai x 2 net cheo = 14). Dung chung 1 pool 14 shape / mat.
    static constexpr int kGlyphCount = 2 * kStairCount;

    void BuildFace();
    void BuildMouth();
    MochiShape MakeShape(bool white, int radius);
    static void Place(MochiShape& s, int x, int y, int w, int h);
    static void Hide(MochiShape& s);
    static void SetRadius(MochiShape& s, int radius);

    void ApplyState(MochiState state);
    FaceParams TargetForState(MochiState state) const;
    static uint32_t FlickTicksFor(MochiState state);

    void StepSpringToTarget();
    void ApplyCurrentToObjects();
    void ApplyStairLid(MochiShape* strips, int ex, int top, int w, int h,
                       float intensity, bool anchor_left);
    void ApplyGlyph(int i, int ex, int top, int w, int h, bool heart, bool xeyes);
    void ApplyDrops(const int* ex, const int* top, const int* w, const int* h);
    void ApplyMouthToObjects(int dx, int dy);

    static void OnAnimTick(lv_timer_t* timer);

    void TickIdle();
    void TickListening();
    void TickSpeaking();
    void TickMouthFlap();
    void TickGaze();
    void TickSleepy();
    void TickDizzy();
    void TickDelicious();
    void TickPleading();
    void TickSick();
    void TickCold();
    void TickHeart();
    void TickSneeze();
    void TickGlitch();
    // Dat thang ca current_/target_ (bo qua lo xo) - dung cho glitch.
    void SnapParam(float FaceParams::*field, float value);
    void TickFlick();
    void TickBlink();

    void ScheduleNextBlink();
    void ScheduleNextQuirk();
    void TriggerStretch();

    // width_/height_ ke thua tu Display.
    lv_obj_t* face_root_ = nullptr;

    // Chi so 0 = mat trai, 1 = mat phai.
    MochiShape eye_[2];
    MochiShape cheek_[2];
    MochiShape lid_top_[2];
    MochiShape lid_in_[2][kStairCount];
    MochiShape lid_out_[2][kStairCount];
    MochiShape mouth_[kMouthSegCount];
    MochiShape glyph_[2][kGlyphCount];
    MochiShape drop_[2];

    lv_timer_t* anim_timer_ = nullptr;

    MochiState state_ = MochiState::kIdle;
    FaceParams current_;
    FaceParams target_;
    FaceParams velocity_;

    // Hinh hoc (tinh trong constructor theo scale man hinh).
    float scale_ = 1.0f;
    int eye_w_ = 34, eye_h_ = 28, eye_gap_ = 10, eye_radius_ = 8;
    int eye_y_ = 4;       // dinh mat luc idle
    int eye_cy_ = 18;     // tam doc mat luc idle
    float amp_x_ = 14.0f, amp_y_ = 3.0f, cross_amp_ = 6.0f;

    int mouth_y_ = 0;
    int mouth_span_ = 0;
    int mouth_seg_w_ = 0;
    int mouth_base_h_ = 0;
    int mouth_open_amp_ = 0;
    int mouth_curve_amp_ = 0;
    int mouth_tilt_amp_ = 0;

    uint32_t tick_count_ = 0;

    // Chop mat: 0 = mo, 1 = dang dong, 2 = dang mo lai.
    uint32_t next_blink_at_ = 0;
    int blink_phase_ = 0;
    float blink_amt_ = 0.0f;   // 0 = mo het, 1 = nham het
    bool double_blink_pending_ = false;

    // Nhip tho mat (Listening/Speaking) + pha chung cho cac tick dac thu.
    int speak_phase_ = 0;
    float anim_phase_ = 0.0f;

    // Huong nhin tu dong (TickGaze).
    float gaze_x_ = 0.0f, gaze_y_ = 0.0f;
    uint32_t gaze_ticks_left_ = 0;
    bool gaze_toggle_ = false;

    // Hat hoi (kSneeze): dem tick trong chu ky; Glitch: so tick nhieu con lai.
    uint32_t sneeze_t_ = 0;
    uint32_t glitch_ticks_left_ = 0;

    // Rung: so tick con lai (0xFFFFFFFF = lien tuc).
    uint32_t flick_ticks_left_ = 0;

    // Mieng nhap nhay luc noi (giu nguyen v4).
    float mouth_flap_target_ = 0.3f;
    uint32_t mouth_flap_ticks_left_ = 0;
    int consecutive_low_picks_ = 0;
    bool talking_override_ = false;
    bool talking_session_active_ = false;

    // Quirk (chi khi kIdle).
    Quirk quirk_ = Quirk::kNone;
    uint32_t quirk_ticks_left_ = 0;
    uint32_t quirk_ticks_total_ = 0;
    uint32_t idle_ticks_ = 0;
    uint32_t next_quirk_at_ = 0;
};