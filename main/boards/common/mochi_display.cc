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


static constexpr float kMouthOverlapRatio = 0.55f;

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
    // duoc tinh RIENG cho 128x64 - neu man hinh cua ban khac ty le,
    // scale se giu dung ty le tuong doi.
    float scale = std::min(width_ / 128.0f, height_ / 64.0f);
    
    eye_w_  = static_cast<int>(34 * scale);
    eye_h_  = static_cast<int>(28 * scale);
    eye_gap_ = static_cast<int>(10 * scale);
    eye_radius_ = std::max(2, static_cast<int>(10 * scale));
    // Neo mat GAN DINH man hinh (thay vi can giua doc nhu ban v3) de
    // nhuong hang duoi cho mieng.
    eye_y_ = std::max(2, static_cast<int>(4 * scale));

    // --- Hinh hoc mieng (v4.1 - overlap thay vi gap) ---
    // Suy nguoc mouth_seg_w_ tu be rong TONG mong muon (mouth_span_): voi
    // n mieng va step = seg_w * kMouthOverlapRatio, tong be rong thuc te
    // la seg_w * (1 + ratio*(n-1)) - giai nguoc de tong nay ~= mouth_span_.
    mouth_span_      = static_cast<int>(44 * scale);
    {
        float denom = 1.0f + kMouthOverlapRatio * (kMouthSegCount - 1);
        mouth_seg_w_ = std::max(3, static_cast<int>(mouth_span_ / denom));
    }
    mouth_base_h_    = std::max(2, static_cast<int>(5 * scale));
    mouth_open_amp_  = static_cast<int>(8 * scale);
    mouth_curve_amp_ = static_cast<int>(7 * scale);
    mouth_tilt_amp_  = static_cast<int>(5 * scale);

    // LUU Y (fix "mieng sat mat"): truoc day mouth_gap duoc cong THANG
    // vao mouth_y_ (tam mieng), nhung mieng con ve XUONG DUOI tam mot
    // khoang mouth_base_h_/2 nua - nen khoang cach THUC TE tu day mat
    // den DINH mieng chi con mouth_gap - mouth_base_h_/2 (vd 4 - 2 = 2px
    // o scale=1), nhin nhu mieng dinh sat vao mat. Sua bang cach cong
    // THEM mouth_base_h_/2 de mouth_top_gap phan anh dung khoang cach
    // toi DINH mieng (canh tren), khong phai toi tam.
    int mouth_top_gap = std::max(3, static_cast<int>(7 * scale));
    mouth_y_ = eye_y_ + eye_h_ + mouth_top_gap + mouth_base_h_ / 2;
    // KIEM TRA NGAY TRONG CONSTRUCTOR: neu voi bien do cong/mo/lech
    // TOI DA, mieng van co the cham day man hinh, day mieng len lai -
    // dam bao KHONG BAO GIO tran khung hinh du man hinh nho toi dau.
    // ApplyMouthToObjects() van kep (clamp) lai lan nua o runtime de
    // an toan kep.
    int mouth_max_extent = mouth_base_h_ / 2 + mouth_open_amp_ + mouth_curve_amp_ + mouth_tilt_amp_;
    if (mouth_y_ + mouth_max_extent > height_ - 1) {
        mouth_y_ = std::max(eye_y_ + eye_h_ + 1, height_ - 1 - mouth_max_extent);
    }
}

void MochiDisplay::SetupUI() {
    Display::SetupUI();

    BuildFace();
    BuildLids();
    BuildEyeShine();
    BuildMouth();
    ScheduleNextBlink();
    ScheduleNextQuirk();

    anim_timer_ = lv_timer_create(OnAnimTick, kTickIntervalMs, this);

    ESP_LOGI(TAG, "Mochi face v4 (FluxGarage-eyes + curved mouth) initialized: %dx%d", width_, height_);
}

MochiDisplay::~MochiDisplay() {
    if (anim_timer_) {
        lv_timer_del(anim_timer_);
        anim_timer_ = nullptr;
    }
}

// ---------------------------------------------------------------------
// 2 mat CHU NHAT BO GOC tren nen den - dung ky thuat cua FluxGarage
// RoboEyes. Khong long may rieng (mieng duoc dung trong BuildMouth()).
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
        // LV_RADIUS_CIRCLE thay vi eye_radius_ co dinh: LVGL tu kep ban
        // kinh ve min(w,h)/2 luc ve, nen mat LUON tron/vien-thuoc hoan
        // hao o MOI kich thuoc (tron xoe luc idle vi w==h, tu chuyen
        // thanh pill khi bi bop dep luc happy/angry/cool/blink...) - khong
        // can tinh lai radius theo tung state nhu truoc, va mem mai hon
        // han so voi bo goc 10px co dinh (nhin "vuong" chu khong "mochi").
        lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, 0);
        return eye;
    };

    eye_left_  = make_eye();
    eye_right_ = make_eye();

    int center_x = width_ / 2;
    lv_obj_set_pos(eye_left_,  center_x - eye_gap_ / 2 - eye_w_, eye_y_);
    lv_obj_set_pos(eye_right_, center_x + eye_gap_ / 2, eye_y_);
}

// ---------------------------------------------------------------------
// 4 "lid" - hinh TRON mau nen (den), dat tai 4 goc tren cua 2 mat. Kich
// thuoc + do mo tang theo cuong do bieu cam (0..1) de gia lap eyelid-cut:
// goc TRONG cho ANGRY, goc NGOAI cho TIRED/SAD. Dung hinh tron thay vi
// vuong-xoay-45-do de mep cat la duong CONG mem (kieu "mochi") thay vi
// duong cheo thang cung. Tao SAU BuildFace() nen ve DE LEN TREN mat (thu
// tu z: con sinh sau nam tren con sinh truoc trong LVGL).
// ---------------------------------------------------------------------
void MochiDisplay::BuildLids() {
    DisplayLockGuard lock(this);

    auto make_lid = [&]() {
        lv_obj_t* lid = lv_obj_create(face_root_);
        lv_obj_remove_style_all(lid);
        lv_obj_set_style_bg_color(lid, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(lid, LV_OPA_TRANSP, 0);
        // Doi tu hinh vuong xoay 45 do (mep cat la 1 duong CHEO THANG,
        // cung va "gay") sang hinh TRON (LV_RADIUS_CIRCLE) - mep an vao
        // goc mat gio la 1 DUONG CONG mem, hop voi phong cach "mochi"
        // (tron, mup) hon nhieu. Khong con can transform_rotation nua.
        lv_obj_set_style_radius(lid, LV_RADIUS_CIRCLE, 0);
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
// 2 diem "shine" mau nen (den), cat mot lo tron nho o goc tren-trong cua
// moi mat de gia lap anh sang phan chieu - chi tiet kinh dien cua mat
// kieu "kawaii" giup mat co chieu sau thay vi la 1 khoi trang phang li.
// Tao SAU BuildLids() nen ve DE LEN TREN lid (khong sao, khong giao
// nhau ve vi tri trong da so bieu cam). Vi tri/do mo thuc te tinh moi
// tick trong ApplyCurrentToObjects() (xem shine_opa_for()).
// ---------------------------------------------------------------------
void MochiDisplay::BuildEyeShine() {
    DisplayLockGuard lock(this);

    auto make_shine = [&]() {
        lv_obj_t* shine = lv_obj_create(face_root_);
        lv_obj_remove_style_all(shine);
        lv_obj_set_style_bg_color(shine, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(shine, LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(shine, LV_RADIUS_CIRCLE, 0);
        return shine;
    };

    eye_shine_left_  = make_shine();
    eye_shine_right_ = make_shine();
}

// ---------------------------------------------------------------------
// Diem noi voi Xiaozhi: application.cc goi SetEmotion(emotion_str) moi
// khi server gui truong "emotion" ve. Bo chuoi khop voi tap emotion
// chuan cua xiaozhi-esp32. Chuoi khong nhan dien duoc se roi ve kIdle.
// ---------------------------------------------------------------------
void MochiDisplay::SetEmotion(const char* emotion) {
    if (emotion == nullptr) {
        talking_session_active_ = false;
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

    // --- Tu dong phat hien "phien dang noi" tu chuoi goi SetEmotion() ---
    // VAN DE THUC TE (nguyen nhan mieng bi dung nhap nhay): server
    // thuong CHI goi SetEmotion("speaking") 1 LAN luc bat dau cau, sau
    // do TRONG LUC VAN DANG PHAT AM THANH lai gui tiep cac emotion
    // "phan ung" theo sac thai cau noi nhu "happy"/"surprised"/"sad"...
    // Neu chi dua vao (state_ == kSpeaking) de quyet dinh co chay
    // TickMouthFlap() hay khong thi moi lan nhu vay mieng se "dung
    // hinh" theo bieu cam moi (vd chuyen sang HAPPY) du loa van dang
    // keu - dung y nhu bug ban bao cao.
    //
    // Cach xu ly: coi "speaking" la diem BAT DAU 1 "phien noi", va CHI
    // coi idle/listening/thinking/sleepy la diem KET THUC phien noi (day
    // la cac trang thai server thuc te se gui khi cau DA NOI XONG). Cac
    // emotion "phan ung" con lai (happy/laughing/sad/crying/angry/
    // surprised/loving/embarrassed/confused/cool) KHONG lam thay doi cai
    // "phien" - neu dang trong phien noi thi VAN TIEP TUC coi la dang
    // noi (mieng nhap nhay duoi lop bieu cam moi qua TargetForState()),
    // neu dang KHONG noi (vd server gui "happy" luc dang idle) thi van
    // khong tu nhien bat nhap nhay.
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
            // happy/laughing/sad/crying/angry/surprised/loving/
            // embarrassed/confused/cool: GIU NGUYEN gia tri hien tai cua
            // talking_session_active_ - day chinh la "chia khoa" giai
            // quyet van de cua ban.
            break;
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
            // Vui ve: mat nhem het co "^_^" cho ro dang cuoi kieu emoji
            // (thay vi chi hoi nhem 0.55 truoc day - nhin van "trung
            // tinh" chu chua ro la dang vui).
            p.eye_l_h = p.eye_r_h = 0.4f;
            p.eye_l_w = p.eye_r_w = 1.08f;
            p.mouth_w = 1.2f;
            p.mouth_curve = 0.8f;   // cuoi ro, sau hon (tu 0.65)
            p.mouth_open = 0.2f;
            break;

        case MochiState::kLaughing:
            // Cuoi lon: mat nhem thanh net "^" mong, mieng mo rong het
            // co - dung phong cach 😂/😆, khong con giu ve dat.
            p.eye_l_h = p.eye_r_h = 0.16f;
            p.eye_l_w = p.eye_r_w = 0.95f;
            p.eye_y_off = 1.2f;  // mat dich len nhat dinh
            p.mouth_w = 1.32f;
            p.mouth_curve = 0.95f;  // cuoi het co (tu 0.85)
            p.mouth_open = 0.75f;   // mieng mo to hon (tu 0.6)
            break;

        case MochiState::kSad:
            // Buon: mat mo rong hon mot chut, mi ru xuong het co, mieng
            // miu sau hon - ro dang 🙁 thay vi chi hoi phung phiu.
            p.eye_l_h = p.eye_r_h = 0.6f;
            p.lid_outer_l = p.lid_outer_r = 1.0f;  // tu 0.9
            p.mouth_w = 0.85f;
            p.mouth_curve = -0.65f;  // miu sau hon (tu -0.5)
            p.mouth_y_off = 1.3f;
            p.eye_y_off = 1.8f;  // mat dich len nhieu hon de to ra su buon
            break;

        case MochiState::kCrying:
            // Khoc: mat nhem hon sad, mi sat nhau nhieu hon, mieng ho to
            // hon kieu nuc no ro ret (giong 😭 thay vi chi hoi ho).
            p.eye_l_h = p.eye_r_h = 0.4f;
            p.lid_outer_l = p.lid_outer_r = 1.0f;
            p.lid_inner_l = p.lid_inner_r = 0.4f;  // them cat goc trong (tu 0.3)
            p.eye_y_off = 2.3f;  // mat dich len nhieu hon
            p.mouth_w = 0.85f;
            p.mouth_curve = -0.75f;  // miu manh hon (tu -0.6)
            p.mouth_open = 0.4f;     // ho mieng to hon (tu 0.3)
            p.mouth_y_off = 1.7f;
            break;

        case MochiState::kAngry:
            // Tuc gian: mat hep nhan, cat goc trong (lid_inner) de tao cam giac mat chau
            p.eye_l_h = p.eye_r_h = 0.35f;
            p.eye_l_w = p.eye_r_w = 0.9f;
            p.lid_inner_l = p.lid_inner_r = 1.0f;  // tu 0.9, "chau" het co
            p.eye_y_off = -1.0f;  // mat dich xuong de to ra su cuc khoan
            p.mouth_w = 0.85f;
            p.mouth_curve = -0.5f;  // mieng cup manh hon (tu -0.35)
            p.mouth_open = 0.15f;
            break;

        case MochiState::kSurprised:
            // Ngac nhien: mat to het co, tron xoe, dich len manh - ro dang
            // "giat minh" kieu 😲 (tu 1.35 len 1.5, dich len nhieu hon).
            p.eye_l_h = p.eye_r_h = 1.5f;
            p.eye_l_w = p.eye_r_w = 1.15f;
            p.eye_y_off = -3.5f;
            p.mouth_w = 0.65f;
            p.mouth_curve = 0.0f;
            p.mouth_open = 1.0f;  // "O" tron het co (tu 0.9)
            break;

        case MochiState::kLoving:
            // Yeu thuong: mat hep, dai ra (hieu ung nheo mat)
            p.eye_l_h = p.eye_r_h = 0.38f;
            p.eye_l_w = p.eye_r_w = 1.15f;
            p.lid_outer_l = p.lid_outer_r = 0.2f;  // mi mat huong ngoai nhat dinh
            p.mouth_w = 1.05f;
            p.mouth_curve = 0.7f;   // cuoi tinh tu, ro net hon (tu 0.55)
            p.mouth_open = 0.25f;
            break;

        case MochiState::kEmbarrassed:
            // Ngan ngung: mat nhem, mi huong ngoai ro hon, cuoi guong
            // guong lech ro ret hon (kieu 😳 thay vi chi hoi lech).
            p.eye_l_h = p.eye_r_h = 0.55f;
            p.lid_outer_l = p.lid_outer_r = 0.6f;  // tu 0.5
            p.lid_inner_l = p.lid_inner_r = 0.2f;
            p.mouth_w = 0.75f;
            p.mouth_curve = 0.25f;  // tu 0.15
            p.mouth_tilt = 0.45f;   // lech mot ben ro hon (tu 0.3)
            break;

        case MochiState::kConfused:
            // Boi roi: mat lech ro hon, nhan may manh hon
            p.eye_l_h = 0.8f;
            p.eye_r_h = 1.25f;
            p.lid_inner_l = 0.55f;
            p.lid_outer_r = 0.45f;
            p.eye_l_w = 0.92f;  // mat trai hep hon
            p.eye_r_w = 1.08f;  // mat phai rong hon
            p.mouth_w = 0.8f;
            p.mouth_curve = -0.1f;
            p.mouth_tilt = 0.5f;  // mieng lech ro hon, kieu boi roi (tu 0.4)
            break;

        case MochiState::kCool:
            // Cool: mot mat nham (winking effect), nhech mep ro va
            // "chau" nhe mat con lai de tu tin hon (kieu 😎 thay vi chi
            // nhech mep hoi hoi nhu truoc).
            p.eye_l_h = 0.85f;  // mat trai hoi hep lai, khong con "binh thuong"
            p.eye_r_h = 0.12f;  // mat phai nhem (wink)
            p.eye_r_w = 1.08f;  // mat phai rong hon mot chut
            p.mouth_w = 0.95f;
            p.mouth_curve = 0.5f;   // tu 0.35
            p.mouth_tilt = 0.55f;   // nhech mep ro ret hon (tu 0.35)
            break;

        case MochiState::kSleepy:
            // Buon ngu: mat nho hon, mi ru xuong ro hon, mat dich xuong
            p.eye_l_h = p.eye_r_h = 0.3f;   // tu 0.35
            p.eye_y_off = 2.8f;
            p.lid_outer_l = p.lid_outer_r = 0.7f;  // tu 0.6
            p.lid_inner_l = p.lid_inner_r = 0.35f;
            p.mouth_w = 0.7f;
            p.mouth_curve = -0.15f;
            p.mouth_open = 0.12f;  // hoi ha he
            p.mouth_y_off = 1.0f;
            break;

        case MochiState::kListening:
            // Tap trung nghe: mat nhem ro hon mot chut, to ra su tap trung
            p.eye_l_h = 0.6f;
            p.eye_r_h = 0.65f;
            p.lid_outer_l = p.lid_outer_r = 0.25f;
            p.mouth_w = 0.9f;
            p.mouth_curve = 0.15f;
            p.mouth_open = 0.05f;  // TickListening() se dao dong nhe quanh gia tri nay
            break;

        case MochiState::kThinking:
            // Dang suy nghi: mat hoi mo rong, nhin xa xoi ro hon
            p.eye_l_h = p.eye_r_h = 0.95f;
            p.eye_l_w = p.eye_r_w = 0.98f;
            p.eye_y_off = -1.2f;  // nhin len truoc, ro hon
            p.mouth_w = 0.85f;
            p.mouth_curve = -0.08f;
            p.mouth_open = 0.05f;
            p.mouth_tilt = 0.2f;  // nhech mieng suy tu ro hon (tu 0.15)
            break;

        case MochiState::kSpeaking:
            // Dang noi: mat to ra, to ra su hoat bong ro hon
            p.eye_l_h = p.eye_r_h = 1.08f;
            p.eye_l_w = p.eye_r_w = 1.03f;
            p.mouth_w = 1.02f;
            p.mouth_curve = 0.25f;
            p.mouth_open = 0.35f;  // gia tri co so, TickSpeaking() se dao dong quanh no de gia lap dang noi
            break;

        case MochiState::kIdle:
        default:
            // neutral: hoi nhech mieng cuoi nhe (khong de mieng thang
            // tuyet doi - mat mac dinh cua FaceParams la du cho mat).
            p.mouth_curve = 0.15f;
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
    // Reset dem "chop mieng" khi doi trang thai - dam bao lan noi tiep
    // theo luon bat dau bang 1 lan chon target moi ngay tick dau tien,
    // khong ke thua chu ky con dang do tu lan kSpeaking truoc.
    mouth_flap_ticks_left_ = 0;
    consecutive_low_picks_ = 0;

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

    StepSpring(current_.mouth_w, velocity_.mouth_w, target_.mouth_w);
    StepSpring(current_.mouth_curve, velocity_.mouth_curve, target_.mouth_curve);
    StepSpring(current_.mouth_open, velocity_.mouth_open, target_.mouth_open);
    StepSpring(current_.mouth_tilt, velocity_.mouth_tilt, target_.mouth_tilt);
    StepSpring(current_.mouth_y_off, velocity_.mouth_y_off, target_.mouth_y_off);
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

    // --- Eye shine (highlight co dinh) ---
    // Sau 2 lan chinh ty le (30% -> lac, 26%/42% -> van lech len tren)
    // van kho canh cho dep tren man hinh nho - va RoboEyes (thu vien goc
    // lam nen tang cho file nay) THUC RA KHONG CO chi tiet "diem sang"
    // nao ca, chi co hinh mat + mood + vi tri nhin. Don gian hoa toi da:
    // dat DUNG TAM HINH HOC cua mat, khong lech huong nao - an toan
    // tuyet doi, khong con tham so % nao de can chinh sai nua.
    // Kich thuoc ti le voi CHIEU CAO mat hien tai (eye_l_h/eye_r_h, khong
    // phai eye_h_ co dinh) nen se tu thu nho dan khi mat nhem lai
    // (happy/angry/blink...), khong bao gio to hon mat dang hien thi.
    // TANG tu 0.22 len ~0.38 de giong ty le trong-den/iris cua mat that
    // hon (iris thuong chiem phan lon chieu cao khe mat mo), thay vi 1
    // cham nho li ti nhu ban truoc. Dong thoi so sanh voi CHIEU RONG mat
    // hien tai (eye_ref_w, nhan he so 0.46 - hoi thoang hon vi mat rong
    // hon cao) va lay gia tri NHO HON trong 2 canh - dam bao "trong den"
    // khong bao gio tran ra ngoai khi mat bi bop hep (angry/happy/wink).
    auto shine_size_for = [&](int eye_ref_h, int eye_ref_w) {
        int by_h = static_cast<int>(eye_ref_h * 0.38f);
        int by_w = static_cast<int>(eye_ref_w * 0.46f);
        return std::max(1, std::min(by_h, by_w));
    };
    // Do mo mo dan theo current_.eye_*_h: an han khi mat gan nhu khep
    // (chop mat) de khong de lai 1 cham den lo lung giua man hinh den,
    // hien du ro (nhung van nhe, opa toi da ~190/255) khi mat mo binh
    // thuong tro len.
    auto shine_opa_for = [](float eye_scale) {
        float t = std::clamp((eye_scale - 0.15f) / 0.2f, 0.0f, 1.0f);
        return static_cast<lv_opa_t>(t * 190.0f);
    };
    int shine_l = shine_size_for(eye_l_h, eye_l_w);
    int shine_r = shine_size_for(eye_r_h, eye_r_w);
    lv_obj_set_size(eye_shine_left_, shine_l, shine_l);
    lv_obj_set_size(eye_shine_right_, shine_r, shine_r);
    // Tam hinh hoc: eye_*_x + eye_*_w/2, eye_*_top + eye_*_h/2 - dung
    // TAM, khong con offset theo % nao nua.
    lv_obj_set_pos(eye_shine_left_,
                    eye_left_x + eye_l_w / 2 - shine_l / 2,
                    eye_l_top + eye_l_h / 2 - shine_l / 2);
    lv_obj_set_pos(eye_shine_right_,
                    eye_right_x + eye_r_w / 2 - shine_r / 2,
                    eye_r_top + eye_r_h / 2 - shine_r / 2);
    lv_obj_set_style_bg_opa(eye_shine_left_, shine_opa_for(current_.eye_l_h), 0);
    lv_obj_set_style_bg_opa(eye_shine_right_, shine_opa_for(current_.eye_r_h), 0);

    ApplyMouthToObjects();
}

// ---------------------------------------------------------------------
// Ap dung current_.mouth_* len kMouthSegCount mieng capsule. Tung mieng
// duoc dat theo trong so parabol (dinh o giua, ve 0 o hai dau) - dung
// CHUNG cong thuc cho ca do cong (curve, lam khoe mieng cao/thap hon
// giua mieng) va do mo (open, lam giua mieng phinh to hon 2 dau, gia
// lap hinh "O"/oval). Do lech (tilt) dung trong so TUYEN TINH tu trai
// sang phai. moi vi tri deu duoc KEP (clamp) de tuyet doi khong tran
// xuong duoi mat hoac qua day man hinh.
// ---------------------------------------------------------------------
void MochiDisplay::ApplyMouthToObjects() {
    int center_x = width_ / 2;

    // Seg rong theo mouth_w hien tai (mo/thu mieng theo bieu cam).
    int seg_w = std::max(3, static_cast<int>(mouth_seg_w_ * current_.mouth_w));
    
    int step = std::max(1, static_cast<int>(seg_w * kMouthOverlapRatio));
    int total_span = (kMouthSegCount - 1) * step + seg_w;
    int start_x = center_x - total_span / 2 + current_x_offset_;

    for (int i = 0; i < kMouthSegCount; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(kMouthSegCount - 1);  // 0..1, trai->phai
        // Trong so parabol: 1.0 o giua mieng (t=0.5), 0.0 o hai dau
        // (t=0 hoac 1) - dung chung cho ca cong va mo.
        float center_weight = 1.0f - 4.0f * (t - 0.5f) * (t - 0.5f);
        center_weight = std::max(0.0f, center_weight);

        float dy = current_.mouth_curve * mouth_curve_amp_ * center_weight;
        dy += current_.mouth_tilt * mouth_tilt_amp_ * (t - 0.5f) * 2.0f;
        dy += current_.mouth_y_off;

        int seg_h = mouth_base_h_ + static_cast<int>(current_.mouth_open * mouth_open_amp_ * center_weight);
        seg_h = std::max(2, seg_h);

        // Thon dan be rong ve 2 khoe mieng (0.6x o hai dau, day du seg_w
        // o giua) - gia lap dang MOI tu nhien thay vi 1 thanh day deu tam
        // canh. 0.6 van > kMouthOverlapRatio (0.55) nen mep VAN chong du
        // de khong roi thanh cham rieng (xem kMouthOverlapRatio o tren).
        float width_weight = 0.6f + 0.4f * center_weight;
        int this_seg_w = std::max(3, static_cast<int>(seg_w * width_weight));
        // Giu tam moi mieng dung vi tri cu (seg_x + seg_w/2) du be rong
        // co the nho hon seg_w danh nghia - khong de lech tam khi thon lai.
        int seg_x = start_x + i * step + (seg_w - this_seg_w) / 2;
        int seg_y = mouth_y_ + static_cast<int>(dy) - seg_h / 2;
        // Kep lai lan nua o runtime (ngoai muc kiem tra tinh o
        // constructor) - khong bao gio de mieng chong len mat hoac
        // tran qua day man hinh, du to hop tham so nao.
        seg_y = std::clamp(seg_y, eye_y_ + eye_h_, height_ - seg_h - 1);

        lv_obj_set_size(mouth_segs_[i], this_seg_w, seg_h);
        lv_obj_set_pos(mouth_segs_[i], seg_x, seg_y);
    }
}


void MochiDisplay::BuildMouth() {
    DisplayLockGuard lock(this);

    for (int i = 0; i < kMouthSegCount; ++i) {
        lv_obj_t* seg = lv_obj_create(face_root_);
        lv_obj_remove_style_all(seg);
        lv_obj_set_style_bg_color(seg, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
        // LV_RADIUS_CIRCLE giong het mat/lid - giu dong bo phong cach
        // "mochi" (moi thu deu tron/mup), va tu dong thanh vien-thuoc
        // (pill) khi seg_h thay doi theo do "mo" (open).
        lv_obj_set_style_radius(seg, LV_RADIUS_CIRCLE, 0);
        mouth_segs_[i] = seg;
    }
    // Vi tri/kich thuoc thuc te tinh moi tick trong ApplyMouthToObjects()
    // vi phu thuoc cac tham so bieu cam hien tai (w/curve/open/tilt).
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

    // MIENG "NOI" DOC LAP VOI BIEU CAM HIEN TAI: chay TickMouthFlap() de
    // mieng tiep tuc "nhap nhay" ngay ca khi state_ KHONG con la
    // kSpeaking, mien la 1 trong 2 dieu kien sau dung:
    //  - talking_override_: app da goi SetMouthTalking(true) thu cong
    //    (dua theo trang thai audio pipeline THUC TE, neu co tich hop).
    //  - talking_session_active_: TU DONG suy ra tu chuoi goi
    //    SetEmotion() - van con "trong phien noi" du emotion vua chuyen
    //    sang mot bieu cam phan ung nhu "happy" (xem SetEmotion()).
    // Khi state_ == kSpeaking, mieng da duoc TickSpeaking() (o tren) xu
    // ly roi nen KHONG goi lai o day - tranh tieu ton random 2 lan/tick.
    if ((self->talking_override_ || self->talking_session_active_) &&
        self->state_ != MochiState::kSpeaking) {
        self->TickMouthFlap();
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
            // to hon binh thuong, roi tro ve idle. Gio co mieng thi
            // cho luon mieng "ngap" dong bo: pha dau khep nho lai (hit
            // hoi vao), pha sau mo to (nha hoi ra) - giong 1 cai ngap that.
            bool first_half = quirk_ticks_left_ > quirk_ticks_total_ / 2;
            target_.eye_l_h = target_.eye_r_h = first_half ? 0.12f : 1.3f;
            target_.mouth_open = first_half ? 0.05f : 0.55f;
            target_.mouth_w = first_half ? 0.75f : 1.15f;
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
    float listen_pulse = 0.6f + 0.08f * sinf(speak_phase_ * 0.105f);
    target_.eye_r_h = listen_pulse;
    target_.eye_l_h = 0.6f;  // mat trai on dinh
    // Mieng hoi mo dao dong nhe, kieu "dang tiep thu"
    target_.mouth_open = 0.05f + 0.05f * sinf(speak_phase_ * 0.105f);
}

void MochiDisplay::TickSpeaking() {
    // Mat: van giu nhip "tho" bang sin muot - phan nay khong can
    // "nhap nhay", chi can song dong nhe.
    speak_phase_ = (speak_phase_ + 1) % 40;
    float pulse = 0.95f + 0.12f * sinf(speak_phase_ * 0.157f);
    target_.eye_l_h = target_.eye_r_h = pulse;

    TickMouthFlap();
}

void MochiDisplay::TickMouthFlap() {
    // Mieng: "nhap nhay" tung am tiet. So voi ban dau, sua 3 diem
    // khien mieng nhin "nho di roi im" du van dang phat am thanh:
    // (1) GIU MOI MUC LAU HON (4-10 tick thay vi 2-6) de he spring
    //     (StepSpring, thoi gian on dinh ~250-300ms) KIP DUOI SAT
    //     target truoc khi bi doi sang muc khac - truoc day chu ky qua
    //     ngan nen mieng chua kip mo het da bi keo di, nhin nho han
    //     gia tri thuc dinh nghia.
    // (2) Muc "mo" nang bien do len 0.40-0.95 (tu 0.35-0.85), muc
    //     "khep" KHONG VE GAN 0 nua ma giu toi thieu ~0.10 - dam bao
    //     khong bao gio trong nhu dang "im lang" hoan toan.
    // (3) GIOI HAN toi da 2 lan "khep" (phu am) LIEN TIEP bang
    //     consecutive_low_picks_ - lan thu 3 BAT BUOC phai mo, tranh
    //     truong hop ngau nhien roi dung 1 chuoi dai toan muc thap
    //     (thong ke van co the xay ra du xac suat thap) khien mieng
    //     nhin nhu dung yen han mot luc trong khi TTS van dang phat.
    if (mouth_flap_ticks_left_ == 0) {
        bool force_open = consecutive_low_picks_ >= 2;
        bool pick_low = !force_open && (esp_random() % 100) < 22;

        if (pick_low) {
            mouth_flap_target_ = 0.1f + (esp_random() % 10) / 100.0f;  // ~0.10-0.19
            consecutive_low_picks_++;
        } else {
            mouth_flap_target_ = 0.4f + (esp_random() % 55) / 100.0f;  // ~0.40-0.95
            consecutive_low_picks_ = 0;
        }
        mouth_flap_ticks_left_ = pick_low ? 4 + (esp_random() % 3)    // 4-6 tick (~160-240ms)
                                           : 6 + (esp_random() % 5);  // 6-10 tick (~240-400ms)
    }
    mouth_flap_ticks_left_--;

    target_.mouth_open = mouth_flap_target_;
    target_.mouth_curve = 0.2f;
}

void MochiDisplay::SetMouthTalking(bool active) {
    talking_override_ = active;
    if (!active) {
        // Reset dem khi THUC SU ngung phat am thanh, de lan noi tiep
        // theo bat dau lai chu ky tu dau (khong ke thua trang thai cu).
        mouth_flap_ticks_left_ = 0;
        consecutive_low_picks_ = 0;
    }
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