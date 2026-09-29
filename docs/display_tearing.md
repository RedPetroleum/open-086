# Display tearing on the HU-086

As of: 2026-09-29. Nothing here has been measured on the device yet. The cause is inferred from
the code, datasheet values and the symptom. Open to-do.

## Symptom

In the Meloni game Huf-Hüpfer ([meloni-games](https://github.com/RedPetroleum/meloni-games),
`games/hufhuepfer`) obstacles move horizontally across the screen. On the console they sometimes
look split: the upper part of an object is shifted a few pixels horizontally against the lower
part, at a varying height. The desktop runner of meloni-games and its screenshots don't show it.

## Why it is not the game or the engine

- Every obstacle is drawn in one piece per frame (one sprite, one x coordinate).
- `present()` in `retro-go/meloni/main/main.c` copies the engine framebuffer with `memcpy` into
  one of two `rg_surface_t` and calls `rg_display_submit()`, but only when `rg_display_sync(false)`
  reports that the display is free. Otherwise the frame is skipped. Half-copied frames can't
  happen in software.

## Likely cause: SPI LCD tearing without a sync signal

- **Display:** ST7789, 240×320 native, used in landscape. SPI mode 3, 40 MHz (`RG_SCREEN_SPEED`
  in `retro-go/hu-086/config.h`), CS not connected, no MISO.
- **Transfer time:** 320×240×16 bit ≈ 1.23 Mbit, about 31 ms at 40 MHz. The panel refreshes at
  about 60 Hz (16.7 ms, FRCTRL2 `0xC6` default). One transfer takes about two panel refreshes,
  and meanwhile the panel shows partly old, partly new data.
- **Partial updates:** `write_update()` in retro-go (`components/retro-go/rg_display.c`) only
  sends lines whose checksum changed, so the transfer time varies. In Huf-Hüpfer the ground
  scrolls, so many lines change every frame.
- **Orientation:** the init sets MADCTL `0x36 = 0x60` (MV|MX). MADCTL only changes the write
  addressing, not the panel's scan direction. The firmware writes landscape rows, the panel scans
  its native rows, which are columns in landscape. The tear line is therefore diagonal; on a
  small object it looks like a horizontal cut at a varying height.
- **Size of the offset:** obstacles move 3.2 (level 1) to 8 px (level 5) per logic frame. About
  30 fps are shown, irregularly, so two shown frames are 6 to 24 px apart. That is the offset
  at the tear line.
- **TE pin:** according to [HU-086_notes.md](HU-086_notes.md) only MOSI 39, CLK 40, DC 38,
  RST 41 and backlight 42 are known at the display. No TE pin is documented.

## Important: TE only helps with native orientation

With MV every landscape row that is sent covers all panel rows. Every transfer that overlaps a
scan tears, and every transfer is longer than the blanking period. With TE the diagonal tear
line would only stay in a fixed place.

In native orientation (no MV), starting on the TE edge:

- **40 MHz** (31 ms): the scan is faster than the write and stays ahead during refresh 1, which
  shows only the old frame. In refresh 2 the scan would catch up with the write only after about
  36 ms, by then the transfer is done. Clean at 30 fps.
- **80 MHz** (15 ms): even 60 fps without tearing is possible.

Cost of native orientation:

- Rotating is cheap: a tiled transpose instead of the `memcpy` in `present()` (estimated a few
  ms, not measured).
- The hard part is `rg_gui` (menu, options), which keeps drawing in landscape. Meloni would have
  to switch MADCTL back when opening the menu, or send game frames past `rg_display`. Changing
  `config.h` globally is not an option, the launcher and emulators depend on it.
- Native rows run across the scrolling ground, so partial updates no longer help. Fine at 30 fps.

## Options

| Option | Assessment |
|---|---|
| Find a TE pin | Send TEON (`0x35`, parameter `0x00`) in the init, sample the free GPIOs for a ~60 Hz square wave. Candidates from the README (free ADC pins): GPIO3, GPIO12, GPIO13, GPIO6. Cross-check: change FRCTRL2, the frequency must follow. Faster: identify the panel from its FPC and check with a multimeter whether its TE contact reaches the ESP32 at all. Only effective together with native orientation. |
| Read the panel (GSCAN) | TE replacement without an extra pin. In 4-line serial interface I the ST7789 data line is often bidirectional. Read over GPIO39 with a second SPI device on the same bus (`SPI_DEVICE_3WIRE \| SPI_DEVICE_HALFDUPLEX`, a few MHz). Test: RDDID (`0x04`), an ST7789V answers `85 85 52`. If that works, GSCAN (`0x45`) returns the current scanline. Depends on how the IM pins are wired. |
| SPI at 80 MHz | `SPI_MASTER_FREQ_80M`. Halves the transfer time, doubles the possible frame rate, halves the offset. Doesn't remove tearing. No values in between (integer divider of 80 MHz: 80 / 40 / 26.7). The datasheet only guarantees ~62.5 MHz; GPIO 39/40 go through the GPIO matrix. Possible failures: garbled image, wrong colors. |
| Fixed frame rate | Show every second frame (30 fps) instead of "when free". Tearing stays, motion gets smoother. Tight at 40 MHz (31 ms plus the per-line work in `write_update` against 33 ms), may drop to 20 fps; safe at 80 MHz. Measure first. |
| Change FRCTRL2 | Useless without a sync signal, only changes how fast the tear line wanders. |

## Plan

1. **Measure without new firmware:** a test game in meloni-games with a full-height white bar
   moving 4 px per frame on black, plus a frame counter. Film it in slow motion (240 fps). A
   diagonal tear line confirms the orientation theory.
2. **One test firmware build:** log transfer time and shown fps, 80 MHz, fixed frame rate, TEON
   with GPIO sampling, RDDID/GSCAN test. Answers all open questions with one flash.
3. **With TE or GSCAN:** native orientation plus sync, which removes the tearing. **Without:**
   stay at 80 MHz with a fixed frame rate.

## Relevant code

- `retro-go/hu-086/config.h`: SPI clock, SPI mode, display init (MADCTL, COLMOD, inversion)
- `retro-go/meloni/main/main.c`: `present()`, main loop with `rg_display_sync(false)`
- `retro-go/retro-go.patch`: patch for `drivers/display/ili9341.h` (configurable SPI mode)
- retro-go source at commit 4ced120 (retro-go 1.46-8): `components/retro-go/rg_display.c`
  (`write_update`, `display_task`, partial updates), `components/retro-go/drivers/display/ili9341.h`
  (SPI setup, `lcd_set_window`, `lcd_send_buffer`)
- Build with `build_retro_go.sh`, flash with `flash_firmware.sh`
