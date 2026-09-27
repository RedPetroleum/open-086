#!/bin/bash
# Schreibt Firmware auf die HU-086 (ESP32-S3) ueber die C3-Bridge.
# ACHTUNG: ueberschreibt den Flash. Vorher backup_firmware.sh laufen lassen.
#
# Aufruf:
#   ./flash_firmware.sh firmware.bin                  # eine Datei nach 0x0
#   ./flash_firmware.sh 0x10000 app.bin               # Adresse explizit
#   ./flash_firmware.sh 0x0 boot.bin 0x8000 part.bin 0x10000 app.bin
#   ./flash_firmware.sh backup_20260927.bin           # Backup zurueckspielen

set -euo pipefail

[ $# -ge 1 ] || { sed -n '2,10p' "$0"; exit 1; }

# Eine einzelne Datei ohne Adresse -> 0x0
if [ $# -eq 1 ]; then ARGS=(0x0 "$1"); else ARGS=("$@"); fi

# Jede genannte Datei muss existieren
for a in "${ARGS[@]}"; do
  case "$a" in 0x*) ;; *) [ -f "$a" ] || { echo "Datei fehlt: $a"; exit 1; };; esac
done

if   command -v esptool.py >/dev/null 2>&1; then ESPTOOL=esptool.py
elif command -v esptool    >/dev/null 2>&1; then ESPTOOL=esptool
else echo "esptool fehlt. Binary: https://github.com/espressif/esptool/releases"; exit 1; fi

PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)
[ -n "$PORT" ] || { echo "Kein /dev/cu.usbmodem* gefunden. Haengt der C3 am Mac?"; exit 1; }

# Warnen, wenn hier noch kein Backup liegt
if ! ls backup*.bin >/dev/null 2>&1; then
  echo "WARNUNG: kein backup*.bin in diesem Ordner gefunden."
fi

echo "Port:  $PORT"
echo "Wird geschrieben:"
printf '  %s\n' "${ARGS[@]}" | paste - - 2>/dev/null || printf '  %s\n' "${ARGS[@]}"
echo
read -r -p "Flash ueberschreiben? Tippe ja: " OK
[ "$OK" = "ja" ] || { echo "Abgebrochen."; exit 1; }

COMMON=(--chip esp32s3 -p "$PORT" -b 115200 --before no-reset --after no-reset)

"$ESPTOOL" "${COMMON[@]}" write_flash --verify "${ARGS[@]}"

echo
echo "Fertig. Konsole am eigenen Schalter aus- und wieder einschalten."
