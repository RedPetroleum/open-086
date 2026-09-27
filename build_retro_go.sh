#!/bin/bash
# Baut retro-go fuer die HU-086 und legt das Image hier ab.
#
# Aufruf:
#   ./build_retro_go.sh                     # retro-go liegt in ../ext/retro-go
#   RETRO_GO=/pfad/zu/retro-go ./build_retro_go.sh
# Danach:
#   ./flash_firmware.sh retro-go_*_hu-086.img
#
# Braucht ESP-IDF 4.4 (export.sh wird aus IDF_PATH oder ~/esp/esp-idf-v4.4.8 geladen).

set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
RG=${RETRO_GO:-$HERE/../ext/retro-go}
IDF=${IDF_PATH:-$HOME/esp/esp-idf-v4.4.8}

[ -d "$RG/components/retro-go" ] || {
  echo "retro-go fehlt in $RG:"
  echo "  git clone https://github.com/ducalex/retro-go.git $RG"; exit 1; }
[ -f "$IDF/export.sh" ] || { echo "ESP-IDF fehlt in $IDF"; exit 1; }

# Target und Patch einspielen (Patch nur, wenn noch nicht drin)
mkdir -p "$RG/components/retro-go/targets/hu-086"
cp "$HERE"/retro-go/hu-086/{config.h,env.py,sdkconfig} "$RG/components/retro-go/targets/hu-086/"
cp "$HERE/retro-go/hu-086/hu086.c" "$RG/components/retro-go/hu086.c"
if git -C "$RG" apply --check "$HERE/retro-go/retro-go.patch" 2>/dev/null; then
  git -C "$RG" apply "$HERE/retro-go/retro-go.patch"
fi

. "$IDF/export.sh" >/dev/null
cd "$RG"
python rg_tool.py --target=hu-086 build-img
cp retro-go_*_hu-086.img "$HERE/"
ls -l "$HERE"/retro-go_*_hu-086.img
