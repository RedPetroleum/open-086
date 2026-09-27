#!/bin/bash
# Sichert den kompletten Flash der HU-086 (ESP32-S3) ueber die C3-Bridge.
# Nur lesend - es wird nichts geschrieben.
#
# Voraussetzung: C3 mit Bridge-Sketch am Mac, Konsole eingeschaltet und verdrahtet.
# Im Download-Modus haelt keine Firmware die Stromversorgung - die Konsole muss
# die ganze Zeit (~4 min) anderweitig an bleiben, z.B. Power-Taster ueberbrueckt.

set -euo pipefail

OUT="${1:-backup_$(date +%Y%m%d_%H%M%S).bin}"

# esptool finden (v5 heisst "esptool", aelter "esptool.py" - v5 wird gebraucht)
if   command -v esptool    >/dev/null 2>&1; then ESPTOOL=esptool
elif command -v esptool.py >/dev/null 2>&1; then ESPTOOL=esptool.py
else
  echo "esptool fehlt. Binary: https://github.com/espressif/esptool/releases"; exit 1
fi

# Port des C3 suchen
PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)
if [ -z "$PORT" ]; then
  echo "Kein /dev/cu.usbmodem* gefunden. Haengt der C3 am Mac?"
  echo "Vorhandene Ports:"; ls /dev/cu.* 2>/dev/null; exit 1
fi
echo "Port:   $PORT"
echo "Ziel:   $OUT"

# C3 neu starten: sein Sketch versetzt den S3 dabei in den Download-Modus.
# Der C3 ist fest verbaut, sein RST-Taster also nicht erreichbar.
echo "--- C3 neu starten, S3 in Download-Modus ---"
"$ESPTOOL" --chip esp32c3 -p "$PORT" --after hard-reset chip-id >/dev/null
sleep 3

COMMON=(--chip esp32s3 -p "$PORT" -b "${BAUD:-921600}" --before no-reset --after no-reset)

echo
echo "--- Verbindung pruefen ---"
"$ESPTOOL" "${COMMON[@]}" flash-id

echo
echo "--- Flash auslesen (bei 921600 Baud ca. 3-4 Minuten) ---"
"$ESPTOOL" "${COMMON[@]}" read-flash 0 ALL "$OUT"

echo
echo "Fertig."
ls -l "$OUT"
shasum -a 256 "$OUT"
