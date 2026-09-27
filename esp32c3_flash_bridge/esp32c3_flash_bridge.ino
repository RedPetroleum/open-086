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
//   esptool.py --chip esp32s3 -p /dev/cu.usbmodemXXXX -b 115200 \
//              --before no-reset --after no-reset read_flash 0 ALL backup.bin

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
  Serial.begin(115200);                               // USB-CDC zum Mac
  Serial1.begin(115200, SERIAL_8N1, PIN_RX, PIN_TX);  // UART zur Konsole
  delay(500);
  enterDownloadMode();
}

void loop() {
  while (Serial.available())  Serial1.write(Serial.read());
  while (Serial1.available()) Serial.write(Serial1.read());
}
