// =============================================================================
//  naugaEyeFX.h  -  Bo hieu ung cam xuc cho MAT NAUGA (Naugahyde, mat hoat hinh)
//
//  Di kem voi naugaEye.h (du lieu anh: sclera / iris / polar / upper / lower).
//  naugaEye.h giu NGUYEN VEN nhu ban Adafruit Uncanny Eyes, khong can sua.
//
//  Kieu mat: long trang tron, mong mat trang, con nguoi den.
//  Giong owlEye ve hinh hoc, chi khac mau mong mat.
//
//  17 cam xuc, API y het terminatorEyeFX.h (class NaugaEye):
//      NEUTRAL HAPPY LAUGH ANGRY SAD CRY SURPRISED SCARED LOVE SLEEPY SLEEP
//      WINK SUSPICIOUS DIZZY SCAN EVIL GLITCH
//
//  Cach dung:
//      #include "naugaEye.h"
//      #include "naugaEyeFX.h"        // theo thu tu nay
//      NaugaEye eye;
//      uint16_t fb[SCREEN_WIDTH * SCREEN_HEIGHT];
//      setup():  eye.begin(false);  eye.setEmotion(EYE_HAPPY);   // mat phai: begin(true)
//      loop():   eye.update(millis());  eye.render(fb);
//
//  Moi lan build chi include MOT kieu mat (cac kieu cung dung chung ten bien).
//  Engine nam trong eyeFXCore.h (dung chung cho tat ca cac kieu).
// =============================================================================
#ifndef NAUGA_EYE_FX_H_
#define NAUGA_EYE_FX_H_

#define EYEFX_CLASS      NaugaEye
// Khoang "scale iris" thuc su dung: LO = con nguoi nho nhat, HI = gian nhat
#define EYEFX_SC_LO      18
#define EYEFX_SC_HI      100
// Do lech toi da (px) cua mat khi liec het co
#define EYEFX_GAZE_SHIFT 18
// Mau vien mi (RGB565)
#define EYEFX_RIM        EYEFX_RGB565(50, 50, 50)
// Mat na mi trong naugaEye.h co goc mat o ben TRAI -> mat trai duoc lat guong
#define EYEFX_DUCT_LEFT  1
// 1: nhuom thau kinh theo cam xuc (chi hop mat do kieu robot) - dong vat de 0
#define EYEFX_COLOR_TINT 0

#include "eyeFXCore.h"

#endif  // NAUGA_EYE_FX_H_
