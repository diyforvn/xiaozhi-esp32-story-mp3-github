#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "lvgl_display.h"
#include "gif/lvgl_gif.h"   // thêm
#include "idle_animation_registry.h"   // THÊM

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

#include <memory>   // thêm

#include <vector> 

class OledDisplay : public LvglDisplay {
private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    lv_obj_t* top_bar_ = nullptr;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* content_left_ = nullptr;
    lv_obj_t* content_right_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    lv_obj_t *emotion_label_ = nullptr;
    lv_obj_t* emoji_image_ = nullptr;               // THÊM: hiện GIF
    std::unique_ptr<LvglGif> gif_controller_;        // THÊM: điều khiển GIF
    lv_obj_t* chat_message_label_ = nullptr;

    lv_obj_t* idle_anim_img_ = nullptr;               // THÊM: full-width GIF
    std::unique_ptr<LvglGif> idle_gif_controller_;     // THÊM
    std::unique_ptr<LvglRawImage> idle_gif_image_;     // THÊM
    std::string idle_anim_current_name_;                // THÊM
    bool idle_anim_active_ = false;

    bool LoadIdleAnimationByName(const std::string& name);   // THÊM (helper private)
    void ApplyIdleState();   // THÊM

    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

    void SetupUI_128x64();
    void SetupUI_128x32();

public:
    OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height, bool mirror_x, bool mirror_y);
    ~OledDisplay();

    virtual void SetupUI() override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetTheme(Theme* theme) override;
    virtual void SetIdleAnimation(bool active) override;      // THÊM
    virtual std::vector<std::string> GetIdleAnimationNames() override;    // THÊM
    virtual bool SetIdleAnimationName(const std::string& name) override;  // THÊM
};

#endif // OLED_DISPLAY_H
