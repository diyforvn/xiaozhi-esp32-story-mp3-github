// =============================================================================
//  dragonEyeFX.h  -  Bo hieu ung cam xuc cho MAT RONG
//
//  Di kem voi dragonEye.h (du lieu anh: sclera / iris / polar / upper / lower).
//  dragonEye.h giu NGUYEN VEN nhu ban Adafruit Uncanny Eyes, khong can sua.
//
//  Kieu mat: khong co long trang, mong mat vang-cam, con nguoi khe doc.
//  Sclera 160px chi pan duoc +-16px nen luc liec mat toi da bi gioi han o 16px.
//
//  17 cam xuc, API y het terminatorEyeFX.h (class DragonEye):
//      NEUTRAL HAPPY LAUGH ANGRY SAD CRY SURPRISED SCARED LOVE SLEEPY SLEEP
//      WINK SUSPICIOUS DIZZY SCAN EVIL GLITCH
//
//  Cach dung:
//      #include "dragonEye.h"
//      #include "dragonEyeFX.h"        // theo thu tu nay
//      DragonEye eye;
//      uint16_t fb[SCREEN_WIDTH * SCREEN_HEIGHT];
//      setup():  eye.begin(false);  eye.setEmotion(EYE_HAPPY);   // mat phai: begin(true)
//      loop():   eye.update(millis());  eye.render(fb);
//
//  Moi lan build chi include MOT kieu mat (cac kieu cung dung chung ten bien).
//  Engine nam trong eyeFXCore.h (dung chung cho tat ca cac kieu).
// =============================================================================
#ifndef DRAGON_EYE_FX_H_
#define DRAGON_EYE_FX_H_

#define EYEFX_CLASS      DragonEye
// Khoang "scale iris" thuc su dung: LO = con nguoi nho nhat, HI = gian nhat
#define EYEFX_SC_LO      80
#define EYEFX_SC_HI      150
// Do lech toi da (px) cua mat khi liec het co
#define EYEFX_GAZE_SHIFT 16
// Mau vien mi (RGB565)
#define EYEFX_RIM        EYEFX_RGB565(80, 32, 0)
// Mat na mi trong dragonEye.h co goc mat o ben TRAI -> mat trai duoc lat guong
#define EYEFX_DUCT_LEFT  1
// 1: nhuom thau kinh theo cam xuc (chi hop mat do kieu robot) - dong vat de 0
#define EYEFX_COLOR_TINT 0

#include "eyeFXCore.h"

#endif  // DRAGON_EYE_FX_H_
