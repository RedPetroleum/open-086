# HU-086 v01 — Recherchestand

AI Multi-functional Intelligent Palm Phone Kit, Hersteller hxfb (hxfbai.com),
verkauft u.a. über diymore auf AliExpress. DIY-Lötbausatz, SMD bereits bestückt,
nur bedrahtete Bauteile selbst löten.

Stand: 16.09.2026. Alles unter "Belegt" stammt aus den unten genannten Quellen.
Alles unter "Vermutung" ist nicht verifiziert.

---

## 1. Hardware (belegt)

| Komponente | Details |
|---|---|
| MCU | ESP32-S3-N16R8 (16 MB Flash, 8 MB PSRAM), vom Modulaufdruck abgelesen |
| Display | 2,4" TFT, 240x320, 10-poliger FPC, Controller ST7789(V) |
| Audio-Verstärker | NS4168 (in der xiaozhi-config als NS4186 bezeichnet, vermutlich Tippfehler) |
| Mikrofon | PDM, nicht I2S |
| Power-IC | ETA9640 |
| Speicher extern | MicroSD-Slot, SD_MMC im 1-Bit-Modus |
| Akku | 3,7 V LiPo, ca. 350 mA bei max. Lautstärke und Helligkeit |
| Gehäusemaß | 81,7 x 109,6 x 21,6 mm |
| Bedienung | Steuerkreuz, A, B, SELECT, START, Power-Taster |

### Display-Betrieb

- Nativ 240x320, in der xiaozhi-Konfiguration quer betrieben: `DISPLAY_WIDTH 320`,
  `DISPLAY_HEIGHT 240`, `SWAP_XY true`, `MIRROR_X true`
- **Effektiv 320x240**
- SPI-Mode 3 (nicht 0), pclk 40 MHz, 16 bpp
- Farbinvertierung nötig (`gfx->invertDisplay(true)` unter Arduino_GFX)
- Backlight-PWM invertiert (`DISPLAY_BACKLIGHT_OUTPUT_INVERT true`)
- CS ist **nicht** angeschlossen
- Der Blogger hat erst ILI9341 und GC9A01 erfolglos probiert, bis ST7789 ging

---

## 2. Pinbelegung (belegt, von JK1VCK durch Leiterbahnverfolgung ermittelt)

### Display
| Signal | GPIO |
|---|---|
| MOSI | 39 |
| CLK | 40 |
| DC | 38 |
| RST | 41 |
| Backlight | 42 |
| CS | nicht verbunden |

### Taster (alle active low, interne Pullups nötig)
| Taste | GPIO |
|---|---|
| UP | 18 |
| LEFT | 8 |
| RIGHT | 46 |
| DOWN | 14 |
| B | 45 |
| START | 48 |
| A | 47 |
| SELECT | 21 |

### MicroSD (SD_MMC, 1-Bit)
| Signal | GPIO |
|---|---|
| CLK | 10 |
| CMD | 9 |
| D0 | 11 |

### Audio
| Signal | GPIO |
|---|---|
| I2S DOUT (Speaker) | 7 |
| I2S BCLK | 15 |
| I2S LRCK | 16 |
| PA Enable (NS4168) | 17 |
| PDM Mic SCK | 5 |
| PDM Mic DIN | 4 |

### Power
| Signal | GPIO | Hinweis |
|---|---|---|
| POWER_SW | 2 | INPUT_PULLUP |
| POWER_CTRL | 1 | **muss beim Start auf HIGH**, sonst schaltet das Board ab |
| freier Pin | 6 | in der xiaozhi-config als LED-Pin genutzt, keine LED bestückt |

### Arduino-Boardeinstellung des Bloggers
`ESP32S3 Dev, USBCDCOnBoot=ds, Flash=16MB, Partition: 16M Flash (3MB APP/9.9MB FATFS), PSRAM=OPI`

**Wichtig:** Octal-PSRAM belegt weitere GPIOs (typischerweise 33–37), die nicht frei nutzbar sind.

---

## 3. Werksfirmware (aus der Produktbeschreibung des Verkäufers)

- 25 vorinstallierte Spiele, ausdrücklich nur als Funktionstest gedacht
- **NES-Emulator mit SD-Karten-Erweiterung**, bis ca. 1000 Spiele
- Ca. 7 MB interner Spielespeicher, **per USB vom Rechner aus befüll- und löschbar**
- Save/Load-Funktion vorhanden
- Nicht alle NES-Spiele laufen
- Zwei-Spieler-Modus per Bluetooth über eine Android-App
- Weiter: Wetteruhr (Zeit, Datum, Temperatur, Luftfeuchte, Vorhersage),
  Musik-Spektrum (4 Modi, 5 Farben, 5 Empfindlichkeitsstufen),
  Xiaozhi-AI-Sprachdialog
- Wetteruhr und AI benötigen WLAN und Geräteaktivierung
- Lautstärke 0–4, Helligkeit 10–100, Auto-Abschaltung nach 1 Minute Inaktivität
- USB-C laut Beschreibung: "power interface for charging **or data transmission**"
- SD-Karte und Spiele sind **nicht** im Lieferumfang

---

## 4. Was damit möglich ist

### a) Eigenes NES-Spiel (kein Umbau nötig)
Toolchain: cc65 (C/Assembler für 6502), NESFab oder 6502-Assembler direkt.
Testen am PC mit Mesen. ROM per USB oder SD aufs Gerät.

Hürden: 2 KB RAM, keine Fließkommazahlen, keine Hardware-Division, kein
Framebuffer (PPU arbeitet tilebasiert, Schreibzugriffe nur im VBlank-Fenster),
max. 8 Sprites pro Scanline, Mapper-Wahl muss zum Emulator passen.
Fehler äußern sich meist als schwarzer Bildschirm ohne Meldung.

### b) Eigene Firmware
xiaozhi-esp32 (MIT-Lizenz) hat mit der Config des Bloggers bereits ein
lauffähiges Board-Target. Für eigene Anwendungen ESP-IDF oder Arduino.
Es existiert ein NES-Emulator (`ESP32-S3_Uno-nofrendo`) sowie ein
Multi-App-Selector, beides vom selben Blogger auf genau diesem Board.

### c) Doom
PrBoom läuft auf ESP32-S3 mit PSRAM. Zwei Referenzen:
- Espressifs eigener Port (kein Sound, keine Savegames, Menüs instabil)
- `prboom-go` in Retro-Go (Sound, Savegames, WAD-Auswahl, Mods), belegt lauffähig
  auf Arduino Nano ESP32 (ESP32-S3, 8 MB PSRAM, 16 MB Flash, ILI9341) über das
  Target `esplay-s3` mit angepasster `config.h`

320x240 passt exakt. WAD kann auf die SD-Karte. Aufwand: eigenes Retro-Go-Target
mit obiger Pinbelegung anlegen. doom1.wad (Shareware) ist frei weitergebbar,
die Engine steht unter GPL.

Vermutung: Bildrate bei 320x240 über SPI eher 15–25 fps. Keine Messung gefunden.

---

## 5. Flashen

**Zuerst testen:** Board per USB-C an den PC. Meldet sich neben dem
Massenspeicher ein serielles Gerät ("USB JTAG/serial debug unit" bzw.
`/dev/ttyACM0`)? Dann geht Flashen direkt über USB-C, ohne Adapter.

Dafür spricht: die Produktbeschreibung nennt USB-C ausdrücklich als
Datenschnittstelle, und der Spielespeicher wird per USB vom Rechner aus verwaltet.
Dann liegen D+/D- am ESP32-S3 (GPIO19/20) und der interne USB-Serial/JTAG-Controller
ist erreichbar.

Dagegen spricht: der Blogger hat trotzdem einen eigenen Adapter gebaut
(Boot-Taster, Reset-Taster, Power-LED, RXD/TXD zu einem USB-Seriell-Wandler,
Schiebeschalter parallel zum Power-Taster). Warum, schreibt er nicht.
Sein Ablauf: Boot-Taster gedrückt halten, dabei einschalten, flashen,
Reset drücken. Er flasht mit `idf.py -p COM10 flash monitor`.

**Falls doch ein Adapter nötig ist:**
1. USB-TTL-Adapter 3,3 V (CH340 oder CP2102). Ein ST-Link geht **nicht**
   (SWD/ARM, der S3 ist Xtensa mit JTAG).
2. Lötkolben, dünne Litze, Multimeter
3. Schiebe- oder Kippschalter parallel zum Power-Taster

Verdrahtung: Adapter TX → Board RX (GPIO44), Adapter RX → Board TX (GPIO43),
GND → GND, 3V3 nicht anschließen. GPIO0 beim Einschalten auf GND ziehen
(Draht reicht, kein Taster nötig). Reset durch Aus- und Einschalten ersetzen.

Der Schalter parallel zum Power-Taster ist **nicht** optional: wegen des
softwaregelatchten POWER_CTRL (GPIO1) kann sich das Board sonst mitten im
Flashen abschalten.

**Vor dem ersten Überschreiben:** kompletten Flash mit
`esptool.py read_flash` sichern. Die Werksfirmware gibt es nirgends zum Download.

Vermutung, unbelegt: wenn der Flash komplett gelöscht ist, hält beim nächsten
Einschalten niemand mehr GPIO1 auf HIGH. Brücke am Power-Taster also vorher
vorbereiten, nicht danach.

---

## 6. Offene Punkte

- Meldet sich das Board am PC als serielles Gerät? (entscheidet über Adapter)
- Wie kommt man in den Download-Modus, wenn die Werksfirmware USB belegt?
  Gibt es eine Tastenkombination?
- Wie taucht der 7-MB-Spielespeicher am Rechner auf (Massenspeicher, Herstellertool)?
- Warum hat der Blogger trotz USB-Buchse einen UART-Adapter gebaut?
- Reale Bildrate bei 320x240 über SPI

---

## 7. Quellen

- **Boardanalyse und Pinbelegung, Testsketches:**
  https://gijin77.blog.jp/archives/46577853.html
  (JK1VCK / "Skyzoo ヨッシーの備忘録", Januar 2026, japanisch)
- **xiaozhi v2.1.0 auf HU-086, vollständige config.h und board.cc:**
  https://gijin77.blog.jp/archives/46637438.html
- **Multi-App-Selector auf HU-086:**
  https://gijin77.blog.jp/archives/46690906.html
- **Herstellerhandbuch:** https://hxfbai.com/newsinfo/8859096.html
  (im Blog verlinkt, von mir nicht abgerufen)
- **xiaozhi-esp32 Quellcode:** https://github.com/78/xiaozhi-esp32 (MIT)
- **Retro-Go / prboom-go:** ESP32-S3-Build über Target `esplay-s3`
- AliExpress-Listings des Bloggers:
  https://ja.aliexpress.com/item/1005010588888605.html und
  https://ja.aliexpress.com/item/1005010589125005.html

---

## 8. Korrekturen gegenüber früheren Annahmen

Diese Punkte waren im Gesprächsverlauf zunächst falsch eingeschätzt:

- Das Board **hat** einen MicroSD-Slot
- Die Werksfirmware ist ein **echter NES-Emulator** mit SD-Erweiterung, keine
  Sammlung fest einkompilierter Spiele. Eigene ROMs laufen ohne Firmware-Eingriff.
- USB-C kann laut Beschreibung Daten, nicht nur laden
