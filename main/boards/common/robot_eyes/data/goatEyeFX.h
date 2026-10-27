// =============================================================================
//  goatEyeFX.h  -  Bo hieu ung cam xuc cho MAT DE
//
//  Di kem voi goatEye.h (du lieu anh: sclera / iris / polar / upper / lower).
//  goatEye.h giu NGUYEN VEN nhu ban Adafruit Uncanny Eyes, khong can sua.
//
//  Kieu mat: mong mat xam xanh, con nguoi hinh chu nhat nam ngang.
//  Cua so iris = 128px = ca man hinh, liec mat nhieu se lo mep -> gioi han 8px.
//
//  17 cam xuc, API y het terminatorEyeFX.h (class GoatEye):
//      NEUTRAL HAPPY LAUGH ANGRY SAD CRY SURPRISED SCARED LOVE SLEEPY SLEEP
//      WINK SUSPICIOUS DIZZY SCAN EVIL GLITCH
//
//  Cach dung:
//      #include "goatEye.h"
//      #include "goatEyeFX.h"        // theo thu tu nay
//      GoatEye eye;
//      uint16_t fb[SCREEN_WIDTH * SCREEN_HEIGHT];
//      setup():  eye.begin(false);  eye.setEmotion(EYE_HAPPY);   // mat phai: begin(true)
//      loop():   eye.update(millis());  eye.render(fb);
//
//  Moi lan build chi include MOT kieu mat (cac kieu cung dung chung ten bien).
//  Engine nam trong eyeFXCore.h (dung chung cho tat ca cac kieu).
// =============================================================================
#ifndef GOAT_EYE_FX_H_
#define GOAT_EYE_FX_H_

#define EYEFX_CLASS      GoatEye
// Khoang "scale iris" thuc su dung: LO = con nguoi nho nhat, HI = gian nhat
#define EYEFX_SC_LO      62
#define EYEFX_SC_HI      150
// Do lech toi da (px) cua mat khi liec het co
#define EYEFX_GAZE_SHIFT 8
// Mau vien mi (RGB565)
#define EYEFX_RIM        EYEFX_RGB565(40, 48, 56)
// Mat na mi trong goatEye.h co goc mat o ben TRAI -> mat trai duoc lat guong
#define EYEFX_DUCT_LEFT  1
// 1: nhuom thau kinh theo cam xuc (chi hop mat do kieu robot) - dong vat de 0
#define EYEFX_COLOR_TINT 0

#include "eyeFXCore.h"

#endif  // GOAT_EYE_FX_H_
