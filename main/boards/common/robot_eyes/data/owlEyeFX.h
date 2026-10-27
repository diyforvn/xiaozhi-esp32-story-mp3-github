// =============================================================================
//  owlEyeFX.h  -  Bo hieu ung cam xuc cho MAT CU
//
//  Di kem voi owlEye.h (du lieu anh: sclera / iris / polar / upper / lower).
//  owlEye.h giu NGUYEN VEN nhu ban Adafruit Uncanny Eyes, khong can sua.
//
//  Kieu mat: long trang, mong mat xanh duong, con nguoi den.
//  Con nguoi to nho thay doi ro nhat trong tat ca cac kieu (vong xanh co/gian).
//
//  17 cam xuc, API y het terminatorEyeFX.h (class OwlEye):
//      NEUTRAL HAPPY LAUGH ANGRY SAD CRY SURPRISED SCARED LOVE SLEEPY SLEEP
//      WINK SUSPICIOUS DIZZY SCAN EVIL GLITCH
//
//  Cach dung:
//      #include "owlEye.h"
//      #include "owlEyeFX.h"        // theo thu tu nay
//      OwlEye eye;
//      uint16_t fb[SCREEN_WIDTH * SCREEN_HEIGHT];
//      setup():  eye.begin(false);  eye.setEmotion(EYE_HAPPY);   // mat phai: begin(true)
//      loop():   eye.update(millis());  eye.render(fb);
//
//  Moi lan build chi include MOT kieu mat (cac kieu cung dung chung ten bien).
//  Engine nam trong eyeFXCore.h (dung chung cho tat ca cac kieu).
// =============================================================================
#ifndef OWL_EYE_FX_H_
#define OWL_EYE_FX_H_

#define EYEFX_CLASS      OwlEye
// Khoang "scale iris" thuc su dung: LO = con nguoi nho nhat, HI = gian nhat
#define EYEFX_SC_LO      18
#define EYEFX_SC_HI      100
// Do lech toi da (px) cua mat khi liec het co
#define EYEFX_GAZE_SHIFT 18
// Mau vien mi (RGB565)
#define EYEFX_RIM        EYEFX_RGB565(60, 60, 70)
// Mat na mi trong owlEye.h co goc mat o ben TRAI -> mat trai duoc lat guong
#define EYEFX_DUCT_LEFT  1
// 1: nhuom thau kinh theo cam xuc (chi hop mat do kieu robot) - dong vat de 0
#define EYEFX_COLOR_TINT 0

#include "eyeFXCore.h"

#endif  // OWL_EYE_FX_H_
