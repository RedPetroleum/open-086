# HU-086 v01 — research notes

AI Multi-functional Intelligent Palm Phone Kit, manufacturer hxfb (hxfbai.com),
sold among others by diymore on AliExpress. DIY soldering kit: SMD parts are
pre-assembled, only the through-hole parts are soldered by hand.

As of: 2026-09-27. Pinout, display settings and flash layout are confirmed on the device
(retro-go runs with them, see the README).

---

## 1. Hardware

| Component | Details |
|---|---|
| MCU | ESP32-S3-N16R8 (16 MB flash, 8 MB PSRAM), read off the module marking |
| Display | 2.4" TFT, 240x320, 10-pin FPC, ST7789(V) controller |
| Audio amplifier | NS4168 (called NS4186 in the xiaozhi config, probably a typo) |
| Microphone | PDM, not I2S |
| Power IC | ETA9640 |
| External storage | MicroSD slot, SD_MMC in 1-bit mode |
| Battery | 3.7 V LiPo, approx. 350 mA at max volume and brightness |
| Case dimensions | 81.7 x 109.6 x 21.6 mm |
| Controls | D-pad, A, B, SELECT, START, power button |

### Display operation

- Native 240x320, used in landscape in the xiaozhi config: `DISPLAY_WIDTH 320`,
  `DISPLAY_HEIGHT 240`, `SWAP_XY true`, `MIRROR_X true`
- **Effectively 320x240**
- SPI mode 3 (not 0), pclk 40 MHz, 16 bpp
- Colour inversion required (`gfx->invertDisplay(true)` with Arduino_GFX)
- Backlight PWM inverted (`DISPLAY_BACKLIGHT_OUTPUT_INVERT true`)
- CS is **not** connected
- GPIO41 (RST) boots muxed to JTAG and needs `gpio_reset_pin()` before use, otherwise the
  panel stays in reset: backlight on, screen black
- The blogger first tried ILI9341 and GC9A01 without success before ST7789 worked

---

## 2. Pinout (traced by JK1VCK, confirmed on the device)

### Display
| Signal | GPIO |
|---|---|
| MOSI | 39 |
| CLK | 40 |
| DC | 38 |
| RST | 41 |
| Backlight | 42 |
| CS | not connected |

### Buttons (all active low, internal pull-ups required)
| Button | GPIO |
|---|---|
| UP | 18 |
| LEFT | 8 |
| RIGHT | 46 |
| DOWN | 14 |
| B | 45 |
| START | 48 |
| A | 47 |
| SELECT | 21 |

### MicroSD (SD_MMC, 1-bit)
| Signal | GPIO |
|---|---|
| CLK | 10 |
| CMD | 9 |
| D0 | 11 |

### Audio
| Signal | GPIO |
|---|---|
| I2S DOUT (speaker) | 7 |
| I2S BCLK | 15 |
| I2S LRCK | 16 |
| PA enable (NS4168) | 17 |
| PDM mic SCK | 5 |
| PDM mic DIN | 4 |

### Power
| Signal | GPIO | Note |
|---|---|---|
| POWER_SW | 2 | INPUT_PULLUP |
| POWER_CTRL | 1 | **must go HIGH at startup**, otherwise the board switches off |
| free pin | 6 | used as LED pin in the xiaozhi config, no LED fitted |

### The blogger's Arduino board settings
`ESP32S3 Dev, USBCDCOnBoot=ds, Flash=16MB, Partition: 16M Flash (3MB APP/9.9MB FATFS), PSRAM=OPI`

**Important:** the octal PSRAM occupies further GPIOs (typically 33–37), which are not free to use.

---

## 3. Factory firmware (from the seller's product description)

- 25 pre-installed games, explicitly intended only as a function test
- **NES emulator with SD card expansion**, up to approx. 1000 games
- Approx. 7 MB internal game storage, **can be filled and cleared over USB from a computer**
- Save/load function available
- Not all NES games run
- Two-player mode over Bluetooth via an Android app
- Also: weather clock (time, date, temperature, humidity, forecast),
  music spectrum (4 modes, 5 colours, 5 sensitivity levels),
  Xiaozhi AI voice dialogue
- Weather clock and AI require Wi-Fi and device activation
- Volume 0–4, brightness 10–100, auto power-off after 1 minute of inactivity
- USB-C according to the description: "power interface for charging **or data transmission**"
- SD card and games are **not** included

### Flash layout (read from our own backup)

| Partition | Offset | Size | Content |
|---|---|---|---|
| nvs | 0x009000 | 20 KB | settings |
| otadata | 0x00e000 | 8 KB | boot selection |
| factory | 0x010000 | 4 MB | main firmware |
| ffat | 0x410000 | 7 MB | game storage (`NO NAME` over USB) |
| app0 | 0xb10000 | 4 MB | second app, presumably Xiaozhi AI |
| model | 0xf10000 | 896 KB | speech model |
| coredump | 0xff0000 | 64 KB | crash dumps |

To read it again from a backup (the partition table is at 0x8000):

```
dd if=backup_XXXX.bin bs=1 skip=$((0x8000)) count=3072 of=pt.bin
python $IDF_PATH/components/partition_table/gen_esp32part.py pt.bin
```

---

## 4. What is possible with it

### a) Your own NES game (no modification needed)
Toolchain: cc65 (C/assembler for the 6502), NESFab or plain 6502 assembler.
Test on the PC with Mesen. Put the ROM on the device via USB or SD.

Hurdles: 2 KB RAM, no floating point, no hardware division, no
framebuffer (the PPU is tile-based, writes only during the VBlank window),
max. 8 sprites per scanline, the mapper choice must match the emulator.
Errors usually show up as a black screen without any message.

### b) Your own firmware
xiaozhi-esp32 (MIT licence) already has a working board target with the
blogger's config. For your own applications, ESP-IDF or Arduino.
There is an NES emulator (`ESP32-S3_Uno-nofrendo`) and a
multi-app selector, both by the same blogger on exactly this board.

### c) retro-go and Doom
Running: retro-go with its own target in this repo (`retro-go/hu-086/`), including
`prboom-go`. Shareware doom1.wad goes to `roms/doom/` on the SD card.

---

## 5. Flashing

The console's USB-C port cannot be used for flashing. The ESP32-C3 bridge on the
pin header (IO0, EN, RXD, TXD, GND) does it, see the README.

The factory firmware latches the power via POWER_CTRL (GPIO1). In download mode
nothing holds it, so the power button has to stay pressed for the whole run.

---

## 6. Open points

- Actual frame rate at 320x240 over SPI
- Battery voltage ADC pin (retro-go shows no battery level)

---

## 7. Sources

- **Board analysis and pinout, test sketches:**
  https://gijin77.blog.jp/archives/46577853.html
  (JK1VCK / "Skyzoo ヨッシーの備忘録", January 2026, Japanese)
- **xiaozhi v2.1.0 on the HU-086, complete config.h and board.cc:**
  https://gijin77.blog.jp/archives/46637438.html
- **Multi-app selector on the HU-086:**
  https://gijin77.blog.jp/archives/46690906.html
- **Manufacturer's manual:** https://hxfbai.com/newsinfo/8859096.html
  (linked in the blog, not retrieved by me)
- **xiaozhi-esp32 source code:** https://github.com/78/xiaozhi-esp32 (MIT)
- **Retro-Go:** https://github.com/ducalex/retro-go
- The blogger's AliExpress listings:
  https://ja.aliexpress.com/item/1005010588888605.html and
  https://ja.aliexpress.com/item/1005010589125005.html
