// BlackHat ESP32-S3 — no TFT display
// This is a minimal stub required by TFT_eSPI build system.
// The actual display is SSD1306 OLED via I2C, handled by BlackHatDisplay.

// We must define a driver to satisfy TFT_eSPI, but it will never be used.
#define ST7789_DRIVER

// Minimal pin definitions (not connected, just to compile)
#define TFT_MISO -1
#define TFT_MOSI -1
#define TFT_SCLK -1
#define TFT_CS   -1
#define TFT_DC   -1
#define TFT_RST  -1
#define TFT_BL   -1

#define TFT_WIDTH  240
#define TFT_HEIGHT 240

#define SPI_FREQUENCY  27000000
#define SPI_READ_FREQUENCY  20000000

// Fonts — keep minimal to save flash
#define LOAD_GLCD
#define LOAD_FONT2

#define SMOOTH_FONT
