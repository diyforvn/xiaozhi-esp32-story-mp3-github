#pragma once

// =====================================================================
// MochiDisplay v4 - dua tren nen tang FluxGarage RoboEyes (2 mat chu
// nhat bo goc, KHONG long may rieng), THEM MIENG bieu cam cho ca 14
// trang thai. Mieng duoc ghep tu 5 mieng capsule nho (kieu "chuoi hat"),
// vi tri tung mieng duoc tinh theo 1 duong parabol/tuyen tinh de gia
// lap duong CONG (cuoi/miu) va do MO - cung triet ly voi ky thuat "lid"
// da dung cho mat (compose bang hinh don gian + tham so hoa, khong ve
// duong cong that).
//
// Bieu cam the hien qua: (1) ty le rong/cao tung mat, (2) cuong do "cat
// goc" (lid) o goc trong (angry) hoac goc ngoai (tired/sad) cua tung
// mat, (3) do cong/mo/lech/rong cua mieng.
//
// Kich thuoc mac dinh cho man 128x64: mat 34x28 (GIAM tu 36x36 cua ban
// v3) cach nhau 10px, neo LEN GAN DINH man hinh (thay vi can giua doc)
// de nhuong hang duoi cho mieng. Vi tri/kich thuoc mieng duoc KEP
// (clamp) trong ApplyMouthToObjects() de dam bao KHONG BAO GIO tran
// day man hinh du bieu cam nao - dung bai hoc tu bug "mieng cu vuot
// qua day man hinh 19px" cua ban rat som truoc do.
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
    kStretch,  // mat nhem lai roi mo bung ra, mieng ngap dong bo theo
               // (xem TickIdle() trong .cc)
    kGlance,   // liec nhanh sang 1 ben roi ve giua
};

// Tap tham so hinh hoc cho 1 bieu cam - gom truong cho mat va mieng
// (khong con long may rieng). TargetForState() trong .cc anh xa moi
// MochiState ve 1 bo FaceParams; moi tick, current_ duoc "chay" toi
// target_ bang mo hinh lo xo (spring, xem StepSpring trong .cc) de co
// do nay/overshoot nhe truoc khi on dinh.
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

    // --- Mieng (moi trong v4) ---
    // Ty le be rong mieng so voi mouth_span_ goc (1.0 = binh thuong).
    float mouth_w = 1.0f;
    // Do "cong" cua mieng: duong (+) = CUOI (2 khoe mieng nhech len,
    // chinh giua vong xuong - hinh chu "U"), am (-) = MIU/BUON (chinh
    // giua vong len, khoe mieng thap hon - hinh chu "U" nguoc). 0 =
    // mieng thang. Ap dung theo trong so parabol (dinh o giua mieng).
    float mouth_curve = 0.0f;
    // Do "mo mieng": 0 = khep (mong, dep), 1 = mo to (tron nhu chu
    // "O", dung cho surprised/laughing, hoac dao dong lien tuc trong
    // TickSpeaking() de gia lap dang noi).
    float mouth_open = 0.0f;
    // Do "lech" mot ben (smirk/awkward smile), tuyen tinh tu trai sang
    // phai: duong = khoe PHAI cao hon, am = khoe TRAI cao hon. Dung
    // cho cool (nhech mep), confused, embarrassed.
    float mouth_tilt = 0.0f;
    // Dich chuyen doc rieng cho mieng (px, doc lap voi eye_y_off).
    float mouth_y_off = 0.0f;
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

    // MOI: dieu khien animation "nhap nhay" cua mieng DOC LAP HOAN TOAN
    // voi SetEmotion()/emotion tu server. LY DO can co ham rieng: emotion
    // "speaking" thuong duoc server gui theo TEXT/TTS stream, khong dong
    // bo voi thoi diem AM THANH THUC SU con dang phat qua I2S/codec -
    // rat de xay ra truong hop server da doi emotion khac (vd sang cau
    // tiep theo, hoac ve "idle") TRUOC KHI buffer am thanh phat het,
    // khien mieng "dung hinh" nhu bai bao du loa van con keu.
    //
    // TICH HOP: goi SetMouthTalking(true) ngay truoc khi BAT DAU ghi du
    // lieu am thanh THUC SU vao I2S/codec (vd dau callback decode audio
    // frame dau tien cua 1 cau), va SetMouthTalking(false) khi BUFFER AM
    // THANH DA PHAT XONG THAT SU (vd callback "decode queue empty" hoac
    // "playback finished" cua audio pipeline) - KHONG goi theo thoi diem
    // nhan duoc text/token tu LLM. Khi active=true, mieng se tu "nhap
    // nhay" (TickMouthFlap) o MOI tick bat ke state_ hien tai la gi,
    // GHI DE len bat ky gia tri mouth_open/mouth_curve nao khac.
    void SetMouthTalking(bool active);

private:
    void BuildFace();
    void BuildLids();
    void BuildEyeShine();
    void BuildMouth();

    void ApplyState(MochiState state);
    // Bang tra cuu trung tam: 1 trang thai -> 1 bo tham so hinh hoc
    // muc tieu. Sua so o day la du de tinh chinh bieu cam.
    FaceParams TargetForState(MochiState state) const;

    void StepSpringToTarget();
    void ApplyCurrentToObjects();
    void ApplyMouthToObjects();

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
    // Logic "nhap nhay" mieng dung chung: goi tu TickSpeaking() khi
    // state_ == kSpeaking, VA tu OnAnimTick() khi talking_override_ == true
    // (xem SetMouthTalking()) de mieng van "song" ngay ca luc state_ da
    // troi sang trang thai khac trong khi am thanh thuc te van dang phat.
    void TickMouthFlap();

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

    // 2 diem "shine" (mau nen, cat vao goc tren-trong cua moi mat) - co
    // dinh vi tri tuong doi (khong doi theo bieu cam) de gia lap anh sang
    // phan chieu co dinh tu 1 huong, giup mat bot "khoi hop chu nhat vo
    // hon" va co chieu sau hon. Mo dan theo do cao mat hien tai
    // (current_.eye_l_h/eye_r_h) de tu an di luc chop mat, tranh bi lo
    // lung mot cham den khi mat gan nhu khep.
    lv_obj_t* eye_shine_left_ = nullptr;
    lv_obj_t* eye_shine_right_ = nullptr;

    // Mieng (v4.1) - ghep tu kMouthSegCount mieng capsule nho CHONG LAN
    // NHAU (overlap), vi tri tung mieng tinh moi tick trong
    // ApplyMouthToObjects() theo cong thuc parabol/tuyen tinh de gia lap
    // duong cong cuoi/mieu/lech. Tang tu 5 len 7 mieng + overlap ratio
    // (xem kMouthOverlapRatio trong .cc) de duong mieng muot hon, khong
    // con bi roi thanh cac "cham tron" rieng le nhu ban v4 goc.
    static constexpr int kMouthSegCount = 7;
    lv_obj_t* mouth_segs_[kMouthSegCount] = {nullptr};

    lv_timer_t* anim_timer_ = nullptr;

    MochiState state_ = MochiState::kIdle;
    FaceParams current_;   // gia tri dang hien thi (duoc spring dan toi target_)
    FaceParams target_;    // gia tri muc tieu ung voi state_
    FaceParams velocity_;  // van toc cho tung truong, dung boi mo hinh lo xo

    // Hinh hoc mat - CHU NHAT BO GOC. Ban v4 GIAM kich thuoc tu 36x36
    // (ban v3) xuong 34x28 va neo LEN GAN DINH man hinh (eye_y_ nho)
    // thay vi can giua doc, de nhuong khong gian phia duoi cho mieng.
    int eye_w_ = 34;
    int eye_h_ = 28;
    int eye_gap_ = 10;
    int eye_y_ = 0;
    int eye_radius_ = 10;  // KHONG con dung truc tiep (mat/lid da doi sang LV_RADIUS_CIRCLE), giu de tuong thich nguoc.

    // Hinh hoc mieng (v4) - tinh trong constructor theo cung ty le
    // scale voi mat. mouth_y_ la hang co so (truoc khi cong/mo lam
    // lech di); mouth_max_extent duoc kiem tra ngay trong constructor
    // de dam bao khong tran day man hinh voi MOI to hop tham so.
    int mouth_y_ = 0;
    int mouth_span_ = 0;       // be rong mieng MONG MUON (dung de suy ra mouth_seg_w_)
    int mouth_seg_w_ = 0;      // be rong 1 mieng (duong kinh hinh tron) tai mouth_w=1.0.
                                // Khoang cach TAM-TAM thuc te giua 2 mieng lien ke KHONG luu
                                // thanh bien rieng - duoc ApplyMouthToObjects() tinh lai MOI
                                // TICK bang seg_w_hien_tai * kMouthOverlapRatio (xem .cc), vi
                                // seg_w thay doi theo current_.mouth_w con so nay thi khong.
    int mouth_base_h_ = 0;     // chieu cao mieng khi khep (open=0)
    int mouth_open_amp_ = 0;   // chieu cao THEM toi da khi mo het (open=1)
    int mouth_curve_amp_ = 0;  // bien do dich doc toi da do "cong" (|curve|=1)
    int mouth_tilt_amp_ = 0;   // bien do dich doc toi da do "lech" (|tilt|=1)

    uint32_t tick_count_ = 0;
    uint32_t next_blink_at_ = 0;
    bool blinking_ = false;
    bool double_blink_pending_ = false;

    // bien dem cho TickSpeaking/TickListening (nhip "tho" cua mat +
    // dao dong mo/khep cua mieng)
    int speak_phase_ = 0;

    // --- Mieng "nhap nhay" luc noi (TickSpeaking) ---
    // Thay vi 1 duong sin muot deu deu, mieng doi target dot ngot theo
    // chu ky ngan ngau nhien (mo phong tung am tiet: dong voi phu am,
    // mo voi nguyen am) - he spring (StepSpring) van lam muot chuyen
    // dong nen khong bi giat cung, nhung cam giac "chop mo" (flap) that
    // hon nhieu so voi dao dong sin deu.
    float mouth_flap_target_ = 0.3f;
    uint32_t mouth_flap_ticks_left_ = 0;
    // Dem so lan LIEN TIEP chon phai muc "khep" (phu am) - dung de bat
    // buoc lan chon tiep theo phai la muc "mo" sau toi da 2 lan khep
    // lien tiep, tranh mieng nhin nhu dung yen mot luc du dang phat.
    int consecutive_low_picks_ = 0;
    // true khi app bao dang THUC SU phat am thanh (xem SetMouthTalking())
    // - doc lap voi state_/emotion, dung de mieng khong bi "dung hinh"
    // neu emotion troi sang trang thai khac truoc khi audio phat xong.
    bool talking_override_ = false;
    // true trong "phien noi" TU DONG PHAT HIEN tu chuoi goi SetEmotion()
    // (xem SetEmotion() trong .cc) - bat khi nhan "speaking", tat khi
    // nhan idle/listening/thinking/sleepy, GIU NGUYEN voi cac emotion
    // "phan ung" khac (happy/sad/...) de mieng khong dung nhap nhay khi
    // server gui bieu cam moi giua luc dang noi.
    bool talking_session_active_ = false;

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