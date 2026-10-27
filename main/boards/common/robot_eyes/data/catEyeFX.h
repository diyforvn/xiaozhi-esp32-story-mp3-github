// =============================================================================
//  catEyeFX.h  -  Bo hieu ung cam xuc cho MAT MEO
//
//  Di kem voi catEye.h (du lieu anh: sclera / iris / polar / upper / lower).
//  catEye.h giu NGUYEN VEN nhu ban Adafruit Uncanny Eyes, khong can sua.
//
//  Kieu mat: mat vang, con nguoi khe doc (slit) co gian theo cam xuc.
//  Con nguoi la khe dung: LOVE/SCARED/ANGRY -> khe manh, SURPRISED/LAUGH -> tron rong.
//
//  17 cam xuc, API y het terminatorEyeFX.h (class CatEye):
//      NEUTRAL HAPPY LAUGH ANGRY SAD CRY SURPRISED SCARED LOVE SLEEPY SLEEP
//      WINK SUSPICIOUS DIZZY SCAN EVIL GLITCH
//
//  Cach dung:
//      #include "catEye.h"
//      #include "catEyeFX.h"        // theo thu tu nay
//      CatEye eye;
//      uint16_t fb[SCREEN_WIDTH * SCREEN_HEIGHT];
//      setup():  eye.begin(false);  eye.setEmotion(EYE_HAPPY);   // mat phai: begin(true)
//      loop():   eye.update(millis());  eye.render(fb);
//
//  Moi lan build chi include MOT kieu mat (cac kieu cung dung chung ten bien).
//  Engine nam trong eyeFXCore.h (dung chung cho tat ca cac kieu).
// =============================================================================
#ifndef CAT_EYE_FX_H_
#define CAT_EYE_FX_H_

#define EYEFX_CLASS      CatEye
// Khoang "scale iris" thuc su dung: LO = con nguoi nho nhat, HI = gian nhat
#define EYEFX_SC_LO      17
#define EYEFX_SC_HI      36
// Do lech toi da (px) cua mat khi liec het co
#define EYEFX_GAZE_SHIFT 18
// Mau vien mi (RGB565)
#define EYEFX_RIM        EYEFX_RGB565(70, 56, 0)
// Mat na mi trong catEye.h co goc mat o ben TRAI -> mat trai duoc lat guong
#define EYEFX_DUCT_LEFT  1
// 1: nhuom thau kinh theo cam xuc (chi hop mat do kieu robot) - dong vat de 0
#define EYEFX_COLOR_TINT 0

#include "eyeFXCore.h"

#endif  // CAT_EYE_FX_H_
