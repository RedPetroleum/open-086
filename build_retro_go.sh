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
# retro-go muss auf RG_COMMIT stehen, sonst passt retro-go.patch evtl. nicht mehr
# (RETRO_GO_ANY=1 baut trotzdem).
#
# Die Meloni-Engine kommt aus meloni-games (Ordner engine/), und zwar aus MELONI_COMMIT,
# unabhaengig vom Arbeitsstand dort. MELONI_LOCAL=1 nimmt stattdessen den Arbeitsstand
# (zum Testen einer Engine-Aenderung auf dem Geraet vor dem Anheben von MELONI_COMMIT).
#   MELONI_GAMES=/pfad/zu/meloni-games ./build_retro_go.sh   # Standard: ../meloni-games

set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
RG=${RETRO_GO:-$HERE/../ext/retro-go}
IDF=${IDF_PATH:-$HOME/esp/esp-idf-v4.4.8}
RG_COMMIT=4ced120669750ca7228fd0414211430c1d923166   # retro-go 1.46-8, 2026-01-19
MG=${MELONI_GAMES:-$HERE/../meloni-games}
MELONI_COMMIT=89236b6753c4a6e5502246877a47e50777060216   # meloni-games, Engine API 3, mel_update/mel_draw (frameskip)

[ -d "$RG/components/retro-go" ] || {
  echo "retro-go fehlt in $RG:"
  echo "  git clone https://github.com/ducalex/retro-go.git $RG"
  echo "  git -C $RG checkout $RG_COMMIT"; exit 1; }
if [ "$(git -C "$RG" rev-parse HEAD)" != "$RG_COMMIT" ] && [ -z "${RETRO_GO_ANY:-}" ]; then
  echo "retro-go in $RG steht nicht auf $RG_COMMIT:"
  echo "  git -C $RG checkout $RG_COMMIT      # oder RETRO_GO_ANY=1 setzen"; exit 1
fi
[ -f "$IDF/export.sh" ] || { echo "ESP-IDF fehlt in $IDF"; exit 1; }
[ -d "$MG/.git" ] || {
  echo "meloni-games fehlt in $MG:"
  echo "  git clone https://github.com/RedPetroleum/meloni-games.git $MG"; exit 1; }

# Target und Patch einspielen (Patch nur, wenn noch nicht drin)
mkdir -p "$RG/components/retro-go/targets/hu-086"
cp "$HERE"/retro-go/hu-086/{config.h,env.py,sdkconfig} "$RG/components/retro-go/targets/hu-086/"
cp "$HERE/retro-go/hu-086/hu086.c" "$RG/components/retro-go/hu086.c"
# Atari 2600 (Stella) als eigene App
rsync -a --delete --exclude build "$HERE/retro-go/retro-extra/" "$RG/retro-extra/"
# Meloni (Lua-Spiele) als eigene App, dazu der Spiele-Updater im Launcher.
# Die App-Huelle kommt von hier, die Engine (components/meloni, components/lua) aus meloni-games.
rsync -a --delete --exclude build --exclude sdkconfig --exclude components "$HERE/retro-go/meloni/" "$RG/meloni/"
if [ -n "${MELONI_LOCAL:-}" ]; then
  echo "Meloni-Engine: Arbeitsstand aus $MG (MELONI_LOCAL)"
  ENGINE="$MG/engine"
else
  git -C "$MG" cat-file -e "$MELONI_COMMIT^{commit}" 2>/dev/null || git -C "$MG" fetch -q origin
  ENGINE_TMP=$(mktemp -d)
  trap 'rm -rf "$ENGINE_TMP"' EXIT
  git -C "$MG" archive "$MELONI_COMMIT" engine | tar -x -C "$ENGINE_TMP"
  ENGINE="$ENGINE_TMP/engine"
fi
mkdir -p "$RG/meloni/components"
rsync -a --delete "$ENGINE/meloni/" "$RG/meloni/components/meloni/"
rsync -a --delete "$ENGINE/lua/" "$RG/meloni/components/lua/"
cp "$HERE"/retro-go/launcher/meloni_update.{c,h} "$RG/launcher/main/"
if git -C "$RG" apply --check "$HERE/retro-go/retro-go.patch" 2>/dev/null; then
  git -C "$RG" apply "$HERE/retro-go/retro-go.patch"
elif ! git -C "$RG" apply --reverse --check "$HERE/retro-go/retro-go.patch" 2>/dev/null; then
  echo "WARNUNG: retro-go.patch passt nicht (teilweise alter Stand?)."
  echo "  git -C $RG checkout -- components launcher   # dann erneut bauen"
  exit 1
fi

. "$IDF/export.sh" >/dev/null
cd "$RG"
python rg_tool.py --target=hu-086 build-img
cp retro-go_*_hu-086.img "$HERE/"
ls -l "$HERE"/retro-go_*_hu-086.img
