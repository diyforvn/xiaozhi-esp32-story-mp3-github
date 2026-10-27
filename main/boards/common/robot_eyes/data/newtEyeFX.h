// =============================================================================
//  newtEyeFX.h  -  Bo hieu ung cam xuc cho MAT SA GIONG
//
//  Di kem voi newtEye.h (du lieu anh: sclera / iris / polar / upper / lower).
//  newtEye.h giu NGUYEN VEN nhu ban Adafruit Uncanny Eyes, khong can sua.
//
//  Kieu mat: vong mong mat nau do, con nguoi den co quang sang.
//  Goc IRIS_MIN/MAX = 180..750; engine dung 140..600 de thay du ca vong nau lan quang sang.
//
//  17 cam xuc, API y het terminatorEyeFX.h (class NewtEye):
//      NEUTRAL HAPPY LAUGH ANGRY SAD CRY SURPRISED SCARED LOVE SLEEPY SLEEP
//      WINK SUSPICIOUS DIZZY SCAN EVIL GLITCH
//
//  Cach dung:
//      #include "newtEye.h"
//      #include "newtEyeFX.h"        // theo thu tu nay
//      NewtEye eye;
//      uint16_t fb[SCREEN_WIDTH * SCREEN_HEIGHT];
//      setup():  eye.begin(false);  eye.setEmotion(EYE_HAPPY);   // mat phai: begin(true)
//      loop():   eye.update(millis());  eye.render(fb);
//
//  Moi lan build chi include MOT kieu mat (cac kieu cung dung chung ten bien).
//  Engine nam trong eyeFXCore.h (dung chung cho tat ca cac kieu).
// =============================================================================
#ifndef NEWT_EYE_FX_H_
#define NEWT_EYE_FX_H_

#define EYEFX_CLASS      NewtEye
// Khoang "scale iris" thuc su dung: LO = con nguoi nho nhat, HI = gian nhat
#define EYEFX_SC_LO      140
#define EYEFX_SC_HI      600
// Do lech toi da (px) cua mat khi liec het co
#define EYEFX_GAZE_SHIFT 18
// Mau vien mi (RGB565)
#define EYEFX_RIM        EYEFX_RGB565(24, 40, 56)
// Mat na mi trong newtEye.h co goc mat o ben TRAI -> mat trai duoc lat guong
#define EYEFX_DUCT_LEFT  1
// 1: nhuom thau kinh theo cam xuc (chi hop mat do kieu robot) - dong vat de 0
#define EYEFX_COLOR_TINT 0

#include "eyeFXCore.h"

#endif  // NEWT_EYE_FX_H_
