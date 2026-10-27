#include "display/lcd_display.h"

#if defined(CONFIG_FACE_STYLE_ROBOT_EYES) && CONFIG_FACE_STYLE_ROBOT_EYES
#include "boards/common/robot_eyes/eyes_display.h"
#endif

LcdDisplay* CreateSpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                                int width, int height, int offset_x, int offset_y,
                                bool mirror_x, bool mirror_y, bool swap_xy) {
#if defined(CONFIG_FACE_STYLE_ROBOT_EYES) && CONFIG_FACE_STYLE_ROBOT_EYES
    return new EyesDisplay(panel_io, panel, width, height, offset_x, offset_y,
                           mirror_x, mirror_y, swap_xy);
#else
    return new SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y,
                             mirror_x, mirror_y, swap_xy);
#endif
}
