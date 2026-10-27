// =============================================================================
//  noScleraEyeFX.h  -  Bo hieu ung cam xuc cho MAT KHONG LONG TRANG
//
//  Di kem voi noScleraEye.h (du lieu anh: sclera / iris / polar / upper / lower).
//  noScleraEye.h giu NGUYEN VEN nhu ban Adafruit Uncanny Eyes, khong can sua.
//
//  Kieu mat: mong mat nau lan toan bo mat, con nguoi tron.
//  Goc IRIS_MIN/MAX = 120..550 lam con nguoi nuot ca man hinh o 128px; engine dung 90..230.
//
//  17 cam xuc, API y het terminatorEyeFX.h (class NoScleraEye):
//      NEUTRAL HAPPY LAUGH ANGRY SAD CRY SURPRISED SCARED LOVE SLEEPY SLEEP
//      WINK SUSPICIOUS DIZZY SCAN EVIL GLITCH
//
//  Cach dung:
//      #include "noScleraEye.h"
//      #include "noScleraEyeFX.h"        // theo thu tu nay
//      NoScleraEye eye;
//      uint16_t fb[SCREEN_WIDTH * SCREEN_HEIGHT];
//      setup():  eye.begin(false);  eye.setEmotion(EYE_HAPPY);   // mat phai: begin(true)
//      loop():   eye.update(millis());  eye.render(fb);
//
//  Moi lan build chi include MOT kieu mat (cac kieu cung dung chung ten bien).
//  Engine nam trong eyeFXCore.h (dung chung cho tat ca cac kieu).
// =============================================================================
#ifndef NOSCLERA_EYE_FX_H_
#define NOSCLERA_EYE_FX_H_

#define EYEFX_CLASS      NoScleraEye
// Khoang "scale iris" thuc su dung: LO = con nguoi nho nhat, HI = gian nhat
#define EYEFX_SC_LO      90
#define EYEFX_SC_HI      230
// Do lech toi da (px) cua mat khi liec het co
#define EYEFX_GAZE_SHIFT 16
// Mau vien mi (RGB565)
#define EYEFX_RIM        EYEFX_RGB565(70, 40, 28)
// Mat na mi trong noScleraEye.h co goc mat o ben TRAI -> mat trai duoc lat guong
#define EYEFX_DUCT_LEFT  1
// 1: nhuom thau kinh theo cam xuc (chi hop mat do kieu robot) - dong vat de 0
#define EYEFX_COLOR_TINT 0

#include "eyeFXCore.h"

#endif  // NOSCLERA_EYE_FX_H_
