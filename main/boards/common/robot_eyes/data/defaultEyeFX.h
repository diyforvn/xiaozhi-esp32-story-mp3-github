// =============================================================================
//  defaultEyeFX.h  -  Bo hieu ung cam xuc cho MAT NGUOI (mac dinh Adafruit)
//
//  Di kem voi defaultEye.h (du lieu anh: sclera / iris / polar / upper / lower).
//  defaultEye.h giu NGUYEN VEN nhu ban Adafruit Uncanny Eyes, khong can sua.
//
//  Kieu mat: long trang co tia mau, mong mat xanh luc, con nguoi tron.
//  Goc IRIS_MIN/MAX cua Adafruit (90..130) qua hep de thay cam xuc nen engine dung khoang 85..190.
//
//  17 cam xuc, API y het terminatorEyeFX.h (class DefaultEye):
//      NEUTRAL HAPPY LAUGH ANGRY SAD CRY SURPRISED SCARED LOVE SLEEPY SLEEP
//      WINK SUSPICIOUS DIZZY SCAN EVIL GLITCH
//
//  Cach dung:
//      #include "defaultEye.h"
//      #include "defaultEyeFX.h"        // theo thu tu nay
//      DefaultEye eye;
//      uint16_t fb[SCREEN_WIDTH * SCREEN_HEIGHT];
//      setup():  eye.begin(false);  eye.setEmotion(EYE_HAPPY);   // mat phai: begin(true)
//      loop():   eye.update(millis());  eye.render(fb);
//
//  Moi lan build chi include MOT kieu mat (cac kieu cung dung chung ten bien).
//  Engine nam trong eyeFXCore.h (dung chung cho tat ca cac kieu).
// =============================================================================
#ifndef DEFAULT_EYE_FX_H_
#define DEFAULT_EYE_FX_H_

#define EYEFX_CLASS      DefaultEye
// Khoang "scale iris" thuc su dung: LO = con nguoi nho nhat, HI = gian nhat
#define EYEFX_SC_LO      85
#define EYEFX_SC_HI      190
// Do lech toi da (px) cua mat khi liec het co
#define EYEFX_GAZE_SHIFT 18
// Mau vien mi (RGB565)
#define EYEFX_RIM        EYEFX_RGB565(74, 40, 32)
// Mat na mi trong defaultEye.h co goc mat o ben TRAI -> mat trai duoc lat guong
#define EYEFX_DUCT_LEFT  1
// 1: nhuom thau kinh theo cam xuc (chi hop mat do kieu robot) - dong vat de 0
#define EYEFX_COLOR_TINT 0

#include "eyeFXCore.h"

#endif  // DEFAULT_EYE_FX_H_
