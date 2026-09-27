// Target definition
#define RG_TARGET_NAME             "HU-086"

// Storage: MicroSD, SD_MMC 1-bit
#define RG_STORAGE_ROOT             "/sd"
#define RG_STORAGE_SDMMC_HOST       SDMMC_HOST_SLOT_1
#define RG_STORAGE_SDMMC_SPEED      SDMMC_FREQ_DEFAULT

// Audio: NS4168, I2S
#define RG_AUDIO_USE_INT_DAC        0   // 0 = Disable, 1 = GPIO25, 2 = GPIO26, 3 = Both
#define RG_AUDIO_USE_EXT_DAC        1   // 0 = Disable, 1 = Enable

// Board-specific
// GPIO1 holds the ETA9640 power latch; without it the console switches off as soon as the
// power button is released. hu086.c keeps it high across esp_restart() and watches the button.
#define HU086_POWER_CTRL            GPIO_NUM_1
#define HU086_POWER_SW              GPIO_NUM_2
void hu086_platform_init(void);
#define RG_CUSTOM_PLATFORM_INIT()   hu086_platform_init()

// Video: ST7789, 240x320 native, used in landscape. CS is not connected, which needs SPI mode 3.
#define RG_SCREEN_DRIVER            0   // 0 = ILI9341/ST7789
#define RG_SCREEN_HOST              SPI2_HOST
#define RG_SCREEN_SPEED             SPI_MASTER_FREQ_40M
#define RG_SCREEN_SPI_MODE          3
#define RG_SCREEN_BACKLIGHT         1
#define RG_SCREEN_WIDTH             320
#define RG_SCREEN_HEIGHT            240
#define RG_SCREEN_ROTATE            0
#define RG_SCREEN_VISIBLE_AREA      {0, 0, 0, 0}
#define RG_SCREEN_SAFE_AREA         {0, 0, 0, 0}
// MADCTL 0x60 = MV|MX, like SWAP_XY + MIRROR_X in the xiaozhi config (tested on the device).
// Same commands as esp_lcd's ST7789 init. The driver's SWRESET comes 5 ms before this, the
// ST7789 needs 120 ms. The driver's own 0x11/0x29 afterwards are harmless.
// The panel reset pin needs gpio_reset_pin() first, see hu086.c.
#define RG_SCREEN_INIT()                                                                                         \
    rg_usleep(150 * 1000);                   /* ST7789: 120 ms after reset */                                    \
    ILI9341_CMD(0x11);                       /* Sleep out */                                                     \
    rg_usleep(120 * 1000);                                                                                       \
    ILI9341_CMD(0x36, 0x60);                 /* Memory Access Control (MV|MX) */                                 \
    ILI9341_CMD(0x3A, 0x55);                 /* COLMOD RGB565 */                                                 \
    ILI9341_CMD(0x21);                       /* Display inversion on */

// Input: all buttons active low. Only 8 buttons, so MENU and OPTION are combos.
#define RG_GAMEPAD_GPIO_MAP {\
    {RG_KEY_UP,     .num = GPIO_NUM_18, .pullup = 1, .level = 0},\
    {RG_KEY_RIGHT,  .num = GPIO_NUM_46, .pullup = 1, .level = 0},\
    {RG_KEY_DOWN,   .num = GPIO_NUM_14, .pullup = 1, .level = 0},\
    {RG_KEY_LEFT,   .num = GPIO_NUM_8,  .pullup = 1, .level = 0},\
    {RG_KEY_SELECT, .num = GPIO_NUM_21, .pullup = 1, .level = 0},\
    {RG_KEY_START,  .num = GPIO_NUM_48, .pullup = 1, .level = 0},\
    {RG_KEY_A,      .num = GPIO_NUM_47, .pullup = 1, .level = 0},\
    {RG_KEY_B,      .num = GPIO_NUM_45, .pullup = 1, .level = 0},\
}
#define RG_GAMEPAD_VIRT_MAP {\
    {RG_KEY_MENU,   .src = RG_KEY_SELECT | RG_KEY_START},\
    {RG_KEY_OPTION, .src = RG_KEY_SELECT | RG_KEY_A},\
}

// Battery: no known ADC pin
#define RG_BATTERY_DRIVER           0

// SPI Display
#define RG_GPIO_LCD_MISO            GPIO_NUM_NC
#define RG_GPIO_LCD_MOSI            GPIO_NUM_39
#define RG_GPIO_LCD_CLK             GPIO_NUM_40
#define RG_GPIO_LCD_CS              GPIO_NUM_NC
#define RG_GPIO_LCD_DC              GPIO_NUM_38
#define RG_GPIO_LCD_BCKL            GPIO_NUM_42
#define RG_GPIO_LCD_BCKL_INVERT
#define RG_GPIO_LCD_RST             GPIO_NUM_41

// SD Card (SDMMC, the SDSPI names are what rg_storage.c reads)
#define RG_GPIO_SDSPI_CLK           GPIO_NUM_10
#define RG_GPIO_SDSPI_CMD           GPIO_NUM_9
#define RG_GPIO_SDSPI_D0            GPIO_NUM_11

// External I2S DAC
#define RG_GPIO_SND_I2S_BCK         15
#define RG_GPIO_SND_I2S_WS          16
#define RG_GPIO_SND_I2S_DATA        7
#define RG_GPIO_SND_AMP_ENABLE      17  // NS4168 CTRL, high = on
