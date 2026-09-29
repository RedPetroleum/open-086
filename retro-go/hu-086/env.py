# This file is injected late into rg_tool.py, you can run arbitrary python code here
# For example override python variables or set environment variables with os.putenv

# Espressif chip in the device
IDF_TARGET = "esp32s3"
# .fw file format, if supported by the device
FW_FORMAT = "none"
# Default apps to build when none is specified; retro-extra (Atari 2600) and meloni (Lua games) come from
# retro-go/retro-extra and retro-go/meloni in this repo, build_retro_go.sh copies them in
DEFAULT_APPS = "launcher retro-core prboom-go gwenesis fmsx retro-extra meloni"
