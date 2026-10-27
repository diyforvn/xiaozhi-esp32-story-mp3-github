// =============================================================================
//  eyeFXCore.h  -  Engine ve mat + cam xuc DUNG CHUNG cho cac kieu mat dong vat
//
//  Port tu terminatorEyeFX.h (giu nguyen toan bo cam xuc / API), nhung bo cac
//  gia dinh chi dung dung cho du lieu "terminatorEye.h":
//
//    * Do mo man trap: bang pose luon viet theo thang cua Terminator (65..500),
//      engine tu doi sang khoang IRIS_MIN..IRIS_MAX cua tung kieu mat.
//    * Ban do iris (iris[]) co the rong 1 / 256 / 402 / 512 cot - khong con
//      fix cung 256.
//    * Sclera co the chi 128..200 px - khoang pan cua sclera duoc kep lai.
//    * Mat na mi (upper/lower) cua dong vat KHONG doi xung (co goc mat o mot
//      ben) -> mat con lai duoc lat guong de goc mat luon huong ve phia mui.
//    * Mau vien mi, tint thau kinh ... cau hinh theo tung kieu (xem *EyeFX.h).
//
//  Khong include truc tiep file nay. Dung <ten>EyeFX.h (catEyeFX.h, ...),
//  chung dinh nghia cac macro EYEFX_* roi include file nay SAU <ten>Eye.h.
//
//  Moi lan build chi duoc include MOT kieu mat (cac kieu cung dung chung ten
//  bien sclera/iris/polar/upper/lower).
// =============================================================================

#ifndef EYEFX_CLASS
#error "Hay include <ten>EyeFX.h (vd. catEyeFX.h) thay vi eyeFXCore.h"
#endif

// ----------------------- Mac dinh neu <ten>Eye.h khong khai bao ---------------
// (Adafruit Uncanny Eyes: default / goat khong #define IRIS_MIN / IRIS_MAX)
#ifndef IRIS_MIN
#define IRIS_MIN 90
#endif
#ifndef IRIS_MAX
#define IRIS_MAX 130
#endif

// ----------------------- Tuy chinh (ghi de truoc khi include) -----------------
#define EYEFX_RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#ifndef EYEFX_COLOR_TINT
#define EYEFX_COLOR_TINT 0       // 1: nhuom do vung "do" cua thau kinh theo cam xuc (chi hop mat do kieu robot)
#endif
#ifndef EYEFX_GAZE_SHIFT
#define EYEFX_GAZE_SHIFT 18      // px mat di chuyen khi liec het co (gaze = 1.0)
#endif
#ifndef EYEFX_MAX_TEARS
#define EYEFX_MAX_TEARS 3
#endif
#ifndef EYEFX_RIM
#define EYEFX_RIM EYEFX_RGB565(60, 50, 50)   // mau vien mi
#endif
#ifndef EYEFX_DUCT_LEFT
#define EYEFX_DUCT_LEFT 1        // 1: trong du lieu, goc mat (tear duct) nam o BEN TRAI anh (mat PHAI that)
#endif
// Khoang "scale iris" thuc su dung cho kieu mat nay (don vi nhu IRIS_MIN/IRIS_MAX trong <ten>Eye.h):
//   EYEFX_SC_LO = con nguoi NHO nhat (cam xuc LOVE), EYEFX_SC_HI = con nguoi GIAN nhat (SURPRISED).
// Mac dinh = IRIS_MIN..IRIS_MAX cua du lieu goc. Tung kieu mat ghi de trong <ten>EyeFX.h.
#ifndef EYEFX_SC_LO
#define EYEFX_SC_LO IRIS_MIN
#endif
#ifndef EYEFX_SC_HI
#define EYEFX_SC_HI IRIS_MAX
#endif

// Thang do mo man trap dung trong bang pose (= IRIS_MIN..IRIS_MAX cua terminatorEye.h)
#define EYEFX_REF_IRIS_MIN 65.f
#define EYEFX_REF_IRIS_MAX 500.f

// ------------------------------- Cam xuc --------------------------------------
enum EyeEmotion : uint8_t {
  EYE_NEUTRAL = 0,
  EYE_HAPPY,
  EYE_LAUGH,
  EYE_ANGRY,
  EYE_SAD,
  EYE_CRY,
  EYE_SURPRISED,
  EYE_SCARED,
  EYE_LOVE,
  EYE_SLEEPY,
  EYE_SLEEP,
  EYE_WINK,
  EYE_SUSPICIOUS,
  EYE_DIZZY,
  EYE_SCAN,
  EYE_EVIL,
  EYE_GLITCH,
  EYE_EMOTION_COUNT
};

enum : uint16_t {
  EF_NOBLINK = 1,    // khong tu chop mat
  EF_NOLOOK  = 2,    // khong tu liec mat ngau nhien
  EF_DART    = 4,    // liec mat nhanh, rong (so hai)
  EF_HEART   = 8,    // con nguoi hinh trai tim
  EF_TEARS   = 16,   // nuoc mat
  EF_SCAN    = 32,   // quet trai-phai + lay net
  EF_ORBIT   = 64,   // mat quay vong tron
  EF_GLITCH  = 128   // nhieu / loi hinh
};

struct EyePose {
  // mi tren: y (px) o giua man hinh, nghieng (+ = phia mui thap xuong), do cong (+ = vong xuong giua)
  float upY, upTilt, upArch;
  // mi duoi: y (px), nghieng, do cong (+ = nhoi len giua, tao ma hong khi cuoi)
  float loY, loTilt, loArch;
  float irisScale;            // do gian con nguoi, thang 65..500 (lon = gian rong)
  float gazeX, gazeY;         // lech huong nhin (-1..1)
  float tremble;              // run (px)
  float bounce, bounceHz;     // nhun len xuong (px, Hz)
  float spin;                 // toc do xoay mong mat (don vi/giay, 256 = 1 vong)
  float tr, tg, tb;           // mau thau kinh 0..1 (chi dung khi EYEFX_COLOR_TINT = 1)
  float blinkSpeed;           // 1 = binh thuong, <1 = cham
  uint16_t flags;
};

// Thu tu PHAI khop voi enum EyeEmotion
static const EyePose EYEFX_POSES[EYE_EMOTION_COUNT] = {
  //  upY upTilt upArch  loY loTilt loArch  iris  gX    gY    trem bnc  bHz  spin   tr    tg    tb   blinkSp flags
  { -30,   0,     0,    160,   0,     0,    260,  0,    0,    0,   0,   0,   0,    1.0f, 0.0f, 0.0f, 1.0f, 0 },                         // NEUTRAL
  { -30,   0,     0,    116,   0,    36,    330,  0,   -0.05f,0,   1.2f,1.6f,0,    1.0f, 0.70f,0.10f,1.0f, 0 },                         // HAPPY
  {  66,   0,   -30,     96,   0,    38,    400,  0,    0,    0.3f,3.2f,6.0f,0,    1.0f, 0.72f,0.10f,1.0f, EF_NOBLINK },                 // LAUGH
  {  34,  34,     4,    126,  -6,    16,    220,  0,    0.05f,0.9f,0,   0,   0,    1.0f, 0.0f, 0.0f, 1.0f, 0 },                         // ANGRY
  {  28, -30,     0,    138,   0,     8,    190,  0,    0.35f,0.2f,0,   0,   0,    0.12f,0.45f,1.0f, 0.8f, 0 },                         // SAD
  {  34, -34,     2,    106,   0,    16,    170,  0,    0.30f,1.0f,0,   0,   0,    0.10f,0.50f,1.0f, 0.8f, EF_TEARS | EF_NOLOOK },      // CRY
  { -30,   0,     0,    160,   0,     0,    500,  0,   -0.10f,0.5f,0,   0,   0,    1.0f, 0.95f,0.45f,1.0f, 0 },                         // SURPRISED
  { -30,   0,     0,    160,   0,     0,    110,  0,    0,    1.8f,0,   0,   0,    0.75f,0.35f,1.0f, 1.0f, EF_DART },                   // SCARED
  { -30,   0,     0,    128,   0,    18,     65,  0,    0,    0,   1.6f,1.5f,0,    1.0f, 0.18f,0.50f,1.0f, EF_HEART | EF_NOLOOK },      // LOVE
  {  44,   0,    10,    134,   0,     0,    150,  0,    0.20f,0,   1.5f,0.3f,0,    0.55f,0.05f,0.0f, 0.35f,EF_NOLOOK },                 // SLEEPY
  {  60,   0,     6,     64,   0,     0,    100,  0,    0.20f,0,   1.0f,0.25f,0,   0.35f,0.03f,0.0f, 1.0f, EF_NOBLINK | EF_NOLOOK },     // SLEEP
  { -30,   0,     0,    116,   0,    36,    330,  0,   -0.05f,0,   1.2f,1.6f,0,    1.0f, 0.70f,0.10f,1.0f, 0 },                         // WINK (mat KHONG nhay = HAPPY)
  {  44,   6,    -2,    112,   0,    10,    180,  0.55f,0,    0,   0,   0,   0,    1.0f, 0.35f,0.0f, 1.0f, EF_NOLOOK },                 // SUSPICIOUS
  { -30,   0,     0,    160,   0,     0,    120,  0,    0,    0,   0,   0,   200,  0.90f,0.30f,1.0f, 1.0f, EF_NOBLINK | EF_ORBIT | EF_NOLOOK }, // DIZZY
  {  40,   4,     0,    118,   0,     0,    130,  0,    0,    0,   0,   0,   0,    1.0f, 0.0f, 0.0f, 1.0f, EF_SCAN | EF_NOBLINK | EF_NOLOOK },  // SCAN
  {  40,  28,     0,    120,  -4,    24,    240,  0,    0.05f,0.25f,0,  0,   0,    0.90f,0.0f, 0.0f, 1.0f, 0 },                         // EVIL
  { -30,   0,     0,    160,   0,     0,    260,  0,    0,    0,   0,   0,   0,    1.0f, 0.0f, 0.0f, 1.0f, EF_GLITCH },                 // GLITCH
};

// Mat dang nhay (chi mat nay nham, mat kia van cuoi)
static const EyePose EYEFX_POSE_WINK_ON =
  {  70,   0,   -26,     98,   0,    36,    350,  0,    0,    0,   0,   0,   0,    1.0f, 0.70f,0.10f,1.0f, EF_NOBLINK | EF_NOLOOK };

// ------------------------------ Tien ich --------------------------------------
static inline uint16_t tefx_rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
// tron a va b, t = 0..256 (trong so cua b)
static inline uint16_t tefx_mix(uint16_t a, uint16_t b, int t) {
  int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
  int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
  int r = (ar * (256 - t) + br * t) >> 8;
  int g = (ag * (256 - t) + bg * t) >> 8;
  int bl = (ab * (256 - t) + bb * t) >> 8;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}
static inline bool tefx_ieq(const char* a, const char* b) {
  while (*a && *b) {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca += 32;
    if (cb >= 'A' && cb <= 'Z') cb += 32;
    if (ca != cb) return false;
    ++a; ++b;
  }
  return *a == *b;
}

static const char* const EYEFX_NAMES[EYE_EMOTION_COUNT] = {
  "neutral", "happy", "laugh", "angry", "sad", "cry", "surprised", "scared",
  "love", "sleepy", "sleep", "wink", "suspicious", "dizzy", "scan", "evil", "glitch"
};

static inline const char* eyeEmotionName(EyeEmotion e) {
  return (e < EYE_EMOTION_COUNT) ? EYEFX_NAMES[e] : "?";
}

// Doi ten (tieng Anh hoac tieng Viet khong dau) -> EyeEmotion. Tra ve false neu khong biet.
static inline bool eyeEmotionFromName(const char* s, EyeEmotion& out) {
  if (!s) return false;
  for (uint8_t i = 0; i < EYE_EMOTION_COUNT; i++)
    if (tefx_ieq(s, EYEFX_NAMES[i])) { out = (EyeEmotion)i; return true; }
  struct Alias { const char* n; EyeEmotion e; };
  static const Alias A[] = {
    {"binhthuong", EYE_NEUTRAL}, {"cuoi", EYE_HAPPY},   {"vui", EYE_HAPPY},
    {"cuoilon", EYE_LAUGH},      {"gian", EYE_ANGRY},   {"buon", EYE_SAD},
    {"khoc", EYE_CRY},           {"ngacnhien", EYE_SURPRISED}, {"so", EYE_SCARED},
    {"yeu", EYE_LOVE},           {"buonngu", EYE_SLEEPY}, {"ngu", EYE_SLEEP},
    {"nhaymat", EYE_WINK},       {"nghingo", EYE_SUSPICIOUS}, {"chongmat", EYE_DIZZY},
    {"quet", EYE_SCAN},          {"giantao", EYE_EVIL}, {"cuoigian", EYE_EVIL},
    {"loi", EYE_GLITCH},
    {"laughing", EYE_LAUGH},     {"crying", EYE_CRY}, {"winking", EYE_WINK},
    {"embarrassed", EYE_SUSPICIOUS}, {"shocked", EYE_SCARED}, {"thinking", EYE_SUSPICIOUS},
    {"cool", EYE_EVIL},           {"relaxed", EYE_SLEEPY}, {"delicious", EYE_HAPPY},
    {"confident", EYE_EVIL},      {"confused", EYE_SUSPICIOUS},
    {"funny", EYE_HAPPY},         {"silly", EYE_HAPPY},
    {"loving", EYE_LOVE},         {"kissy", EYE_LOVE}
  };
  for (uint8_t i = 0; i < sizeof(A) / sizeof(A[0]); i++)
    if (tefx_ieq(s, A[i].n)) { out = A[i].e; return true; }
  return false;
}

// ------------------------------ Lop mat ---------------------------------------
class EYEFX_CLASS {
public:
  // mirror = false: mat TRAI (goc trong o ben phai man hinh); true: mat PHAI.
  // Hai mat dung cung seed se chop mat / liec mat dong bo.
  void begin(bool mirror = false, uint32_t seed = 0x2545F491u) {
    _mirror = mirror;
    _rng = seed ? seed : 1;
    memset(&_c, 0, sizeof(_c));
    const EyePose& p = EYEFX_POSES[EYE_NEUTRAL];
    _loadPose(p);
    _emo = _base = EYE_NEUTRAL;
    _holdUntil = 0; _started = false;
    _blink = 0; _blinkPhase = 0; _nextBlink = 1500;
    _nextLook = 800; _manualUntil = 0;
    _lookX = _lookY = 0;
    _autoBlink = true; _autoLook = true;
    for (uint8_t i = 0; i < EYEFX_MAX_TEARS; i++) _tears[i].on = false;
  }

  void setMirror(bool m)        { _mirror = m; }
  void setWinkEye(bool w)       { _winkEye = w; }     // mat nay se nhay khi EYE_WINK
  void setAutoBlink(bool on)    { _autoBlink = on; }
  void setAutoLook(bool on)     { _autoLook = on; }
  EyeEmotion emotion() const    { return _emo; }

  // holdMs > 0: sau do tu quay lai thenEmotion (vd. wink 800ms roi ve neutral)
  void setEmotion(EyeEmotion e, uint32_t holdMs = 0, EyeEmotion thenEmotion = EYE_NEUTRAL) {
    if (e >= EYE_EMOTION_COUNT) return;
    _emo = e;
    if (holdMs) { _holdMs = holdMs; _holdArm = true; _base = thenEmotion; }
    else        { _holdArm = false; _holdUntil = 0; _base = e; }
  }

  // Nhin ve (x,y) trong -1..1 ; tam dung tu liec ngau nhien 2.5s
  void lookAt(float x, float y) {
    _lookX = _clamp(x, -1.f, 1.f); _lookY = _clamp(y, -1.f, 1.f);
    _manualUntil = _now + 2500;
  }

  void blink() { if (_blinkPhase == 0) _startBlink(); }

  // ------------------------------- update ---------------------------------
  void update(uint32_t nowMs) {
    if (!_started) { _last = nowMs; _started = true; }
    float dt = (nowMs - _last) * 0.001f;
    _last = nowMs; _now = nowMs;
    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0.0005f) dt = 0.0005f;
    _t += dt;

    if (_holdArm) { _holdUntil = nowMs + _holdMs; _holdArm = false; }
    if (_holdUntil && (int32_t)(nowMs - _holdUntil) >= 0) { _holdUntil = 0; _emo = _base; }

    const EyePose& tg = _pose();
    const uint16_t fl = tg.flags;

    // --- lam muot cac tham so ve gia tri dich
    float kL = 1.f - expf(-dt / 0.09f);
    float kI = 1.f - expf(-dt / 0.14f);
    float kC = 1.f - expf(-dt / 0.25f);
    _c.upY += (tg.upY - _c.upY) * kL;  _c.upTilt += (tg.upTilt - _c.upTilt) * kL;
    _c.upArch += (tg.upArch - _c.upArch) * kL;
    _c.loY += (tg.loY - _c.loY) * kL;  _c.loTilt += (tg.loTilt - _c.loTilt) * kL;
    _c.loArch += (tg.loArch - _c.loArch) * kL;
    _c.irisScale += (tg.irisScale - _c.irisScale) * kI;
    _c.gazeX += (tg.gazeX - _c.gazeX) * kI;  _c.gazeY += (tg.gazeY - _c.gazeY) * kI;
    _c.tremble += (tg.tremble - _c.tremble) * kI;
    _c.bounce += (tg.bounce - _c.bounce) * kI;
    _c.bounceHz += (tg.bounceHz - _c.bounceHz) * kI;
    _c.spin += (tg.spin - _c.spin) * kI;
    _c.tr += (tg.tr - _c.tr) * kC;  _c.tg += (tg.tg - _c.tg) * kC;  _c.tb += (tg.tb - _c.tb) * kC;
    _c.blinkSpeed += (tg.blinkSpeed - _c.blinkSpeed) * kI;

    // --- nhin
    if (fl & EF_SCAN) {
      _gx = sinf(_t * 1.4f) * 0.9f;
      _gy = sinf(_t * 0.5f) * 0.15f;
      _irisMod = sinf(_t * 6.0f) * 45.f + (_frand(-1.f, 1.f) * 12.f);
    } else if (fl & EF_ORBIT) {
      _gx = cosf(_t * 5.0f) * 0.55f;
      _gy = sinf(_t * 5.0f) * 0.55f;
      _irisMod = 0;
    } else {
      _irisMod += (0 - _irisMod) * kI;
      if (fl & EF_NOLOOK) { _lookX += (0 - _lookX) * kI; _lookY += (0 - _lookY) * kI; }
      else if (_autoLook && (int32_t)(nowMs - _manualUntil) >= 0 && (int32_t)(nowMs - _nextLook) >= 0) {
        float r = (fl & EF_DART) ? 0.95f : 0.55f;
        _lookX = _frand(-r, r); _lookY = _frand(-r * 0.6f, r * 0.6f);
        _nextLook = nowMs + ((fl & EF_DART) ? (uint32_t)_frand(150, 500) : (uint32_t)_frand(900, 3200));
      }
      float tx = _clamp(_lookX + _c.gazeX, -1.f, 1.f);
      float ty = _clamp(_lookY + _c.gazeY, -1.f, 1.f);
      float kG = 1.f - expf(-dt / 0.05f);
      _gx += (tx - _gx) * kG; _gy += (ty - _gy) * kG;
    }

    // --- chop mat
    float bs = _c.blinkSpeed < 0.1f ? 0.1f : _c.blinkSpeed;
    switch (_blinkPhase) {
      case 0:
        if (_autoBlink && !(fl & EF_NOBLINK) && (int32_t)(nowMs - _nextBlink) >= 0) _startBlink();
        break;
      case 1: _blink += dt / (0.07f / bs); if (_blink >= 1.f) { _blink = 1.f; _blinkPhase = 2; _blinkT = 0; } break;
      case 2: _blinkT += dt; if (_blinkT >= 0.04f / bs) _blinkPhase = 3; break;
      case 3: _blink -= dt / (0.12f / bs);
        if (_blink <= 0.f) {
          _blink = 0.f; _blinkPhase = 0;
          _nextBlink = nowMs + ((_frand(0, 1) < 0.18f) ? 180u
                      : (fl & EF_DART) ? (uint32_t)_frand(900, 2000) : (uint32_t)_frand(2200, 5500));
        }
        break;
    }
    if (fl & EF_NOBLINK) { if (_blinkPhase) { _blink -= dt * 8.f; if (_blink <= 0) { _blink = 0; _blinkPhase = 0; } } }

    // --- run / nhun
    if (_c.tremble > 0.02f) { _jx = _frand(-1, 1) * _c.tremble; _jy = _frand(-1, 1) * _c.tremble; }
    else { _jx = _jy = 0; }
    _bph += dt * 6.2831853f * _c.bounceHz;
    _by = sinf(_bph) * _c.bounce;
    _irisRot += _c.spin * dt;

    // --- trai tim
    float heartTarget = (fl & EF_HEART) ? 1.f : 0.f;
    _heart += (heartTarget - _heart) * kI;
    _hph += dt * 6.2831853f * 1.5f;
    { float s = sinf(_hph); _beat = 1.f + 0.12f * (s > 0 ? s * s * s * s : 0.f); }

    // --- nuoc mat
    float tearTarget = (fl & EF_TEARS) ? 1.f : 0.f;
    _tearLine += (tearTarget - _tearLine) * kC;
    _updateTears(dt, nowMs, tearTarget > 0.5f);

    // --- glitch
    _updateGlitch(nowMs, (fl & EF_GLITCH) != 0);
  }

  // ------------------------------- render ---------------------------------
  // fb: SCREEN_WIDTH * SCREEN_HEIGHT pixel RGB565
  void render(uint16_t* fb) {
    const int W = SCREEN_WIDTH, H = SCREEN_HEIGHT;
    int16_t U[SCREEN_WIDTH], L[SCREEN_WIDTH];
    for (int x = 0; x < W; x++) {
      float u, l; _edges(x, u, l);
      U[x] = (int16_t)_clamp(u * 16.f, -4000.f, 4000.f);
      L[x] = (int16_t)_clamp(l * 16.f, -4000.f, 4000.f);
    }

    // "Khong gian anh": neu flip, moi toa do x cua du lieu (mat na, sclera, iris)
    // duoc lat guong -> goc mat luon quay ve phia mui.
    const bool flip = _flipArt();
    const int sdx = (int)lroundf(_gx * EYEFX_GAZE_SHIFT + _jx);            // lech tren man hinh
    const int sdy = (int)lroundf(_gy * EYEFX_GAZE_SHIFT + _jy + _by);
    const int adx = flip ? -sdx : sdx;                                      // lech trong khong gian anh

    // Sclera chi pan duoc trong khoang con du lieu; iris van di chuyen du ca khoang
    const int baseX = (SCLERA_WIDTH - W) / 2, baseY = (SCLERA_HEIGHT - H) / 2;
    const int sclDx = adx < -baseX ? -baseX : (adx > baseX ? baseX : adx);
    const int sclDy = sdy < -baseY ? -baseY : (sdy > baseY ? baseY : sdy);
    const int sclX0 = baseX - sclDx;
    const int sclY0 = baseY - sclDy;

    const int irisCx = W / 2 + adx;
    const int irisCy = H / 2 + sdy;
    const int irisL = irisCx - IRIS_WIDTH / 2, irisT = irisCy - IRIS_HEIGHT / 2;

    // Doi do gian (thang Terminator 65..500) -> EYEFX_SC_LO..EYEFX_SC_HI cua kieu mat nay
    float sf = _c.irisScale + _irisMod;
    sf = _clamp(sf, EYEFX_REF_IRIS_MIN, EYEFX_REF_IRIS_MAX);
    float tn = (sf - EYEFX_REF_IRIS_MIN) / (EYEFX_REF_IRIS_MAX - EYEFX_REF_IRIS_MIN);
    // Noi suy theo 1/sc: ban kinh con nguoi ti le tuyen tinh voi 1/sc nen cac muc
    // gian (t) cach deu nhau ve mat thi giac.
    const float invLo = 1.f / (float)EYEFX_SC_LO, invHi = 1.f / (float)EYEFX_SC_HI;
    int sc = (int)(1.f / (invLo + tn * (invHi - invLo)) + 0.5f);
    if (sc < 1) sc = 1;

    // Xoay mong mat: 256 don vi = 1 vong, doi sang don vi cot cua ban do iris
    const int rotMap = (((int)lroundf(_irisRot) & 255) * IRIS_MAP_WIDTH) >> 8;

#if EYEFX_COLOR_TINT
    int tR = (int)(_clamp(_c.tr, 0.f, 1.f) * 256.f);
    int tG = (int)(_clamp(_c.tg, 0.f, 1.f) * 256.f);
    int tB = (int)(_clamp(_c.tb, 0.f, 1.f) * 256.f);
#else
    const int tR = 256, tG = 0, tB = 0;
#endif
    const bool tintOn = !(tR == 256 && tG == 0 && tB == 0);

    // Trai tim
    const bool heartOn = _heart > 0.03f;
    const float hR = 27.f * _heart * _beat;
    const float hCx = irisCx, hCy = irisCy;
    const int hx0 = (int)(hCx - hR * 1.45f), hx1 = (int)(hCx + hR * 1.45f);
    const int hy0 = (int)(hCy - hR * 1.45f), hy1 = (int)(hCy + hR * 1.45f);
    const uint16_t heartCol = tefx_rgb((uint8_t)(tR > 255 ? 255 : tR), (uint8_t)(tG > 255 ? 255 : tG), (uint8_t)(tB > 255 ? 255 : tB));
    const int hr8 = (heartCol >> 11) << 3, hg8 = ((heartCol >> 5) & 63) << 2, hb8 = (heartCol & 31) << 3;

    const bool wet = _tearLine > 0.03f;
    const uint16_t RIM = (uint16_t)EYEFX_RIM;
    const uint16_t WET = tefx_rgb(150, 205, 255);

    for (int y = 0; y < H; y++) {
      int sy = y + sclY0; if (sy < 0) sy = 0; if (sy >= SCLERA_HEIGHT) sy = SCLERA_HEIGHT - 1;
      const int y16 = y * 16 + 8;
      const int iy = y - irisT;
      for (int x = 0; x < W; x++) {
        const int ax = flip ? (W - 1 - x) : x;          // cot trong khong gian anh
        const int midx = y * W + ax;                     // chi so mat na mi
        uint16_t* out = &fb[y * W + x];
        if (pgm_read_byte(&upper[midx]) == 0 || pgm_read_byte(&lower[midx]) == 0) { *out = 0; continue; }

        const int dU = y16 - U[x];
        const int dL = L[x] - y16;
        const int d = dU < dL ? dU : dL;
        if (d < -20) { *out = 0; continue; }             // ben trong mi
        if (d <= 0)  { *out = RIM; continue; }           // vien mi

        // --- mau pixel mat
        int sx = ax + sclX0; if (sx < 0) sx = 0; if (sx >= SCLERA_WIDTH) sx = SCLERA_WIDTH - 1;
        uint16_t p = pgm_read_word(&sclera[sy * SCLERA_WIDTH + sx]);

        const int ix = ax - irisL;
        if ((unsigned)ix < IRIS_WIDTH && (unsigned)iy < IRIS_HEIGHT) {
          const uint16_t pp = pgm_read_word(&polar[iy * IRIS_WIDTH + ix]);
          const int dist = (sc * (pp & 0x7F)) >> 7;
          if (dist < IRIS_MAP_HEIGHT) {
            int a = (IRIS_MAP_WIDTH * (pp >> 7)) >> 9;   // goc 0..511 -> cot ban do iris
            a += rotMap; if (a >= IRIS_MAP_WIDTH) a -= IRIS_MAP_WIDTH;
            p = pgm_read_word(&iris[dist * IRIS_MAP_WIDTH + a]);
          }
        }

#if EYEFX_COLOR_TINT
        // --- doi mau thau kinh do (chi pixel co "do" > xanh/luc; nen xam/trang giu nguyen)
        if (tintOn) {
          int r5 = p >> 11, g6 = (p >> 5) & 63, b5 = p & 31;
          int r = (r5 << 3) | (r5 >> 2), g = (g6 << 2) | (g6 >> 4), b = (b5 << 3) | (b5 >> 2);
          int e = r - (g > b ? g : b);
          if (e > 0) {
            r = r - e + ((e * tR) >> 8);
            g += (e * tG) >> 8;
            b += (e * tB) >> 8;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            p = tefx_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
          }
        }
#else
        (void)tintOn;
#endif

        // --- trai tim
        if (heartOn && ax >= hx0 && ax <= hx1 && y >= hy0 && y <= hy1) {
          const float u = (ax - hCx) / hR, v = (hCy - y) / hR + 0.12f;
          const float q = u * u + v * v - 1.f;
          const float f = q * q * q - u * u * v * v * v;
          if (f <= 0.f) {
            float t = (v + 1.f) * 0.43f; if (t < 0) t = 0; if (t > 1) t = 1;
            float k = 0.62f + 0.38f * t;
            if (f > -0.10f) k *= 0.62f;                       // vien toi
            float hx = (flip ? -u : u) + 0.55f, hy = v - 0.55f; // diem sang luon o goc tren-trai tren man hinh
            float h2 = hx * hx + hy * hy;
            int rr = (int)(hr8 * k), gg = (int)(hg8 * k), bb = (int)(hb8 * k);
            if (h2 < 0.05f) { float w = 1.f - h2 * 20.f; rr += (int)((255 - rr) * w); gg += (int)((255 - gg) * w); bb += (int)((255 - bb) * w); }
            p = tefx_rgb((uint8_t)rr, (uint8_t)gg, (uint8_t)bb);
          }
        }

        // --- anh uot doc theo mi duoi (khi khoc)
        if (wet && dL < 56) {
          int a = (int)(_tearLine * 140.f * (1.f - dL / 56.f));
          p = tefx_mix(p, WET, a);
        }

        // --- khu rang cua tai mep mi
        if (d < 16) p = tefx_mix(RIM, p, d * 16);
        *out = p;
      }
    }

    _drawTears(fb);
    if (_glitchOn) _applyGlitch(fb);
  }

private:
  struct Params {
    float upY, upTilt, upArch, loY, loTilt, loArch, irisScale, gazeX, gazeY;
    float tremble, bounce, bounceHz, spin, tr, tg, tb, blinkSpeed;
  };
  struct Tear { bool on; float x, y, vy; };

  Params _c;
  bool _mirror = false, _winkEye = false, _autoBlink = true, _autoLook = true, _started = false;
  EyeEmotion _emo = EYE_NEUTRAL, _base = EYE_NEUTRAL;
  uint32_t _holdMs = 0, _holdUntil = 0, _last = 0, _now = 0;
  bool _holdArm = false;
  float _t = 0, _lookX = 0, _lookY = 0, _gx = 0, _gy = 0, _irisMod = 0, _irisRot = 0;
  uint32_t _nextLook = 0, _manualUntil = 0, _nextBlink = 0;
  float _blink = 0, _blinkT = 0; uint8_t _blinkPhase = 0;
  float _jx = 0, _jy = 0, _by = 0, _bph = 0;
  float _heart = 0, _hph = 0, _beat = 1, _tearLine = 0;
  Tear _tears[EYEFX_MAX_TEARS];
  uint32_t _nextTear = 0;
  // glitch
  bool _glitchOn = false; uint32_t _nextGlitch = 2000, _glitchEnd = 0, _glitchTintAt = 0;
  struct Band { int16_t y, h, dx; } _bands[5]; uint8_t _nBands = 0;
  uint32_t _rng = 1;

  uint32_t _rand() { _rng ^= _rng << 13; _rng ^= _rng >> 17; _rng ^= _rng << 5; return _rng; }
  float _frand(float a, float b) { return a + (b - a) * ((_rand() & 0xFFFF) * (1.0f / 65535.0f)); }
  static float _clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

  // Du lieu mat na co goc mat o ben TRAI (EYEFX_DUCT_LEFT = 1) -> mat trai (goc trong o
  // ben phai) phai lat guong, mat phai giu nguyen. Nguoc lai neu du lieu goc mat o ben phai.
  bool _flipArt() const { return _mirror == (EYEFX_DUCT_LEFT == 0); }

  const EyePose& _pose() const {
    if (_emo == EYE_WINK) return _winkEye ? EYEFX_POSE_WINK_ON : EYEFX_POSES[EYE_HAPPY];
    return EYEFX_POSES[_emo];
  }
  void _loadPose(const EyePose& p) {
    _c.upY = p.upY; _c.upTilt = p.upTilt; _c.upArch = p.upArch;
    _c.loY = p.loY; _c.loTilt = p.loTilt; _c.loArch = p.loArch;
    _c.irisScale = p.irisScale; _c.gazeX = p.gazeX; _c.gazeY = p.gazeY;
    _c.tremble = p.tremble; _c.bounce = p.bounce; _c.bounceHz = p.bounceHz; _c.spin = p.spin;
    _c.tr = p.tr; _c.tg = p.tg; _c.tb = p.tb; _c.blinkSpeed = p.blinkSpeed;
  }
  void _startBlink() { _blinkPhase = 1; _blink = 0; }

  // Canh mi tren (u) va mi duoi (l) tai cot MAN HINH x, da tinh chop mat + nhun
  void _edges(int x, float& u, float& l) const {
    float xn = (x - 63.5f) * (1.f / 64.f);
    float xi = _mirror ? -xn : xn;
    float arc = 1.f - xn * xn;
    u = _c.upY + _c.upTilt * xi + _c.upArch * arc;
    l = _c.loY + _c.loTilt * xi - _c.loArch * arc;
    if (_blink > 0.001f && u < l) {
      float m = 0.5f * (u + l);
      float lo = l < 64.f ? l : 64.f;
      if (lo > m) m = lo;
      u += (m - u) * _blink;
      l += (m - l) * _blink;
    }
    float oy = _by + _jy;
    u += oy; l += oy;
  }

  // ------------------------------ nuoc mat -------------------------------
  void _updateTears(float dt, uint32_t now, bool spawn) {
    if (spawn && (int32_t)(now - _nextTear) >= 0) {
      for (uint8_t i = 0; i < EYEFX_MAX_TEARS; i++) if (!_tears[i].on) {
        float x = _frand(30, 98); float u, l; _edges((int)x, u, l);
        _tears[i].on = true; _tears[i].x = x; _tears[i].y = (l < 124.f ? l : 124.f) - 1.f; _tears[i].vy = 8.f;
        break;
      }
      _nextTear = now + (uint32_t)_frand(450, 1100);
    }
    for (uint8_t i = 0; i < EYEFX_MAX_TEARS; i++) if (_tears[i].on) {
      _tears[i].vy += 300.f * dt;
      _tears[i].y += _tears[i].vy * dt;
      if (_tears[i].y > 142.f) _tears[i].on = false;
    }
  }
  void _drawTears(uint16_t* fb) {
    const uint16_t BODY = tefx_rgb(150, 205, 255), SHINE = tefx_rgb(240, 250, 255);
    const bool flip = _flipArt();
    for (uint8_t i = 0; i < EYEFX_MAX_TEARS; i++) if (_tears[i].on) {
      int cx = (int)_tears[i].x, cy = (int)_tears[i].y;
      for (int dy = -10; dy <= 4; dy++) {
        int y = cy + dy; if (y < 0 || y >= SCREEN_HEIGHT) continue;
        float hw = (dy <= 0) ? 4.5f * (dy + 10) / 10.f : sqrtf(20.25f - (float)(dy * dy));
        int ihw = (int)(hw + 0.5f);
        for (int dx = -ihw; dx <= ihw; dx++) {
          int x = cx + dx; if (x < 0 || x >= SCREEN_WIDTH) continue;
          int idx = y * SCREEN_WIDTH + x;
          int midx = y * SCREEN_WIDTH + (flip ? (SCREEN_WIDTH - 1 - x) : x);
          if (pgm_read_byte(&upper[midx]) == 0) continue;
          int a = 225; if (y > 112) a = 225 - (y - 112) * 12; if (a < 0) a = 0;
          uint16_t c = (dx <= -1 && dx >= -2 && dy >= -1 && dy <= 1) ? SHINE : BODY;
          fb[idx] = tefx_mix(fb[idx], c, a);
        }
      }
    }
  }

  // ------------------------------- glitch --------------------------------
  void _updateGlitch(uint32_t now, bool enabled) {
    if (!enabled) { _glitchOn = false; return; }
    if (!_glitchOn && (int32_t)(now - _nextGlitch) >= 0) {
      _glitchOn = true; _glitchEnd = now + (uint32_t)_frand(90, 260); _glitchTintAt = 0;
    }
    if (_glitchOn) {
      if ((int32_t)(now - _glitchEnd) >= 0) {
        _glitchOn = false; _nextGlitch = now + (uint32_t)_frand(500, 1800);
        return;
      }
      _nBands = 3 + (_rand() % 3);
      for (uint8_t i = 0; i < _nBands; i++) {
        _bands[i].y = (int16_t)_frand(0, 118); _bands[i].h = (int16_t)_frand(3, 16);
        _bands[i].dx = (int16_t)_frand(-22, 22);
      }
      _jx += _frand(-4, 4);
      if ((int32_t)(now - _glitchTintAt) >= 0) {
        static const float T[4][3] = {{0, .8f, 1}, {1, 1, 1}, {0, 1, .3f}, {1, 0, 0}};
        const float* t = T[_rand() & 3];
        _c.tr = t[0]; _c.tg = t[1]; _c.tb = t[2];
        _glitchTintAt = now + 70;
      }
    }
  }
  void _applyGlitch(uint16_t* fb) {
    uint16_t tmp[SCREEN_WIDTH];
    for (uint8_t b = 0; b < _nBands; b++) {
      for (int y = _bands[b].y; y < _bands[b].y + _bands[b].h && y < SCREEN_HEIGHT; y++) {
        uint16_t* row = fb + y * SCREEN_WIDTH;
        memcpy(tmp, row, sizeof(tmp));
        for (int x = 0; x < SCREEN_WIDTH; x++) row[(x + _bands[b].dx) & (SCREEN_WIDTH - 1)] = tmp[x];
      }
    }
  }
};
