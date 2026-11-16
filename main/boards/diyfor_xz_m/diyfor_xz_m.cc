#include "sdkconfig.h"

// DIYfor XZ M pin map and display configuration
#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE 16000
#define AUDIO_OUTPUT_SAMPLE_RATE 16000
#define AUDIO_I2S_GPIO_WS GPIO_NUM_12
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_11
#define AUDIO_I2S_GPIO_DIN GPIO_NUM_14
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_13

#define BUILTIN_LED_GPIO GPIO_NUM_48
#define BOOT_BUTTON_GPIO GPIO_NUM_0

#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_OLED)
#if !defined(CONFIG_OLED_SSD1306_128X32) && !defined(CONFIG_OLED_SSD1306_128X64) && !defined(CONFIG_OLED_SH1106_128X64)
#define CONFIG_OLED_SSD1306_128X32 1
#endif
#define DIYFOR_XZ_M_USE_OLED 1
#define TOUCH_BUTTON_GPIO GPIO_NUM_47
#define VOLUME_UP_BUTTON_GPIO GPIO_NUM_4
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_5
#define DISPLAY_SDA_PIN GPIO_NUM_1
#define DISPLAY_SCL_PIN GPIO_NUM_2
#define DISPLAY_WIDTH 128
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true
#define LAMP_GPIO GPIO_NUM_6
#if defined(CONFIG_OLED_SSD1306_128X32)
#define DISPLAY_HEIGHT 32
#elif defined(CONFIG_OLED_SSD1306_128X64)
#define DISPLAY_HEIGHT 64
#elif defined(CONFIG_OLED_SH1106_128X64)
#define DISPLAY_HEIGHT 64
#define SH1106
#else
#error "Select an OLED type for DIYfor XZ M"
#endif
#elif defined(CONFIG_DIYFOR_XZ_M_DISPLAY_LCD)
#if !defined(CONFIG_LCD_ST7789_240X320) && !defined(CONFIG_LCD_ST7789_240X320_NO_IPS) && \
    !defined(CONFIG_LCD_ST7789_170X320) && !defined(CONFIG_LCD_ST7789_172X320) && \
    !defined(CONFIG_LCD_ST7789_240X280) && !defined(CONFIG_LCD_ST7789_240X240) && \
    !defined(CONFIG_LCD_ST7789_240X240_7PIN) && !defined(CONFIG_LCD_ST7789_240X135) && \
    !defined(CONFIG_LCD_ST7735_128X160) && !defined(CONFIG_LCD_ST7735_128X128) && \
    !defined(CONFIG_LCD_ST7796_320X480) && !defined(CONFIG_LCD_ST7796_320X480_NO_IPS) && \
    !defined(CONFIG_LCD_ILI9341_240X320) && !defined(CONFIG_LCD_ILI9341_240X320_NO_IPS) && \
    !defined(CONFIG_LCD_GC9A01_240X240) && !defined(CONFIG_LCD_CUSTOM)
#define CONFIG_LCD_ST7789_240X320 1
#endif
#define DIYFOR_XZ_M_USE_LCD 1
#define TOUCH_BUTTON_GPIO GPIO_NUM_NC
#define VOLUME_UP_BUTTON_GPIO GPIO_NUM_NC
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC
#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_16
#define DISPLAY_MOSI_PIN GPIO_NUM_18
#define DISPLAY_CLK_PIN GPIO_NUM_17
#define DISPLAY_DC_PIN GPIO_NUM_3
#define DISPLAY_RST_PIN GPIO_NUM_46
#define DISPLAY_CS_PIN GPIO_NUM_10
#define LAMP_GPIO GPIO_NUM_38
#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_LCD) && defined(CONFIG_LCD_ST7789_240X320)
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_LCD) && defined(CONFIG_LCD_ST7789_240X320_NO_IPS)
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_LCD) && defined(CONFIG_LCD_ST7789_170X320)
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   170
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  35
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_LCD) && defined(CONFIG_LCD_ST7789_172X320)
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   172
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  34
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_LCD) && defined(CONFIG_LCD_ST7789_240X280)
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  280
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  20
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_LCD) && defined(CONFIG_LCD_ST7789_240X240)
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_LCD) && defined(CONFIG_LCD_ST7789_240X240_7PIN)
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 3
#endif

#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_LCD) && defined(CONFIG_LCD_ST7789_240X135)
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  135
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY true
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  40
#define DISPLAY_OFFSET_Y  53
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_LCD) && defined(CONFIG_LCD_ST7735_128X160)
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  160
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_LCD) && defined(CONFIG_LCD_ST7735_128X128)
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  128
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR  false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  32
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_LCD) && defined(CONFIG_LCD_ST7796_320X480)
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  480
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_LCD) && defined(CONFIG_LCD_ST7796_320X480_NO_IPS)
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  480
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_LCD) && defined(CONFIG_LCD_ILI9341_240X320)
#define LCD_TYPE_ILI9341_SERIAL
//#define DISPLAY_WIDTH   240
//#define DISPLAY_HEIGHT  320
//#define DISPLAY_MIRROR_X false
//#define DISPLAY_MIRROR_Y true
//#define DISPLAY_SWAP_XY false
#define DISPLAY_WIDTH   320      // đổi từ 240
#define DISPLAY_HEIGHT  240      // đổi từ 320
#define DISPLAY_MIRROR_X false   // giữ nguyên
#define DISPLAY_MIRROR_Y false   // đổi từ true
#define DISPLAY_SWAP_XY true     // đổi từ false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_LCD) && defined(CONFIG_LCD_ILI9341_240X320_NO_IPS)
#define LCD_TYPE_ILI9341_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_LCD) && defined(CONFIG_LCD_GC9A01_240X240)
#define LCD_TYPE_GC9A01_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_LCD) && defined(CONFIG_LCD_CUSTOM)
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif


#else
#error "Select OLED or LCD for DIYfor XZ M"
#endif

#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <esp_system.h>
#include <algorithm>
#include <string>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

#ifdef CONFIG_ROBOT_BASE_ENABLE
#include "robot_base/robot_base.h"
#endif

#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_OLED)
#include "display/oled_display.h"
#include "mochi_display.h"
#include "settings.h"
#include <esp_lcd_panel_sh1106.h>
#elif defined(CONFIG_DIYFOR_XZ_M_DISPLAY_LCD)
#include "display/lcd_display.h"
#include <driver/spi_common.h>
#if defined(LCD_TYPE_ILI9341_SERIAL)
#include <esp_lcd_ili9341.h>
#endif
#if defined(LCD_TYPE_GC9A01_SERIAL)
#include <esp_lcd_gc9a01.h>
#endif
#else
#error "Select OLED or LCD for DIYfor XZ M"
#endif

#define TAG "DIYforXzMBoard"

class DiyforXzMBoard : public WifiBoard {
private:
    Display* display_ = nullptr;
    Button boot_button_;

#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_OLED)
    i2c_master_bus_handle_t display_i2c_bus_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Button touch_button_;
    Button volume_up_button_;
    Button volume_down_button_;

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {.enable_internal_pullup = 1},
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeOledDisplay() {
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {.dc_low_on_data = 0, .disable_control_phase = 0},
            .scl_speed_hz = 400 * 1000,
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;
        esp_lcd_panel_ssd1306_config_t oled_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &oled_config;
#ifdef SH1106
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
#endif
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize OLED");
            display_ = new NoDisplay();
            return;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        Settings settings("board", false);
        const std::string face = settings.GetString("face", "mochi");
        if (face == "default") {
            display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                       DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        } else {
            display_ = new MochiDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                        DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        }
    }
#else
    void InitializeSpi() {
        spi_bus_config_t bus_config = {};
        bus_config.mosi_io_num = DISPLAY_MOSI_PIN;
        bus_config.miso_io_num = GPIO_NUM_NC;
        bus_config.sclk_io_num = DISPLAY_CLK_PIN;
        bus_config.quadwp_io_num = GPIO_NUM_NC;
        bus_config.quadhd_io_num = GPIO_NUM_NC;
        bus_config.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus_config, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
#if defined(LCD_TYPE_ILI9341_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        display_ = CreateSpiLcdDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                       DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
                                       DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }
#endif

    void InitializeButtons() {
#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_OLED)
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
        touch_button_.OnPressDown([]() { Application::GetInstance().StartListening(); });
        touch_button_.OnPressUp([]() { Application::GetInstance().StopListening(); });

        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            const int volume = std::min(codec->output_volume() + 10, 100);
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });
        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });
        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            const int volume = std::max(codec->output_volume() - 10, 0);
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });
        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
#else
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
#endif
    }

    void InitializeTools() {
#ifndef CONFIG_ROBOT_BASE_ENABLE
        static LampController lamp(LAMP_GPIO);
#endif

#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_OLED)
        McpServer::GetInstance().AddTool(
            "self.screen.switch_face",
            "Save the display face as 'default' or 'mochi' and restart to apply it.",
            PropertyList({Property("face", kPropertyTypeString)}),
            [](const PropertyList& properties) -> ReturnValue {
                const auto face = properties["face"].value<std::string>();
                if (face != "default" && face != "mochi") {
                    return std::string("unknown face: ") + face;
                }
                Settings settings("board", true);
                settings.SetString("face", face);
                xTaskCreate([](void*) {
                    vTaskDelay(pdMS_TO_TICKS(1500));
                    esp_restart();
                }, "restart_delay", 2048, nullptr, 5, nullptr);
                return std::string("Saved display face: ") + face;
            });
#endif
    }

public:
#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_OLED)
    DiyforXzMBoard()
        : boot_button_(BOOT_BUTTON_GPIO), touch_button_(TOUCH_BUTTON_GPIO), volume_up_button_(VOLUME_UP_BUTTON_GPIO),
          volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        InitializeDisplayI2c();
        InitializeOledDisplay();
        InitializeButtons();
        InitializeTools();
#ifdef CONFIG_ROBOT_BASE_ENABLE
        RobotBase::GetInstance().Init(display_i2c_bus_);
#endif
    }
#else
    DiyforXzMBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        InitializeTools();
        GetBacklight()->RestoreBrightness();
#ifdef CONFIG_ROBOT_BASE_ENABLE
        RobotBase::GetInstance().Init(RobotBase::CreateI2cBus());
#endif
    }
#endif

    Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    AudioCodec* GetAudioCodec() override {
#if defined(CONFIG_DIYFOR_XZ_M_MIC_RIGHT)
        constexpr auto mic_slot = I2S_STD_SLOT_RIGHT;
#elif defined(CONFIG_DIYFOR_XZ_M_MIC_BOTH)
        constexpr auto mic_slot = I2S_STD_SLOT_BOTH;
#else
        constexpr auto mic_slot = I2S_STD_SLOT_LEFT;
#endif
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            mic_slot);
        return &audio_codec;
    }

    Display* GetDisplay() override { return display_; }

    Backlight* GetBacklight() override {
#if defined(CONFIG_DIYFOR_XZ_M_DISPLAY_LCD)
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
#else
        return nullptr;
#endif
    }
};

DECLARE_BOARD(DiyforXzMBoard);
