# DIYfor XZ M

DIYfor XZ M is an independent ESP32-S3 board implementation. Its GPIO map, panel configuration, and display initialization are all in `diyfor_xz_m.cc`; it does not include either Compact WiFi board source or config header. Add future board IO and applications to this board implementation without changing the original boards.

Select the connected display and microphone slot in `idf.py menuconfig` under **Xiaozhi Assistant**:

- **DIYfor XZ M Display** selects OLED (Compact WiFi I2C pins) or SPI LCD (Compact WiFi LCD pins).
- **OLED Type** or **LCD Type** selects the panel driver and geometry. Each display selection activates only its GPIO set.
- **DIYfor XZ M I2S Microphone Channel** selects left, right, or both I2S slots. Both slots are averaged to mono to preserve the existing audio pipeline format.

Current pin map:

| Peripheral | GPIO |
| --- | --- |
| I2S BCLK / WS / DIN / DOUT | 11 / 12 / 14 / 13 |
| LED / boot button | 48 / 0 |
| OLED SDA / SCL | 1 / 2 |
| OLED touch / volume up / volume down | 47 / 4 / 5 |
| LCD MOSI / CLK / DC / RST / CS / backlight | 18 / 17 / 3 / 46 / 10 / 16 |
| Lamp MCP output (OLED / LCD) | 6 / 38 |

Edit the GPIO/panel section at the top of `diyfor_xz_m.cc` for board pins and panel defaults; add peripheral initialization and application behavior in the same file.
