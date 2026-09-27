// Flash-Bridge: ESP32-C3 SuperMini flasht den ESP32-S3 der HU-086.
//
// Der C3 haengt per USB am Mac und reicht die Daten an die Konsole durch.
// IO0 und EN steuert er selbst, deshalb sind keine Taster noetig: beim
// eigenen Start versetzt er den S3 in den Download-Modus.
//
// Verkabelung        C3        Konsole
//                    GPIO6  -> RXD
//                    GPIO20 <- TXD
//                    GPIO7  -> IO0
//                    GPIO10 -> EN
//                    GND    -- GND
//                    VCC nicht verbinden, Konsole laeuft auf ihrem Akku
//
// Arduino-IDE: Board "ESP32C3 Dev Module", USB CDC On Boot: Enabled
//
// Danach am Mac (Port des C3, aber --chip esp32s3):
//   esptool --chip esp32s3 -p /dev/cu.usbmodemXXXX -b 921600 \
//           --before no-reset --after no-reset read-flash 0 ALL backup.bin

const int PIN_TX = 6, PIN_RX = 20, PIN_IO0 = 7, PIN_EN = 10;

// Open-Drain: LOW = gezogen, INPUT = losgelassen.
// Nicht aktiv auf HIGH treiben - IO0 und EN haben eigene Pull-ups.
void pull(int p)   { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
void let_go(int p) { pinMode(p, INPUT); }

void enterDownloadMode() {
  pull(PIN_IO0);    delay(10);
  pull(PIN_EN);     delay(50);
  let_go(PIN_EN);   delay(200);   // ROM liest die Strapping-Pins
  let_go(PIN_IO0);
}

void setup() {
  let_go(PIN_EN);
  let_go(PIN_IO0);
  // esptool schickt Bloecke von mehreren KB. Per USB kommen die schlagartig,
  // per UART fliessen sie nur mit 115200 ab - daher grosse Puffer.
  // Muss vor begin() stehen.
  Serial.setRxBufferSize(16384);
  Serial1.setRxBufferSize(8192);
  Serial1.setTxBufferSize(8192);
  Serial.begin(115200);                               // USB-CDC zum Mac
  Serial1.begin(115200, SERIAL_8N1, PIN_RX, PIN_TX);  // UART zur Konsole
  delay(500);
  enterDownloadMode();
}

// esptool -b 921600 verbindet mit 115200 und schickt dann CHANGE_BAUDRATE (0x0F).
// Die Bridge liest die SLIP-Pakete vom Mac mit und zieht die UART nach, sobald
// der S3 den Befehl bestaetigt hat. Der USB-Port selbst hat keine Baudrate.
uint32_t pendingBaud = 0;

void scanHostByte(uint8_t b) {
  static uint8_t hdr[12];
  static int pos = -1;          // -1: ausserhalb eines Pakets
  static bool esc = false;
  if (b == 0xC0) {
    // Anfrage: 00 0F len(2) chk(4) new_baud(4) old_baud(4)
    if (pos >= 12 && hdr[0] == 0x00 && hdr[1] == 0x0F)
      pendingBaud = hdr[8] | hdr[9] << 8 | hdr[10] << 16 | (uint32_t)hdr[11] << 24;
    pos = 0; esc = false;
    return;
  }
  if (pos < 0) return;
  if (esc)            { b = (b == 0xDC) ? 0xC0 : 0xDB; esc = false; }
  else if (b == 0xDB) { esc = true; return; }
  if (pos < 12) hdr[pos] = b;
  pos++;
}

void switchBaud() {
  Serial1.flush();  // Befehl ist komplett raus
  // Antwort des S3 (C0 ... C0) kommt noch mit alter Baudrate - erst durchreichen
  int delims = 0;
  unsigned long t0 = millis();
  while (delims < 2 && millis() - t0 < 500) {
    if (Serial1.available()) {
      uint8_t b = Serial1.read();
      Serial.write(b);
      if (b == 0xC0) delims++;
    }
  }
  delay(5);
  Serial1.updateBaudRate(pendingBaud);
  pendingBaud = 0;
}

void loop() {
  static uint8_t buf[1024];
  size_t n;
  if ((n = Serial.available())) {
    n = Serial.read(buf, min(n, sizeof(buf)));
    for (size_t i = 0; i < n; i++) scanHostByte(buf[i]);
    Serial1.write(buf, n);
    if (pendingBaud) switchBaud();
  }
  if ((n = Serial1.available())) Serial.write(buf, Serial1.read(buf, min(n, sizeof(buf))));
}
